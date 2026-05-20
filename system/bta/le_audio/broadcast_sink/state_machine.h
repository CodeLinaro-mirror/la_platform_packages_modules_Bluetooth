/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <array>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

#include "base/functional/callback.h"
#include "broadcast_sink_types.h"
#include "main/shim/le_scanning_manager.h"

namespace {
template <int S, typename StateT = uint8_t>
class StateMachine {
 public:
  StateMachine() : state_(std::numeric_limits<StateT>::min()) {}

 protected:
  StateT GetState() const { return state_; }
  void SetState(StateT state) {
    if (state < S) {
      state_ = state;
    }
  }

 private:
  StateT state_;
};
}  // namespace

/* Broadcast Sink state machine possible states:
 * IDLE       - Initial state, no active syncs. Ready to start PA sync.
 * PA_SYNCING - Establishing periodic advertising sync to broadcast source.
 *              Waiting for PA sync established event from controller.
 * PA_SYNCED  - PA sync established successfully. Receiving periodic
 *              advertisements and parsing BASE (Broadcast Audio Source
 *              Endpoint) data. Ready to establish BIG sync.
 * BIG_SYNCING- Establishing BIG (Broadcast Isochronous Group) sync.
 *              Waiting for BIG sync established event from controller.
 * BIG_SYNCED - BIG sync established, ISO data paths configured.
 *              Actively receiving broadcast audio data packets.
 * STOPPING   - Terminating all syncs (BIG and PA). Cleaning up resources.
 *              Target state is IDLE.
 */

namespace bluetooth::le_audio::broadcast_sink {

// Invalid PA sync registration ID value (indicates allocation failure)
static constexpr uint8_t kInvalidPaSyncRegId = 0x00;

// Maximum PA sync registration ID value (limits number of concurrent PA syncs)
static constexpr uint8_t kMaxPaSyncRegId = 0x05;

// Configuration for state machine initialization
struct BroadcastSinkStateMachineConfig {
  uint32_t reg_id;              // Internal registration identifier
  RawAddress address;           // Broadcast source address
  uint8_t address_type;         // Address type (public/random)
  uint8_t adv_sid;              // Advertising SID
  uint32_t broadcast_id;        // 24-bit broadcast ID
  std::string broadcast_name;   // Human-readable broadcast name
  bool is_public;               // Public broadcast announcement
  uint16_t pa_sync_timeout;     // PA sync timeout in ms
  std::optional<PublicBroadcastAnnouncementData> public_announcement;  // Public broadcast announcement data

  BroadcastSinkStateMachineConfig()
      : reg_id(0),
        address(RawAddress::kEmpty),
        address_type(0),
        adv_sid(0),
        broadcast_id(0),
        broadcast_name(""),
        is_public(false),
        pa_sync_timeout(0),
        public_announcement(std::nullopt) {}

  BroadcastSinkStateMachineConfig(uint32_t registration_id, RawAddress addr, uint8_t addr_type, uint8_t sid,
                                  uint32_t bcast_id, std::string bcast_name, bool pub,
                                  uint16_t pa_timeout,
                                  std::optional<PublicBroadcastAnnouncementData> pub_announcement = std::nullopt)
      : reg_id(registration_id),
        address(addr),
        address_type(addr_type),
        adv_sid(sid),
        broadcast_id(bcast_id),
        broadcast_name(std::move(bcast_name)),
        is_public(pub),
        pa_sync_timeout(pa_timeout),
        public_announcement(std::move(pub_announcement)) {}

  bool operator==(const BroadcastSinkStateMachineConfig& other) const {
    return reg_id == other.reg_id && address == other.address &&
           address_type == other.address_type && adv_sid == other.adv_sid &&
           broadcast_id == other.broadcast_id && broadcast_name == other.broadcast_name &&
           is_public == other.is_public && pa_sync_timeout == other.pa_sync_timeout;
  }

  bool operator!=(const BroadcastSinkStateMachineConfig& other) const {
    return !(*this == other);
  }
};

class IBroadcastSinkStateMachineCallbacks;

class BroadcastSinkStateMachine : public StateMachine<7> {
 public:
  // Scanner client ID for LE Audio broadcast sink (matches native client support)
  static constexpr uint8_t kScannerClientIdLeAudio = 0x1;

  static void Initialize(IBroadcastSinkStateMachineCallbacks* callbacks,
                         BleScannerInterface* ble_scanner);
  static std::unique_ptr<BroadcastSinkStateMachine> CreateInstance(
      BroadcastSinkStateMachineConfig config);

  // Messages that can be sent to the state machine
  enum class Message : uint8_t {
    START_BIG_SYNC = 0, // Start BIG sync (requires PA sync established)
    STOP_BIG_SYNC,      // Stop BIG sync only (keep PA sync)
    STOP_SYNC,          // Stop both BIG and PA sync
    MESSAGE_COUNT = 3,
  };

  inline SinkState GetState(void) const {
    return static_cast<SinkState>(StateMachine::GetState());
  }

  virtual bool Initialize() = 0;

