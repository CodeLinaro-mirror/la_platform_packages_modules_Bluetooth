/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "bta/le_audio/broadcast_sink/state_machine.h"

#include <bluetooth/log.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "bta/le_audio/le_audio_types.h"
#include "btm_api_types.h"
#include "btm_iso_api_types.h"
#include "common/strings.h"
#include "hcidefs.h"
#include "main/shim/le_scanning_manager.h"
#include "stack/include/btm_iso_api.h"
#include "types/raw_address.h"

using bluetooth::common::ToString;
using bluetooth::hci::IsoManager;
using bluetooth::hci::iso_manager::big_sync_established_evt;
using bluetooth::hci::iso_manager::big_sync_lost_evt;
using bluetooth::hci::iso_manager::big_terminate_sync_cmpl_evt;

using namespace bluetooth::le_audio::broadcast_sink;
using namespace bluetooth;

namespace {

class BroadcastSinkStateMachineImpl : public BroadcastSinkStateMachine {
 public:
  BroadcastSinkStateMachineImpl(BroadcastSinkStateMachineConfig sm_config)
      : sm_config_(std::move(sm_config)),
        sink_config_(),
        pa_sync_info_(std::nullopt),
        big_sync_info_(std::nullopt),
        base_data_(std::nullopt),
        is_encrypted_(sm_config_.public_announcement.has_value() ?
                      (sm_config_.public_announcement->features & 0x01) != 0 : false),
        broadcast_code_(std::nullopt),
        pa_sync_lost_(false) {
    stats_ = BroadcastSinkStats();
  }

  ~BroadcastSinkStateMachineImpl() {
    log::info("broadcast_id=0x{:x}, state={}, pa_sync_lost={}", GetBroadcastId(),
              SinkStateToString(GetState()), pa_sync_lost_);

    // Clean up any active syncs
    if (GetState() == SinkState::BIG_SYNCED || GetState() == SinkState::BIG_SYNCING) {
      TerminateBigSync();
    }

    if (GetState() != SinkState::IDLE && GetState() != SinkState::DISABLING &&
        GetState() != SinkState::STOPPING) {
      TerminatePaSync();
    }

    if (callbacks_) {
      // Determine reason code based on whether PA sync was lost
      uint8_t reason = pa_sync_lost_ ? kBroadcastSinkDestroyReasonPaSyncLost
                                     : kBroadcastSinkDestroyReasonNormal;
      log::info("Notifying state machine destroyed with reason=0x{:02x} ({})",
                reason, pa_sync_lost_ ? "PA sync lost" : "normal/user requested");
      callbacks_->OnStateMachineDestroyed(GetBroadcastId(), reason);
    }
  }

  bool Initialize() override {
    log::info("broadcast_id=0x{:x}, address={}, adv_sid={}", GetBroadcastId(),
              sm_config_.address.ToString(), sm_config_.adv_sid);

    // Initialize with default configuration
    sink_config_ = BroadcastSinkConfiguration();

    // Start PA sync automatically during initialization
    SetState(SinkState::PA_SYNCING);
    if (callbacks_) {
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    }
    StartPaSync();

    return true;
  }

  // State machine configuration getters
  uint32_t GetRegId() const override { return sm_config_.reg_id; }
  uint32_t GetBroadcastId() const override { return sm_config_.broadcast_id; }
  const RawAddress& GetSourceAddress() const override { return sm_config_.address; }
  uint8_t GetAddressType() const override { return sm_config_.address_type; }
  uint8_t GetAdvSid() const override { return sm_config_.adv_sid; }
  const std::string& GetBroadcastName() const override { return sm_config_.broadcast_name; }
  bool IsPublic() const override { return sm_config_.is_public; }
  uint16_t GetPaSyncTimeout() const override { return sm_config_.pa_sync_timeout; }

  // Runtime state getters
  bool IsEncrypted() const override { return is_encrypted_; }
  const BroadcastSinkConfiguration& GetSinkConfiguration() const override { return sink_config_; }
  std::optional<BroadcastCode> GetBroadcastCode() const override { return broadcast_code_; }
  std::optional<bluetooth::le_audio::BasicAudioAnnouncementData> GetBaseData() const override {
    return base_data_;
  }
  std::optional<bluetooth::le_audio::PublicBroadcastAnnouncementData> GetPublicAnnouncement() const override {
    return sm_config_.public_announcement;
  }

  void StartSync() override {
    log::info("broadcast_id=0x{:x}, current_state={}", GetBroadcastId(), SinkStateToString(GetState()));
    // StartSync() starts BIG sync (PA sync already started during Initialize)
    ProcessMessage(Message::START_BIG_SYNC, nullptr);
  }

  void StopSync() override {
    log::info("broadcast_id=0x{:x}, current_state={}", GetBroadcastId(), SinkStateToString(GetState()));
    ProcessMessage(Message::STOP_SYNC, nullptr);
  }

  void UpdateBroadcastCode(const bluetooth::le_audio::BroadcastCode& code) override {
    log::info("broadcast_id=0x{:x}, updating broadcast code", GetBroadcastId());
    broadcast_code_ = code;

    // If we're already synced and have a broadcast code, we may need to re-establish BIG sync
    // Note: Encryption status comes from BIGInfo report, not BASE data
    if (GetState() == SinkState::PA_SYNCED && base_data_.has_value()) {
      log::info("Broadcast code updated, may need to re-establish BIG sync if encrypted");
      // BIG sync re-establishment will be triggered when needed
    }
  }

