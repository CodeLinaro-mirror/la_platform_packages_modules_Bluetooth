/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <array>
#include <optional>
#include <vector>

#include "bta/le_audio/broadcast_sink/broadcast_sink_types.h"
#include "types/raw_address.h"

// Forward declaration - callbacks are defined in hardware interface
namespace bluetooth {
namespace le_audio {
namespace broadcast_sink {
class BroadcastSinkCallbacks;
}  // namespace broadcast_sink
}  // namespace le_audio
}  // namespace bluetooth

/**
 * Broadcast Sink Manager Interface
 * This interface follows the same pattern as BTIF layer interfaces
 */
class LeAudioBroadcastSink {
 public:
  virtual ~LeAudioBroadcastSink() = default;

  /**
   * Initialize the broadcast sink manager
   * @param callbacks Callbacks for broadcast sink events
   * @param max_source_capacity Maximum number of sources that can be PA synced simultaneously
   */
  static void Initialize(bluetooth::le_audio::broadcast_sink::BroadcastSinkCallbacks* callbacks,
                         uint8_t max_source_capacity);

  /**
   * Stop all active operations
   */
  static void Stop();

  /**
   * Clean up and shut down the broadcast sink manager
   */
  static void Cleanup();

  /**
   * Get the singleton instance
   * @return Pointer to the LeAudioBroadcastSink instance
   */
  static LeAudioBroadcastSink* Get();

  /**
   * Check if the broadcast sink manager is running
   * @return true if running, false otherwise
   */
  static bool IsLeAudioBroadcastSinkRunning();

  /**
   * Debug dump for logging
   * @param fd File descriptor to write to
   */
  static void DebugDump(int fd);

  /**
   * Add a broadcast source (PA sync)
   * Synchronizes with the Periodic Advertisements of a specific Broadcast Source.
   * Success results in OnSourceAdded callback with full metadata.
   *
   * @param addr Bluetooth address of the broadcast source
   * @param addr_type Address type (public/random)
   * @param adv_sid Advertising SID
   * @param broadcast_id Broadcast ID
   * @param rssi RSSI value
   * @param broadcast_name Broadcast name (empty string if not available)
   * @param is_public Whether this is a public broadcast
   * @param public_metadata Public broadcast metadata (raw LTV bytes from PBA)
   * @param public_features Public broadcast features (audio config quality bits)
   */
  virtual void AddSource(const RawAddress& addr, uint8_t addr_type,
                         uint8_t adv_sid,
                         bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id,
                         int8_t rssi, const std::string& broadcast_name,
                         bool is_public,
                         const std::vector<uint8_t>& public_metadata,
                         uint8_t public_features) = 0;
  /**
   * Join an enhanced (enhanced broadcast) broadcast source (BIG sync).
   *
   * Intended for enhanced sources
   * where at least one subgroup carries >= 3 BISes.  The state machine will
   * configure bidirectional (RX + TX) ISO data paths for every BIS.
   * Enhanced broadcast always syncs to all BISes in the BIG — no BIS
   * selection is supported.
   *
   * The caller should invoke this method after receiving the
   * BroadcastSinkCallbacks::OnEnhancedSourceDetected() callback.
   *
   * @param broadcast_id    Unique identifier for the broadcast
   * @param broadcast_code  Optional broadcast code for encrypted broadcasts
   */
  virtual void StartEnhancedBroadcastSink(
      bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id,
      const std::optional<bluetooth::le_audio::broadcast_sink::BroadcastCode>& broadcast_code) = 0;

  /**
   * Leave a broadcast source (stop BIG sync, keep PA sync)
   * Stops receiving broadcast audio but keeps PA synchronized.
   *
   * @param broadcast_id Unique identifier for the broadcast
   */
  virtual void StopEnhancedBroadcastSink(uint8_t mode) = 0;

  // Atomically arms call-preemption (SetSuspendedByCall) and stops the BIG on
  // the BTA main thread, eliminating the race where REMOVE_RX_PATHS arrives
  // before SetSuspendedByCall is processed.
  virtual void StopEnhancedBroadcastSinkPreempt(bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id) = 0;

  /**
   * Remove a broadcast source (stop PA sync)
   * Stops PA sync and removes the broadcast source completely.
   * If BIG is synced, it will be stopped first.
   *
   * @param broadcast_id Unique identifier for the broadcast
   */
  virtual void RemoveSource(bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id) = 0;

  /**
   * Destroy a broadcast source
   * Completely destroys the broadcast source and frees all associated resources.
   * This should be called after RemoveSource completes (OnSourceRemoved callback).
   *
   * @param broadcast_id Unique identifier for the broadcast
   */
  virtual void DestroySource(bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id) = 0;
  /**
   * Update source metadata
   * Updates the broadcast name and public announcement metadata for a source
   *
   * @param broadcast_id Unique identifier for the broadcast
   * @param broadcast_name New broadcast name
   * @param public_metadata New public metadata (raw LTV bytes)
   */
  virtual void SourcePublicMetadataChanged(
      bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id,
      const std::string& broadcast_name,
      const std::vector<uint8_t>& public_metadata) = 0;

  /**
   * Read the controller's supported LE states for enhanced broadcast sink.
   * Issues a VS HCI command and returns the result. The result is also cached
   * for later retrieval via GetEnhancedBroadcastSinkCap().
   * @return capability bitmask: bit0=Terminate, bit1=Remove Device. 0 if not ready.
   */
  virtual uint32_t ReadSupportedStatesForSink(void) = 0;

  /**
   * Get the enhanced broadcast sink capability bitmask returned by the
   * controller after ReadSupportedStatesForSink() completed.
   * @return capability bitmask, or 0 if not yet available.
   */
  virtual uint32_t GetEnhancedBroadcastSinkCap(void) = 0;

  /**
   * Push the 12-byte DBIG parameter block received from the broadcast source
   * (extracted from the vendor-specific PA LTV in the BASE subgroup metadata)
   * into the sink stack so it can be used when creating the DBIG.
   *
   * @param dbig_params  12-byte parameter vector
   */
  virtual void SetEnhancedDbigParams(const std::vector<uint8_t>& dbig_params) = 0;

  /**
   * Terminate the DBIG (spec §5.3 PGP Terminates procedure).
   * Sends HCI_VS_LE_Texit_DBIG(TERMINATE) so BT FW sends PGP_REQUEST(TERMINATE) to PGO.
   * Only valid for enhanced (DBIG) sources. Operates on the single active enhanced source.
   */
  virtual void TerminateDbig() = 0;

  // Arm/disarm sync-only mode on the given broadcast for HFP concurrency.
  // Must be called before disabling achat audio paths on call-start (isCallActive=true),
  // and before re-enabling them on call-end (isCallActive=false).
  virtual void NotifyCallState(uint32_t broadcast_id, bool isCallActive) = 0;
};
