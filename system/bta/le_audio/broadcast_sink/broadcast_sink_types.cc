/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include "broadcast_sink_types.h"

#include "bta/le_audio/audio_hal_client/audio_hal_client.h"

namespace bluetooth::le_audio::broadcast_sink {

LeAudioCodecConfiguration BroadcastSinkConfiguration::GetAudioHalClientConfig() const {
  return {
          // Get the maximum number of channels across all subgroups
          .num_channels = GetNumChannelsMax(),
          // Get the maximum sampling frequency across all subgroups
          .sample_rate = GetSamplingFrequencyHzMax(),
          // Use the default 16 bits per sample resolution in the audio framework
          .bits_per_sample = 16,
          // Get the maximum SDU interval (frame duration) across all subgroups
          .data_interval_us = GetSduIntervalUsMax(),
  };
}

bool IsValidBroadcastMetadataForCallback(const BroadcastMetadata& metadata) {
  const auto& subgroups = metadata.basic_audio_announcement.subgroup_configs;
  if (subgroups.empty()) {
    return false;
  }

  for (const auto& subgroup : subgroups) {
    if (subgroup.bis_configs.empty()) {
      return false;
    }
  }

  return true;
}

}  // namespace bluetooth::le_audio::broadcast_sink
