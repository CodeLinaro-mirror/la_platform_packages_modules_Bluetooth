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

using namespace bluetooth;
using namespace bluetooth::le_audio::broadcast_sink;
using bluetooth::le_audio::PublicBroadcastAnnouncementData;

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
                                  public BigSyncCallbacks {
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

    // Get BLE scanner interface
    ble_scanner_ = bluetooth::shim::get_ble_scanner_instance();
    if (!ble_scanner_) {
      log::error("Failed to get BLE scanner instance");
      return false;
    }

    // Register callbacks for native client (kScannerClientIdLeAudio = 0x1)
    ble_scanner_->RegisterCallbacksNative(&scanning_callbacks_,
                                         kScannerClientIdLeAudio);

    log::info("BLE scanner instance obtained and callbacks registered successfully");
    return true;
  }

  static void InitializeStateMachine(LeAudioBroadcastSinkImpl* instance) {
    log::info("Initializing BroadcastSinkStateMachine subsystem");
    BroadcastSinkStateMachine::Initialize(&state_machine_callbacks_, ble_scanner_);
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

  static void CleanupScanner() {
    ble_scanner_ = nullptr;
  }

  void Stop() {
    log::info("Stopping broadcast sink");
    StopScanning();
  }

  void JoinSource(BroadcastId broadcast_id,
                  const std::optional<BroadcastCode>& broadcast_code,
                  const std::vector<uint8_t>& bis_indices) override {
    log::info("JoinSource: broadcast_id=0x{:08x}, has_code={}, bis_indices_count={}",
              broadcast_id, broadcast_code.has_value(), bis_indices.size());

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

    if (callbacks_) {
      callbacks_->OnBroadcastSinkAudioSessionCreated(true);  // Notify success
    }

    // Send START_BIG_SYNC message to state machine
    log::info("Sending START_BIG_SYNC message to state machine for broadcast_id=0x{:08x}", broadcast_id);
    state_machine->ProcessMessage(
        BroadcastSinkStateMachine::Message::START_BIG_SYNC, nullptr);
  }

  void LeaveSource(BroadcastId broadcast_id) override {
    log::info("LeaveSource: broadcast_id=0x{:08x}", broadcast_id);

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

    // LeaveSource requested by user - clear any pending rejoin
    tracked_source.pending_big_rejoin = false;
    log::info("LeaveSource requested for broadcast_id=0x{:08x}, cleared rejoin flags", broadcast_id);

    // Stop audio HAL session
    if (le_audio_sink_hal_client_) {
      log::info("Stopping audio HAL session for broadcast_id=0x{:08x}", broadcast_id);
      le_audio_sink_hal_client_->Stop();
      log::info("Stopped audio HAL session for broadcast_id=0x{:08x}", broadcast_id);
    }

    // Send STOP_BIG_SYNC message to state machine to stop BIG sync while keeping PA sync
    log::info("Sending STOP_BIG_SYNC message to state machine for broadcast_id=0x{:08x}", broadcast_id);
    state_machine->ProcessMessage(
        BroadcastSinkStateMachine::Message::STOP_BIG_SYNC, nullptr);
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

  void GetSourceMetadata(BroadcastId broadcast_id) override {
    log::info("GetSourceMetadata: broadcast_id=0x{:08x}", broadcast_id);

    if (tracked_sources_.count(broadcast_id) == 0) {
      log::error("No such broadcast_id=0x{:08x}", broadcast_id);
      return;
    }

    auto& tracked_source = tracked_sources_[broadcast_id];
    if (!tracked_source.state_machine) {
      log::error("State machine not found for broadcast_id=0x{:08x}", broadcast_id);
      return;
    }

    auto metadata = ConvertToMetadata(tracked_source.state_machine.get());
    if (callbacks_) {
      callbacks_->OnSourceMetadataChanged(broadcast_id, metadata);
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
      log::info("Updating existing broadcast_id=0x{:08x}", broadcast_id);
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
      tracked_source.state_machine->OnSetupIsoDataPath(status, conn_handle);
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

  BroadcastSinkCallbacks* callbacks_;
  uint8_t max_source_capacity_;  // Maximum number of sources that can be PA synced simultaneously
  bool is_scanning_;
  std::set<uint32_t> pa_sync_reg_ids_;  // Set of allocated PA sync registration IDs
  std::map<uint32_t, TrackedSource> tracked_sources_;  // Keyed by broadcast_id
  std::unique_ptr<bluetooth::le_audio::LeAudioSinkAudioHalClient> le_audio_sink_hal_client_;  // Audio HAL client for broadcast sink

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

    void OnBigSyncEstablished(uint32_t broadcast_id, uint8_t big_handle,
                             const std::vector<uint16_t>& bis_handles) override {
      if (!instance) {
        return;
      }
      log::info("BIG sync established: broadcast_id=0x{:08x}, big_handle={}, num_bis={}",
                broadcast_id, big_handle, bis_handles.size());
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

      // State machine will handle the transition and OnBroadcastSinkStateChanged will be called
      // No need to call OnSourceLeft here
    }

    void OnBigSyncTerminated(uint32_t broadcast_id, uint8_t big_handle, uint8_t status) override {
      if (!instance) {
        return;
      }

      log::info("BIG sync terminated (INTENTIONAL): broadcast_id=0x{:08x}, big_handle={}, status=0x{:02x}",
                broadcast_id, big_handle, status);

      // State machine will handle the transition and OnBroadcastSinkStateChanged will be called
      // No need to call OnSourceLeft here
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
  } state_machine_callbacks_;

  static class LeAudioSinkCallbacksImpl : public bluetooth::le_audio::LeAudioSinkAudioHalClient::Callbacks {
   public:
    LeAudioSinkCallbacksImpl() = default;

    void OnAudioSuspend(void) override {
      log::info("Audio HAL suspend callback");

      // Always confirm suspend request
      if (instance && instance->le_audio_sink_hal_client_) {
        log::info("Confirming suspend request");
        instance->le_audio_sink_hal_client_->ConfirmSuspendRequest();
      }
    }

    void OnAudioResume(void) override {
      log::info("Audio HAL resume callback");

      // Confirm streaming request only if broadcast sink is in BIG_SYNCED state
      if (instance && instance->le_audio_sink_hal_client_) {
        bool is_streaming = instance->IsAnySinkStreaming();

        if (is_streaming) {
          log::info("Broadcast sink is streaming, confirming streaming request");
          instance->le_audio_sink_hal_client_->ConfirmStreamingRequest(false);
        } else {
          log::info("No streaming sources, not confirming streaming request");
        }
      }
    }

    void OnAudioMetadataUpdate(
            const std::vector<struct record_track_metadata_v7> sink_metadata) override {
      log::info("Audio HAL metadata update callback");
      // Handle metadata update if needed
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