  void UpdatePublicAnnouncement(const std::string& broadcast_name, const bluetooth::le_audio::PublicBroadcastAnnouncementData& public_announcement) override {
    log::info("broadcast_id=0x{:x}, updating broadcast name: '{}', public announcement with features: 0x{:02x}, metadata size: {}",
             GetBroadcastId(), broadcast_name, public_announcement.features, public_announcement.metadata.size());

    // Update the state machine configuration with new broadcast name and public announcement
    sm_config_.broadcast_name = broadcast_name;
    sm_config_.public_announcement = public_announcement;

    log::debug("Broadcast name and public announcement updated successfully for broadcast_id=0x{:x}", GetBroadcastId());
  }

  void UpdateBisIndices(const std::vector<uint8_t>& bis_indices) override {
    log::info("broadcast_id=0x{:x}, updating BIS indices", GetBroadcastId());
    sink_config_.bis_indices = bis_indices;
  }

  void UpdateSinkConfiguration(const BroadcastSinkConfiguration& config) override {
    log::info("broadcast_id=0x{:x}, updating sink configuration", GetBroadcastId());
    sink_config_ = config;

    // If we're in BIG_SYNCED state and configuration changed, we may need to re-establish BIG sync
    if (GetState() == SinkState::BIG_SYNCED) {
      log::info("Sink configuration updated while streaming, may need to re-establish BIG sync");
      // Re-establishment logic can be added here if needed
    }
  }

  bool IsStreaming() const override { return GetState() == SinkState::BIG_SYNCED; }

  bool IsPaSynced() const override {
    return GetState() == SinkState::PA_SYNCED || GetState() == SinkState::BIG_SYNCING ||
           GetState() == SinkState::BIG_SYNCED || GetState() == SinkState::DISABLING;
  }

  bool IsPaSyncLost() const override { return pa_sync_lost_; }

  std::optional<PaSyncInfo> GetPaSyncInfo() const override { return pa_sync_info_; }

  std::optional<BigSyncInfo> GetBigSyncInfo() const override { return big_sync_info_; }

  const BroadcastSinkStats& GetStats() const override { return stats_; }

  void ProcessMessage(Message msg, const void* data) override {
    log::info("broadcast_id=0x{:x}, state={}, message={}", GetBroadcastId(), SinkStateToString(GetState()),
              ToString(msg));

    switch (msg) {
      case Message::START_BIG_SYNC:
        start_big_sync_handlers[static_cast<uint8_t>(GetState())](data);
        break;
      case Message::STOP_BIG_SYNC:
        stop_big_sync_handlers[static_cast<uint8_t>(GetState())](data);
        break;
      case Message::STOP_SYNC:
        stop_sync_handlers[static_cast<uint8_t>(GetState())](data);
        break;
      case Message::MESSAGE_COUNT:
        log::error("Invalid message type MESSAGE_COUNT");
        break;
    }
  }

  // Scanning callbacks
  void OnSyncEstablished(uint8_t status, uint16_t sync_handle, uint8_t adv_sid,
                         uint8_t address_type, RawAddress address, uint8_t phy,
                         uint16_t interval) override {
    log::info(
        "broadcast_id=0x{:x}, status=0x{:02x}, sync_handle=0x{:04x}, adv_sid={}, address={}, phy={}, "
        "interval={}",
        GetBroadcastId(), status, sync_handle, adv_sid, address.ToString(), phy, interval);

    if (status != 0x00) {
      log::error("PA sync failed for broadcast_id=0x{:x}, status=0x{:02x}", GetBroadcastId(), status);
      SetState(SinkState::IDLE);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      return;
    }

    // Store PA sync info
    PaSyncInfo info;
    info.sync_handle = sync_handle;
    info.adv_sid = adv_sid;
    info.address = address;
    info.address_type = address_type;
    info.phy = phy;
    info.interval = interval;
    pa_sync_info_ = info;

    // Reset PA sync lost flag since we successfully established PA sync
    pa_sync_lost_ = false;

    SetState(SinkState::PA_SYNCED);
    callbacks_->OnPaSyncEstablished(GetBroadcastId(), sync_handle, adv_sid, address, address_type);
    callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
  }

  void OnSyncLost(uint16_t sync_handle) override {
    log::warn("broadcast_id=0x{:x}, sync_handle=0x{:04x}, state={}", GetBroadcastId(), sync_handle,
              SinkStateToString(GetState()));

    if (!pa_sync_info_.has_value() || pa_sync_info_->sync_handle != sync_handle) {
      log::warn("Sync lost for unknown sync_handle=0x{:04x}", sync_handle);
      return;
    }

    stats_.sync_lost_count++;

    // Set PA sync lost flag
    pa_sync_lost_ = true;

    // Clean up
    uint16_t lost_sync_handle = pa_sync_info_->sync_handle;
    pa_sync_info_ = std::nullopt;
    base_data_ = std::nullopt;

    // If we had BIG sync, it's also lost
    if (big_sync_info_.has_value()) {
      uint8_t big_handle = big_sync_info_->big_handle;
      big_sync_info_ = std::nullopt;
      callbacks_->OnBigSyncLost(GetBroadcastId(), big_handle, 0x13 /* Connection Terminated */);
    }

    SetState(SinkState::IDLE);
    callbacks_->OnPaSyncLost(GetBroadcastId(), lost_sync_handle);
    callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
  }

