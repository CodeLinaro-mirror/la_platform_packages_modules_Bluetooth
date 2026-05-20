/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include <bluetooth/log.h>

#include <optional>
#include <vector>

#include "bta/include/bta_le_audio_api.h"
#include "bta/le_audio/broadcaster/broadcaster_types.h"
#include "bta/le_audio/le_audio_types.h"
#include "hardware/bt_le_audio_broadcast_sink.h"
#include "types/raw_address.h"

/* Types used internally by the broadcast sink implementation */

namespace bluetooth::le_audio {
struct LeAudioCodecConfiguration;

namespace broadcast_sink {

// Note: BroadcastId, BroadcastCode, and BroadcastMetadata are defined in
// system/include/hardware/bt_le_audio_broadcast_sink.h to avoid redefinition

// Broadcast sink source destroyed reason codes
constexpr uint8_t kBroadcastSinkDestroyReasonNormal = 0x00;      // User requested
constexpr uint8_t kBroadcastSinkDestroyReasonPaSyncLost = 0x01; // PA sync lost

// Default timeout and configuration values
constexpr uint16_t kDefaultPaSyncTimeout = 0x00C8;   // 2000ms (in units of 10ms)
constexpr uint16_t kDefaultBigSyncTimeout = 0x00C8;  // 2000ms (in units of 10ms)
constexpr uint8_t kDefaultMse = 0x09;                // Maximum number of subevents

// Broadcast sink configuration (codec and BIS configuration)
struct BroadcastSinkConfiguration {
  std::vector<broadcaster::BroadcastSubgroupCodecConfig> subgroups;
  types::DataPathConfiguration data_path;
  std::vector<uint8_t> bis_indices;  // BIS indices to sync (1-based)
  uint16_t big_sync_timeout;   // BIG sync timeout in units of 10ms
  uint8_t mse;                 // Maximum number of subevents

  BroadcastSinkConfiguration()
      : data_path(),
        big_sync_timeout(kDefaultBigSyncTimeout),
        mse(kDefaultMse) {
    // Initialize with software encoder data path (similar to broadcaster)
    data_path.dataPathId = bluetooth::hci::iso_manager::kIsoDataPathPlatformDefault;
    data_path.dataPathConfig = {};
    data_path.isoDataPathConfig.codecId = {
        .coding_format = types::kLeAudioCodingFormatLC3,
        .vendor_company_id = types::kLeAudioVendorCompanyIdUndefined,
        .vendor_codec_id = types::kLeAudioVendorCodecIdUndefined};
    data_path.isoDataPathConfig.isTransparent = true;
    data_path.isoDataPathConfig.controllerDelayUs = 0x00000000;
    data_path.isoDataPathConfig.configuration = {};
  }

  bool operator==(const BroadcastSinkConfiguration& other) const {
    return (subgroups == other.subgroups) && (data_path == other.data_path) &&
           (bis_indices == other.bis_indices) && (big_sync_timeout == other.big_sync_timeout) &&
           (mse == other.mse);
  }

  bool operator!=(const BroadcastSinkConfiguration& other) const { return !(*this == other); }

  bool IsValid() const {
    // Configuration is valid even without subgroups (will be populated from BASE data)
    return true;
  }

  uint8_t GetNumBis() const { return static_cast<uint8_t>(bis_indices.size()); }

  // Get the maximum number of channels across all subgroups
  uint8_t GetNumChannelsMax() const {
    uint8_t value = 0;
    for (auto const& cfg : subgroups) {
      if (cfg.GetNumChannelsTotal() > value) {
        value = cfg.GetNumChannelsTotal();
      }
    }
    return value;
  }

  // Get the maximum sampling frequency across all subgroups
  uint32_t GetSamplingFrequencyHzMax() const {
    uint32_t value = 0;
    for (auto const& cfg : subgroups) {
      if (cfg.GetSamplingFrequencyHzMax() > value) {
        value = cfg.GetSamplingFrequencyHzMax();
      }
    }
    return value;
  }