  // State machine configuration getters
  virtual uint32_t GetRegId() const = 0;
  virtual uint32_t GetBroadcastId() const = 0;
  virtual const RawAddress& GetSourceAddress() const = 0;
  virtual uint8_t GetAddressType() const = 0;
  virtual uint8_t GetAdvSid() const = 0;
  virtual const std::string& GetBroadcastName() const = 0;
  virtual bool IsPublic() const = 0;
  virtual uint16_t GetPaSyncTimeout() const = 0;

  // Runtime state getters
  virtual bool IsEncrypted() const = 0;
  virtual const BroadcastSinkConfiguration& GetSinkConfiguration() const = 0;
  virtual std::optional<BroadcastCode> GetBroadcastCode() const = 0;
  virtual std::optional<BasicAudioAnnouncementData> GetBaseData() const = 0;
  virtual std::optional<PublicBroadcastAnnouncementData> GetPublicAnnouncement() const = 0;

  // Sync operations
  virtual void StartSync() = 0;
  virtual void StopSync() = 0;

  // Configuration updates
  virtual void UpdateBroadcastCode(const BroadcastCode& code) = 0;
  virtual void UpdateBisIndices(const std::vector<uint8_t>& bis_indices) = 0;
  virtual void UpdateSinkConfiguration(const BroadcastSinkConfiguration& config) = 0;
  virtual void UpdatePublicAnnouncement(const std::string& broadcast_name, const PublicBroadcastAnnouncementData& public_announcement) = 0;

  // Status queries
  virtual bool IsStreaming() const = 0;
  virtual bool IsPaSynced() const = 0;
  virtual bool IsPaSyncLost() const = 0;
  virtual std::optional<PaSyncInfo> GetPaSyncInfo() const = 0;
  virtual std::optional<BigSyncInfo> GetBigSyncInfo() const = 0;
  virtual const BroadcastSinkStats& GetStats() const = 0;

  // HCI event handlers
  virtual void HandleHciEvent(uint16_t event, void* data) = 0;
  virtual void OnSetupIsoDataPath(uint8_t status, uint16_t conn_handle) = 0;
  virtual void OnRemoveIsoDataPath(uint8_t status, uint16_t conn_handle) = 0;

  // BIG terminate sync complete callback
  virtual void OnBigTerminateSyncComplete(uint8_t big_handle, uint8_t status) = 0;

  // Scanning callbacks (from LE Scanning Manager)
  virtual void OnSyncEstablished(uint8_t status, uint16_t sync_handle, uint8_t adv_sid,
                                 uint8_t address_type, RawAddress address, uint8_t phy,
                                 uint16_t interval) = 0;
  virtual void OnSyncLost(uint16_t sync_handle) = 0;
  virtual void OnPeriodicScanResult(uint16_t sync_handle, int8_t tx_power, int8_t rssi,
                                    uint8_t status, std::vector<uint8_t> data) = 0;
  virtual void OnBigInfoReport(uint16_t sync_handle, bool encrypted) = 0;

  // Message processing
  virtual void ProcessMessage(Message msg, const void* data = nullptr) = 0;

  virtual ~BroadcastSinkStateMachine() {}

 protected:
  BroadcastSinkStateMachine() = default;

  void SetState(SinkState state) {
    StateMachine::SetState(static_cast<std::underlying_type<SinkState>::type>(state));
  }
};

// Callbacks from state machine to broadcast sink manager
class IBroadcastSinkStateMachineCallbacks {
 public:
  IBroadcastSinkStateMachineCallbacks() = default;
  virtual ~IBroadcastSinkStateMachineCallbacks() = default;

  // State machine lifecycle
  virtual void OnStateMachineCreateStatus(uint32_t broadcast_id, bool initialized) = 0;
  virtual void OnStateMachineDestroyed(uint32_t broadcast_id, uint8_t reason) = 0;
  virtual void OnStateMachineEvent(uint32_t broadcast_id, SinkState state,
                                   const void* data = nullptr) = 0;

  // PA sync events
  virtual void OnPaSyncEstablished(uint32_t broadcast_id, uint16_t pa_sync_handle, uint8_t adv_sid,
                                   RawAddress address, uint8_t address_type) = 0;
  virtual void OnPaSyncLost(uint32_t broadcast_id, uint16_t pa_sync_handle) = 0;

  // BASE data
  virtual void OnBaseDataReceived(uint32_t broadcast_id,
                                  const BasicAudioAnnouncementData& base_data) = 0;

  // BIG sync events
  virtual void OnBigSyncEstablished(uint32_t broadcast_id, uint8_t big_handle,
                                    const std::vector<uint16_t>& bis_handles) = 0;
  virtual void OnBigSyncLost(uint32_t broadcast_id, uint8_t big_handle, uint8_t reason) = 0;
  virtual void OnBigSyncTerminated(uint32_t broadcast_id, uint8_t big_handle, uint8_t status) = 0;

  // BIGInfo report (indicates broadcast is transmitting)
  virtual void OnBigInfoReport(uint32_t broadcast_id, uint16_t sync_handle, bool encrypted) = 0;
};

// Stream operators for logging
std::ostream& operator<<(std::ostream& os, const BroadcastSinkStateMachine::Message& msg);
std::ostream& operator<<(std::ostream& os, const BroadcastSinkStateMachine& machine);

}  // namespace bluetooth::le_audio::broadcast_sink