  void OnPeriodicScanResult(uint16_t sync_handle, int8_t tx_power, int8_t rssi, uint8_t status,
                            std::vector<uint8_t> data) override {
    if (!pa_sync_info_.has_value() || pa_sync_info_->sync_handle != sync_handle) {
      return;
    }

    stats_.last_rssi = rssi;

    // Parse BASE data
    if (!data.empty()) {
      bluetooth::le_audio::BasicAudioAnnouncementData base;
      if (ParseBasicAudioAnnouncement(data, base)) {
        // Check if BASE data changed
        bool changed = !base_data_.has_value() || !(base == *base_data_);
        base_data_ = base;

        if (changed) {
          log::info("broadcast_id=0x{:x}, BASE data changed, presentation_delay={}us",
                    GetBroadcastId(), base.presentation_delay_us);
          callbacks_->OnBaseDataReceived(GetBroadcastId(), base);
        }
      } else {
        log::warn("broadcast_id=0x{:x}, failed to parse BASE data", GetBroadcastId());
      }
    }
  }

  void OnBigInfoReport(uint16_t sync_handle, bool encrypted) override {
    if (!pa_sync_info_.has_value() || pa_sync_info_->sync_handle != sync_handle) {
      return;
    }

    log::info("broadcast_id=0x{:x}, sync_handle=0x{:04x}, encrypted={}, state={}", GetBroadcastId(),
              sync_handle, encrypted, SinkStateToString(GetState()));

    // Store encryption status from BIG Info Report
    is_encrypted_ = encrypted;

    callbacks_->OnBigInfoReport(GetBroadcastId(), sync_handle, encrypted);

    // If we're in PA_SYNCED state and have BASE data, we can proceed to BIG sync
    if (GetState() == SinkState::PA_SYNCED && base_data_.has_value()) {
      // Check if encryption matches our configuration
      bool has_code = broadcast_code_.has_value();
      if (encrypted && !has_code) {
        log::warn("broadcast_id=0x{:x}, broadcast is encrypted but no code provided", GetBroadcastId());
        return;
      }

      log::info("broadcast_id=0x{:x}, BIGInfo received, ready for BIG sync", GetBroadcastId());
    }
  }

  void HandleHciEvent(uint16_t event, void* data) override {
    switch (event) {
      case HCI_BLE_BIG_SYNC_EST_EVT: {
        auto* evt = static_cast<big_sync_established_evt*>(data);
        OnBigSyncEstablished(evt);
      } break;

      case HCI_BLE_BIG_SYNC_LOST_EVT: {
        auto* evt = static_cast<big_sync_lost_evt*>(data);
        OnBigSyncLost(evt);
      } break;

      default:
        log::warn("broadcast_id=0x{:x}, unknown HCI event=0x{:04x}", GetBroadcastId(), event);
        break;
    }
  }

  void OnSetupIsoDataPath(uint8_t status, uint16_t conn_handle) override {
    log::info("broadcast_id=0x{:x}, status=0x{:02x}, conn_handle=0x{:04x}", GetBroadcastId(), status,
              conn_handle);

    if (!big_sync_info_.has_value()) {
      log::error("OnSetupIsoDataPath called but no BIG sync info");
      return;
    }

    if (status != 0x00) {
      log::error("Failed to setup ISO data path, status=0x{:02x}", status);
      SetState(SinkState::STOPPING);
      TerminateBigSync();
      return;
    }

    // Find next BIS handle to setup
    auto& handles = big_sync_info_->bis_conn_handles;
    auto it = std::find(handles.begin(), handles.end(), conn_handle);
    if (it == handles.end()) {
      log::error("Unknown conn_handle=0x{:04x}", conn_handle);
      return;
    }

    it = std::next(it);
    if (it == handles.end()) {
      // All data paths setup - transition to BIG_SYNCED
      log::info("broadcast_id=0x{:x}, all ISO data paths established", GetBroadcastId());
      SetState(SinkState::BIG_SYNCED);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    } else {
      // Setup next data path
      log::info("broadcast_id=0x{:x}, setting up next ISO data path", GetBroadcastId());
      TriggerIsoDatapathSetup(*it);
    }
  }

