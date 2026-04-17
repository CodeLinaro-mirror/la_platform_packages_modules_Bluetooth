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
        pa_sync_lost_(false),
        is_enhanced_(false),
        enhanced_iso_phase_(EnhancedIsoPhase::IDLE),
        big_info_iso_interval_(8),   // default 10 ms (8 × 1.25 ms)
        big_info_phy_(2),            // default LE2M
        big_info_num_bis_(4),        // default 4 BISes
        enhanced_iso_setup_index_(0),
        pending_tx_teardown_(false),
        pending_rx_teardown_(false),
        tx_paths_removed_(false),
        rx_paths_removed_(false) {
    stats_ = BroadcastSinkStats();
  }

  ~BroadcastSinkStateMachineImpl() {
    log::info("broadcast_id=0x{:x}, state={}, pa_sync_lost={}", GetBroadcastId(),
              SinkStateToString(GetState()), pa_sync_lost_);

    if (GetState() == SinkState::BIG_SYNCED || GetState() == SinkState::BIG_SYNCING) {
      TerminateBigSync();
    }

    if (GetState() != SinkState::IDLE && GetState() != SinkState::DISABLING &&
        GetState() != SinkState::STOPPING) {
      TerminatePaSync();
    }

    if (callbacks_) {
      uint8_t reason = pa_sync_lost_ ? kBroadcastSinkDestroyReasonPaSyncLost
                                     : kBroadcastSinkDestroyReasonNormal;
      callbacks_->OnStateMachineDestroyed(GetBroadcastId(), reason);
    }
  }

  bool Initialize() override {
    log::info("broadcast_id=0x{:x}, address={}, adv_sid={}", GetBroadcastId(),
              sm_config_.address.ToString(), sm_config_.adv_sid);

    sink_config_ = BroadcastSinkConfiguration();
    SetState(SinkState::PA_SYNCING);
    if (callbacks_) {
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    }
    StartPaSync();
    return true;
  }

  uint32_t GetRegId() const override { return sm_config_.reg_id; }
  uint32_t GetBroadcastId() const override { return sm_config_.broadcast_id; }
  const RawAddress& GetSourceAddress() const override { return sm_config_.address; }
  uint8_t GetAddressType() const override { return sm_config_.address_type; }
  uint8_t GetAdvSid() const override { return sm_config_.adv_sid; }
  const std::string& GetBroadcastName() const override { return sm_config_.broadcast_name; }
  bool IsPublic() const override { return sm_config_.is_public; }
  uint16_t GetPaSyncTimeout() const override { return sm_config_.pa_sync_timeout; }

  bool IsEncrypted() const override { return is_encrypted_; }
  const BroadcastSinkConfiguration& GetSinkConfiguration() const override { return sink_config_; }
  std::optional<BroadcastCode> GetBroadcastCode() const override { return broadcast_code_; }
  std::optional<bluetooth::le_audio::BasicAudioAnnouncementData> GetBaseData() const override {
    return base_data_;
  }
  std::optional<bluetooth::le_audio::PublicBroadcastAnnouncementData> GetPublicAnnouncement() const override {
    return sm_config_.public_announcement;
  }
  bool IsEnhanced() const override { return is_enhanced_; }

  void OnAudioStart() override {
    if (!is_enhanced_) return;

    switch (enhanced_iso_phase_) {
      case EnhancedIsoPhase::IDLE:
        log::info("broadcast_id=0x{:x}, OnAudioStart [1st]: sending enhanced broadcast command", GetBroadcastId());
        enhanced_iso_phase_ = EnhancedIsoPhase::DBIG_SETUP;
        SendDbigSetupCommand();
        break;
      case EnhancedIsoPhase::TX_DONE:
        if (!big_sync_info_.has_value() || big_sync_info_->bis_conn_handles.empty()) {
          log::error("broadcast_id=0x{:x}, OnAudioStart [2nd]: no BIG sync info", GetBroadcastId());
          return;
        }
        log::info("broadcast_id=0x{:x}, OnAudioStart [2nd]: starting RX ISO path setup", GetBroadcastId());
        enhanced_iso_phase_ = EnhancedIsoPhase::RX_SETUP;
        enhanced_iso_setup_index_ = 0;
        TriggerIsoDatapathSetup(big_sync_info_->bis_conn_handles[0],
            bluetooth::hci::iso_manager::kIsoDataPathDirectionOut /* RX */);
        break;
      default:
        log::warn("broadcast_id=0x{:x}, OnAudioStart in unexpected phase={}", GetBroadcastId(),
                  static_cast<int>(enhanced_iso_phase_));
        break;
    }
  }

  void OnDbigSetupComplete(uint8_t status) override {
    if (!is_enhanced_ || enhanced_iso_phase_ != EnhancedIsoPhase::DBIG_SETUP) return;

    if (status != 0x00) {
      log::error("broadcast_id=0x{:x}, enhanced broadcast setup failed, status=0x{:02x}", GetBroadcastId(), status);
      enhanced_iso_phase_ = EnhancedIsoPhase::IDLE;
      SetState(SinkState::PA_SYNCED);
      if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      return;
    }
    log::info("broadcast_id=0x{:x}, enhanced broadcast setup complete, issuing BIG_CREATE_SYNC", GetBroadcastId());
    CreateBigSync();
  }

  void StartSync() override {
    ProcessMessage(Message::START_BIG_SYNC, nullptr);
  }

  void StopSync() override {
    ProcessMessage(Message::STOP_SYNC, nullptr);
  }

  void UpdateBroadcastCode(const bluetooth::le_audio::BroadcastCode& code) override {
    broadcast_code_ = code;
  }

  void UpdatePublicAnnouncement(const std::string& broadcast_name,
                                const bluetooth::le_audio::PublicBroadcastAnnouncementData& pa) override {
    sm_config_.broadcast_name = broadcast_name;
    sm_config_.public_announcement = pa;
  }

  void UpdateBisIndices(const std::vector<uint8_t>& bis_indices) override {
    sink_config_.bis_indices = bis_indices;
  }

  void UpdateSinkConfiguration(const BroadcastSinkConfiguration& config) override {
    sink_config_ = config;
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
    log::info("broadcast_id=0x{:x}, state={}, message={}", GetBroadcastId(),
              SinkStateToString(GetState()), ToString(msg));

    switch (msg) {
      case Message::START_BIG_SYNC:
        start_big_sync_handlers[static_cast<uint8_t>(GetState())](data);
        break;

      case Message::REMOVE_TX_PATHS:
        /* Triggered by source HAL OnAudioSuspend.
         * For enhanced sources: remove TX ISO paths, then terminate BIG sync.
         * For standard sources: terminate BIG sync directly.
         * BTA layer acks source HAL in OnBigSyncTerminated callback. */
        REMOVE_TX_PATHS_handlers[static_cast<uint8_t>(GetState())](data);
        break;

      case Message::REMOVE_RX_PATHS:
        /* Triggered by sink HAL OnAudioSuspend.
         * Remove RX ISO paths; when all done:
         *   1. Ack sink HAL (OnRxIsoPathsRemoved).
         *   2. If TX paths also removed → terminate BIG sync.
         * If TX teardown (REMOVE_TX_PATHS) is already in progress, queue RX teardown. */
        if (is_enhanced_) {
          if (enhanced_iso_phase_ == EnhancedIsoPhase::TX_TEARDOWN) {
            /* TX teardown in progress — queue RX teardown */
            log::info("broadcast_id=0x{:x}, REMOVE_RX_PATHS: TX teardown in progress, queuing",
                      GetBroadcastId());
            pending_rx_teardown_ = true;
          } else {
            /* Start RX teardown immediately */
            rx_paths_removed_ = false;
            if (teardown_bis_handles_.empty() && big_sync_info_.has_value()) {
              teardown_bis_handles_ = big_sync_info_->bis_conn_handles;
            }
            if (!teardown_bis_handles_.empty()) {
              log::info("broadcast_id=0x{:x}, REMOVE_RX_PATHS: starting RX ISO path removal, "
                        "num_bis=%zu", GetBroadcastId(), teardown_bis_handles_.size());
              enhanced_iso_phase_ = EnhancedIsoPhase::RX_TEARDOWN;
              enhanced_iso_setup_index_ = 0;
              TriggerIsoDatapathTeardown(teardown_bis_handles_[0],
                  bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput /* RX */);
            } else {
              log::warn("broadcast_id=0x{:x}, REMOVE_RX_PATHS: no BIS handles, acking immediately",
                        GetBroadcastId());
              if (callbacks_) callbacks_->OnRxIsoPathsRemoved(GetBroadcastId());
              rx_paths_removed_ = true;
              if (tx_paths_removed_) TerminateBigSync();
            }
          }
        } else {
          log::warn("broadcast_id=0x{:x}, REMOVE_RX_PATHS for non-enhanced source", GetBroadcastId());
        }
        break;

      case Message::STOP_SYNC:
        stop_sync_handlers[static_cast<uint8_t>(GetState())](data);
        break;

      case Message::MESSAGE_COUNT:
        log::error("Invalid message type MESSAGE_COUNT");
        break;
    }
  }

  void OnSyncEstablished(uint8_t status, uint16_t sync_handle, uint8_t adv_sid,
                         uint8_t address_type, RawAddress address, uint8_t phy,
                         uint16_t interval) override {
    log::info("broadcast_id=0x{:x}, status=0x{:02x}, sync_handle=0x{:04x}", GetBroadcastId(),
              status, sync_handle);

    if (status != 0x00) {
      log::error("PA sync failed, status=0x{:02x}", status);
      SetState(SinkState::IDLE);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      return;
    }

    PaSyncInfo info;
    info.sync_handle = sync_handle;
    info.adv_sid = adv_sid;
    info.address = address;
    info.address_type = address_type;
    info.phy = phy;
    info.interval = interval;
    pa_sync_info_ = info;
    pa_sync_lost_ = false;

    SetState(SinkState::PA_SYNCED);
    callbacks_->OnPaSyncEstablished(GetBroadcastId(), sync_handle, adv_sid, address, address_type);
    callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
  }

  void OnSyncLost(uint16_t sync_handle) override {
    log::warn("broadcast_id=0x{:x}, sync_handle=0x{:04x}", GetBroadcastId(), sync_handle);

    if (!pa_sync_info_.has_value() || pa_sync_info_->sync_handle != sync_handle) return;

    stats_.sync_lost_count++;
    pa_sync_lost_ = true;

    uint16_t lost_handle = pa_sync_info_->sync_handle;
    pa_sync_info_ = std::nullopt;
    base_data_ = std::nullopt;

    if (big_sync_info_.has_value()) {
      uint8_t big_handle = big_sync_info_->big_handle;
      big_sync_info_ = std::nullopt;
      callbacks_->OnBigSyncLost(GetBroadcastId(), big_handle, 0x13);
    }

    SetState(SinkState::IDLE);
    callbacks_->OnPaSyncLost(GetBroadcastId(), lost_handle);
    callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
  }

  void OnPeriodicScanResult(uint16_t sync_handle, int8_t tx_power, int8_t rssi, uint8_t status,
                            std::vector<uint8_t> data) override {
    if (!pa_sync_info_.has_value() || pa_sync_info_->sync_handle != sync_handle) return;

    stats_.last_rssi = rssi;

    if (!data.empty()) {
      bluetooth::le_audio::BasicAudioAnnouncementData base;
      if (ParseBasicAudioAnnouncement(data, base)) {
        bool changed = !base_data_.has_value() || !(base == *base_data_);
        base_data_ = base;
        if (changed) {
          callbacks_->OnBaseDataReceived(GetBroadcastId(), base);
        }
      }
    }
  }

  void OnBigInfoReport(uint16_t sync_handle, bool encrypted) override {
    if (!pa_sync_info_.has_value() || pa_sync_info_->sync_handle != sync_handle) return;

    is_encrypted_ = encrypted;
    callbacks_->OnBigInfoReport(GetBroadcastId(), sync_handle, encrypted);
  }

  void SetBigInfoParams(uint16_t iso_interval, uint8_t phy, uint8_t num_bis) override {
    big_info_iso_interval_ = iso_interval;
    big_info_phy_          = phy;
    big_info_num_bis_      = num_bis;
    log::info("broadcast_id=0x{:x}, SetBigInfoParams: iso_interval={} ({}ms), phy={}, num_bis={}",
              GetBroadcastId(), iso_interval,
              static_cast<uint32_t>(iso_interval) * 125 / 100,
              phy, num_bis);
  }

  void HandleHciEvent(uint16_t event, void* data) override {
    switch (event) {
      case HCI_BLE_BIG_SYNC_EST_EVT:
        OnBigSyncEstablished(static_cast<big_sync_established_evt*>(data));
        break;
      case HCI_BLE_BIG_SYNC_LOST_EVT:
        OnBigSyncLost(static_cast<big_sync_lost_evt*>(data));
        break;
      default:
        log::warn("broadcast_id=0x{:x}, unknown HCI event=0x{:04x}", GetBroadcastId(), event);
        break;
    }
  }

  void OnSetupIsoDataPath(uint8_t status, uint16_t conn_handle, uint8_t direction) override {
    log::info("broadcast_id=0x{:x}, status=0x{:02x}, conn_handle=0x{:04x}, direction={}",
              GetBroadcastId(), status, conn_handle,
              direction == bluetooth::hci::iso_manager::kIsoDataPathDirectionIn ? "TX" : "RX");

    if (!big_sync_info_.has_value()) {
      log::error("OnSetupIsoDataPath: no BIG sync info");
      return;
    }

    if (status != 0x00) {
      log::error("Failed to setup ISO data path, status=0x{:02x}", status);
      SetState(SinkState::STOPPING);
      TerminateBigSync();
      return;
    }

    if (is_enhanced_) {
      auto& handles = big_sync_info_->bis_conn_handles;
      enhanced_iso_setup_index_++;

      if (enhanced_iso_phase_ == EnhancedIsoPhase::TX_SETUP) {
        if (enhanced_iso_setup_index_ >= static_cast<int>(handles.size())) {
          log::info("broadcast_id=0x{:x}, all TX ISO paths established", GetBroadcastId());
          enhanced_iso_phase_ = EnhancedIsoPhase::TX_DONE;
          enhanced_iso_setup_index_ = 0;
          if (callbacks_) callbacks_->OnTxIsoPathsReady(GetBroadcastId());
        } else {
          TriggerIsoDatapathSetup(handles[enhanced_iso_setup_index_],
              bluetooth::hci::iso_manager::kIsoDataPathDirectionIn /* TX */);
        }
      } else if (enhanced_iso_phase_ == EnhancedIsoPhase::RX_SETUP) {
        if (enhanced_iso_setup_index_ >= static_cast<int>(handles.size())) {
          log::info("broadcast_id=0x{:x}, all RX ISO paths established — enhanced source ready",
                    GetBroadcastId());
          enhanced_iso_phase_ = EnhancedIsoPhase::IDLE;
          enhanced_iso_setup_index_ = 0;
          SetState(SinkState::BIG_SYNCED);
          if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
        } else {
          TriggerIsoDatapathSetup(handles[enhanced_iso_setup_index_],
              bluetooth::hci::iso_manager::kIsoDataPathDirectionOut /* RX */);
        }
      } else {
        log::warn("broadcast_id=0x{:x}, OnSetupIsoDataPath in unexpected phase={}",
                  GetBroadcastId(), static_cast<int>(enhanced_iso_phase_));
      }
    } else {
      auto& handles = big_sync_info_->bis_conn_handles;
      auto it = std::find(handles.begin(), handles.end(), conn_handle);
      if (it == handles.end()) {
        log::error("Unknown conn_handle=0x{:04x}", conn_handle);
        return;
      }
      it = std::next(it);
      if (it == handles.end()) {
        log::info("broadcast_id=0x{:x}, all ISO data paths established", GetBroadcastId());
        SetState(SinkState::BIG_SYNCED);
        callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      } else {
        TriggerIsoDatapathSetup(*it, bluetooth::hci::iso_manager::kIsoDataPathDirectionOut);
      }
    }
  }

  void OnRemoveIsoDataPath(uint8_t status, uint16_t conn_handle) override {
    log::info("broadcast_id=0x{:x}, status=0x{:02x}, conn_handle=0x{:04x}", GetBroadcastId(),
              status, conn_handle);

    if (status != 0x00) {
      log::error("Failed to remove ISO data path, status=0x{:02x} — continuing teardown", status);
    }

    if (is_enhanced_ && (enhanced_iso_phase_ == EnhancedIsoPhase::TX_TEARDOWN ||
                         enhanced_iso_phase_ == EnhancedIsoPhase::RX_TEARDOWN)) {
      /* ---------------------------------------------------------------
       * Enhanced source teardown — correct sequence per enhanced broadcast spec:
       *
       * RX_TEARDOWN (sink HAL suspend via REMOVE_RX_PATHS):
       *   Remove RX paths (kRemoveIsoDataPathDirectionOutput) one by one.
       *   When all done:
       *     1. Ack sink HAL (OnRxIsoPathsRemoved).
       *     2. Set rx_paths_removed_=true.
       *     3. If tx_paths_removed_ → TerminateBigSync().
       *        Else wait for TX teardown to complete.
       *
       * TX_TEARDOWN (source HAL suspend via REMOVE_TX_PATHS):
       *   Remove TX paths (kRemoveIsoDataPathDirectionInput) one by one.
       *   When all done:
       *     1. Set tx_paths_removed_=true.
       *     2. If pending_rx_teardown_ → start RX teardown now.
       *        Else if rx_paths_removed_ → TerminateBigSync().
       *        Else wait for RX teardown to complete.
       *
       * BIG sync is terminated only after BOTH TX and RX paths are removed.
       * OnBigTerminateSyncComplete acks source HAL via OnBigSyncTerminated.
       * --------------------------------------------------------------- */
      enhanced_iso_setup_index_++;
      bool is_tx = (enhanced_iso_phase_ == EnhancedIsoPhase::TX_TEARDOWN);
      const char* phase_name = is_tx ? "TX" : "RX";

      if (enhanced_iso_setup_index_ >= static_cast<int>(teardown_bis_handles_.size())) {
        log::info("broadcast_id=0x{:x}, all %s ISO paths removed", GetBroadcastId(), phase_name);
        enhanced_iso_setup_index_ = 0;

        if (is_tx) {
          /* All TX paths removed */
          enhanced_iso_phase_ = EnhancedIsoPhase::IDLE;
          tx_paths_removed_ = true;

          if (pending_rx_teardown_) {
            /* RX teardown was queued — start it now */
            pending_rx_teardown_ = false;
            log::info("broadcast_id=0x{:x}, TX done, starting queued RX ISO path removal, "
                      "num_bis=%zu", GetBroadcastId(), teardown_bis_handles_.size());
            enhanced_iso_phase_ = EnhancedIsoPhase::RX_TEARDOWN;
            enhanced_iso_setup_index_ = 0;
            TriggerIsoDatapathTeardown(teardown_bis_handles_[0],
                bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput /* RX */);
          } else if (rx_paths_removed_) {
            /* RX already done — both paths removed, terminate BIG sync */
            log::info("broadcast_id=0x{:x}, TX done, RX already done — terminating BIG sync",
                      GetBroadcastId());
            TerminateBigSync();
          } else {
            /* Waiting for RX teardown (REMOVE_RX_PATHS not yet received) */
            log::info("broadcast_id=0x{:x}, TX done, waiting for RX teardown",
                      GetBroadcastId());
          }
        } else {
          /* All RX paths removed — ack sink HAL immediately */
          enhanced_iso_phase_ = EnhancedIsoPhase::IDLE;
          if (callbacks_) callbacks_->OnRxIsoPathsRemoved(GetBroadcastId());
          rx_paths_removed_ = true;

          if (pending_tx_teardown_) {
            /* TX teardown was queued — start it now */
            pending_tx_teardown_ = false;
            log::info("broadcast_id=0x{:x}, RX done, starting queued TX ISO path removal, "
                      "num_bis=%zu", GetBroadcastId(), teardown_bis_handles_.size());
            enhanced_iso_phase_ = EnhancedIsoPhase::TX_TEARDOWN;
            enhanced_iso_setup_index_ = 0;
            TriggerIsoDatapathTeardown(teardown_bis_handles_[0],
                bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionInput /* TX */);
          } else if (tx_paths_removed_) {
            /* TX already done — both paths removed, terminate BIG sync */
            log::info("broadcast_id=0x{:x}, RX done, TX already done — terminating BIG sync",
                      GetBroadcastId());
            TerminateBigSync();
          } else {
            /* Waiting for TX teardown (REMOVE_TX_PATHS not yet received) */
            log::info("broadcast_id=0x{:x}, RX done, waiting for TX teardown",
                      GetBroadcastId());
          }
        }
      } else {
        uint8_t direction = is_tx
            ? bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionInput   /* TX */
            : bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput; /* RX */
        log::info("broadcast_id=0x{:x}, removing %s ISO path [%d/%zu]",
                  GetBroadcastId(), phase_name,
                  enhanced_iso_setup_index_, teardown_bis_handles_.size());
        TriggerIsoDatapathTeardown(teardown_bis_handles_[enhanced_iso_setup_index_], direction);
      }
      return;
    }

    /* Standard source teardown: use teardown_bis_handles_ or big_sync_info_ */
    const std::vector<uint16_t>* handles_ptr = nullptr;
    if (!teardown_bis_handles_.empty()) {
      handles_ptr = &teardown_bis_handles_;
    } else if (big_sync_info_.has_value()) {
      handles_ptr = &big_sync_info_->bis_conn_handles;
    }

    if (!handles_ptr) {
      log::warn("OnRemoveIsoDataPath: no BIS handles available");
      return;
    }

    const auto& handles = *handles_ptr;
    auto it = std::find(handles.begin(), handles.end(), conn_handle);
    if (it == handles.end()) {
      log::error("Unknown conn_handle=0x{:04x}", conn_handle);
      return;
    }

    it = std::next(it);
    if (it == handles.end()) {
      log::info("broadcast_id=0x{:x}, all ISO paths removed, terminating BIG sync", GetBroadcastId());
      teardown_bis_handles_.clear();
      TerminateBigSync();
    } else {
      TriggerIsoDatapathTeardown(*it,
          bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput /* RX */);
    }
  }

  void OnBigTerminateSyncComplete(uint8_t big_handle, uint8_t status) override {
    log::info("broadcast_id=0x{:x}, big_handle={}, status=0x{:02x}", GetBroadcastId(),
              big_handle, status);

    if (big_sync_info_.has_value() && big_sync_info_->big_handle == big_handle) {
      big_sync_info_ = std::nullopt;
    }

    /* BIG sync is terminated only after BOTH TX and RX ISO paths are removed.
     * Notify upper layer — BTA layer acks source HAL in OnBigSyncTerminated. */
    callbacks_->OnBigSyncTerminated(GetBroadcastId(), big_handle, status);

    if (GetState() == SinkState::DISABLING) {
      /* Both TX and RX paths already removed before we got here.
       * Reset teardown state and transition to PA_SYNCED. */
      log::info("broadcast_id=0x{:x}, BIG terminated (all paths removed), transitioning to PA_SYNCED",
                GetBroadcastId());
      enhanced_iso_phase_ = EnhancedIsoPhase::IDLE;
      teardown_bis_handles_.clear();
      tx_paths_removed_ = false;
      rx_paths_removed_ = false;
      pending_rx_teardown_ = false;
      pending_tx_teardown_ = false;
      SetState(SinkState::PA_SYNCED);
      if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    } else if (GetState() == SinkState::STOPPING) {
      log::info("broadcast_id=0x{:x}, BIG terminated, continuing PA sync termination", GetBroadcastId());
      TerminatePaSync();
    } else {
      log::warn("broadcast_id=0x{:x}, BIG terminate complete in unexpected state={}",
                GetBroadcastId(), SinkStateToString(GetState()));
    }
  }

  static IBroadcastSinkStateMachineCallbacks* callbacks_;
  static BleScannerInterface* ble_scanner_;

 private:
  BroadcastSinkStateMachineConfig sm_config_;
  BroadcastSinkConfiguration sink_config_;
  std::optional<PaSyncInfo> pa_sync_info_;
  std::optional<BigSyncInfo> big_sync_info_;
  std::optional<bluetooth::le_audio::BasicAudioAnnouncementData> base_data_;
  BroadcastSinkStats stats_;
  bool is_encrypted_;
  std::optional<BroadcastCode> broadcast_code_;
  bool pa_sync_lost_;
  bool is_enhanced_;

  /**
   * Phase of the enhanced (enhanced broadcast) ISO data path setup/teardown.
   *
   * Setup:
   *   IDLE       -- waiting for 1st HIDL start.
   *   DBIG_SETUP -- enhanced broadcast vendor HCI command sent.
   *   TX_SETUP   -- BIG sync established; setting up TX paths.
   *   TX_DONE    -- All TX paths configured; waiting for 2nd HIDL start.
   *   RX_SETUP   -- Setting up RX paths (2nd HIDL start).
   *
   * Teardown:
   *   TX_TEARDOWN -- Removing TX paths (source HAL suspend via REMOVE_TX_PATHS).
   *                  When done → TerminateBigSync().
   *   RX_TEARDOWN -- Removing RX paths (sink HAL suspend via REMOVE_RX_PATHS).
   *                  When done → OnRxIsoPathsRemoved() → PA_SYNCED.
   */
  enum class EnhancedIsoPhase : uint8_t {
    IDLE        = 0,
    DBIG_SETUP  = 1,
    TX_SETUP    = 2,
    TX_DONE     = 3,
    RX_SETUP    = 4,
    TX_TEARDOWN = 5,
    RX_TEARDOWN = 6,
  };
  EnhancedIsoPhase enhanced_iso_phase_;
  int enhanced_iso_setup_index_;

  /**
   * BIS connection handles saved at the start of teardown.
   * big_sync_info_ may be cleared by OnBigSyncLost before teardown completes.
   */
  std::vector<uint16_t> teardown_bis_handles_;

  /**
   * Set when REMOVE_RX_PATHS (sink HAL suspend) arrives while TX teardown
   * (REMOVE_TX_PATHS) is already in progress.  RX teardown starts after TX
   * teardown completes in OnRemoveIsoDataPath (TX_TEARDOWN case).
   */
  bool pending_rx_teardown_;

  /**
   * Set when REMOVE_TX_PATHS (source HAL suspend) arrives while RX teardown
   * (REMOVE_RX_PATHS) is already in progress.  TX teardown starts after RX
   * teardown completes in OnRemoveIsoDataPath (RX_TEARDOWN case).
   */
  bool pending_tx_teardown_;

  /**
   * Set when all TX ISO data paths have been removed.
   * BIG sync is terminated only when both tx_paths_removed_ and
   * rx_paths_removed_ are true.
   */
  bool tx_paths_removed_;

  /**
   * Set when all RX ISO data paths have been removed and sink HAL has been
   * acknowledged (OnRxIsoPathsRemoved called).
   * BIG sync is terminated only when both tx_paths_removed_ and
   * rx_paths_removed_ are true.
   */
  bool rx_paths_removed_;

  /**
   * BIG info parameters captured from the controller BIG Info Report
   * (via OnBigInfoReportFull → SetBigInfoParams).
   * Used in SendDbigSetupCommand() to select the correct
   * bis_control_event_interval from the parameter table.
   *
   * iso_interval: ISO interval in units of 1.25 ms (e.g. 8 = 10 ms)
   * phy:          PHY: 1=LE1M, 2=LE2M, 3=Coded
   * num_bis:      Number of BISes in the BIG
   */
  uint16_t big_info_iso_interval_;
  uint8_t  big_info_phy_;
  uint8_t  big_info_num_bis_;

  // Message handlers for each state
  typedef std::function<void(const void*)> msg_handler_t;

  // START_BIG_SYNC handlers
  const std::array<msg_handler_t, static_cast<size_t>(SinkState::STATE_COUNT)>
      start_big_sync_handlers{
          /* IDLE */
          [this](const void*) {
            log::warn("broadcast_id=0x{:x}, cannot start BIG sync from IDLE", GetBroadcastId());
          },
          /* PA_SYNCING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, PA syncing, wait for PA sync complete", GetBroadcastId());
          },
          /* PA_SYNCED */
          [this](const void*) {
            if (!base_data_.has_value()) {
              log::warn("broadcast_id=0x{:x}, no BASE data, cannot start BIG sync", GetBroadcastId());
              return;
            }
            SetState(SinkState::BIG_SYNCING);
            if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            if (is_enhanced_) {
              enhanced_iso_phase_ = EnhancedIsoPhase::IDLE;
              log::info("broadcast_id=0x{:x}, enhanced source: waiting for 1st HIDL start",
                        GetBroadcastId());
            } else {
              CreateBigSync();
            }
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

  // REMOVE_TX_PATHS handlers — triggered by source HAL OnAudioSuspend
  // Enhanced: remove TX ISO paths → TerminateBigSync → ack source HAL
  // Standard: TerminateBigSync directly
  const std::array<msg_handler_t, static_cast<size_t>(SinkState::STATE_COUNT)>
      REMOVE_TX_PATHS_handlers{
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
          },
          /* STREAMING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, source HAL suspend (REMOVE_TX_PATHS): "
                      "starting TX ISO path removal",
                      GetBroadcastId());
            SetState(SinkState::DISABLING);
            if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            /* Preserve rx_paths_removed_ — RX teardown may have already completed
             * (REMOVE_RX_PATHS arrived before REMOVE_TX_PATHS). */
            tx_paths_removed_ = false;
            pending_rx_teardown_ = false;
            pending_tx_teardown_ = false;
            /* Save BIS handles — big_sync_info_ may be cleared by OnBigSyncLost */
            if (big_sync_info_.has_value()) {
              teardown_bis_handles_ = big_sync_info_->bis_conn_handles;
            } else {
              teardown_bis_handles_.clear();
            }
            if (is_enhanced_ && !teardown_bis_handles_.empty()) {
              /* Start TX teardown immediately — no need to wait for RX teardown.
               * TX and RX teardowns are independent; BIG sync is terminated only
               * after BOTH tx_paths_removed_ and rx_paths_removed_ are true. */
              log::info("broadcast_id=0x{:x}, REMOVE_TX_PATHS: starting TX ISO path removal, "
                        "num_bis=%zu", GetBroadcastId(), teardown_bis_handles_.size());
              enhanced_iso_phase_ = EnhancedIsoPhase::TX_TEARDOWN;
              enhanced_iso_setup_index_ = 0;
              TriggerIsoDatapathTeardown(teardown_bis_handles_[0],
                  bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionInput /* TX */);
            } else {
              /* Standard source or no handles: terminate BIG sync directly */
              TerminateBigSync();
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

  // STOP_SYNC handlers
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
            if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            TerminatePaSync();
          },
          /* BIG_SYNCING */
          [this](const void*) {
            SetState(SinkState::STOPPING);
            if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
          },
          /* STREAMING */
          [this](const void*) {
            SetState(SinkState::STOPPING);
            if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            if (big_sync_info_.has_value() && !big_sync_info_->bis_conn_handles.empty()) {
              teardown_bis_handles_ = big_sync_info_->bis_conn_handles;
              TriggerIsoDatapathTeardown(teardown_bis_handles_[0],
                  bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput);
            } else {
              TerminateBigSync();
            }
          },
          /* DISABLING */
          [this](const void*) {
            SetState(SinkState::STOPPING);
            if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
            if (big_sync_info_.has_value() && !big_sync_info_->bis_conn_handles.empty()) {
              TriggerIsoDatapathTeardown(big_sync_info_->bis_conn_handles[0],
                  bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput);
            } else {
              TerminatePaSync();
            }
          },
          /* STOPPING */
          [this](const void*) {
            log::info("broadcast_id=0x{:x}, already stopping", GetBroadcastId());
          },
      };

  void StartPaSync() {
    if (!ble_scanner_) { log::error("BLE scanner not initialized"); return; }
    ble_scanner_->StartSync(sm_config_.adv_sid, sm_config_.address, 0,
                            sm_config_.pa_sync_timeout, sm_config_.reg_id,
                            kScannerClientIdLeAudio);
  }

  void TerminatePaSync() {
    if (!pa_sync_info_.has_value()) {
      SetState(SinkState::IDLE);
      if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      return;
    }
    if (!ble_scanner_) { log::error("BLE scanner not initialized"); return; }
    ble_scanner_->StopSync(pa_sync_info_->sync_handle, kScannerClientIdLeAudio);
    SetState(SinkState::IDLE);
    if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
  }

  void CreateBigSync() {
    if (!pa_sync_info_.has_value() || !base_data_.has_value()) {
      log::error("broadcast_id=0x{:x}, cannot create BIG sync: missing PA sync or BASE data",
                 GetBroadcastId());
      return;
    }
    bluetooth::hci::iso_manager::big_sync_params params = {
        .sync_handle   = pa_sync_info_->sync_handle,
        .encryption    = is_encrypted_ ? static_cast<uint8_t>(0x01) : static_cast<uint8_t>(0x00),
        .broadcast_code = broadcast_code_.value_or(std::array<uint8_t, 16>({0})),
        .mse           = sink_config_.mse,
        .big_sync_timeout = sink_config_.big_sync_timeout,
        .bis           = sink_config_.bis_indices,
    };
    IsoManager::GetInstance()->BigCreateSync(static_cast<uint8_t>(sm_config_.reg_id),
                                             std::move(params));
  }

  void TerminateBigSync() {
    if (!big_sync_info_.has_value()) {
      if (GetState() == SinkState::DISABLING) {
        SetState(SinkState::PA_SYNCED);
        if (callbacks_) callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      } else if (GetState() == SinkState::STOPPING) {
        TerminatePaSync();
      }
      return;
    }
    IsoManager::GetInstance()->BigTerminateSync(big_sync_info_->big_handle);
  }

  /**
   * Select bis_control_event_interval from the parameter table:
   *
   *  ISO interval (1.25 ms units) | ISO interval | bis_control_event_interval
   *  ─────────────────────────────┼──────────────┼───────────────────────────
   *   ≤  6  ( 7.5 ms)             │  7.5 ms      │  12
   *   ≤  8  (10   ms)             │ 10   ms      │   9
   *   ≤ 16  (20   ms)             │ 20   ms      │   6
   *   > 16  (30   ms)             │ 30   ms      │   4
   *
   * Both LE2M and Coded(S2) use the same mapping.
   * iso_interval is in units of 1.25 ms (controller BIG Info Report field).
   */
  static uint8_t SelectBisControlEventInterval(uint16_t iso_interval_1_25ms) {
    if (iso_interval_1_25ms <= 6)  return 12;  //  7.5 ms
    if (iso_interval_1_25ms <= 8)  return 9;   // 10   ms
    if (iso_interval_1_25ms <= 16) return 6;   // 20   ms
    return 4;                                   // 30   ms
  }

  void SendDbigSetupCommand() {
    uint8_t bis_ctrl_interval = SelectBisControlEventInterval(big_info_iso_interval_);

    log::info("broadcast_id=0x{:x}, SendDbigSetupCommand: iso_interval={} ({}ms), "
              "phy={}, num_bis={}, bis_control_event_interval={}",
              GetBroadcastId(), big_info_iso_interval_,
              static_cast<uint32_t>(big_info_iso_interval_) * 125 / 100,
              big_info_phy_, big_info_num_bis_, bis_ctrl_interval);

    bluetooth::hci::iso_manager::dbig_create_params params = {};
    params.dbig_handle                = static_cast<uint8_t>(sm_config_.reg_id);
    params.dbig_feature_set           = 0x03;  // Feature set: duplex TX+RX
    params.bis_detection_attempts     = 0x0A;  // 10 detection attempts
    params.max_payload_dbig_control   = 0x10;  // 16 bytes max payload
    params.bis_control_event_interval = bis_ctrl_interval;
    params.send_exit                  = 0x02;  // 2
    params.pgp_timeout                = 0x0A;  // 10
    params.pgo_timeout                = 0x0A;  // 10
    params.sgo_timeout                = 0x06;  // 6
    params.tx_power                   = 0x08;  // 8
    IsoManager::GetInstance()->CreateDbig(params);
  }

  void OnBigSyncEstablished(big_sync_established_evt* evt) {
    log::info("broadcast_id=0x{:x}, status=0x{:02x}, big_handle={}, num_bis={}",
              GetBroadcastId(), evt->status, evt->big_handle, evt->conn_handles.size());

    if (evt->status != 0x00) {
      log::error("BIG sync failed, status=0x{:02x}", evt->status);
      SetState(SinkState::PA_SYNCED);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
      return;
    }

    BigSyncInfo info;
    info.big_handle           = evt->big_handle;
    info.pa_sync_handle       = pa_sync_info_->sync_handle;
    info.bis_conn_handles     = evt->conn_handles;
    info.transport_latency_us = evt->transport_latency_big;
    info.nse                  = evt->nse;
    info.bn                   = evt->bn;
    info.pto                  = evt->pto;
    info.irc                  = evt->irc;
    info.max_pdu              = evt->max_pdu;
    info.iso_interval         = evt->iso_interval;
    info.num_bis              = static_cast<uint8_t>(evt->conn_handles.size());
    big_sync_info_ = info;

    callbacks_->OnBigSyncEstablished(GetBroadcastId(), evt->big_handle, evt->conn_handles);

    if (evt->conn_handles.empty()) {
      log::error("No BIS connection handles in BIG sync established event");
      return;
    }

    if (is_enhanced_) {
      log::info("broadcast_id=0x{:x}, enhanced source: BIG sync established, starting TX ISO setup",
                GetBroadcastId());
      enhanced_iso_phase_ = EnhancedIsoPhase::TX_SETUP;
      enhanced_iso_setup_index_ = 0;
      TriggerIsoDatapathSetup(evt->conn_handles[0],
          bluetooth::hci::iso_manager::kIsoDataPathDirectionIn /* TX */);
    } else {
      log::info("broadcast_id=0x{:x}, standard source: starting RX ISO setup", GetBroadcastId());
      enhanced_iso_setup_index_ = 0;
      TriggerIsoDatapathSetup(evt->conn_handles[0],
          bluetooth::hci::iso_manager::kIsoDataPathDirectionOut /* RX */);
    }
  }

  void OnBigSyncLost(big_sync_lost_evt* evt) {
    log::warn("broadcast_id=0x{:x}, big_handle={}, reason=0x{:02x}",
              GetBroadcastId(), evt->big_handle, evt->reason);

    if (!big_sync_info_.has_value() || big_sync_info_->big_handle != evt->big_handle) return;

    uint8_t big_handle = big_sync_info_->big_handle;
    big_sync_info_ = std::nullopt;
    callbacks_->OnBigSyncLost(GetBroadcastId(), big_handle, evt->reason);

    if (GetState() == SinkState::DISABLING) {
      SetState(SinkState::PA_SYNCED);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    } else if (GetState() == SinkState::STOPPING) {
      TerminatePaSync();
    } else {
      SetState(SinkState::PA_SYNCED);
      callbacks_->OnStateMachineEvent(GetBroadcastId(), GetState());
    }
  }

  void TriggerIsoDatapathSetup(uint16_t conn_handle, uint8_t direction) {
    if (!base_data_.has_value()) {
      log::error("No BASE data for ISO data path setup");
      return;
    }
    auto& cfg = sink_config_.data_path.isoDataPathConfig;
    bluetooth::hci::iso_manager::iso_data_path_params params = {
        .data_path_dir  = direction,
        .data_path_id   = static_cast<uint8_t>(sink_config_.data_path.dataPathId),
        .codec_id_format = static_cast<uint8_t>(
                cfg.isTransparent ? bluetooth::hci::kIsoCodingFormatTransparent
                                  : cfg.codecId.coding_format),
        .codec_id_company = static_cast<uint16_t>(
                cfg.isTransparent ? 0x0000 : cfg.codecId.vendor_company_id),
        .codec_id_vendor  = static_cast<uint16_t>(
                cfg.isTransparent ? 0x0000 : cfg.codecId.vendor_codec_id),
        .controller_delay = cfg.controllerDelayUs,
        .codec_conf       = cfg.configuration,
    };
    IsoManager::GetInstance()->SetupIsoDataPath(conn_handle, std::move(params));
  }

  void TriggerIsoDatapathTeardown(
      uint16_t conn_handle,
      uint8_t direction = bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionOutput) {
    log::info("broadcast_id=0x{:x}, conn_handle=0x{:04x}, direction={}",
              GetBroadcastId(), conn_handle,
              direction == bluetooth::hci::iso_manager::kRemoveIsoDataPathDirectionInput
                  ? "TX" : "RX");
    IsoManager::GetInstance()->RemoveIsoDataPath(conn_handle, direction);
  }

  bool ParseBasicAudioAnnouncement(const std::vector<uint8_t>& data,
                                   bluetooth::le_audio::BasicAudioAnnouncementData& base) {
    if (data.size() < 8) { log::error("BASE data too short: {} bytes", data.size()); return false; }

    size_t offset = 0;
    const uint8_t* p = data.data();

    uint8_t ad_length = p[offset++];
    uint8_t ad_type   = p[offset++];
    uint16_t service_uuid = p[offset] | (p[offset + 1] << 8);
    offset += 2;
    log::info("AD: length={}, type=0x{:02x}, uuid=0x{:04x}", ad_length, ad_type, service_uuid);

    const uint8_t* base_ptr = p + offset;
    STREAM_TO_UINT24(base.presentation_delay_us, base_ptr);
    offset += 3;

    uint8_t num_subgroups = p[offset++];
    if (num_subgroups == 0) { log::error("BASE has no subgroups"); return false; }

    for (uint8_t sg = 0; sg < num_subgroups; sg++) {
      if (offset >= data.size()) { log::error("Unexpected end at subgroup {}", sg); return false; }

      bluetooth::le_audio::BasicAudioAnnouncementSubgroup subgroup;
      uint8_t num_bis = p[offset++];
      if (num_bis == 0) { log::error("Subgroup {} has no BIS", sg); return false; }

      if (offset + 5 > data.size()) { log::error("Not enough data for codec ID"); return false; }
      subgroup.codec_config.codec_id = p[offset++];
      const uint8_t* tp = p + offset;
      STREAM_TO_UINT16(subgroup.codec_config.vendor_company_id, tp); offset += 2;
      tp = p + offset;
      STREAM_TO_UINT16(subgroup.codec_config.vendor_codec_id, tp);   offset += 2;

      if (offset >= data.size()) return false;
      uint8_t cc_len = p[offset++];
      if (offset + cc_len > data.size()) return false;
      if (cc_len > 0) {
        size_t lo = 0;
        while (lo < cc_len) {
          uint8_t ll = p[offset + lo++];
          if (ll == 0 || lo + ll > cc_len) break;
          uint8_t lt = p[offset + lo++];
          subgroup.codec_config.codec_specific_params[lt] =
              std::vector<uint8_t>(p + offset + lo, p + offset + lo + ll - 1);
          lo += (ll - 1);
        }
        offset += cc_len;
      }

      if (offset >= data.size()) return false;
      uint8_t meta_len = p[offset++];
      if (offset + meta_len > data.size()) return false;
      if (meta_len > 0) {
        size_t lo = 0;
        while (lo < meta_len) {
          uint8_t ll = p[offset + lo++];
          if (ll == 0 || lo + ll > meta_len) break;
          uint8_t lt = p[offset + lo++];
          subgroup.metadata[lt] =
              std::vector<uint8_t>(p + offset + lo, p + offset + lo + ll - 1);
          lo += (ll - 1);
        }
        offset += meta_len;
      }

      for (uint8_t bis = 0; bis < num_bis; bis++) {
        if (offset >= data.size()) return false;
        bluetooth::le_audio::BasicAudioAnnouncementBisConfig bc;
        bc.bis_index = p[offset++];
        if (offset >= data.size()) return false;
        uint8_t bcc_len = p[offset++];
        if (offset + bcc_len > data.size()) return false;
        if (bcc_len > 0) {
          size_t lo = 0;
          while (lo < bcc_len) {
            uint8_t ll = p[offset + lo++];
            if (ll == 0 || lo + ll > bcc_len) break;
            uint8_t lt = p[offset + lo++];
            bc.codec_specific_params[lt] =
                std::vector<uint8_t>(p + offset + lo, p + offset + lo + ll - 1);
            lo += (ll - 1);
          }
          offset += bcc_len;
        }
        subgroup.bis_configs.push_back(std::move(bc));
      }

      if (num_bis >= 3 && !is_enhanced_) {
        is_enhanced_ = true;
        log::info("broadcast_id=0x{:x}, subgroup {} has {} BISes: enhanced source detected",
                  GetBroadcastId(), sg, num_bis);
        if (callbacks_) callbacks_->OnEnhancedSourceDetected(GetBroadcastId(), num_bis);
      }

      base.subgroup_configs.push_back(std::move(subgroup));
    }

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
}

namespace bluetooth::le_audio::broadcast_sink {

std::ostream& operator<<(std::ostream& os, const BroadcastSinkStateMachine::Message& msg) {
  static const char* names[] = {
      "START_BIG_SYNC", "REMOVE_TX_PATHS", "REMOVE_RX_PATHS", "STOP_SYNC"};
  os << names[static_cast<uint8_t>(msg)];
  return os;
}

std::ostream& operator<<(std::ostream& os, const BroadcastSinkStateMachine& m) {
  os << "BroadcastSinkStateMachine{broadcast_id=0x" << std::hex << m.GetBroadcastId()
     << std::dec << ", state=" << m.GetState()
     << ", config=" << m.GetSinkConfiguration();
  if (m.GetPaSyncInfo().has_value()) os << ", pa_sync=" << *m.GetPaSyncInfo();
  if (m.GetBigSyncInfo().has_value()) os << ", big_sync=" << *m.GetBigSyncInfo();
  os << "}";
  return os;
}

}  // namespace bluetooth::le_audio::broadcast_sink
