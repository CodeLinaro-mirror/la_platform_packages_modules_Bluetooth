/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "raw_address.h"

namespace bluetooth {
namespace le_audio {

// Forward declarations from bt_le_audio.h to avoid circular dependency
// These types are defined in bt_le_audio.h and shared between broadcast source and sink
using BroadcastId = uint32_t;
using BroadcastCode = std::array<uint8_t, 16>;

struct BasicAudioAnnouncementCodecConfig;
struct BasicAudioAnnouncementBisConfig;
struct BasicAudioAnnouncementSubgroup;
struct BasicAudioAnnouncementData;
struct PublicBroadcastAnnouncementData;
struct BroadcastMetadata;

namespace broadcast_sink {

// Re-export the unified types for backward compatibility
using bluetooth::le_audio::BroadcastId;
using bluetooth::le_audio::BroadcastCode;
using bluetooth::le_audio::BroadcastMetadata;

// Callbacks from BTIF to JNI layer
class BroadcastSinkCallbacks {
 public:
  virtual ~BroadcastSinkCallbacks() = default;

  // Failure callbacks
  virtual void OnSourceAddFailed(BroadcastId broadcast_id, uint8_t reason) = 0;
  virtual void OnSourceJoinFailed(BroadcastId broadcast_id, uint8_t reason) = 0;
  virtual void OnSourceLeaveFailed(BroadcastId broadcast_id, uint8_t reason) = 0;
  virtual void OnSourceRemoveFailed(BroadcastId broadcast_id, uint8_t reason) = 0;

  // Source destroyed callback (after DestroySource completes)
  virtual void OnSourceDestroyed(BroadcastId broadcast_id, uint8_t reason) = 0;

  // Metadata update callback
  virtual void OnSourceMetadataChanged(
      BroadcastId broadcast_id,
      const BroadcastMetadata& broadcast_metadata) = 0;

  // Audio session callback
  virtual void OnBroadcastSinkAudioSessionCreated(bool success) = 0;

  // State change callback - replaces OnSourceAdded, OnSourceJoined, OnSourceLeft, OnSourceRemoved
  virtual void OnBroadcastSinkStateChanged(BroadcastId broadcast_id, uint8_t state) = 0;

  /**
   * Called when BIG sync is successfully established (BIG_SYNCED).
   *
   * For enhanced (enhanced broadcast) sources this fires only after all TX and
   * RX ISO data paths have been configured — Java never sees intermediate
   * enhanced broadcast or ISO data path events.
   *
   * @param broadcast_id  Broadcast ID
   * @param big_handle    BIG handle assigned by the controller
   * @param bis_handles   Connection handles for each BIS in the BIG
   */
  virtual void OnBigSyncCreated(BroadcastId broadcast_id,
                                uint8_t big_handle,
                                const std::vector<uint16_t>& bis_handles) = 0;

  /**
   * Called when BIG sync is lost (unexpected disconnection or controller
   * termination).
   *
   * @param broadcast_id  Broadcast ID
   * @param big_handle    BIG handle that was lost
   * @param reason        HCI disconnect reason code
   */
  virtual void OnBigSyncLost(BroadcastId broadcast_id,
                             uint8_t big_handle,
                             uint8_t reason) = 0;

  /**
   * Called when BIG sync is intentionally terminated by the local device
   * (user-initiated StopEnhancedBroadcastSink).  This fires after all TX and RX ISO data
   * paths have been removed and the controller has confirmed BIG termination.
   *
   * @param broadcast_id  Broadcast ID
   * @param big_handle    BIG handle that was terminated
   * @param status        HCI status code (0x00 = success)
   */
  virtual void OnBigSyncTerminated(BroadcastId broadcast_id,
                                   uint8_t big_handle,
                                   uint8_t status) = 0;

  /**
   * Called when BASE data parsing reveals that the broadcast source is an
   * enhanced (enhanced broadcast) source, i.e. at least one subgroup carries
   * >= 3 BISes.  The upper layer should call StartEnhancedBroadcastSink() instead
   * of JoinSource() for such sources so that bidirectional ISO data paths
   * (RX + TX per BIS) are configured correctly.
   *
   * @param broadcast_id  Broadcast ID of the enhanced source
   * @param num_bis       Total number of BISes detected in the subgroup
   */
  virtual void OnEnhancedSourceDetected(BroadcastId broadcast_id,
                                        uint8_t num_bis) = 0;

  /**
   * Called when a DBIG status update event is received from the controller.
   *
   * @param dbig_handle       BIG handle from the controller event
   * @param status            DBIG status value from the controller event
   * @param dev_id            Device ID (12-bit)
   * @param name              Device name (up to 10 bytes)
   * @param num_bis           Number of BIS channels
   * @param bis_dev_ids       BIS device IDs (12-bit each)
   * @param broadcast_features Broadcast features bitmask
   */
  virtual void OnDbigStatusChanged(uint8_t dbig_handle, uint16_t status,
                                    uint16_t dev_id, std::vector<uint8_t> name,
                                    uint8_t num_bis,
                                    std::vector<uint16_t> bis_dev_ids,
                                    uint16_t broadcast_features) = 0;
};

// Interface from JNI to BTIF layer
class BroadcastSinkInterface {
 public:
  virtual ~BroadcastSinkInterface() = default;

  // Lifecycle management
  virtual void Initialize(BroadcastSinkCallbacks* callbacks,
                         uint8_t max_source_capacity) = 0;
  virtual void Stop(void) = 0;
  virtual void Cleanup(void) = 0;

  // PA sync operations
  virtual void AddSource(const RawAddress& addr, uint8_t addr_type,
                        uint8_t adv_sid, BroadcastId broadcast_id,
                        int8_t rssi, const std::string& broadcast_name,
                        bool is_public,
                        const std::vector<uint8_t>& public_metadata,
                        uint8_t public_features) = 0;

  // BIG sync operations
  /**
   * Join an enhanced (enhanced broadcast) broadcast source (BIG sync).
   *
   * Identical to JoinSource() but explicitly intended for enhanced sources
   * where at least one subgroup carries >= 3 BISes.  The state machine will
   * configure bidirectional (RX + TX) ISO data paths for every BIS.
   * Enhanced broadcast always syncs to all BISes in the BIG — no BIS
   * selection is supported.
   *
   * Should be called after receiving OnEnhancedSourceDetected().
   *
   * @param broadcast_id    Unique identifier for the broadcast
   * @param broadcast_code  Optional broadcast code for encrypted broadcasts
   */
  virtual void StartEnhancedBroadcastSink(
      BroadcastId broadcast_id,
      const std::optional<BroadcastCode>& broadcast_code) = 0;

  virtual void StopEnhancedBroadcastSink(BroadcastId broadcast_id) = 0;

  // Source removal (terminates both PA and BIG sync)
  virtual void RemoveSource(BroadcastId broadcast_id) = 0;

  // Destroy source (frees all resources after removal)
  virtual void DestroySource(BroadcastId broadcast_id) = 0;

  // Query operations
  // Metadata update notification
  virtual void SourcePublicMetadataChanged(BroadcastId broadcast_id,
                                   const std::string& broadcast_name,
                                   const std::vector<uint8_t>& public_metadata) = 0;
};

}  // namespace broadcast_sink
}  // namespace le_audio
}  // namespace bluetooth