  void OnRemoveIsoDataPath(uint8_t status, uint16_t conn_handle) override {
    log::info("broadcast_id=0x{:x}, status=0x{:02x}, conn_handle=0x{:04x}", GetBroadcastId(), status,
              conn_handle);

    if (!big_sync_info_.has_value()) {
      log::warn("OnRemoveIsoDataPath called but no BIG sync info");
      return;
    }

    if (status != 0x00) {
      log::error("Failed to remove ISO data path, status=0x{:02x}, forcing BIG termination",
                 status);
      TerminateBigSync();
      return;
    }

    // Find next BIS handle to teardown
    auto& handles = big_sync_info_->bis_conn_handles;
    auto it = std::find(handles.begin(), handles.end(), conn_handle);
    if (it == handles.end()) {
      log::error("Unknown conn_handle=0x{:04x}", conn_handle);
      return;
    }

    it = std::next(it);
    if (it == handles.end()) {
      // All data paths removed - terminate BIG
      log::info("broadcast_id=0x{:x}, all ISO data paths removed, terminating BIG", GetBroadcastId());
      TerminateBigSync();
    } else {
      // Remove next data path
      log::info("broadcast_id=0x{:x}, removing next ISO data path", GetBroadcastId());
      TriggerIsoDatapathTeardown(*it);
    }
  }

  void OnBigTerminateSyncComplete(uint8_t big_handle, uint8_t status) override {
    log::info("broadcast_id=0x{:x}, big_handle={}, status=0x{:02x} - INTENTIONAL BIG TERMINATE COMPLETE",
              GetBroadcastId(), big_handle, status);

    if (status != HCI_SUCCESS) {
      log::error("BIG terminate sync command failed, status=0x{:02x}", status);
      // Even if terminate failed, clean up our state
    }

    // Clean up BIG sync info if we still have it
    // Note: In normal flow, BIG Sync Lost event should arrive before this complete event
    // and will have already cleaned up big_sync_info_. But handle both cases.
    if (big_sync_info_.has_value() && big_sync_info_->big_handle == big_handle) {
      log::info("Cleaning up BIG sync info (not yet cleaned by BIG Sync Lost event)");
      big_sync_info_ = std::nullopt;
    }

    // Notify upper layer about intentional termination (separate from unexpected loss)
    callbacks_->OnBigSyncTerminated(GetBroadcastId(), big_handle, status);

    // Determine next state based on current state
    if (GetState() == SinkState::DISABLING) {
      // DISABLING state: Leave operation - keep PA sync, transition to PA_SYNCED
      log::info("BIG terminate sync complete, transitioning to PA_SYNCED (Leave operation)");
      SetState(SinkState::PA_SYNCED);
      if (callbacks_) {
        callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      }
    } else if (GetState() == SinkState::STOPPING) {
      // STOPPING state: Remove operation - terminate PA sync, transition to IDLE
      log::info("BIG terminate sync complete, continuing with PA sync termination (Remove operation)");
      TerminatePaSync();
    } else {
      log::warn("Received BIG terminate sync complete in unexpected state: {}",
                SinkStateToString(GetState()));
    }
  }

  static IBroadcastSinkStateMachineCallbacks* callbacks_;
  static BleScannerInterface* ble_scanner_;

 private:
  BroadcastSinkStateMachineConfig sm_config_;  // State machine configuration (immutable)
  BroadcastSinkConfiguration sink_config_;     // Sink configuration (codec, BIS, etc.)
  std::optional<PaSyncInfo> pa_sync_info_;
  std::optional<BigSyncInfo> big_sync_info_;
  std::optional<bluetooth::le_audio::BasicAudioAnnouncementData> base_data_;
  BroadcastSinkStats stats_;
  bool is_encrypted_;      // Encryption status from BIG Info Report
  std::optional<BroadcastCode> broadcast_code_;  // Broadcast code for encrypted sources
  bool pa_sync_lost_;      // Flag to track if PA sync was lost

  // Message handlers for each state
  typedef std::function<void(const void*)> msg_handler_t;