  // Get the maximum SDU interval across all subgroups
  uint32_t GetSduIntervalUsMax() const {
    uint32_t value = 0;
    for (auto const& cfg : subgroups) {
      auto codec_spec = cfg.GetCommonBisCodecSpecData();
      auto frame_duration_us = codec_spec.GetAsCoreCodecConfig().GetFrameDurationUs();
      if (frame_duration_us > value) {
        value = frame_duration_us;
      }
    }
    return value;
  }

  // Get audio HAL client configuration for this broadcast sink
  // This configuration is used to set up the audio data path to the Audio HAL
  LeAudioCodecConfiguration GetAudioHalClientConfig() const;
};

// Sink state machine states
enum class SinkState : uint8_t {
  IDLE = 0,         // Initial state, no active syncs
  PA_SYNCING,       // Establishing periodic advertising sync
  PA_SYNCED,        // PA sync established, parsing BASE
  BIG_SYNCING,      // Establishing BIG sync
  BIG_SYNCED,       // BIG sync established, receiving broadcast audio
  DISABLING,        // Terminating BIG sync only (target: PA_SYNCED)
  STOPPING,         // Terminating both BIG and PA sync (target: IDLE)
  STATE_COUNT = 7,  // Total number of states
};

// Sink state machine events
enum class SinkEvent : uint8_t {
  START_SYNC = 0,
  STOP_SYNC,
  PA_SYNC_ESTABLISHED,
  PA_SYNC_LOST,
  BASE_DATA_RECEIVED,
  BIG_SYNC_ESTABLISHED,
  BIG_SYNC_LOST,
  AUDIO_DATA_READY,
  ISO_DATA_PATH_ESTABLISHED,
  ISO_DATA_PATH_REMOVED,
  EVENT_COUNT = 10,  // Total number of events
};

// BIG sync information
struct BigSyncInfo {
  uint8_t big_handle;
  uint16_t pa_sync_handle;
  std::vector<uint16_t> bis_conn_handles;
  uint32_t transport_latency_us;
  uint8_t nse;  // Number of subevents
  uint8_t bn;   // Burst number
  uint8_t pto;  // Pre-transmission offset
  uint8_t irc;  // Immediate repetition count
  uint16_t max_pdu;
  uint16_t iso_interval;
  uint8_t num_bis;

  BigSyncInfo()
      : big_handle(0xFF),
        pa_sync_handle(0xFFFF),
        transport_latency_us(0),
        nse(0),
        bn(0),
        pto(0),
        irc(0),
        max_pdu(0),
        iso_interval(0),
        num_bis(0) {}

  bool IsValid() const {
    return (big_handle != 0xFF) && (pa_sync_handle != 0xFFFF) && !bis_conn_handles.empty();
  }
};

// PA sync information
struct PaSyncInfo {
  uint16_t sync_handle;
  uint8_t adv_sid;
  RawAddress address;
  uint8_t address_type;
  uint8_t phy;
  uint16_t interval;  // In units of 1.25ms

  PaSyncInfo()
      : sync_handle(0xFFFF),
        adv_sid(0xFF),
        address(RawAddress::kEmpty),
        address_type(0),
        phy(0),
        interval(0) {}

  bool IsValid() const {
    return (sync_handle != 0xFFFF) && (adv_sid != 0xFF) && (address != RawAddress::kEmpty);
  }
};

// Audio data packet
struct AudioDataPacket {
  uint16_t conn_handle;
  uint32_t timestamp;
  std::vector<uint8_t> data;
  uint8_t packet_status;  // 0=Valid, 1=Possibly invalid, 2=Lost data

  AudioDataPacket() : conn_handle(0xFFFF), timestamp(0), packet_status(0) {}
};

// Broadcast sink statistics
struct BroadcastSinkStats {
  uint32_t packets_received;
  uint32_t packets_lost;
  uint32_t sync_lost_count;
  uint32_t sync_reestablished_count;
  uint64_t total_bytes_received;
  uint32_t last_rssi;

  BroadcastSinkStats()
      : packets_received(0),
        packets_lost(0),
        sync_lost_count(0),
        sync_reestablished_count(0),
        total_bytes_received(0),
        last_rssi(0) {}

