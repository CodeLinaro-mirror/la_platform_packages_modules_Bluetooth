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
   * Join a broadcast source (BIG sync)
   * Starts receiving the broadcast audio from the specified source.
   * This performs BIG sync. If the device has not synced to PA, this will fail.
   *
   * @param broadcast_id Unique identifier for the broadcast
   * @param broadcast_code Optional broadcast code for encrypted broadcasts
   * @param bis_indices Vector of BIS indices to sync to (empty means sync to all BISes)
   */
  virtual void JoinSource(
      bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id,
      const std::optional<bluetooth::le_audio::broadcast_sink::BroadcastCode>& broadcast_code,
      const std::vector<uint8_t>& bis_indices) = 0;

  /**
   * Leave a broadcast source (stop BIG sync, keep PA sync)
   * Stops receiving broadcast audio but keeps PA synchronized.
   *
   * @param broadcast_id Unique identifier for the broadcast
   */
  virtual void LeaveSource(bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id) = 0;

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
   * Get metadata for a specific broadcast source
   * Triggers OnSourceMetadataChanged callback with the metadata
   *
   * @param broadcast_id Unique identifier for the broadcast
   */
  virtual void GetSourceMetadata(
      bluetooth::le_audio::broadcast_sink::BroadcastId broadcast_id) = 0;

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
};
