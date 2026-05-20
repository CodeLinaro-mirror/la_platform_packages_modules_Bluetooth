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
  virtual void JoinSource(BroadcastId broadcast_id,
                         const std::optional<BroadcastCode>& broadcast_code,
                         const std::vector<uint8_t>& bis_indices) = 0;
  virtual void LeaveSource(BroadcastId broadcast_id) = 0;

  // Source removal (terminates both PA and BIG sync)
  virtual void RemoveSource(BroadcastId broadcast_id) = 0;

  // Destroy source (frees all resources after removal)
  virtual void DestroySource(BroadcastId broadcast_id) = 0;

  // Query operations
  virtual void GetSourceMetadata(BroadcastId broadcast_id) = 0;

  // Metadata update notification
  virtual void SourcePublicMetadataChanged(BroadcastId broadcast_id,
                                   const std::string& broadcast_name,
                                   const std::vector<uint8_t>& public_metadata) = 0;
};

}  // namespace broadcast_sink
}  // namespace le_audio
}  // namespace bluetooth
