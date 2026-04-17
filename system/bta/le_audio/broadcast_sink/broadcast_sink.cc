/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "bta/include/bta_le_audio_broadcast_sink_api.h"

#include <base/functional/bind.h>
#include <bluetooth/log.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "bta/le_audio/audio_hal_client/audio_hal_client.h"
#include "bta/le_audio/broadcast_sink/broadcast_sink_types.h"
#include "bta/le_audio/broadcast_sink/state_machine.h"
#include "bta/le_audio/codec_manager.h"
#include "gd/os/rand.h"
#include "hardware/ble_scanner.h"
#include "main/shim/le_scanning_manager.h"
#include "stack/include/advertise_data_parser.h"
#include "stack/include/btm_ble_api.h"
#include "stack/include/btm_iso_api.h"
#include "stack/include/hci_error_code.h"
#include "types/bluetooth/uuid.h"
#include "types/raw_address.h"

using bluetooth::hci::IsoManager;
using bluetooth::hci::iso_manager::BigSyncCallbacks;
using bluetooth::hci::iso_manager::DbigCallbacks;

using namespace bluetooth;
using namespace bluetooth::le_audio::broadcast_sink;
using bluetooth::le_audio::PublicBroadcastAnnouncementData;
using bluetooth::le_audio::DsaMode;

namespace {

class LeAudioBroadcastSinkImpl;
LeAudioBroadcastSinkImpl* instance = nullptr;
std::mutex instance_mutex;

/**
 * Tracked source information with state machine
 */
struct TrackedSource {
  std::unique_ptr<BroadcastSinkStateMachine> state_machine;
  bool is_notified;  // Flag to avoid duplicate source found notifications
  bool pending_big_rejoin;  // Flag to track if BIG rejoin is pending after sync lost