  void Reset() {
    packets_received = 0;
    packets_lost = 0;
    sync_lost_count = 0;
    sync_reestablished_count = 0;
    total_bytes_received = 0;
    last_rssi = 0;
  }
};

// Helper functions for state and event names (for logging/debugging)
inline const char* SinkStateToString(SinkState state) {
  switch (state) {
    case SinkState::IDLE:
      return "IDLE";
    case SinkState::PA_SYNCING:
      return "PA_SYNCING";
    case SinkState::PA_SYNCED:
      return "PA_SYNCED";
    case SinkState::BIG_SYNCING:
      return "BIG_SYNCING";
    case SinkState::BIG_SYNCED:
      return "BIG_SYNCED";
    case SinkState::DISABLING:
      return "DISABLING";
    case SinkState::STOPPING:
      return "STOPPING";
    default:
      return "UNKNOWN";
  }
}

inline const char* SinkEventToString(SinkEvent event) {
  switch (event) {
    case SinkEvent::START_SYNC:
      return "START_SYNC";
    case SinkEvent::STOP_SYNC:
      return "STOP_SYNC";
    case SinkEvent::PA_SYNC_ESTABLISHED:
      return "PA_SYNC_ESTABLISHED";
    case SinkEvent::PA_SYNC_LOST:
      return "PA_SYNC_LOST";
    case SinkEvent::BASE_DATA_RECEIVED:
      return "BASE_DATA_RECEIVED";
    case SinkEvent::BIG_SYNC_ESTABLISHED:
      return "BIG_SYNC_ESTABLISHED";
    case SinkEvent::BIG_SYNC_LOST:
      return "BIG_SYNC_LOST";
    case SinkEvent::AUDIO_DATA_READY:
      return "AUDIO_DATA_READY";
    case SinkEvent::ISO_DATA_PATH_ESTABLISHED:
      return "ISO_DATA_PATH_ESTABLISHED";
    case SinkEvent::ISO_DATA_PATH_REMOVED:
      return "ISO_DATA_PATH_REMOVED";
    default:
      return "UNKNOWN";
  }
}

// Stream operators for logging
inline std::ostream& operator<<(std::ostream& os, const SinkState& state) {
  return os << SinkStateToString(state);
}

inline std::ostream& operator<<(std::ostream& os, const SinkEvent& event) {
  return os << SinkEventToString(event);
}

inline std::ostream& operator<<(std::ostream& os, const BroadcastSinkConfiguration& config) {
  os << "BroadcastSinkConfiguration{";
  os << "num_subgroups=" << config.subgroups.size();
  os << ", data_path_id=" << +config.data_path.dataPathId;
  os << ", bis_indices=[";
  for (size_t i = 0; i < config.bis_indices.size(); ++i) {
    if (i > 0) os << ",";
    os << +config.bis_indices[i];
  }
  os << "]";
  os << ", big_sync_timeout=" << config.big_sync_timeout;
  os << ", mse=" << +config.mse;
  os << "}";
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const BigSyncInfo& info) {
  os << "BigSyncInfo{";
  os << "big_handle=" << +info.big_handle;
  os << ", pa_sync_handle=0x" << std::hex << info.pa_sync_handle << std::dec;
  os << ", num_bis=" << +info.num_bis;
  os << ", bis_handles=[";
  for (size_t i = 0; i < info.bis_conn_handles.size(); ++i) {
    if (i > 0) os << ",";
    os << "0x" << std::hex << info.bis_conn_handles[i] << std::dec;
  }
  os << "]";
  os << ", transport_latency=" << info.transport_latency_us << "us";
  os << "}";
  return os;
}

inline std::ostream& operator<<(std::ostream& os, const PaSyncInfo& info) {
  os << "PaSyncInfo{";
  os << "sync_handle=0x" << std::hex << info.sync_handle << std::dec;
  os << ", adv_sid=" << +info.adv_sid;
  os << ", address=" << info.address.ToString();
  os << ", phy=" << +info.phy;
  os << ", interval=" << info.interval << " (1.25ms units)";
  os << "}";
  return os;
}

}  // namespace broadcast_sink
}  // namespace bluetooth::le_audio
