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
  std::vector<uint8_t> dbig_params;  // DBIG parameters from advertising packet

  BroadcastSinkStateMachineConfig()
      : reg_id(0),
        address(RawAddress::kEmpty),
        address_type(0),
        adv_sid(0),
        broadcast_id(0),
        broadcast_name(""),
        is_public(false),
        pa_sync_timeout(0),
        public_announcement(std::nullopt),
        dbig_params() {}

  BroadcastSinkStateMachineConfig(uint32_t registration_id, RawAddress addr, uint8_t addr_type, uint8_t sid,
                                  uint32_t bcast_id, std::string bcast_name, bool pub,
                                  uint16_t pa_timeout,
                                  std::optional<PublicBroadcastAnnouncementData> pub_announcement = std::nullopt,
                                  std::vector<uint8_t> dbig_params_vec = {})
      : reg_id(registration_id),
        address(addr),
        address_type(addr_type),
        adv_sid(sid),
        broadcast_id(bcast_id),
        broadcast_name(std::move(bcast_name)),
        is_public(pub),
        pa_sync_timeout(pa_timeout),
        public_announcement(std::move(pub_announcement)),
        dbig_params(std::move(dbig_params_vec)) {}

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
    START_BIG_SYNC  = 0, // Start BIG sync (requires PA sync established)
    REMOVE_TX_PATHS,       // Triggered by source HAL suspend: remove TX ISO paths, terminate BIG sync
    REMOVE_RX_PATHS,     // Triggered by sink HAL suspend: remove RX ISO paths, ack sink HAL
    STOP_SYNC,           // Stop both BIG and PA sync (RemoveSource / full teardown)
    MESSAGE_COUNT   = 4,
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

  /**
   * Returns true when the broadcast source is an enhanced (enhanced broadcast)
   * source, i.e. at least one subgroup carries ≥ 3 BISes.  For such sources
   * the sink must set up bidirectional ISO data paths (TX first, then RX).
   */
  virtual bool IsEnhanced() const = 0;

  /** Set the TExitDbig mode to use when stopping/disabling BIG sync.
   * Must be called before MSG_STOP triggers teardown.
   * mode: HCI_TEXIT_MODE_EXIT (0x01) or HCI_TEXIT_MODE_TERMINATE (0x02) */
  virtual void SetTexitMode(uint8_t mode) = 0;

  /**
   * Called when the audio HAL (HIDL) sends a start indication to the BTA layer.
   *
   * For enhanced (enhanced broadcast) sources this drives the full setup sequence:
   *
   *   1st call (phase IDLE):
   *     → Sends the vendor enhanced broadcast setup HCI command.
   *     → Phase transitions to DBIG_SETUP.
   *     → On enhanced broadcast complete (OnDbigSetupComplete), BIG_CREATE_SYNC is issued.
   *     → On BIG sync established, TX ISO paths are set up for all BISes.
   *     → When all TX paths are ready, OnTxIsoPathsReady() fires so the BTA
   *       layer can acknowledge the 1st start to the HAL.
   *
   *   2nd call (phase TX_DONE):
   *     → Sets up RX ISO data paths for all BISes.
   *     → When all RX paths are ready, the state machine transitions to
   *       BIG_SYNCED and fires OnStateMachineEvent(BIG_SYNCED) so the BTA
   *       layer can acknowledge the 2nd start to the HAL.
   *
   * For standard (non-enhanced) sources this method is a no-op; RX paths are
   * set up automatically after BIG sync is established.
   */
  virtual void OnAudioStart() = 0;

  /**
   * Called when the vendor enhanced broadcast setup HCI command completes.
   * Only relevant for enhanced (enhanced broadcast) sources.
   *
   * On success, issues BIG_CREATE_SYNC to establish BIG sync.
   * On failure, transitions back to PA_SYNCED and fires OnStateMachineEvent().
   *
   * @param status  HCI status (0x00 = success)
   */
  virtual void OnDbigSetupComplete(uint8_t status) = 0;

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
  /* True while waiting for Source HAL start (achat_tx_enable=true) after
   * sync-only disable.  Sink HAL start must defer until TX paths are ready. */
  virtual bool IsEnhancedCallResumeReady() const = 0;

  // HCI event handlers
  virtual void HandleHciEvent(uint16_t event, void* data) = 0;
  /**
   * Called when an ISO data path setup completes.
   *
   * @param status      HCI status (0x00 = success)
   * @param conn_handle BIS connection handle
   * @param direction   ISO data path direction:
   *                    kIsoDataPathDirectionOut (0x00) = RX (sink receives)
   *                    kIsoDataPathDirectionIn  (0x01) = TX (sink sends, enhanced only)
   */
  virtual void OnSetupIsoDataPath(uint8_t status, uint16_t conn_handle,
                                  uint8_t direction) = 0;

  virtual void OnRemoveIsoDataPath(uint8_t status, uint16_t conn_handle) = 0;

  // DBIG TExitDbig complete callback
  virtual void OnTexitDbigComplete(uint8_t dbig_handle, uint8_t status, uint8_t reason) = 0;

  // Scanning callbacks (from LE Scanning Manager)
  virtual void OnSyncEstablished(uint8_t status, uint16_t sync_handle, uint8_t adv_sid,
                                 uint8_t address_type, RawAddress address, uint8_t phy,
                                 uint16_t interval) = 0;
  virtual void OnSyncLost(uint16_t sync_handle) = 0;
  virtual void OnPeriodicScanResult(uint16_t sync_handle, int8_t tx_power, int8_t rssi,
                                    uint8_t status, std::vector<uint8_t> data) = 0;
  virtual void OnBigInfoReport(uint16_t sync_handle, bool encrypted) = 0;

  /**
   * Store BIG info parameters received from the controller BIG Info Report.
   * Called by broadcast_sink.cc via OnBigInfoReportFull() before JoinSource()
   * so that SendDbigSetupCommand() can select the correct bis_control_event_interval.
   *
   * @param iso_interval  ISO interval in units of 1.25 ms (e.g. 8 = 10 ms)
   * @param phy           PHY: 1=LE1M, 2=LE2M, 3=Coded
   * @param num_bis       Number of BISes in the BIG
   */
  virtual void SetBigInfoParams(uint16_t iso_interval, uint8_t phy, uint8_t num_bis) = 0;

  /**
   * Set DBIG parameters from PA vendor LTV.
   * Must be called before OnAudioStart() for enhanced broadcast sources.
   *
   * @param dbig_params  12-byte DBIG parameter array from PA vendor LTV
   */
  virtual void SetDbigParams(const std::vector<uint8_t>& dbig_params) = 0;

  // Call-preemption state for sync-only mode.
  // SetSuspendedByCall(true) — set BEFORE achat disable so REMOVE_TX/RX_PATHS
  //   handlers skip SetState(DISABLING) and OnRemoveIsoDataPath skips
  //   TerminateBigSync, sending HCI VS DBIG_SYNC_ONLY(1) instead.
  // SetResumingAfterCall(true) — set on call-end so the PA_SYNCED resume path
  //   sends HCI VS DBIG_SYNC_ONLY(0) + re-setups ISOs instead of a full BIG sync.
  void SetSuspendedByCall(bool suspended) {
    suspended_by_call_ = suspended;
    resuming_after_call_ = false;
  }
  bool IsSuspendedByCall() const { return suspended_by_call_; }
  void SetResumingAfterCall(bool resuming) {
    resuming_after_call_ = resuming;
    suspended_by_call_ = false;
  }
  bool IsResumingAfterCall() const { return resuming_after_call_; }

  // Message processing
  virtual void ProcessMessage(Message msg, const void* data = nullptr) = 0;

  virtual ~BroadcastSinkStateMachine() {}

 protected:
  BroadcastSinkStateMachine() = default;

  void SetState(SinkState state) {
    StateMachine::SetState(static_cast<std::underlying_type<SinkState>::type>(state));
  }

  bool suspended_by_call_ = false;
  bool resuming_after_call_ = false;
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

  /**
   * Called when BASE data parsing reveals that the broadcast source is an
   * enhanced (enhanced broadcast) source (at least one subgroup has ≥ 3 BISes).
   * The upper layer should call StartEnhancedBroadcastSink() instead of JoinSource()
   * for such sources.
   *
   * @param broadcast_id  Broadcast ID of the enhanced source
   * @param num_bis       Total number of BISes detected in the subgroup
   */
  virtual void OnEnhancedSourceDetected(uint32_t broadcast_id, uint8_t num_bis) = 0;

  // PA sync events
  virtual void OnPaSyncEstablished(uint32_t broadcast_id, uint16_t pa_sync_handle, uint8_t adv_sid,
                                   RawAddress address, uint8_t address_type) = 0;
  virtual void OnPaSyncLost(uint32_t broadcast_id, uint16_t pa_sync_handle) = 0;

  // BASE data
  virtual void OnBaseDataReceived(uint32_t broadcast_id,
                                  const BasicAudioAnnouncementData& base_data) = 0;

  /**
   * Called by the state machine when all TX ISO data paths have been
   * configured for an enhanced (enhanced broadcast) source.  The BTA layer
   * should acknowledge the first HIDL start indication at this point
   * (e.g. by calling LeAudioSinkAudioHalClient::ConfirmStreamingRequest).
   *
   * @param broadcast_id  Broadcast ID of the enhanced source
   */
  virtual void OnTxIsoPathsReady(uint32_t broadcast_id) = 0;

  /**
   * Called by the state machine when all RX ISO data paths have been removed
   * for an enhanced (enhanced broadcast) source during teardown.
   * Triggered by the sink HAL OnAudioSuspend path (REMOVE_RX_PATHS message).
   * The BTA layer should acknowledge the sink HAL suspend at this point
   * (e.g. by calling LeAudioSinkAudioHalClient::ConfirmSuspendRequest).
   *
   * @param broadcast_id  Broadcast ID of the enhanced source
   */
  virtual void OnRxIsoPathsRemoved(uint32_t broadcast_id) = 0;

  /**
   * Called by the state machine when HCI VS DBIG_SYNC_ONLY(enable=1) completes
   * during call preemption. At this point TX ISO paths are removed and the BIG
   * is in sync-only mode. The BTA layer must acknowledge the TX (source HAL)
   * suspend that was deferred because BIG termination was skipped.
   *
   * @param broadcast_id  Broadcast ID of the preempted enhanced source
   */
  virtual void OnTxSuspendAckedBySyncOnly(uint32_t broadcast_id) = 0;

  /**
   * Called when HCI VS DBIG_SYNC_ONLY(enable=1) completes: all ISO paths removed,
   * controller idle, BIG alive in sync-only mode.
   * Safe to send AT+BCC now. Mirrors PGO's LeAudioBroadcasterCallbacks::OnSyncOnlyModeActive.
   */
  virtual void OnSyncOnlyModeActive(uint32_t broadcast_id) = 0;

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