  TrackedSource() : is_notified(false), pending_big_rejoin(false) {}
};

/**
 * Helper function to generate PublicBroadcastAnnouncementData from raw metadata and features
 */
std::optional<PublicBroadcastAnnouncementData> GeneratePublicAnnouncement(
    const std::vector<uint8_t>& public_metadata, uint8_t public_features) {
  if (public_metadata.empty()) {
    return std::nullopt;
  }

  PublicBroadcastAnnouncementData pub_announcement;
  pub_announcement.features = public_features;

  // Parse public_metadata LTV bytes into metadata map
  bool is_public_metadata_valid = false;
  auto metadata_ltv = bluetooth::le_audio::types::LeAudioLtvMap::Parse(
      public_metadata.data(), public_metadata.size(), is_public_metadata_valid);

  if (!is_public_metadata_valid) {
    log::warn("Failed to parse public metadata LTV data");
    return std::nullopt;
  }

  // Directly assign the parsed LTV map values
  pub_announcement.metadata = metadata_ltv.Values();

  log::info("Generated public announcement: {} LTV entries, features=0x{:02x}",
            pub_announcement.metadata.size(), pub_announcement.features);

  return pub_announcement;
}

/**
 * Helper function to convert state machine data to BroadcastMetadata
 */
BroadcastMetadata ConvertToMetadata(const BroadcastSinkStateMachine* state_machine) {
  BroadcastMetadata metadata;
  metadata.broadcast_id = state_machine->GetBroadcastId();
  metadata.addr = state_machine->GetSourceAddress();
  metadata.addr_type = state_machine->GetAddressType();
  metadata.adv_sid = state_machine->GetAdvSid();
  metadata.broadcast_name = state_machine->GetBroadcastName();
  metadata.is_public = state_machine->IsPublic();
  metadata.pa_interval = 0;  // Will be filled when PA sync is established

  // Copy broadcast code if present
  auto broadcast_code = state_machine->GetBroadcastCode();
  if (broadcast_code.has_value()) {
    metadata.broadcast_code = broadcast_code;
  }

  // Copy BASE data if present
  auto base_data = state_machine->GetBaseData();
  if (base_data.has_value()) {
    metadata.basic_audio_announcement = base_data.value();
  }

  // Copy public announcement if present
  auto public_announcement = state_machine->GetPublicAnnouncement();
  if (public_announcement.has_value()) {
    metadata.public_announcement = public_announcement.value();
  }

  // Set encryption status
  metadata.is_encrypted = state_machine->IsEncrypted();

  return metadata;
}

/**
 * Implementation of the Broadcast Sink Manager
 */
class LeAudioBroadcastSinkImpl : public LeAudioBroadcastSink,
                                  public BigSyncCallbacks,
                                  public DbigCallbacks {
 public:
  explicit LeAudioBroadcastSinkImpl(BroadcastSinkCallbacks* callbacks,
                                     uint8_t max_source_capacity)
      : callbacks_(callbacks),
        max_source_capacity_(max_source_capacity),
        is_scanning_(false) {
    log::info("LeAudioBroadcastSinkImpl created with max_source_capacity={}",
              max_source_capacity_);
  }

  static bool InitializeScanner() {
    log::info("Initializing BLE scanner for broadcast sink");
    ble_scanner_ = bluetooth::shim::get_ble_scanner_instance();
    if (!ble_scanner_) {
      log::error("Failed to get BLE scanner instance");
      return false;
    }
    ble_scanner_->RegisterCallbacksNative(&scanning_callbacks_, kScannerClientIdLeAudio);
    log::info("BLE scanner instance obtained and callbacks registered successfully");
    return true;
  }

  static void InitializeStateMachine(LeAudioBroadcastSinkImpl* instance) {
    log::info("Initializing BroadcastSinkStateMachine subsystem");
    BroadcastSinkStateMachine::Initialize(&state_machine_callbacks_, ble_scanner_);
    // NOTE: DBIG callbacks are NOT registered here at BT turn-on.
    // They are registered lazily in StartEnhancedBroadcastSink() to avoid overriding
    // the DBIG callbacks registered by broadcast source.
    log::info("BroadcastSinkStateMachine initialized (DBIG callbacks deferred to StartEnhancedBroadcastSink)");
  }

  ~LeAudioBroadcastSinkImpl() override {
    log::info("LeAudioBroadcastSinkImpl destroyed");
  }

  void CleanUp() {
    log::info("Cleaning up broadcast sink");
    StopScanning();
    tracked_sources_.clear();
    callbacks_ = nullptr;
  }

  static void CleanupScanner() { ble_scanner_ = nullptr; }

  void Stop() {
    log::info("Stopping broadcast sink");
    StopScanning();
  }

  // Internal helper used by StartEnhancedBroadcastSink().
  // Not part of the public API — JoinSource was removed from the interface.
  void JoinSource(BroadcastId broadcast_id,
                  const std::optional<BroadcastCode>& broadcast_code,
                  const std::vector<uint8_t>& bis_indices) {
    log::info("JoinSource: broadcast_id=0x{:08x}, has_code={}, bis_indices_count={}",
              broadcast_id, broadcast_code.has_value(), bis_indices.size());

    if (tracked_sources_.count(broadcast_id) == 0) {
      log::error("No such broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) callbacks_->OnSourceJoinFailed(broadcast_id, 0);
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (!tracked_source.state_machine) {
      log::error("State machine not found for broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) callbacks_->OnSourceJoinFailed(broadcast_id, 0);
      return;
    }

    // Clear pending rejoin flags for ALL tracked sources when user explicitly requests to join
    for (auto& [id, source] : tracked_sources_) {
      if (source.pending_big_rejoin) {
        source.pending_big_rejoin = false;
        log::info("Cleared pending_big_rejoin flag for broadcast_id=0x{:08x}", id);
      }
    }
    log::info("JoinSource requested for broadcast_id=0x{:08x}, cleared all pending_big_rejoin flags", broadcast_id);

    auto* state_machine = tracked_source.state_machine.get();

    // Update broadcast code if present
    if (broadcast_code.has_value()) {
      log::info("Updated broadcast code for broadcast_id=0x{:08x}", broadcast_id);
      state_machine->UpdateBroadcastCode(broadcast_code.value());
      log::info("Broadcast code updated in state machine for broadcast_id=0x{:08x}", broadcast_id);
    }

    // Update BIS indices if provided, otherwise use all BISes from BASE data
    std::vector<uint8_t> selected_bis_indices;
    if (!bis_indices.empty()) {
      selected_bis_indices = bis_indices;
      log::info("Using provided BIS indices for broadcast_id=0x{:08x}: count={}",
                broadcast_id, bis_indices.size());
    } else {
      // Extract all BIS indices from BASE data
      auto base_data = state_machine->GetBaseData();
      if (base_data.has_value()) {
        uint8_t bis_index = 1;
        for (const auto& subgroup : base_data->subgroup_configs) {
          for (size_t i = 0; i < subgroup.bis_configs.size(); i++) {
            selected_bis_indices.push_back(bis_index++);
          }
        }
        log::info("No BIS indices provided, using all BISes from BASE data for broadcast_id=0x{:08x}: count={}",
                  broadcast_id, selected_bis_indices.size());
      }
    }

    // Update BIS indices in state machine
    if (!selected_bis_indices.empty()) {
      state_machine->UpdateBisIndices(selected_bis_indices);
      log::info("Updated BIS indices in state machine for broadcast_id=0x{:08x}", broadcast_id);
    }

    // Get BroadcastSinkConfiguration from CodecManager
    auto base_data = state_machine->GetBaseData();
    if (!base_data.has_value()) {
      log::error("No BASE data available for broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceJoinFailed(broadcast_id, 0);
      }
      return;
    }

    // Prepare requirements for GetBroadcastSinkConfig
    bluetooth::le_audio::CodecManager::BroadcastSinkConfigurationRequirements requirements;
    requirements.base_data = base_data.value();
    requirements.bis_indices = selected_bis_indices;

    // Get broadcast sink configuration from CodecManager
    auto sink_config = bluetooth::le_audio::CodecManager::GetInstance()->GetBroadcastSinkConfig(requirements);
    if (!sink_config) {
      log::error("Failed to get broadcast sink configuration for broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceJoinFailed(broadcast_id, 0);
      }
      return;
    }

    log::info("Got BroadcastSinkConfiguration: num_subgroups={}, data_path_id={}, num_bis={}, big_sync_timeout={}, mse={}",
              sink_config->subgroups.size(), sink_config->data_path.dataPathId,
              sink_config->bis_indices.size(), sink_config->big_sync_timeout, sink_config->mse);

    // Update state machine with the configuration
    state_machine->UpdateSinkConfiguration(*sink_config);

    /* Acquire and start the appropriate HAL client(s):
     *   Enhanced source: BOTH source (TX) and sink (RX) HAL clients are started
     *     here so the QTI HIDL can start the session (it waits for both sides).
     *     The 1st HIDL start (OnAudioResume on source HAL) triggers CreateBigSync().
     *     The 2nd HIDL start (OnAudioResume on sink HAL) triggers RX ISO path setup.
     *     OnTxIsoPathsReady() only acks the 1st HIDL start — it does NOT re-start
     *     the sink HAL client since it is already running.
     *   Standard source: sink HAL client (RX) only.
     *
     * OnBroadcastSinkAudioSessionCreated is called with the combined success
     * status so Java can notify MM audio (active device change) only after
     * both sessions are confirmed started — mirroring the broadcast source
     * pattern where active device is set after OnBroadcastAudioSessionCreated. */
    bool session_started = false;
    if (state_machine->IsEnhanced()) {
      log::info("Enhanced source: starting source (TX) and sink (RX) HAL clients "
                "for broadcast_id=0x{:08x}", broadcast_id);
      bool source_ok = StartSourceHalClient(*sink_config);
      bool sink_ok   = StartSinkHalClient(*sink_config);
      session_started = source_ok && sink_ok;
      log::info("Enhanced source HAL session start: source_ok={}, sink_ok={}, "
                "session_started={}", source_ok, sink_ok, session_started);
    } else {
      log::info("Standard source: starting sink HAL client (RX) for broadcast_id=0x{:08x}",
                broadcast_id);
      session_started = StartSinkHalClient(*sink_config);
    }

    if (callbacks_) {
      callbacks_->OnBroadcastSinkAudioSessionCreated(session_started);
    }

    // Send START_BIG_SYNC message to state machine
    log::info("Sending START_BIG_SYNC message to state machine for broadcast_id=0x{:08x}", broadcast_id);
    state_machine->ProcessMessage(
        BroadcastSinkStateMachine::Message::START_BIG_SYNC, nullptr);
  }

  /**
   * StartEnhancedBroadcastSink - join an enhanced (enhanced broadcast) broadcast source.
   *
   * Identical to JoinSource() except that it explicitly marks the source as
   * enhanced so that the state machine sets up bidirectional (RX + TX) ISO
   * data paths for every BIS instead of RX-only paths.
   *
   * The caller should invoke this method after receiving the
   * OnEnhancedSourceDetected() callback.
   */
  void StartEnhancedBroadcastSink(BroadcastId broadcast_id,
                          const std::optional<BroadcastCode>& broadcast_code) override {
    log::info("StartEnhancedBroadcastSink: broadcast_id=0x{:08x}, has_code={}",
              broadcast_id, broadcast_code.has_value());

    if (tracked_sources_.count(broadcast_id) == 0) {
      log::error("No such broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceJoinFailed(broadcast_id, 0);
      }
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (!tracked_source.state_machine) {
      log::error("State machine not found for broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceJoinFailed(broadcast_id, 0);
      }
      return;
    }

    if (!tracked_source.state_machine->IsEnhanced()) {
      log::warn("broadcast_id=0x{:08x} is not an enhanced source; "
                "falling back to standard JoinSource() with all BISes", broadcast_id);
      // Pass empty bis_indices so JoinSource syncs to all BISes
      JoinSource(broadcast_id, broadcast_code, {});
      return;
    }

    log::info("broadcast_id=0x{:08x} confirmed as enhanced (enhanced broadcast) source, "
              "will configure bidirectional ISO data paths for all BISes", broadcast_id);

    // Register DBIG callbacks now (lazy registration to avoid overriding
    // broadcast source's DBIG callbacks at BT turn-on).
    if (!dbig_callbacks_registered_) {
      log::info("StartEnhancedBroadcastSink: registering DBIG callbacks with ISO manager");
      IsoManager::GetInstance()->RegisterDbigCallbacks(this);
      dbig_callbacks_registered_ = true;
    }

    // Delegate to JoinSource with empty bis_indices (all BISes).
    // The state machine already knows it is enhanced and will set up
    // RX+TX paths automatically via OnSetupIsoDataPath.
    JoinSource(broadcast_id, broadcast_code, {});
  }

  void StopEnhancedBroadcastSink(BroadcastId broadcast_id) override {
    log::info("StopEnhancedBroadcastSink: broadcast_id=0x{:08x}", broadcast_id);

    if (tracked_sources_.count(broadcast_id) == 0) {
      log::error("No such broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceLeaveFailed(broadcast_id, 0);
      }
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (!tracked_source.state_machine) {
      log::error("State machine not found for broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceLeaveFailed(broadcast_id, 0);
      }
      return;
    }

    auto* state_machine = tracked_source.state_machine.get();
    auto current_state = state_machine->GetState();

    // Can only leave if currently in BIG_SYNCING or BIG_SYNCED state
    if (current_state != SinkState::BIG_SYNCING &&
        current_state != SinkState::BIG_SYNCED) {
      log::error("Cannot leave source: not in BIG_SYNCED state, current_state={}, broadcast_id=0x{:08x}",
                SinkStateToString(current_state), broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceLeaveFailed(broadcast_id, 0);
      }
      return;
    }

    // StopEnhancedBroadcastSink requested by user - clear any pending rejoin and stale suspend flags.
    tracked_source.pending_big_rejoin = false;
    pending_source_suspend_ = false;
    pending_sink_suspend_   = false;

    // Clear the DBIG registration flag so StartEnhancedBroadcastSink() will re-register
    // when the next enhanced source is joined.
    // NOTE: We do NOT call RegisterDbigCallbacks(nullptr) here because
    // handle_register_dbig_callbacks() asserts callbacks != nullptr and would crash.
    // The broadcast source will overwrite the DBIG callback pointer when it
    // calls RegisterDbigCallbacks(instance) during its own Initialize().
    if (dbig_callbacks_registered_) {
      log::info("StopEnhancedBroadcastSink: clearing DBIG registration flag for broadcast_id=0x{:08x} "
                "(broadcast source will overwrite callback pointer on next Initialize)",
                broadcast_id);
      dbig_callbacks_registered_ = false;
    }

    log::info("StopEnhancedBroadcastSink: state validated, flags cleared for broadcast_id=0x{:08x}. "
              "MSG_STOP from Java will drive HAL teardown via blocking setParameters().",
              broadcast_id);

    /* Do NOT call Stop() on the HAL clients here.
     *
     * Java stopEnhancedBroadcastSink() calls native stopEnhancedBroadcastSink() FIRST (this function),
     * then posts MSG_STOP to mHandler.  MSG_STOP calls:
     *   setParameters("achat_rx_enable=false")  — BLOCKING until sink HAL
     *     OnAudioSuspend is fully acked (RX ISO paths removed +
     *     ConfirmSuspendRequest on sink HAL).
     *   setParameters("achat_tx_enable=false")  — BLOCKING until source HAL
     *     OnAudioSuspend is fully acked (TX ISO paths removed + BIG sync
     *     terminated + ConfirmSuspendRequest on source HAL).
     *
     * Calling Stop() here would trigger duplicate OnAudioSuspend callbacks
     * and race with the MSG_STOP-driven teardown. */
  }

  void RemoveSource(BroadcastId broadcast_id) override {
    log::info("RemoveSource: broadcast_id=0x{:08x}", broadcast_id);

    if (tracked_sources_.count(broadcast_id) == 0) {
      log::error("No such broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceRemoveFailed(broadcast_id, 0);
      }
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (!tracked_source.state_machine) {
      log::error("State machine not found for broadcast_id=0x{:08x}", broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceRemoveFailed(broadcast_id, 0);
      }
      return;
    }

    // RemoveSource requested by user - clear any pending rejoin
    tracked_source.pending_big_rejoin = false;
    log::info("RemoveSource requested for broadcast_id=0x{:08x}, cleared rejoin flags", broadcast_id);

    auto* state_machine = tracked_source.state_machine.get();
    // Check if state machine is in a synced state (PA_SYNCED, BIG_SYNCING or BIG_SYNCED)
    auto current_state = state_machine->GetState();
    if (current_state != SinkState::PA_SYNCED &&
        current_state != SinkState::BIG_SYNCING &&
        current_state != SinkState::BIG_SYNCED) {
      log::error("Cannot remove source: state machine not in synced state, current_state={}, broadcast_id=0x{:08x}",
                SinkStateToString(current_state), broadcast_id);
      if (callbacks_) {
        callbacks_->OnSourceRemoveFailed(broadcast_id, 0);
      }
      return;
    }

    // Stop audio HAL session
    if (le_audio_sink_hal_client_) {
      log::info("Stopping audio HAL session for broadcast_id=0x{:08x}", broadcast_id);
      le_audio_sink_hal_client_->Stop();
      log::info("Stopped audio HAL session for broadcast_id=0x{:08x}", broadcast_id);
    }

    // Send STOP_SYNC message to state machine
    log::info("Sending STOP_SYNC message to state machine for broadcast_id=0x{:08x}", broadcast_id);
    state_machine->ProcessMessage(
        BroadcastSinkStateMachine::Message::STOP_SYNC, nullptr);
  }

  void DestroySource(BroadcastId broadcast_id) override {
    log::info("DestroySource: broadcast_id=0x{:08x}", broadcast_id);

    if (tracked_sources_.count(broadcast_id) == 0) {
      log::error("No such broadcast_id=0x{:08x}", broadcast_id);
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (tracked_source.state_machine) {
      // Release the reg_id back to the pool
      uint32_t reg_id = tracked_source.state_machine->GetRegId();
      ReleasePaSyncRegId(reg_id);
      log::info("Released reg_id={} for broadcast_id=0x{:08x}", reg_id, broadcast_id);
    }

    // Erase the tracked source
    tracked_sources_.erase(broadcast_id);
    log::info("Destroyed broadcast source: broadcast_id=0x{:08x}", broadcast_id);

    // Stop and release audio HAL client if no more sources exist
    if (tracked_sources_.empty() && le_audio_sink_hal_client_) {
      log::info("No more sources, stopping and releasing audio HAL client");
      le_audio_sink_hal_client_->Stop();
      le_audio_sink_hal_client_.reset();
    }
  }
  void SourcePublicMetadataChanged(BroadcastId broadcast_id, const std::string& broadcast_name, const std::vector<uint8_t>& public_metadata) override {
    log::info("SourcePublicMetadataChanged: broadcast_id=0x{:08x}, broadcast_name='{}', public_metadata_len={}",
              broadcast_id, broadcast_name, public_metadata.size());

    if (tracked_sources_.count(broadcast_id) == 0) {
      log::error("No such broadcast_id=0x{:08x}", broadcast_id);
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (!tracked_source.state_machine) {
      log::error("State machine not found for broadcast_id=0x{:08x}", broadcast_id);
      return;
    }

    auto* state_machine = tracked_source.state_machine.get();

    // Parse public metadata to extract public announcement data
    if (!public_metadata.empty()) {
      // Get existing features from state machine's public announcement
      uint8_t features = 0;
      auto existing_public_announcement = state_machine->GetPublicAnnouncement();
      if (existing_public_announcement.has_value()) {
        features = existing_public_announcement->features;
        log::info("Using existing features from state machine: 0x{:02x}", features);
      }

      // Use the entire public_metadata as metadata TLV
      auto public_announcement = GeneratePublicAnnouncement(public_metadata, features);
      if (public_announcement.has_value()) {
        log::info("Updated public announcement for broadcast_id=0x{:08x}, features=0x{:02x}",
                  broadcast_id, features);

        // Update the broadcast name and public announcement in the state machine
        state_machine->UpdatePublicAnnouncement(broadcast_name, public_announcement.value());

        log::info("Public announcement updated: features=0x{:02x}, metadata_size={}",
                  public_announcement->features, public_announcement->metadata.size());
      } else {
        log::warn("Failed to generate public announcement from metadata for broadcast_id=0x{:08x}",
                  broadcast_id);
      }
    } else {
      log::info("No public metadata provided for broadcast_id=0x{:08x}", broadcast_id);
    }

    // Notify callbacks about the metadata change
    auto updated_metadata = ConvertToMetadata(state_machine);
    if (callbacks_) {
      callbacks_->OnSourceMetadataChanged(broadcast_id, updated_metadata);
    }

    log::info("SourcePublicMetadataChanged completed for broadcast_id=0x{:08x}", broadcast_id);
  }

  void AddSource(const RawAddress& addr, uint8_t addr_type,
                 uint8_t adv_sid, BroadcastId broadcast_id,
                 int8_t rssi, const std::string& broadcast_name,
                 bool is_public,
                 const std::vector<uint8_t>& public_metadata,
                 uint8_t public_features) override {
    log::info("AddSource: addr={}, addr_type={}, advSid={}, broadcast_id=0x{:08x}, rssi={}, broadcast_name='{}', is_public={}, public_metadata_len={}, public_features=0x{:02x}",
              addr.ToRedactedStringForLogging(), addr_type, adv_sid, broadcast_id, rssi, broadcast_name, is_public, public_metadata.size(), public_features);

    // Create or update tracked source
    auto& tracked_source = tracked_sources_[broadcast_id];

    bool is_new_source = !tracked_source.state_machine;

    // Create state machine for new source
    if (is_new_source) {
      // Allocate a reg_id for this new source
      uint32_t reg_id = AllocatePaSyncRegId();
      if (reg_id == kInvalidPaSyncRegId) {
        log::error("Failed to allocate PA sync reg_id for broadcast_id=0x{:08x}: limit reached",
                   broadcast_id);
        callbacks_->OnSourceAddFailed(broadcast_id, 0);
        return;
      }

      log::info("Creating state machine for broadcast_id=0x{:08x} with reg_id={}",
                broadcast_id, reg_id);

      // Generate PublicBroadcastAnnouncementData only if is_public is true
      std::optional<PublicBroadcastAnnouncementData> public_announcement;
      if (is_public) {
        if (!public_metadata.empty()) {
          public_announcement = GeneratePublicAnnouncement(public_metadata, public_features);
          if (public_announcement.has_value()) {
            log::info("Generated public announcement for broadcast_id=0x{:08x}", broadcast_id);
          }
        } else {
          log::warn("Public broadcast but no public metadata provided for broadcast_id=0x{:08x}", broadcast_id);
        }
      }

      // Create config
      BroadcastSinkStateMachineConfig sm_config(
          reg_id,                                    // reg_id
          addr,                                      // address
          addr_type,                                 // address_type
          adv_sid,                                   // adv_sid
          broadcast_id,                              // broadcast_id
          broadcast_name,                            // broadcast_name
          is_public,                                 // is_public
          kDefaultPaSyncTimeout,                     // pa_sync_timeout
          is_public ? public_announcement : std::nullopt  // public_announcement (only if is_public)
      );

      tracked_source.state_machine = BroadcastSinkStateMachine::CreateInstance(sm_config);
      if (tracked_source.state_machine) {
        if (!tracked_source.state_machine->Initialize()) {
          log::error("Failed to initialize state machine for broadcast_id=0x{:08x}, reg_id={}",
                     broadcast_id, reg_id);
          tracked_source.state_machine.reset();
          // Return reg_id to pool since initialization failed
          ReleasePaSyncRegId(reg_id);
        } else {
          log::info("State machine created and initialized for broadcast_id=0x{:08x}, reg_id={}",
                    broadcast_id, reg_id);
        }
      } else {
        log::error("Failed to create state machine for broadcast_id=0x{:08x}, reg_id={}",
                   broadcast_id, reg_id);
        // Return reg_id to pool since creation failed
        ReleasePaSyncRegId(reg_id);
      }
    } else {
      // State machine already exists for this broadcast_id.
      // Once PA sync is established (PA_SYNCED or beyond), ignore further
      // scan results from the same enhanced broadcast source.
      auto current_state = tracked_source.state_machine->GetState();
      if (current_state == SinkState::PA_SYNCED ||
          current_state == SinkState::BIG_SYNCING ||
          current_state == SinkState::BIG_SYNCED ||
          current_state == SinkState::DISABLING ||
          current_state == SinkState::STOPPING) {
        log::info("Ignoring scan result for broadcast_id=0x{:08x}: already PA synced (state={})",
                  broadcast_id, SinkStateToString(current_state));
        return;
      }
      log::info("Updating existing broadcast_id=0x{:08x} (state={})",
                broadcast_id, SinkStateToString(current_state));
    }
  }

  void Dump(int fd) {
    dprintf(fd, "  Broadcast Sink Manager:\n");
    dprintf(fd, "    Scanning: %s\n", is_scanning_ ? "true" : "false");
    dprintf(fd, "    Tracked sources: %zu\n", tracked_sources_.size());

    for (const auto& [broadcast_id, tracked_source] : tracked_sources_) {
      dprintf(fd, "      Broadcast ID: 0x%06X\n", broadcast_id);
      if (tracked_source.state_machine) {
        dprintf(fd, "        Address: %s\n",
                tracked_source.state_machine->GetSourceAddress().ToString().c_str());
        dprintf(fd, "        State: %s\n",
                SinkStateToString(tracked_source.state_machine->GetState()));
      }
      dprintf(fd, "        Audio HAL: %s\n",
              le_audio_sink_hal_client_ ? "active" : "inactive");
    }
  }

 private:
  bool StartScanning() {
    log::info("Starting broadcast source scanning");

    if (is_scanning_) {
      log::warn("Already scanning");
      return true;
    }

    if (!ble_scanner_) {
      log::error("BLE scanner instance not available");
      return false;
    }

    // Start scanning
    ble_scanner_->Scan(true);
    is_scanning_ = true;
    log::info("Broadcast source scanning started");
    return true;
  }

  void StopScanning() {
    log::info("Stopping broadcast source scanning");

    if (!is_scanning_) {
      log::warn("Not currently scanning");
      return;
    }

    if (!ble_scanner_) {
      log::error("BLE scanner instance not available");
      is_scanning_ = false;
      return;
    }

    // Stop scanning
    ble_scanner_->Scan(false);
    is_scanning_ = false;
    log::info("Broadcast source scanning stopped");
  }

  BroadcastSinkStateMachine* FindStateMachineByAddress(const RawAddress& address,
                                                        uint8_t adv_sid) {
    for (auto& [broadcast_id, tracked_source] : tracked_sources_) {
      if (tracked_source.state_machine &&
          tracked_source.state_machine->GetSourceAddress() == address &&
          tracked_source.state_machine->GetAdvSid() == adv_sid) {
        return tracked_source.state_machine.get();
      }
    }
    return nullptr;
  }

  BroadcastSinkStateMachine* FindStateMachineBySyncHandle(uint16_t sync_handle) {
    for (auto& [broadcast_id, tracked_source] : tracked_sources_) {
      if (tracked_source.state_machine) {
        auto pa_sync_info = tracked_source.state_machine->GetPaSyncInfo();
        if (pa_sync_info.has_value() && pa_sync_info->sync_handle == sync_handle) {
          return tracked_source.state_machine.get();
        }
      }
    }
    return nullptr;
  }

  bool IsAnySinkStreaming() {
    for (const auto& [broadcast_id, tracked_source] : tracked_sources_) {
      if (tracked_source.state_machine && tracked_source.state_machine->IsStreaming()) {
        return true;
      }
    }
    return false;
  }

  // Helper function to find broadcast_id from big_handle
  BroadcastId BroadcastIdFromBigHandle(uint8_t big_handle) const {
    auto pair_it = std::find_if(tracked_sources_.begin(), tracked_sources_.end(),[big_handle](const auto& entry) {
        if (entry.second.state_machine) {
          return entry.second.state_machine->GetRegId() == big_handle;
        }
        return false;
    });
    if (pair_it != tracked_sources_.end()) {
      return pair_it->first;
    }
    return bluetooth::le_audio::kBroadcastIdInvalid;
  }

  // BigSyncCallbacks implementation
  void OnSetupIsoDataPath(uint8_t status, uint16_t conn_handle, uint8_t big_handle) override {
    log::info("ISO data path setup: status={}, conn_handle=0x{:04X}, big_handle={}",
              status, conn_handle, big_handle);

    // Use helper function to find broadcast_id from big_handle
    BroadcastId broadcast_id = BroadcastIdFromBigHandle(big_handle);
    if (broadcast_id == bluetooth::le_audio::kBroadcastIdInvalid) {
      log::warn("No state machine found for big_handle={}", big_handle);
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (tracked_source.state_machine) {
      /* For enhanced (enhanced broadcast) sources the state machine tracks the
       * current direction internally via enhanced_iso_setup_index_.
       * We always pass kIsoDataPathDirectionOut here; the state machine
       * derives the actual next direction from its index.
       */
      uint8_t direction = bluetooth::hci::iso_manager::kIsoDataPathDirectionOut;
      if (tracked_source.state_machine->IsEnhanced()) {
        log::info("Enhanced source ISO data path callback: broadcast_id=0x{:08x}", broadcast_id);
      }
      tracked_source.state_machine->OnSetupIsoDataPath(status, conn_handle, direction);
    }
  }

  void OnRemoveIsoDataPath(uint8_t status, uint16_t conn_handle, uint8_t big_handle) override {
    log::info("ISO data path removed: status={}, conn_handle=0x{:04X}, big_handle={}",
              status, conn_handle, big_handle);

    // Use helper function to find broadcast_id from big_handle
    BroadcastId broadcast_id = BroadcastIdFromBigHandle(big_handle);
    if (broadcast_id == bluetooth::le_audio::kBroadcastIdInvalid) {
      log::warn("No state machine found for big_handle={}", big_handle);
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (tracked_source.state_machine) {
      tracked_source.state_machine->OnRemoveIsoDataPath(status, conn_handle);
    }
  }

  void OnBigSyncEvent(uint8_t event, void* data) override {
    switch (event) {
      case bluetooth::hci::iso_manager::kIsoEventBigOnSyncEstablished: {
        auto* big_sync_established =
            static_cast<bluetooth::hci::iso_manager::big_sync_established_evt*>(
                data);

        BroadcastId broadcast_id = BroadcastIdFromBigHandle(big_sync_established->big_handle);
        if (broadcast_id == bluetooth::le_audio::kBroadcastIdInvalid) {
          log::warn("No state machine found for big_handle={}", big_sync_established->big_handle);
          return;
        }
        auto& tracked_source = tracked_sources_[broadcast_id];
        if (tracked_source.state_machine) {
          tracked_source.state_machine->HandleHciEvent(HCI_BLE_BIG_SYNC_EST_EVT, data);
        }
        break;
      }
      case bluetooth::hci::iso_manager::kIsoEventBigOnSyncLost: {
        auto* big_sync_lost =
            static_cast<bluetooth::hci::iso_manager::big_sync_lost_evt*>(data);
        log::info("BIG sync lost: big_handle={}, reason={}",
                  big_sync_lost->big_handle, big_sync_lost->reason);

        BroadcastId broadcast_id = BroadcastIdFromBigHandle(big_sync_lost->big_handle);
        if (broadcast_id == bluetooth::le_audio::kBroadcastIdInvalid) {
          log::warn("No state machine found for big_handle={}", big_sync_lost->big_handle);
          return;
        }
        auto& tracked_source = tracked_sources_[broadcast_id];
        if (tracked_source.state_machine) {
          tracked_source.state_machine->HandleHciEvent(HCI_BLE_BIG_SYNC_LOST_EVT, data);
        }
        break;
      }
      case bluetooth::hci::iso_manager::kIsoEventBigOnTerminateSyncCmpl: {
        auto* big_terminate_sync_cmpl = static_cast<
            bluetooth::hci::iso_manager::big_terminate_sync_cmpl_evt*>(data);
        log::info("BIG terminate sync complete: big_handle={}, status={}",
                  big_terminate_sync_cmpl->big_handle,
                  big_terminate_sync_cmpl->status);

        BroadcastId broadcast_id = BroadcastIdFromBigHandle(big_terminate_sync_cmpl->big_handle);
        if (broadcast_id == bluetooth::le_audio::kBroadcastIdInvalid) {
          log::warn("No state machine found for big_handle={}", big_terminate_sync_cmpl->big_handle);
          return;
        }
        auto& tracked_source = tracked_sources_[broadcast_id];
        if (tracked_source.state_machine) {
          tracked_source.state_machine->OnBigTerminateSyncComplete(big_terminate_sync_cmpl->big_handle, big_terminate_sync_cmpl->status);
        }
        break;
      }
      default:
        log::warn("Unhandled BIG sync event: {}", event);
        break;
    }
  }

  /* ------------------------------------------------------------------
   * DbigCallbacks implementation
   *
   * Called by IsoManager when a enhanced broadcast-related event arrives from the
   * controller.  We handle kIsoEventDbigCreateCmpl to drive the
   * enhanced (enhanced broadcast) source setup sequence.
   * ------------------------------------------------------------------ */
  void OnDbigEvent(uint8_t event, void* data) override {
    switch (event) {
      case bluetooth::hci::iso_manager::kIsoEventDbigCreateCmpl: {
        auto* evt = static_cast<bluetooth::hci::iso_manager::dbig_create_cmpl_evt*>(data);
        log::info("enhanced broadcast create complete: status=0x{:02x}, sub_opcode=0x{:02x}, "
                  "dbig_handle={}", evt->status, evt->sub_opcode, evt->dbig_handle);

        /* Find the state machine whose reg_id matches the dbig_handle */
        BroadcastId broadcast_id = BroadcastIdFromBigHandle(evt->dbig_handle);
        if (broadcast_id == bluetooth::le_audio::kBroadcastIdInvalid) {
          log::warn("OnDbigEvent: no state machine found for dbig_handle={}",
                    evt->dbig_handle);
          return;
        }

        auto& tracked_source = tracked_sources_[broadcast_id];
        if (tracked_source.state_machine) {
          tracked_source.state_machine->OnDbigSetupComplete(evt->status);
        }
        break;
      }

      case bluetooth::hci::iso_manager::kIsoEventDbigUpdate: {
        auto* evt = static_cast<bluetooth::hci::iso_manager::dbig_update_evt*>(data);
        log::info("enhanced broadcast update: status=0x{:02x}, big_handle={}, bis_state={}, "
                  "timing_source={}, local_bis_id={}",
                  evt->status, evt->big_handle, evt->bis_state,
                  evt->timing_source, evt->local_bis_id);
        /* enhanced broadcast update events are informational; no state machine action needed */
        break;
      }

      default:
        log::warn("OnDbigEvent: unhandled event=0x{:02x}", event);
        break;
    }
  }

  void OnBisEvent(uint8_t event, void* data) override {
    log::info("BIS event: event={}", event);

    // Handle BIS data events
    switch (event) {
      case bluetooth::hci::iso_manager::kIsoEventBisDataAvailable: {
        auto* evt = static_cast<bluetooth::hci::iso_manager::bis_data_evt*>(data);
        log::verbose("BIS data available: bis_conn_hdl=0x{:04X}, big_handle={}, seq_nb={}",
                    evt->bis_conn_hdl, evt->big_handle, evt->seq_nb);
        // TODO: Forward BIS data to audio framework
        break;
      }
      default:
        log::warn("Unhandled BIS event: {}", event);
        break;
    }
  }

  // Allocate a PA sync reg_id (starts from 1, up to max_source_capacity_)
  uint32_t AllocatePaSyncRegId() {
    // Find the first available reg_id from 1 to max_source_capacity_
    uint32_t reg_id = 1;
    while (reg_id <= max_source_capacity_ && pa_sync_reg_ids_.count(reg_id) != 0) {
      reg_id++;
    }
    if (reg_id > max_source_capacity_) {
      log::warn("Number of max PA sync {} reached", max_source_capacity_);
      return kInvalidPaSyncRegId;
    }
    pa_sync_reg_ids_.insert(reg_id);
    log::info("Allocated PA sync reg_id={}", reg_id);
    return reg_id;
  }

  // Release a PA sync reg_id
  void ReleasePaSyncRegId(uint32_t reg_id) {
    if (pa_sync_reg_ids_.count(reg_id) == 0) {
      return;
    }
    pa_sync_reg_ids_.erase(reg_id);
    log::info("Released PA sync reg_id={}", reg_id);
  }

  /**
   * Build a LeAudioCodecConfiguration from a BroadcastSinkConfiguration.
   *
   * The codec parameters are derived from the first subgroup's codec config.
   * Defaults are used for fields that cannot be determined from the sink config.
   */
  bluetooth::le_audio::LeAudioCodecConfiguration BuildCodecConfiguration(
      const BroadcastSinkConfiguration& sink_config) {
    bluetooth::le_audio::LeAudioCodecConfiguration codec_config;

    /* Codec ID from the ISO data path config */
    codec_config.codec.coding_format =
        static_cast<uint8_t>(sink_config.data_path.isoDataPathConfig.codecId.coding_format);
    codec_config.codec.vendor_company_id =
        static_cast<uint16_t>(sink_config.data_path.isoDataPathConfig.codecId.vendor_company_id);
    codec_config.codec.vendor_codec_id =
        static_cast<uint16_t>(sink_config.data_path.isoDataPathConfig.codecId.vendor_codec_id);

    /* Derive audio parameters from the first subgroup if available.
     * LTV types for Codec_Specific_Configuration (Bluetooth Assigned Numbers):
     *   0x01 = Sampling_Frequency
     *   0x02 = Frame_Duration
     *   0x03 = Audio_Channel_Allocation
     *   0x04 = Octets_per_Codec_Frame
     *   0x05 = Codec_Frame_Blocks_Per_SDU
     */
    if (!sink_config.subgroups.empty()) {
      const auto& sg = sink_config.subgroups[0];
      const auto ltv_map = sg.GetCommonBisCodecSpecData();
      const auto& params = ltv_map.Values();

      /* Sampling frequency (LTV 0x01): value is a 1-byte index per spec */
      auto it = params.find(0x01);
      if (it != params.end() && !it->second.empty()) {
        switch (it->second[0]) {
          case 0x01: codec_config.sample_rate = 8000;  break;
          case 0x03: codec_config.sample_rate = 16000; break;
          case 0x05: codec_config.sample_rate = 24000; break;
          case 0x06: codec_config.sample_rate = 32000; break;
          case 0x07: codec_config.sample_rate = 44100; break;
          case 0x08: codec_config.sample_rate = 48000; break;
          default:   codec_config.sample_rate = 48000; break;
        }
      } else {
        codec_config.sample_rate = 48000;  /* default */
      }

      /* Frame duration (LTV 0x02): 0x00 = 7.5ms, 0x01 = 10ms */
      it = params.find(0x02);
      if (it != params.end() && !it->second.empty()) {
        codec_config.data_interval_us = (it->second[0] == 0x00) ? 7500 : 10000;
      } else {
        codec_config.data_interval_us = 10000;  /* default 10ms */
      }

      /* Octets per codec frame (LTV 0x04): 2-byte little-endian */
      it = params.find(0x04);
      if (it != params.end() && it->second.size() >= 2) {
        codec_config.octets_per_codec_frame =
            static_cast<uint16_t>(it->second[0] | (it->second[1] << 8));
      } else {
        codec_config.octets_per_codec_frame = 120;  /* default */
      }

      /* num_channels = total number of BISes being synced.
       * Each BIS carries exactly 1 MONO channel (per-BIS ch=1).
       * The audio channel allocation LTV (0x03) is often 0 (unspecified) for
       * enhanced broadcast sources, so we cannot rely on popcount(alloc).
       * Use bis_indices.size() which is always correct. */
      if (!sink_config.bis_indices.empty()) {
        codec_config.num_channels =
                static_cast<uint8_t>(sink_config.bis_indices.size());
        log::info("BuildCodecConfiguration: num_channels={} from bis_indices.size()",
                  codec_config.num_channels);
      } else {
        /* Fallback: derive from audio channel allocation LTV (0x03) */
        it = params.find(0x03);
        if (it != params.end() && it->second.size() >= 4) {
          uint32_t alloc = static_cast<uint32_t>(it->second[0]) |
                           (static_cast<uint32_t>(it->second[1]) << 8) |
                           (static_cast<uint32_t>(it->second[2]) << 16) |
                           (static_cast<uint32_t>(it->second[3]) << 24);
          codec_config.num_channels = static_cast<uint8_t>(__builtin_popcount(alloc));
          if (codec_config.num_channels == 0) codec_config.num_channels = 1;
        } else {
          codec_config.num_channels = 2;  /* default stereo */
        }
      }
    } else {
      /* No subgroup info: use safe defaults */
      codec_config.sample_rate        = 48000;
      codec_config.data_interval_us   = 10000;
      codec_config.octets_per_codec_frame = 120;
      codec_config.num_channels       = 2;
    }

    codec_config.bits_per_sample = 16;  /* LC3 always uses 16-bit PCM */

    log::info("Built codec config: format=0x{:02x}, sample_rate={}, channels={}, "
              "interval_us={}, octets_per_frame={}",
              codec_config.codec.coding_format, codec_config.sample_rate,
              codec_config.num_channels, codec_config.data_interval_us,
              codec_config.octets_per_codec_frame);
    return codec_config;
  }

  /**
   * Acquire and start the sink HAL client (RX/decoder side).
   *
   * Source HAL = TX/encoder: Source::SetPcmParameters() → is_encoder=true → encoder_channel_count
   * Sink HAL   = RX/decoder: Sink::SetPcmParameters()   → is_encoder=false → decoder_channel_count
   *
   * The QTI HAL hardcodes NumStreamIDGroup=5 (1 TX + 4 RX) for the 4-BIS case.
   * decoder_channel_count drives the number of RX streams.
   * Sink HAL must be started with num_channels=N (all BISes) so that
   * decoder_channel_count=N, giving N RX streams in the stream map.
   */
  bool StartSinkHalClient(const BroadcastSinkConfiguration& sink_config) {
    if (le_audio_sink_hal_client_) {
      log::info("Sink HAL client already active, stopping before re-start");
      le_audio_sink_hal_client_->Stop();
      le_audio_sink_hal_client_.reset();
    }

    le_audio_sink_hal_client_ = bluetooth::le_audio::LeAudioSinkAudioHalClient::AcquireUnicast();
    if (!le_audio_sink_hal_client_) {
      log::error("Failed to acquire sink HAL client");
      return false;
    }

    // Sink HAL = RX/decoder: num_channels = N (all BISes) → decoder_channel_count = N
    // The QTI HAL uses decoder_channel_count to build N RX streams in the stream map.
    auto codec_config = BuildCodecConfiguration(sink_config);
    log::info("StartSinkHalClient: num_channels={} for RX/decoder side (Sink HAL)",
              codec_config.num_channels);

    bool started = le_audio_sink_hal_client_->Start(codec_config, &audio_receiver_);
    if (!started) {
      log::error("Failed to start sink HAL client");
      le_audio_sink_hal_client_.reset();
      return false;
    }
    log::info("Sink HAL client acquired and started (RX/decoder, 2nd HIDL start)");
    return true;
  }

  /**
   * Acquire and start the source HAL client (TX/encoder side).
   *
   * Source HAL = TX/encoder: Source::SetPcmParameters() → is_encoder=true → encoder_channel_count
   * Sink HAL   = RX/decoder: Sink::SetPcmParameters()   → is_encoder=false → decoder_channel_count
   *
   * The QTI HAL hardcodes 1 TX stream regardless of encoder_channel_count.
   * Source HAL must be started with num_channels=1 so that encoder_channel_count=1.
   * The 1st HIDL start (OnAudioResume on source HAL) will arrive after Start().
   */
  bool StartSourceHalClient(const BroadcastSinkConfiguration& sink_config) {
    if (le_audio_source_hal_client_) {
      log::info("Source HAL client already active, stopping before re-start");
      le_audio_source_hal_client_->Stop();
      le_audio_source_hal_client_.reset();
    }

    le_audio_source_hal_client_ =
        bluetooth::le_audio::LeAudioSourceAudioHalClient::AcquireBroadcast();
    if (!le_audio_source_hal_client_) {
      log::error("Failed to acquire source HAL client");
      return false;
    }

    // Source HAL = TX/encoder: num_channels=1 → encoder_channel_count=1
    // The QTI HAL hardcodes 1 TX stream in the stream map regardless of encoder_channel_count.
    auto codec_config = BuildCodecConfiguration(sink_config);
    codec_config.num_channels = 1;  // TX/encoder: always 1 channel
    log::info("StartSourceHalClient: overriding num_channels=1 for TX/encoder side (Source HAL), "
              "sink has {} BISes", sink_config.bis_indices.size());

    bool started = le_audio_source_hal_client_->Start(codec_config, &source_audio_receiver_);
    if (!started) {
      log::error("Failed to start source HAL client");
      le_audio_source_hal_client_.reset();
      return false;
    }
    log::info("Source HAL client acquired and started (TX/encoder, 1st HIDL start)");
    return true;
  }

  BroadcastSinkCallbacks* callbacks_;
  uint8_t max_source_capacity_;  // Maximum number of sources that can be PA synced simultaneously
  bool is_scanning_;
  std::set<uint32_t> pa_sync_reg_ids_;  // Set of allocated PA sync registration IDs
  std::map<uint32_t, TrackedSource> tracked_sources_;  // Keyed by broadcast_id
  std::unique_ptr<bluetooth::le_audio::LeAudioSinkAudioHalClient> le_audio_sink_hal_client_;    // Sink HAL client (RX, 2nd HIDL start)
  std::unique_ptr<bluetooth::le_audio::LeAudioSourceAudioHalClient> le_audio_source_hal_client_; // Source HAL client (TX, 1st HIDL start)
  bool pending_source_suspend_ = false;  // Set when source HAL OnAudioSuspend arrives (StopEnhancedBroadcastSink)
  bool pending_sink_suspend_   = false;  // Set when sink   HAL OnAudioSuspend arrives (StopEnhancedBroadcastSink)
  bool dbig_callbacks_registered_ = false;  // DBIG callbacks registered lazily in StartEnhancedBroadcastSink

  static BleScannerInterface* ble_scanner_;

  static class BroadcastSinkStateMachineCallbacks : public IBroadcastSinkStateMachineCallbacks {
    void OnStateMachineCreateStatus(uint32_t broadcast_id, bool initialized) override {
      if (!instance) {
        return;
      }
      log::info("State machine create status: broadcast_id=0x{:08x}, initialized={}",
                broadcast_id, initialized);
    }

    void OnStateMachineDestroyed(uint32_t broadcast_id, uint8_t reason) override {
      if (!instance) {
        return;
      }
      log::info("State machine destroyed: broadcast_id=0x{:08x}, reason={}", broadcast_id, reason);

      // Notify that source has been destroyed with reason
      if (instance->callbacks_) {
        instance->callbacks_->OnSourceDestroyed(broadcast_id, reason);
      }
    }

    void OnStateMachineEvent(uint32_t broadcast_id, SinkState state,
                            const void* data) override {
      if (!instance) {
        return;
      }

      log::info("State machine event: broadcast_id=0x{:08x}, state={}",
                broadcast_id, SinkStateToString(state));

      if (!instance->callbacks_) {
        return;
      }

      // Find the tracked source for this broadcast_id
      if (instance->tracked_sources_.count(broadcast_id) == 0) {
        log::error("No such broadcast_id=0x{:08x}", broadcast_id);
        return;
      }

      auto& tracked_source = instance->tracked_sources_[broadcast_id];
      if (!tracked_source.state_machine) {
        log::error("No state machine for broadcast_id=0x{:08x}", broadcast_id);
        return;
      }

      // Log state transitions - state changes are now reported via OnBroadcastSinkStateChanged
      switch (state) {
        case SinkState::IDLE:
          log::info("State machine transitioned to IDLE for broadcast_id=0x{:06X}", broadcast_id);
          break;

        case SinkState::PA_SYNCING:
          log::info("State machine transitioned to PA_SYNCING for broadcast_id=0x{:06X}", broadcast_id);
          break;

        case SinkState::PA_SYNCED:
          log::info("State machine transitioned to PA_SYNCED for broadcast_id=0x{:06X}", broadcast_id);
          break;

        case SinkState::BIG_SYNCING:
          log::info("State machine transitioned to BIG_SYNCING for broadcast_id=0x{:06X}", broadcast_id);
          break;

        case SinkState::BIG_SYNCED:
          log::info("State machine transitioned to BIG_SYNCED for broadcast_id=0x{:06X}", broadcast_id);
          /* For enhanced (enhanced broadcast) sources, acknowledge the 2nd HIDL
           * start now that all RX ISO data paths have been configured. */
          if (tracked_source.state_machine->IsEnhanced() && instance->le_audio_sink_hal_client_) {
            log::info("Enhanced source BIG_SYNCED: acknowledging 2nd HIDL start for "
                      "broadcast_id=0x{:06X}", broadcast_id);
            instance->le_audio_sink_hal_client_->ConfirmStreamingRequest(false);
          }
          break;

        case SinkState::DISABLING:
          log::info("State machine transitioned to DISABLING for broadcast_id=0x{:06X}", broadcast_id);
          break;

        case SinkState::STOPPING:
          log::info("State machine transitioned to STOPPING for broadcast_id=0x{:06X}", broadcast_id);
          break;

        default:
          log::warn("Unhandled state: {}", SinkStateToString(state));
          break;
      }

      // Invoke OnBroadcastSinkStateChanged callback to BTIF layer
      instance->callbacks_->OnBroadcastSinkStateChanged(broadcast_id, static_cast<uint8_t>(state));
    }

    void OnPaSyncEstablished(uint32_t broadcast_id, uint16_t pa_sync_handle, uint8_t adv_sid,
                            RawAddress address, uint8_t address_type) override {
      if (!instance) {
        return;
      }
      log::info("PA sync established: broadcast_id=0x{:08x}, sync_handle=0x{:04X}, sid={}, address={}",
                broadcast_id, pa_sync_handle, adv_sid, address.ToString());

      // Do NOT notify OnSourceAdded here - wait for BIG Info Report
      // which provides complete metadata including encryption status
      log::info("PA sync established for broadcast_id=0x{:08x}, waiting for BIG Info Report before notifying OnSourceAdded",
                broadcast_id);
    }

    void OnPaSyncLost(uint32_t broadcast_id, uint16_t pa_sync_handle) override {
      if (!instance) {
        return;
      }
      log::info("PA sync lost: broadcast_id=0x{:08x}, sync_handle=0x{:04X}",
                broadcast_id, pa_sync_handle);

      // Do NOT notify OnSourceRemoved here - wait for state machine to transition to IDLE
      // The OnStateMachineEvent callback will handle the notification with appropriate reason code
      log::info("PA sync lost for broadcast_id=0x{:08x}, waiting for state machine to transition to IDLE",
                broadcast_id);
    }

    void OnBaseDataReceived(uint32_t broadcast_id,
                           const bluetooth::le_audio::BasicAudioAnnouncementData& base_data) override {
      if (!instance) {
        return;
      }

      log::info("BASE data received: broadcast_id=0x{:08x}, num_subgroups={}",
                broadcast_id, base_data.subgroup_configs.size());

      // Update BASE data in tracked_sources_
      if (instance->tracked_sources_.count(broadcast_id) == 0) {
        log::error("No such broadcast_id=0x{:08x}", broadcast_id);
        return;
      }

      auto& tracked_source = instance->tracked_sources_[broadcast_id];
      log::info("Updated BASE data for broadcast_id=0x{:08x}: num_subgroups={}",
                broadcast_id, base_data.subgroup_configs.size());

      if (tracked_source.state_machine) {
        // Reset is_notified flag when BASE data changes to allow metadata update notification
        tracked_source.is_notified = false;
        log::info("Reset is_notified flag for broadcast_id=0x{:08x} due to BASE data change", broadcast_id);

        // Call OnSourceMetadataChanged when BASE data is received if public_announcement exists
        auto public_announcement = tracked_source.state_machine->GetPublicAnnouncement();
        if (public_announcement.has_value() && !tracked_source.is_notified && instance->callbacks_) {
          tracked_source.is_notified = true;
          log::info("OnSourceMetadataChanged called from OnBaseDataReceived for broadcast_id=0x{:08x} (has public_announcement)",
                    broadcast_id);
          BroadcastMetadata metadata = ConvertToMetadata(tracked_source.state_machine.get());
          instance->callbacks_->OnSourceMetadataChanged(broadcast_id, metadata);
        }
      }
    }

    void OnTxIsoPathsReady(uint32_t broadcast_id) override {
      if (!instance) return;

      log::info("OnTxIsoPathsReady: all TX ISO paths configured for enhanced source "
                "broadcast_id=0x{:08x}", broadcast_id);

      /* The sink HAL client (RX) was already started in JoinSource() together
       * with the source HAL client so that the QTI HIDL could start the session
       * (it requires both sides to be ready).  Do NOT re-start it here.
       * Just ack the 1st HIDL start so the audio framework sends the 2nd start
       * (OnAudioResume on sink HAL), which triggers RX ISO path setup. */

      /* Acknowledge the 1st HIDL start on the source HAL client.
       * The audio framework will then send a 2nd start on the sink HAL client,
       * which triggers RX ISO data path setup via OnAudioStart(). */
      if (instance->le_audio_source_hal_client_) {
        log::info("OnTxIsoPathsReady: acknowledging 1st HIDL start on source HAL client "
                  "for broadcast_id=0x{:08x}", broadcast_id);
        instance->le_audio_source_hal_client_->ConfirmStreamingRequest(false);
      }
    }

    void OnBigSyncEstablished(uint32_t broadcast_id, uint8_t big_handle,
                             const std::vector<uint16_t>& bis_handles) override {
      if (!instance) {
        return;
      }
      log::info("BIG sync established: broadcast_id=0x{:08x}, big_handle={}, num_bis={}",
                broadcast_id, big_handle, bis_handles.size());

      /* Forward BIG sync creation to Java layer.
       * For enhanced sources this fires only after all TX+RX ISO data paths
       * are configured (state machine reaches BIG_SYNCED).
       * Java never sees intermediate enhanced broadcast or ISO data path events. */
      if (instance->callbacks_) {
        instance->callbacks_->OnBigSyncCreated(broadcast_id, big_handle, bis_handles);
      }
    }

    void OnBigSyncLost(uint32_t broadcast_id, uint8_t big_handle, uint8_t reason) override {
      if (!instance) {
        return;
      }

      log::warn("BIG sync lost (UNEXPECTED): broadcast_id=0x{:08x}, big_handle={}, reason=0x{:02x}",
                broadcast_id, big_handle, reason);

      // Find the tracked source and implement rejoin logic based on HCI error codes
      if (instance->tracked_sources_.count(broadcast_id) > 0) {
        auto& tracked_source = instance->tracked_sources_[broadcast_id];

        // Classify error codes for rejoin decision
        if (reason == HCI_ERR_PEER_USER || reason == HCI_ERR_CONNECTION_TOUT) {
          // Rejoin scenarios: Remote User Terminated Connection (0x13) or Connection Timeout (0x08)
          tracked_source.pending_big_rejoin = true;
          log::info("BIG sync lost due to rejoinable reason 0x{:02x}, will attempt rejoin on next BIG Info Report", reason);
        } else if (reason == HCI_ERR_CONN_TERM_MIC_FAILURE) {
          // MIC Failure (0x3D) - do not rejoin, just log the error
          tracked_source.pending_big_rejoin = false;
          log::warn("BIG sync lost due to MIC failure (0x{:02x}), not rejoining", reason);
        } else {
          tracked_source.pending_big_rejoin = false;
          log::warn("BIG sync lost due to other reason (0x{:02x}), not rejoin", reason);
        }
      }

      /* Forward BIG sync lost to Java layer. */
      if (instance->callbacks_) {
        instance->callbacks_->OnBigSyncLost(broadcast_id, big_handle, reason);
      }
    }

    void OnBigSyncTerminated(uint32_t broadcast_id, uint8_t big_handle, uint8_t status) override {
      if (!instance) return;

      log::info("BIG sync terminated: broadcast_id=0x{:08x}, big_handle={}, status=0x{:02x}",
                broadcast_id, big_handle, status);

      /* Ack source HAL suspend: TX paths removed + BIG terminated. */
      if (instance->pending_source_suspend_ && instance->le_audio_source_hal_client_) {
        log::info("OnBigSyncTerminated: acking source HAL suspend for broadcast_id=0x{:08x}",
                  broadcast_id);
        instance->le_audio_source_hal_client_->ConfirmSuspendRequest();
        instance->pending_source_suspend_ = false;
      }

      /* Notify Java layer that BIG sync has been intentionally terminated
       * (user-initiated StopEnhancedBroadcastSink).  Java handles onSinkStopped from this
       * event rather than from the MSG_STOP handler. */
      if (instance->callbacks_) {
        log::info("OnBigSyncTerminated: notifying Java layer for broadcast_id=0x{:08x}",
                  broadcast_id);
        instance->callbacks_->OnBigSyncTerminated(broadcast_id, big_handle, status);
      }
    }

    void OnRxIsoPathsRemoved(uint32_t broadcast_id) override {
      if (!instance) return;

      log::info("OnRxIsoPathsRemoved: all RX ISO paths removed for broadcast_id=0x{:08x}",
                broadcast_id);

      /* Ack sink HAL suspend: RX paths removed. */
      if (instance->pending_sink_suspend_ && instance->le_audio_sink_hal_client_) {
        log::info("OnRxIsoPathsRemoved: acking sink HAL suspend for broadcast_id=0x{:08x}",
                  broadcast_id);
        instance->le_audio_sink_hal_client_->ConfirmSuspendRequest();
        instance->pending_sink_suspend_ = false;
      }
    }

    void OnBigInfoReport(uint32_t broadcast_id, uint16_t sync_handle, bool encrypted) override {
      if (!instance) {
        return;
      }

      log::info("BIG info report: broadcast_id=0x{:08x}, sync_handle=0x{:04X}, encrypted={}",
                broadcast_id, sync_handle, encrypted);

      // Update encryption status from BIG Info Report and report source found (BASS pattern)
      if (instance->tracked_sources_.count(broadcast_id) == 0) {
        log::warn("No such broadcast_id=0x{:08x}", broadcast_id);
        return;
      }

      auto& tracked_source = instance->tracked_sources_[broadcast_id];
      if (!tracked_source.state_machine) {
        log::warn("No state machine for broadcast_id=0x{:08x}", broadcast_id);
        return;
      }

      log::info("Updated encryption status for broadcast_id=0x{:08x}: encrypted={}",
                broadcast_id, encrypted);

      // Check if automatic rejoin is pending after BIG sync lost
      if (tracked_source.pending_big_rejoin) {
        auto current_state = tracked_source.state_machine->GetState();
        log::info("Checking automatic BIG rejoin: broadcast_id=0x{:08x}, pending_big_rejoin=true, current_state={}",
                  broadcast_id, SinkStateToString(current_state));

        // If we're in PA_SYNCED state and rejoin is pending, restore BIG sync
        if (current_state == SinkState::PA_SYNCED) {
          log::info("Performing automatic BIG rejoin after sync lost for broadcast_id=0x{:08x}", broadcast_id);
          tracked_source.state_machine->ProcessMessage(
              BroadcastSinkStateMachine::Message::START_BIG_SYNC, nullptr);

          // Clear the rejoin flag after attempting rejoin
          tracked_source.pending_big_rejoin = false;
        }
      }

      // Notify OnSourceMetadataChanged when BIG Info Report is received (first time only)
      if (!tracked_source.is_notified && instance->callbacks_) {
        tracked_source.is_notified = true;
        log::info("Marked broadcast_id=0x{:08x} as notified (encrypted={})",
                  broadcast_id, encrypted);
        BroadcastMetadata metadata = ConvertToMetadata(tracked_source.state_machine.get());
        instance->callbacks_->OnSourceMetadataChanged(broadcast_id, metadata);
      } else {
        log::info("Source already notified for broadcast_id=0x{:08x}", broadcast_id);
      }
    }

    /**
     * Called by the state machine when BASE data parsing reveals that the
     * broadcast source is an enhanced (enhanced broadcast) source, i.e. at
     * least one subgroup carries >= 3 BISes.
     *
     * The upper layer should use StartEnhancedBroadcastSink() instead of
     * JoinSource() for such sources so that bidirectional ISO data paths
     * are configured correctly.
     */
    void OnEnhancedSourceDetected(uint32_t broadcast_id, uint8_t num_bis) override {
      if (!instance) {
        return;
      }

      log::info("Enhanced (enhanced broadcast) source detected: broadcast_id=0x{:08x}, "
                "num_bis={} (>= 3 BISes in subgroup)", broadcast_id, num_bis);

      if (instance->tracked_sources_.count(broadcast_id) == 0) {
        log::warn("No tracked source for broadcast_id=0x{:08x}", broadcast_id);
        return;
      }

      // Notify upper layer so it can present the correct join UI / API
      if (instance->callbacks_) {
        instance->callbacks_->OnEnhancedSourceDetected(broadcast_id, num_bis);
      }
    }
  } state_machine_callbacks_;

  /* -----------------------------------------------------------------------
   * Source HAL client callbacks (TX side, enhanced sources only).
   *
   * OnAudioResume() here is the 1st HIDL start.  It drives:
   *   phase IDLE -> DBIG_SETUP -> (enhanced broadcast complete) -> BIG_CREATE_SYNC
   *              -> BIG sync established -> TX ISO paths
   *              -> OnTxIsoPathsReady -> ConfirmStreamingRequest (source)
   * ----------------------------------------------------------------------- */
  static class LeAudioSourceCallbacksImpl
      : public bluetooth::le_audio::LeAudioSourceAudioHalClient::Callbacks {
   public:
    LeAudioSourceCallbacksImpl() = default;

    void OnAudioServerRestart(void) override {
      log::info("Source HAL: audio server restart");
    }

    void OnAudioSuspend(void) override {
      log::info("Source HAL: suspend callback — sending REMOVE_TX_PATHS to state machine");
      if (!instance) return;

      /* Guard against duplicate suspend callbacks (e.g. both MSG_STOP from Java
       * and native stopEnhancedBroadcastSink() Stop() triggering OnAudioSuspend). */
      if (instance->pending_source_suspend_) {
        log::warn("Source HAL: duplicate suspend callback, ignoring");
        return;
      }

      /* Source HAL suspend: remove TX ISO paths, then terminate BIG sync.
       * State machine calls OnBigSyncTerminated when done; BTA layer acks
       * source HAL there via ConfirmSuspendRequest(). */
      instance->pending_source_suspend_ = true;
      for (auto& [broadcast_id, tracked_source] : instance->tracked_sources_) {
        if (!tracked_source.state_machine) continue;
        auto st = tracked_source.state_machine->GetState();
        if (tracked_source.state_machine->IsEnhanced() &&
            (st == SinkState::BIG_SYNCED || st == SinkState::DISABLING)) {
          log::info("Source HAL suspend: REMOVE_TX_PATHS for broadcast_id=0x{:08x}", broadcast_id);
          tracked_source.state_machine->ProcessMessage(
              BroadcastSinkStateMachine::Message::REMOVE_TX_PATHS, nullptr);
          break;
        }
      }
    }

    void OnAudioResume(void) override {
      log::info("Source HAL: resume callback — 1st HIDL start for enhanced source");

      if (!instance) return;

      /* This is the 1st HIDL start (source HAL client).
       * Forward to the enhanced state machine in BIG_SYNCING state so it
       * sends the enhanced broadcast command (phase IDLE -> DBIG_SETUP). */
      for (auto& [broadcast_id, tracked_source] : instance->tracked_sources_) {
        if (!tracked_source.state_machine) continue;

        auto state = tracked_source.state_machine->GetState();
        if (tracked_source.state_machine->IsEnhanced() &&
            state == SinkState::BIG_SYNCING) {
          log::info("Source HAL OnAudioResume: forwarding 1st HIDL start to enhanced "
                    "source broadcast_id=0x{:08x}", broadcast_id);
          tracked_source.state_machine->OnAudioStart();
          /* Only one enhanced source active at a time */
          break;
        }
      }
    }

    void OnAudioDataReady(const std::vector<uint8_t>& data) override {
      /* TX audio data from the audio framework for enhanced (enhanced broadcast) sources.
       * TODO: forward to IsoManager::SendIsoData() for each TX BIS handle. */
      log::verbose("Source HAL: audio data ready, size={}", data.size());
    }

    void OnAudioMetadataUpdate(
        const std::vector<struct playback_track_metadata_v7> source_metadata,
        DsaMode dsa_mode) override {
      log::info("Source HAL: metadata update callback");
    }
  } source_audio_receiver_;

  /* -----------------------------------------------------------------------
   * Sink HAL client callbacks (RX side).
   *
   * For enhanced sources, OnAudioResume() here is the 2nd HIDL start.
   * It drives: phase TX_DONE -> RX_SETUP -> RX ISO paths
   *         -> BIG_SYNCED -> ConfirmStreamingRequest (sink)
   *
   * For standard sources, OnAudioResume() confirms streaming immediately.
   * ----------------------------------------------------------------------- */
  static class LeAudioSinkCallbacksImpl : public bluetooth::le_audio::LeAudioSinkAudioHalClient::Callbacks {
   public:
    LeAudioSinkCallbacksImpl() = default;

    void OnAudioSuspend(void) override {
      log::info("Sink HAL: suspend callback — sending REMOVE_RX_PATHS to state machine");
      if (!instance) return;

      /* Guard against duplicate suspend callbacks (e.g. both MSG_STOP from Java
       * and native stopEnhancedBroadcastSink() Stop() triggering OnAudioSuspend). */
      if (instance->pending_sink_suspend_) {
        log::warn("Sink HAL: duplicate suspend callback, ignoring");
        return;
      }

      /* Sink HAL suspend: remove RX ISO paths.
       * State machine calls OnRxIsoPathsRemoved when done; BTA layer acks
       * sink HAL there via ConfirmSuspendRequest(). */
      instance->pending_sink_suspend_ = true;
      for (auto& [broadcast_id, tracked_source] : instance->tracked_sources_) {
        if (!tracked_source.state_machine) continue;
        auto st = tracked_source.state_machine->GetState();
        if (tracked_source.state_machine->IsEnhanced() &&
            (st == SinkState::BIG_SYNCED || st == SinkState::DISABLING)) {
          log::info("Sink HAL suspend: REMOVE_RX_PATHS for broadcast_id=0x{:08x}", broadcast_id);
          tracked_source.state_machine->ProcessMessage(
              BroadcastSinkStateMachine::Message::REMOVE_RX_PATHS, nullptr);
          break;
        }
      }
    }

    void OnAudioResume(void) override {
      log::info("Sink HAL: resume callback (HIDL start indication)");

      if (!instance) return;

      /* For enhanced sources this is the 2nd HIDL start (sink HAL client).
       * Forward to the state machine in BIG_SYNCING state with phase TX_DONE
       * so it starts RX ISO data path setup. */
      bool handled_enhanced = false;
      for (auto& [broadcast_id, tracked_source] : instance->tracked_sources_) {
        if (!tracked_source.state_machine) continue;

        auto state = tracked_source.state_machine->GetState();
        if (tracked_source.state_machine->IsEnhanced() &&
            state == SinkState::BIG_SYNCING) {
          log::info("Sink HAL OnAudioResume: forwarding 2nd HIDL start to enhanced "
                    "source broadcast_id=0x{:08x}", broadcast_id);
          tracked_source.state_machine->OnAudioStart();
          handled_enhanced = true;
          break;
        }
      }

      if (!handled_enhanced && instance->le_audio_sink_hal_client_) {
        /* Standard source: confirm streaming if any source is streaming */
        bool is_streaming = instance->IsAnySinkStreaming();
        if (is_streaming) {
          log::info("Sink HAL OnAudioResume: standard source streaming, confirming");
          instance->le_audio_sink_hal_client_->ConfirmStreamingRequest(false);
        } else {
          log::info("Sink HAL OnAudioResume: no streaming sources");
        }
      }
    }

    void OnAudioMetadataUpdate(
            const std::vector<struct record_track_metadata_v7> sink_metadata) override {
      log::info("Sink HAL: metadata update callback");
    }
  } audio_receiver_;

  static class BroadcastSinkScanningCallbacks : public ScanningCallbacks {
   public:
    BroadcastSinkScanningCallbacks() = default;

    void OnScannerRegistered(const bluetooth::Uuid app_uuid, uint8_t scanner_id,
                             uint8_t status) override {}
    void OnSetScannerParameterComplete(uint8_t scanner_id, uint8_t status) override {}
    void OnScanResult(uint16_t event_type, uint8_t addr_type, RawAddress bda,
                      uint8_t primary_phy, uint8_t secondary_phy, uint8_t advertising_sid,
                      int8_t tx_power, int8_t rssi, uint16_t periodic_adv_int,
                      std::vector<uint8_t> adv_data) override {}
    void OnTrackAdvFoundLost(AdvertisingTrackInfo advertising_track_info) override {}
    void OnBatchScanReports(int client_if, int status, int report_format, int num_records,
                            std::vector<uint8_t> data) override {}
    void OnBatchScanThresholdCrossed(int client_if) override {}
    void OnPeriodicSyncStarted(int reg_id, uint8_t status, uint16_t sync_handle,
                               uint8_t advertising_sid, uint8_t address_type,
                               RawAddress address, uint8_t phy, uint16_t interval) override {
      if (!instance) {
        return;
      }

      log::info("PA sync started: status={}, sync_handle=0x{:04X}, sid={}, address={}",
                status, sync_handle, advertising_sid, address.ToString());

      // Find the state machine for this source
      auto* state_machine = instance->FindStateMachineByAddress(address, advertising_sid);
      if (state_machine) {
        state_machine->OnSyncEstablished(status, sync_handle, advertising_sid,
                                        address_type, address, phy, interval);
      } else {
        log::warn("No state machine found for PA sync: address={}, sid={}",
                  address.ToString(), advertising_sid);
      }
    }

    void OnPeriodicSyncReport(uint16_t sync_handle, int8_t tx_power, int8_t rssi,
                              uint8_t status, std::vector<uint8_t> data) override {
      if (!instance) {
        return;
      }

      // Find the state machine for this sync handle
      auto* state_machine = instance->FindStateMachineBySyncHandle(sync_handle);
      if (state_machine) {
        state_machine->OnPeriodicScanResult(sync_handle, tx_power, rssi, status, data);
      }
    }

    void OnPeriodicSyncLost(uint16_t sync_handle) override {
      if (!instance) {
        return;
      }

      log::info("PA sync lost: sync_handle=0x{:04X}", sync_handle);

      // Find the state machine for this sync handle
      auto* state_machine = instance->FindStateMachineBySyncHandle(sync_handle);
      if (state_machine) {
        state_machine->OnSyncLost(sync_handle);
      } else {
        log::warn("No state machine found for PA sync lost: sync_handle=0x{:04X}", sync_handle);
      }
    }

    void OnPeriodicSyncTransferred(int pa_source, uint8_t status, RawAddress address) override {}

    void OnBigInfoReport(uint16_t sync_handle, bool encrypted) override {
      if (!instance) {
        return;
      }

      log::info("BIG info report: sync_handle=0x{:04X}, encrypted={}", sync_handle, encrypted);

      // Find the state machine for this sync handle
      auto* state_machine = instance->FindStateMachineBySyncHandle(sync_handle);
      if (state_machine) {
        state_machine->OnBigInfoReport(sync_handle, encrypted);
      } else {
        log::warn("No state machine found for BIG info report: sync_handle=0x{:04X}", sync_handle);
      }
    }

    /**
     * Extended BIG info report — captures iso_interval and phy for DBIG setup.
     * Called by le_periodic_sync_manager.h in addition to OnBigInfoReport.
     * Passes the full controller parameters to the state machine so that
     * SendDbigSetupCommand() can select the correct bis_control_event_interval.
     */
    void OnBigInfoReportFull(uint16_t sync_handle,
                             uint16_t iso_interval,
                             uint8_t  phy,
                             uint8_t  num_bis,
                             bool     encrypted) override {
      if (!instance) return;

      log::info("BIG info report (full): sync_handle=0x{:04X}, iso_interval={} ({}ms), "
                "phy={}, num_bis={}, encrypted={}",
                sync_handle, iso_interval,
                static_cast<uint32_t>(iso_interval) * 125 / 100,
                phy, num_bis, encrypted);

      auto* state_machine = instance->FindStateMachineBySyncHandle(sync_handle);
      if (state_machine) {
        state_machine->SetBigInfoParams(iso_interval, phy, num_bis);
      }
    }
  } scanning_callbacks_;
};

// Static member initialization
BleScannerInterface* LeAudioBroadcastSinkImpl::ble_scanner_ = nullptr;
LeAudioBroadcastSinkImpl::BroadcastSinkStateMachineCallbacks
        LeAudioBroadcastSinkImpl::state_machine_callbacks_;
LeAudioBroadcastSinkImpl::BroadcastSinkScanningCallbacks
        LeAudioBroadcastSinkImpl::scanning_callbacks_;
LeAudioBroadcastSinkImpl::LeAudioSinkCallbacksImpl
        LeAudioBroadcastSinkImpl::audio_receiver_;
LeAudioBroadcastSinkImpl::LeAudioSourceCallbacksImpl
        LeAudioBroadcastSinkImpl::source_audio_receiver_;

}  // namespace

// Static method implementations
void LeAudioBroadcastSink::Initialize(
    BroadcastSinkCallbacks* callbacks, uint8_t max_source_capacity) {
  std::scoped_lock<std::mutex> lock(instance_mutex);
  log::info("Initializing LeAudioBroadcastSink with max_source_capacity={}", max_source_capacity);

  if (instance) {
    log::error("Already initialized");
    return;
  }

  // Initialize BLE scanner
  if (!LeAudioBroadcastSinkImpl::InitializeScanner()) {
    log::error("Failed to initialize BLE scanner");
    return;
  }

  instance = new LeAudioBroadcastSinkImpl(callbacks, max_source_capacity);

  // Register BIG Sync callbacks with ISO manager
  IsoManager::GetInstance()->RegisterBigSyncCallbacks(instance);
  log::info("Registered BIG Sync callbacks with ISO manager");

  // Initialize state machine subsystem
  LeAudioBroadcastSinkImpl::InitializeStateMachine(instance);
}

bool LeAudioBroadcastSink::IsLeAudioBroadcastSinkRunning() {
  return instance != nullptr;
}

LeAudioBroadcastSink* LeAudioBroadcastSink::Get() {
  log::assert_that(instance != nullptr, "LeAudioBroadcastSink not initialized");
  return instance;
}

void LeAudioBroadcastSink::Stop() {
  log::info("Stopping LeAudioBroadcastSink");

  if (instance) {
    instance->Stop();
  }
}

void LeAudioBroadcastSink::Cleanup() {
  std::scoped_lock<std::mutex> lock(instance_mutex);
  log::info("Cleaning up LeAudioBroadcastSink");

  if (instance == nullptr) {
    return;
  }

  LeAudioBroadcastSinkImpl* ptr = instance;
  instance = nullptr;

  ptr->CleanUp();
  delete ptr;

  // Cleanup scanner
  LeAudioBroadcastSinkImpl::CleanupScanner();
}

void LeAudioBroadcastSink::DebugDump(int fd) {
  std::scoped_lock<std::mutex> lock(instance_mutex);
  dprintf(fd, "Le Audio Broadcast Sink:\n");

  if (instance) {
    instance->Dump(fd);
  } else {
    dprintf(fd, "  Not initialized\n");
  }

  dprintf(fd, "\n");
}