  // START_BIG_SYNC message handlers
  const std::array<msg_handler_t, static_cast<size_t>(SinkState::STATE_COUNT)>
      start_big_sync_handlers{
          /* IDLE */
          [this](const void*) {
            log::warn("broadcast_id=0x{:x}, cannot start BIG sync from IDLE (PA sync not started)", GetBroadcastId());
          },
          /* PA_SYNCING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, PA syncing in progress, wait for PA sync complete",
                      GetBroadcastId());
          },
          /* PA_SYNCED */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, starting BIG sync", GetBroadcastId());
            if (!base_data_.has_value()) {
              log::warn("broadcast_id=0x{:x}, no BASE data yet, cannot start BIG sync",
                        GetBroadcastId());
              return;
            }
            SetState(SinkState::BIG_SYNCING);
            if (callbacks_) {
              callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            }
            CreateBigSync();
          },
          /* BIG_SYNCING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, already syncing BIG", GetBroadcastId());
          },
          /* STREAMING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, already streaming", GetBroadcastId());
          },
          /* DISABLING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, cannot start BIG sync while disabling", GetBroadcastId());
          },
          /* STOPPING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, currently stopping", GetBroadcastId());
          },
      };

  // STOP_BIG_SYNC message handlers (stop BIG sync only, keep PA sync)
  const std::array<msg_handler_t, static_cast<size_t>(SinkState::STATE_COUNT)>
      stop_big_sync_handlers{
          /* IDLE */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, no BIG sync to stop (IDLE)", GetBroadcastId());
          },
          /* PA_SYNCING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, no BIG sync to stop (PA_SYNCING)", GetBroadcastId());
          },
          /* PA_SYNCED */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, no BIG sync to stop (PA_SYNCED)", GetBroadcastId());
          },
          /* BIG_SYNCING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, stopping BIG sync (BIG_SYNCING)", GetBroadcastId());
            // BIG sync will fail and we'll transition back to PA_SYNCED in the event handler
          },
          /* STREAMING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, stopping BIG sync only (keeping PA sync)", GetBroadcastId());
            SetState(SinkState::DISABLING);
            if (callbacks_) {
              callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            }
            if (big_sync_info_.has_value() && !big_sync_info_->bis_conn_handles.empty()) {
              TriggerIsoDatapathTeardown(big_sync_info_->bis_conn_handles[0]);
            }
          },
          /* DISABLING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, already disabling BIG sync", GetBroadcastId());
          },
          /* STOPPING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, already stopping", GetBroadcastId());
          },
      };

  // STOP_SYNC message handlers
  const std::array<msg_handler_t, static_cast<size_t>(SinkState::STATE_COUNT)>
      stop_sync_handlers{
          /* IDLE */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, already idle", GetBroadcastId());
          },
          /* PA_SYNCING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, stopping PA sync", GetBroadcastId());
          },
          /* PA_SYNCED */
          [this](const void*) {
            SetState(SinkState::STOPPING);
            if (callbacks_) {
              callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            }
            log::info("broadcast_id=0x{:x}, stopping PA sync", GetBroadcastId());
            TerminatePaSync();
          },
          /* BIG_SYNCING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, stopping BIG sync", GetBroadcastId());
            SetState(SinkState::STOPPING);
            if (callbacks_) {
              callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            }
            // BIG sync will fail and we'll clean up in the event handler
          },
          /* STREAMING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, stopping stream", GetBroadcastId());
            SetState(SinkState::STOPPING);
            if (callbacks_) {
              callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            }
            if (big_sync_info_.has_value() && !big_sync_info_->bis_conn_handles.empty()) {
              TriggerIsoDatapathTeardown(big_sync_info_->bis_conn_handles[0]);
            }
          },
          /* DISABLING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, stopping both BIG and PA sync from DISABLING state", GetBroadcastId());
            SetState(SinkState::STOPPING);
            if (callbacks_) {
              callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            }
            // If still have BIG sync, teardown datapaths first
            if (big_sync_info_.has_value() && !big_sync_info_->bis_conn_handles.empty()) {
              TriggerIsoDatapathTeardown(big_sync_info_->bis_conn_handles[0]);
            } else {
              // No BIG sync, directly terminate PA sync
              TerminatePaSync();
            }
          },
          /* STOPPING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, already stopping", GetBroadcastId());
          },
      };

  void StartPaSync() {
    log::info("reg_id={}, broadcast_id=0x{:x}, address={}, adv_sid={}", GetRegId(),
              GetBroadcastId(), sm_config_.address.ToString(), sm_config_.adv_sid);

    if (ble_scanner_ == nullptr) {
      log::error("BLE scanner not initialized");
      return;
    }

    // Use BLE Scanner interface to start PA sync with allocated reg_id
    ble_scanner_->StartSync(
        sm_config_.adv_sid, sm_config_.address, 0 /* skip */, sm_config_.pa_sync_timeout,
        sm_config_.reg_id /* reg_id */, kScannerClientIdLeAudio);
  }

  void TerminatePaSync() {
    if (!pa_sync_info_.has_value()) {
      log::warn("broadcast_id=0x{:x}, no PA sync to terminate", GetBroadcastId());
      SetState(SinkState::IDLE);
      if (callbacks_) {
        callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      }
      return;
    }

    log::info("broadcast_id=0x{:x}, sync_handle=0x{:04x}", GetBroadcastId(), pa_sync_info_->sync_handle);

    if (ble_scanner_ == nullptr) {
      log::error("BLE scanner not initialized");
      return;
    }

    ble_scanner_->StopSync(pa_sync_info_->sync_handle, kScannerClientIdLeAudio);
    SetState(SinkState::IDLE);
    if (callbacks_) {
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    }
    // Clean up will happen in OnSyncLost callback
  }

  void CreateBigSync() {
    if (!pa_sync_info_.has_value()) {
      log::error("broadcast_id=0x{:x}, no PA sync established", GetBroadcastId());
      return;
    }

    if (!base_data_.has_value()) {
      log::error("broadcast_id=0x{:x}, no BASE data available", GetBroadcastId());
      return;
    }
    log::info("broadcast_id=0x{:x}, pa_sync_handle=0x{:04x}, num_bis={}", GetBroadcastId(),
              pa_sync_info_->sync_handle, sink_config_.bis_indices.size());

    // Prepare BIG sync parameters
    // Use encryption status from BIG Info Report
    bluetooth::hci::iso_manager::big_sync_params params = {
        .sync_handle = pa_sync_info_->sync_handle,
        .encryption = is_encrypted_ ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00),
        .broadcast_code = broadcast_code_.value_or(std::array<uint8_t, 16>({0})),
        .mse = sink_config_.mse,
        .big_sync_timeout = sink_config_.big_sync_timeout,
        .bis = sink_config_.bis_indices,
    };

    log::info("broadcast_id=0x{:x}, encryption={}, has_broadcast_code={}", GetBroadcastId(),
              is_encrypted_, broadcast_code_.has_value());

    // Use reg_id as big_handle
    uint8_t big_handle = sm_config_.reg_id;

    IsoManager::GetInstance()->BigCreateSync(big_handle, std::move(params));
  }

  void TerminateBigSync() {
    if (!big_sync_info_.has_value()) {
      log::warn("broadcast_id=0x{:x}, no BIG sync to terminate", GetBroadcastId());
      // Handle based on current state
      if (GetState() == SinkState::DISABLING) {
        // DISABLING: No BIG sync, just transition to PA_SYNCED
        log::info("No BIG sync to terminate in DISABLING state, transitioning to PA_SYNCED");
        SetState(SinkState::PA_SYNCED);
        if (callbacks_) {
          callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
        }
      } else if (GetState() == SinkState::STOPPING) {
        // STOPPING: No BIG sync, terminate PA sync
        TerminatePaSync();
      }
      return;
    }

    log::info("broadcast_id=0x{:x}, big_handle={}", GetBroadcastId(), big_sync_info_->big_handle);

    IsoManager::GetInstance()->BigTerminateSync(big_sync_info_->big_handle);

    // Clean up will happen in OnBigSyncLost callback
  }

  void OnBigSyncEstablished(big_sync_established_evt* evt) {
    log::info("broadcast_id=0x{:x}, status=0x{:02x}, big_handle={}, num_bis={}", GetBroadcastId(),
              evt->status, evt->big_handle, evt->conn_handles.size());

    if (evt->status != 0x00) {
      log::error("BIG sync failed, status=0x{:02x}", evt->status);
      SetState(SinkState::PA_SYNCED);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      return;
    }

    // Store BIG sync info
    BigSyncInfo info;
    info.big_handle = evt->big_handle;
    info.pa_sync_handle = pa_sync_info_->sync_handle;
    info.bis_conn_handles = evt->conn_handles;
    info.transport_latency_us = evt->transport_latency_big;
    info.nse = evt->nse;
    info.bn = evt->bn;
    info.pto = evt->pto;
    info.irc = evt->irc;
    info.max_pdu = evt->max_pdu;
    info.iso_interval = evt->iso_interval;
    info.num_bis = static_cast<uint8_t>(evt->conn_handles.size());
    big_sync_info_ = info;

    callbacks_->OnBigSyncEstablished(GetBroadcastId(), evt->big_handle, evt->conn_handles);

    // Setup ISO data paths
    if (!evt->conn_handles.empty()) {
      TriggerIsoDatapathSetup(evt->conn_handles[0]);
    } else {
      log::error("No BIS connection handles in BIG sync established event");
    }
  }

  void OnBigSyncLost(big_sync_lost_evt* evt) {
    log::warn("broadcast_id=0x{:x}, big_handle={}, reason=0x{:02x} - UNEXPECTED BIG SYNC LOST",
              GetBroadcastId(), evt->big_handle, evt->reason);

    if (!big_sync_info_.has_value() || big_sync_info_->big_handle != evt->big_handle) {
      log::warn("BIG sync lost for unknown big_handle={}", evt->big_handle);
      return;
    }

    uint8_t big_handle = big_sync_info_->big_handle;
    big_sync_info_ = std::nullopt;

    // This is an UNEXPECTED loss (not due to our terminate command)
    // Notify upper layer with the reason
    callbacks_->OnBigSyncLost(GetBroadcastId(), big_handle, evt->reason);

    // Transition based on current state
    if (GetState() == SinkState::DISABLING) {
      // BIG sync lost during DISABLING - this is expected, transition to PA_SYNCED
      log::info("BIG sync lost during DISABLING state, transitioning to PA_SYNCED");
      SetState(SinkState::PA_SYNCED);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    } else if (GetState() == SinkState::STOPPING) {
      // BIG sync lost during STOPPING - continue with PA sync termination
      log::info("BIG sync lost during STOPPING state, continuing with PA termination");
      TerminatePaSync();
    } else {
      // Unexpected loss during normal operation - go back to PA_SYNCED
      // Upper layer can decide whether to retry BIG sync
      log::info("BIG sync lost unexpectedly, transitioning to PA_SYNCED");
      SetState(SinkState::PA_SYNCED);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    }
  }

  void TriggerIsoDatapathSetup(uint16_t conn_handle) {
    log::info("broadcast_id=0x{:x}, conn_handle=0x{:04x}", GetBroadcastId(), conn_handle);

    if (!base_data_.has_value()) {
      log::error("No BASE data available for ISO data path setup");
      return;
    }

    /* Note: If coding format is transparent, 'codec_id_company' and
     * 'codec_id_vendor' shall be ignored.
     */
    auto& iso_datapath_config = sink_config_.data_path.isoDataPathConfig;
    bluetooth::hci::iso_manager::iso_data_path_params params = {
        .data_path_dir = bluetooth::hci::iso_manager::kIsoDataPathDirectionOut,
        .data_path_id = static_cast<uint8_t>(sink_config_.data_path.dataPathId),
        .codec_id_format = static_cast<uint8_t>(
                iso_datapath_config.isTransparent ? bluetooth::hci::kIsoCodingFormatTransparent
                                                  : iso_datapath_config.codecId.coding_format),
        .codec_id_company =
                static_cast<uint16_t>(iso_datapath_config.isTransparent
                                              ? 0x0000
                                              : iso_datapath_config.codecId.vendor_company_id),
        .codec_id_vendor =
                static_cast<uint16_t>(iso_datapath_config.isTransparent
                                              ? 0x0000
                                              : iso_datapath_config.codecId.vendor_codec_id),
        .controller_delay = iso_datapath_config.controllerDelayUs,
        .codec_conf = iso_datapath_config.configuration,
    };

    IsoManager::GetInstance()->SetupIsoDataPath(conn_handle, std::move(params));
  }

  void TriggerIsoDatapathTeardown(uint16_t conn_handle) {
    log::info("broadcast_id=0x{:x}, conn_handle=0x{:04x}", GetBroadcastId(), conn_handle);

    IsoManager::GetInstance()->RemoveIsoDataPath(
        conn_handle, bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput);
  }

  bool ParseBasicAudioAnnouncement(const std::vector<uint8_t>& data,
                                   bluetooth::le_audio::BasicAudioAnnouncementData& base) {
    // AD format: [Length][AD Type][Service UUID (2 bytes)][BASE Data...]
    // BASE Data: [Presentation Delay (3 bytes)][Num Subgroups][Subgroups...]
    // Minimum: 1 + 1 + 2 + 3 + 1 = 8 bytes
    if (data.size() < 8) {
      log::error("BASE data too short: {} bytes", data.size());
      return false;
    }

    size_t offset = 0;
    const uint8_t* p = data.data();

    // Parse AD Length (1 byte) and AD Type (1 byte)
    uint8_t ad_length = p[offset++];
    uint8_t ad_type = p[offset++];

    // Parse Basic Audio Announcement Service UUID (2 bytes)
    uint16_t service_uuid = p[offset] | (p[offset + 1] << 8);
    offset += 2;

    log::info("AD format: length={}, type=0x{:02x}, service_uuid=0x{:04x}",
              ad_length, ad_type, service_uuid);

    // Parse presentation delay (3 bytes, 24-bit value in microseconds)
    const uint8_t* base_data = p + offset;
    STREAM_TO_UINT24(base.presentation_delay_us, base_data);
    offset += 3;

    // Parse number of subgroups (1 byte)
    uint8_t num_subgroups = p[offset++];
    if (num_subgroups == 0) {
      log::error("BASE has no subgroups");
      return false;
    }

    log::info("Parsing BASE: presentation_delay={}us, num_subgroups={}",
              base.presentation_delay_us, num_subgroups);

    // Parse each subgroup
    for (uint8_t sg = 0; sg < num_subgroups; sg++) {
      if (offset >= data.size()) {
        log::error("Unexpected end of BASE data at subgroup {}", sg);
        return false;
      }

      bluetooth::le_audio::BasicAudioAnnouncementSubgroup subgroup;

      // Parse number of BIS in this subgroup (1 byte)
      uint8_t num_bis = p[offset++];
      if (num_bis == 0) {
        log::error("Subgroup {} has no BIS", sg);
        return false;
      }

      // Parse Codec ID (5 bytes)
      if (offset + 5 > data.size()) {
        log::error("Not enough data for codec ID in subgroup {}", sg);
        return false;
      }

      subgroup.codec_config.codec_id = p[offset++];
      const uint8_t* temp_p = p + offset;
      STREAM_TO_UINT16(subgroup.codec_config.vendor_company_id, temp_p);
      offset += 2;
      temp_p = p + offset;
      STREAM_TO_UINT16(subgroup.codec_config.vendor_codec_id, temp_p);
      offset += 2;

      log::info("Subgroup {}: num_bis={}, codec_id=0x{:02x}, vendor_company=0x{:04x}, vendor_codec=0x{:04x}",
                sg, num_bis, subgroup.codec_config.codec_id,
                subgroup.codec_config.vendor_company_id, subgroup.codec_config.vendor_codec_id);

      // Parse Codec Specific Configuration Length (1 byte)
      if (offset >= data.size()) {
        log::error("Not enough data for codec config length in subgroup {}", sg);
        return false;
      }
      uint8_t codec_config_len = p[offset++];

      // Parse Codec Specific Configuration (LTV format)
      if (offset + codec_config_len > data.size()) {
        log::error("Not enough data for codec config in subgroup {}", sg);
        return false;
      }

      if (codec_config_len > 0) {
        size_t ltv_offset = 0;
        while (ltv_offset < codec_config_len) {
          uint8_t ltv_len = p[offset + ltv_offset++];
          if (ltv_len == 0 || ltv_offset + ltv_len > codec_config_len) {
            log::warn("Invalid LTV length in codec config");
            break;
          }
          uint8_t ltv_type = p[offset + ltv_offset++];
          std::vector<uint8_t> ltv_value(p + offset + ltv_offset,
                                         p + offset + ltv_offset + ltv_len - 1);
          subgroup.codec_config.codec_specific_params[ltv_type] = ltv_value;
          ltv_offset += (ltv_len - 1);
        }
        offset += codec_config_len;
      }

      // Parse Metadata Length (1 byte)
      if (offset >= data.size()) {
        log::error("Not enough data for metadata length in subgroup {}", sg);
        return false;
      }
      uint8_t metadata_len = p[offset++];

      // Parse Metadata (LTV format)
      if (offset + metadata_len > data.size()) {
        log::error("Not enough data for metadata in subgroup {}", sg);
        return false;
      }

      if (metadata_len > 0) {
        size_t ltv_offset = 0;
        while (ltv_offset < metadata_len) {
          uint8_t ltv_len = p[offset + ltv_offset++];
          if (ltv_len == 0 || ltv_offset + ltv_len > metadata_len) {
            log::warn("Invalid LTV length in metadata");
            break;
          }
          uint8_t ltv_type = p[offset + ltv_offset++];
          std::vector<uint8_t> ltv_value(p + offset + ltv_offset,
                                         p + offset + ltv_offset + ltv_len - 1);
          subgroup.metadata[ltv_type] = ltv_value;
          ltv_offset += (ltv_len - 1);
        }
        offset += metadata_len;
      }

      // Parse BIS configurations
      for (uint8_t bis = 0; bis < num_bis; bis++) {
        if (offset >= data.size()) {
          log::error("Not enough data for BIS {} in subgroup {}", bis, sg);
          return false;
        }

        bluetooth::le_audio::BasicAudioAnnouncementBisConfig bis_config;

        // Parse BIS Index (1 byte)
        bis_config.bis_index = p[offset++];

        // Parse Codec Specific Configuration Length (1 byte)
        if (offset >= data.size()) {
          log::error("Not enough data for BIS codec config length");
          return false;
        }
        uint8_t bis_codec_config_len = p[offset++];

        // Parse BIS Codec Specific Configuration (LTV format)
        if (offset + bis_codec_config_len > data.size()) {
          log::error("Not enough data for BIS codec config");
          return false;
        }

        if (bis_codec_config_len > 0) {
          size_t ltv_offset = 0;
          while (ltv_offset < bis_codec_config_len) {
            uint8_t ltv_len = p[offset + ltv_offset++];
            if (ltv_len == 0 || ltv_offset + ltv_len > bis_codec_config_len) {
              log::warn("Invalid LTV length in BIS codec config");
              break;
            }
            uint8_t ltv_type = p[offset + ltv_offset++];
            std::vector<uint8_t> ltv_value(p + offset + ltv_offset,
                                           p + offset + ltv_offset + ltv_len - 1);
            bis_config.codec_specific_params[ltv_type] = ltv_value;
            ltv_offset += (ltv_len - 1);
          }
          offset += bis_codec_config_len;
        }

        log::info("  BIS {}: index={}", bis, bis_config.bis_index);
        subgroup.bis_configs.push_back(std::move(bis_config));
      }

      base.subgroup_configs.push_back(std::move(subgroup));
    }

    if (offset != data.size()) {
      log::warn("BASE parsing completed but {} bytes remain", data.size() - offset);
    }

    log::info("BASE parsing successful: {} subgroups, {} total BIS",
              base.subgroup_configs.size(),
              std::accumulate(base.subgroup_configs.begin(), base.subgroup_configs.end(), 0,
                            [](int sum, const auto& sg) { return sum + sg.bis_configs.size(); }));

    return true;
  }
};

IBroadcastSinkStateMachineCallbacks* BroadcastSinkStateMachineImpl::callbacks_ = nullptr;
BleScannerInterface* BroadcastSinkStateMachineImpl::ble_scanner_ = nullptr;

}  // namespace

std::unique_ptr<BroadcastSinkStateMachine> BroadcastSinkStateMachine::CreateInstance(
    BroadcastSinkStateMachineConfig config) {
  return std::make_unique<BroadcastSinkStateMachineImpl>(std::move(config));
}

void BroadcastSinkStateMachine::Initialize(IBroadcastSinkStateMachineCallbacks* callbacks,
                                           BleScannerInterface* ble_scanner) {
  BroadcastSinkStateMachineImpl::callbacks_ = callbacks;
  BroadcastSinkStateMachineImpl::ble_scanner_ = ble_scanner;
  log::info("Broadcast sink state machine initialized with ble_scanner={}",
            static_cast<void*>(ble_scanner));
}

namespace bluetooth::le_audio::broadcast_sink {

std::ostream& operator<<(std::ostream& os, const BroadcastSinkStateMachine::Message& msg) {
  static const char* msg_strings[] = {"START_BIG_SYNC", "STOP_BIG_SYNC", "STOP_SYNC"};
  os << msg_strings[static_cast<uint8_t>(msg)];
  return os;
}

std::ostream& operator<<(std::ostream& os, const BroadcastSinkStateMachine& machine) {
  os << "BroadcastSinkStateMachine{";
  os << "broadcast_id=0x" << std::hex << machine.GetBroadcastId() << std::dec;
  os << ", state=" << machine.GetState();
  os << ", config=" << machine.GetSinkConfiguration();

  if (machine.GetPaSyncInfo().has_value()) {
    os << ", pa_sync=" << *machine.GetPaSyncInfo();
  }

  if (machine.GetBigSyncInfo().has_value()) {
    os << ", big_sync=" << *machine.GetBigSyncInfo();
  }

  os << "}";
  return os;
}

}  // namespace bluetooth::le_audio::broadcast_sink
