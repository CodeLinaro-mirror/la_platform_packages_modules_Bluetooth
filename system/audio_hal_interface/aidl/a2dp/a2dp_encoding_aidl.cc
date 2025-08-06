/*
 * Copyright 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#define LOG_TAG "bluetooth-a2dp-aidl"

#include "a2dp_encoding_aidl.h"

#include <bluetooth/log.h>
#include <com_android_bluetooth_flags.h>

#include <vector>

#include "a2dp_aidl_transport.h"
#include "a2dp_encoding_aidl_utils.h"
#include "a2dp_provider_info.h"
#include "audio_aidl_interfaces.h"
#include "client_interface_aidl.h"
#include "codec_status_aidl.h"

namespace bluetooth {
namespace audio {
namespace aidl {
namespace a2dp {

using ::bluetooth::audio::a2dp::ahal_codec_configuration;

using ::bluetooth::audio::a2dp::Status;
using ::bluetooth::audio::a2dp::StreamCallbacks;
using ::bluetooth::audio::aidl::a2dp::LatencyMode;
using bluetooth::audio::a2dp::MAX_A2DP_CONN;

static StreamCallbacks null_stream_callbacks_;

namespace {

using ::aidl::android::hardware::bluetooth::audio::A2dpStreamConfiguration;
using ::aidl::android::hardware::bluetooth::audio::AudioConfiguration;
using ::aidl::android::hardware::bluetooth::audio::AudioContext;
using ::aidl::android::hardware::bluetooth::audio::ChannelMode;
using ::aidl::android::hardware::bluetooth::audio::CodecConfiguration;
using ::aidl::android::hardware::bluetooth::audio::PcmConfiguration;
using ::aidl::android::hardware::bluetooth::audio::SessionType;

using ::bluetooth::audio::aidl::a2dp::BluetoothAudioClientInterface;
using ::bluetooth::audio::aidl::a2dp::codec::getHalCodecConfiguration;
using ::bluetooth::audio::aidl::a2dp::codec::getHalPcmConfiguration;

// Common interface to call-out into Bluetooth Audio HAL
BluetoothAudioClientInterface* software_hal_interface[MAX_A2DP_CONN] = {};
BluetoothAudioClientInterface* offloading_hal_interface[MAX_A2DP_CONN] = {};
BluetoothAudioClientInterface* decoder_offloading_hal_interface = nullptr;
BluetoothAudioClientInterface* active_hal_interface[MAX_A2DP_CONN] = {};

// ProviderInfo for A2DP hardware offload encoding and decoding data paths,
// if supported by the HAL and enabled. nullptr if not supported
// or disabled.
std::array<std::unique_ptr<::bluetooth::audio::aidl::a2dp::ProviderInfo>, MAX_A2DP_CONN> provider_info;

// Save the value if the remote reports its delay before this interface is
// initialized
uint16_t remote_delay[MAX_A2DP_CONN] = {0};

bool is_low_latency_mode_allowed = false;

}  // namespace

// Check if a2dp source index is valid
bool is_index_valid(uint8_t index) {
  return index < MAX_A2DP_CONN;
}

bool update_codec_offloading_capabilities(
        const std::vector<btav_a2dp_codec_config_t>& framework_preference,
        bool supports_a2dp_hw_offload_v2, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }
  /* Load the provider information if supported by the HAL. */
  provider_info[index] = ::bluetooth::audio::aidl::a2dp::ProviderInfo::GetProviderInfo(
          supports_a2dp_hw_offload_v2, offloading_hal_interface[index]);
  return ::bluetooth::audio::aidl::a2dp::codec::UpdateOffloadingCapabilities(framework_preference,
                                                                             offloading_hal_interface[index]);
}

// Checking if new bluetooth_audio is enabled
bool is_hal_enabled(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }
  return active_hal_interface[index] != nullptr;
}

// Check if new bluetooth_audio is running with offloading encoders
bool is_hal_offloading(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }
  if (!is_hal_enabled(index)) {
    return false;
  }
  return active_hal_interface[index]->GetTransportInstance()->GetSessionType() ==
         SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
}

// Opens the HAL client interface of the specified session type and check
// that is is valid. Returns nullptr if the client interface did not open
// properly.
static BluetoothAudioClientInterface* new_hal_interface(SessionType session_type,
                                                        StreamCallbacks const* stream_callbacks,
                                                        uint8_t index) {
  auto hal_interface = new BluetoothAudioClientInterface(session_type, stream_callbacks, index);
  if (hal_interface->IsValid()) {
    return hal_interface;
  }
  log::error("BluetoothAudio HAL for a2dp is invalid");
  delete hal_interface;
  return nullptr;
}

/// Delete the selected HAL client interface.
static void delete_hal_interface(BluetoothAudioClientInterface* hal_interface) {
  delete hal_interface;
}

// Initialize BluetoothAudio HAL: openProvider
bool init(bluetooth::common::MessageLoopThread* /*message_loop*/,
          StreamCallbacks const* stream_callbacks, bool offload_enabled,
          uint8_t index) {
  log::info("");

  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }

  if (software_hal_interface[index] != nullptr) {
    return true;
  }

  software_hal_interface[index] = new_hal_interface(SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH,
                                                    stream_callbacks, index);
  if (software_hal_interface[index] == nullptr) {
    return false;
  }

  if (!software_hal_interface[index]->is_aidl_available()) {
    log::error("BluetoothAudio AIDL implementation does not exist");
    delete_hal_interface(software_hal_interface[index]);
    software_hal_interface[index] = nullptr;
    return false;
  }

  if (offload_enabled && offloading_hal_interface[index] == nullptr) {
    offloading_hal_interface[index] =
            new_hal_interface(SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH,
                              stream_callbacks, index);
    if (offloading_hal_interface[index] == nullptr) {
      log::error("BluetoothAudio Offload HAL for a2dp is invalid");
      delete_hal_interface(software_hal_interface[index]);
      software_hal_interface[index] = nullptr;
      return false;
    }
  }

  active_hal_interface[index] = (offloading_hal_interface[index] != nullptr
                                        ? offloading_hal_interface[index]
                                        : software_hal_interface[index]);

  if (remote_delay[index] != 0) {
    log::info("restore DELAY {} ms", static_cast<float>(remote_delay[index] / 10.0));
    active_hal_interface[index]->GetTransportInstance()->SetRemoteDelay(remote_delay[index]);
    remote_delay[index] = 0;
  }
  return true;
}

// Initialize BluetoothAudio HAL for decoding session
bool init_decoder(StreamCallbacks const* stream_callbacks, bool offload_enabled) {
  log::info("");
  log::assert_that(stream_callbacks != nullptr, "stream_callbacks != nullptr");
  if (decoder_offloading_hal_interface != nullptr) {
    return true;
  }

  if (offload_enabled) {
    decoder_offloading_hal_interface = new BluetoothAudioClientInterface(
            SessionType::A2DP_HARDWARE_OFFLOAD_DECODING_DATAPATH, stream_callbacks);
    if (!decoder_offloading_hal_interface->is_aidl_available()) {
      log::error("BluetoothAudio AIDL implementation does not exist");
      delete decoder_offloading_hal_interface;
      decoder_offloading_hal_interface = nullptr;
      return false;
    }
    if (!decoder_offloading_hal_interface->IsValid()) {
      log::error("BluetoothAudio HAL for a2dp decoder is invalid");
      delete decoder_offloading_hal_interface;
      decoder_offloading_hal_interface = nullptr;
      return false;
    }
  }
  return true;
}

// Clean up BluetoothAudio HAL
void cleanup() {
  for (int i = 0; i < MAX_A2DP_CONN; i++) {
    if (!is_hal_enabled(i)) {
      continue;
    }
    end_session(i);

    auto transport = active_hal_interface[i]->GetTransportInstance();
    transport->ResetPendingCmd();
    transport->ResetPresentationPosition();
    active_hal_interface[i] = nullptr;

    delete_hal_interface(software_hal_interface[i]);
    software_hal_interface[i] = nullptr;
    if (offloading_hal_interface[i] != nullptr) {
      delete_hal_interface(offloading_hal_interface[i]);
      offloading_hal_interface[i] = nullptr;
    }

    remote_delay[i] = 0;
  }

  if (com_android_bluetooth_flags_a2dp_sink_offload() &&
      decoder_offloading_hal_interface != nullptr) {
    delete_hal_interface(decoder_offloading_hal_interface);
    decoder_offloading_hal_interface = nullptr;
  }
}

// Set up the codec into BluetoothAudio HAL
bool setup_codec(const ahal_codec_configuration& config, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return false;
  }

  if (provider::supports_codec(config.codec_config.codec_type)) {
    // The codec is supported in the provider info (AIDL v4).
    // In this case, the codec is offloaded, and the configuration passed
    // as A2dpStreamConfiguration to the UpdateAudioConfig() interface
    // method.
    A2dpStreamConfiguration a2dp_stream_configuration;

    a2dp_stream_configuration.peerMtu = config.peer_mtu;
    a2dp_stream_configuration.codecId =
            provider_info[index]->GetCodec(config.codec_config.codec_type).value()->id;

    size_t parameters_start = 0;
    size_t parameters_end = 0;
    size_t codec_info_length = static_cast<size_t>(config.codec_specific_information_elements[0]);
    switch (config.codec_config.codec_type) {
      case BTAV_A2DP_CODEC_INDEX_SOURCE_SBC:
      case BTAV_A2DP_CODEC_INDEX_SOURCE_AAC:
        parameters_start = 3;
        parameters_end = 1 + codec_info_length;
        break;
      default:
        parameters_start = 9;
        parameters_end = 1 + codec_info_length;
        break;
    }

    a2dp_stream_configuration.configuration.insert(
            a2dp_stream_configuration.configuration.end(),
            config.codec_specific_information_elements + parameters_start,
            config.codec_specific_information_elements + parameters_end);

    if (!is_hal_offloading(index)) {
      log::warn("Switching BluetoothAudio HAL to Hardware");
      end_session(index);
      active_hal_interface[index] = offloading_hal_interface[index];
    }

    return active_hal_interface[index]->UpdateAudioConfig(AudioConfiguration(a2dp_stream_configuration));
  }

  // Fallback to legacy offloading path.
  AudioConfiguration audio_config{};
  CodecConfiguration codec_config{};
  PcmConfiguration pcm_config{};

  // Compute the codec configuration for the hardware encoding session and
  // check if the parameters are supported.
  if (getHalCodecConfiguration(config, &codec_config)) {
    if (!is_hal_offloading(index)) {
      log::info("Switching BluetoothAudio HAL to Hardware");
      end_session(index);
      active_hal_interface[index] = offloading_hal_interface[index];
    }

    audio_config.set<AudioConfiguration::a2dpConfig>(codec_config);
    return active_hal_interface[index]->UpdateAudioConfig(audio_config);
  }

  // Compute the PCM configuration for the software encoding session and
  // check if the parameters are supported.
  if (getHalPcmConfiguration(config, &pcm_config)) {
    if (is_hal_offloading(index)) {
      log::info("Switching BluetoothAudio HAL to Software");
      end_session(index);
      active_hal_interface[index] = software_hal_interface[index];
    }
    audio_config.set<AudioConfiguration::pcmConfig>(pcm_config);
    return active_hal_interface[index]->UpdateAudioConfig(audio_config);
  }

  log::error(
          "The codec configuration cannot be set for either"
          " software or hardware sessions:\n{}",
          config.ToString());
  return false;
}

void start_session(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  std::vector<LatencyMode> latency_modes = {LatencyMode::FREE};
  if (is_low_latency_mode_allowed) {
    latency_modes.push_back(LatencyMode::LOW_LATENCY);
  }
  active_hal_interface[index]->SetAllowedLatencyModes(latency_modes);
  active_hal_interface[index]->StartSession();
}

void end_session(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  active_hal_interface[index]->EndSession();
  active_hal_interface[index]->GetTransportInstance()->ResetPendingCmd();
  active_hal_interface[index]->GetTransportInstance()->ResetPresentationPosition();
}

void ack_stream_started(Status ack, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }

  if (com_android_bluetooth_flags_a2dp_clear_pending_status_before_binder_call()) {
    if (ack == Status::PENDING) {
      log::warn("ignoring PENDING status");
      return;
    }

    log::info("result={}", ack);

    auto transport = active_hal_interface[index]->GetTransportInstance();
    auto pending_cmd = transport->GetPendingCmd();
    if (pending_cmd == A2DP_CTRL_CMD_START) {
      // Clear the pending cmd state before reporting the status to the IBluetoothAudioProvider.
      // The BT audio HAL can invoke another command immediately after on the same thread and the
      // state would be incorrect.
      transport->ResetPendingCmd();
      active_hal_interface[index]->StreamStarted(ack);
    } else {
      log::warn("pending={} ignore result={}", pending_cmd, ack);
    }

  } else {
    log::info("result={}", ack);
    auto transport = active_hal_interface[index]->GetTransportInstance();
    auto pending_cmd = transport->GetPendingCmd();
    if (pending_cmd == A2DP_CTRL_CMD_START) {
      active_hal_interface[index]->StreamStarted(ack);
    } else {
      log::warn("pending={} ignore result={}", pending_cmd, ack);
      return;
    }
    if (ack != Status::PENDING) {
      transport->ResetPendingCmd();
    }
  }
}

// Executed from the BT main thread.
void ack_stream_suspended(Status ack, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }

  if (ack == Status::PENDING) {
    log::warn("ignoring PENDING status");
    return;
  }

  if (com_android_bluetooth_flags_a2dp_clear_pending_status_before_binder_call()) {
    log::info("result={}", ack);

    // The pending cmd state is set from one of the binder threads.
    auto transport = active_hal_interface[index]->GetTransportInstance();
    auto pending_cmd = transport->GetPendingCmd();
    if (pending_cmd == A2DP_CTRL_CMD_SUSPEND) {
      // Clear the pending cmd state before reporting the status to the IBluetoothAudioProvider.
      // The BT audio HAL can invoke another command immediately after on the same thread and the
      // state would be incorrect.
      transport->ResetPendingCmd();
      active_hal_interface[index]->StreamSuspended(ack);
    } else if (pending_cmd == A2DP_CTRL_CMD_STOP) {
      transport->ResetPendingCmd();
      log::info("A2DP_CTRL_CMD_STOP result={}", ack);
    } else {
      log::warn("pending={} ignore result={}", pending_cmd, ack);
    }

  } else {
    log::info("result={}", ack);
    auto transport = active_hal_interface[index]->GetTransportInstance();
    auto pending_cmd = transport->GetPendingCmd();
    if (pending_cmd == A2DP_CTRL_CMD_SUSPEND) {
      active_hal_interface[index]->StreamSuspended(ack);
    } else if (pending_cmd == A2DP_CTRL_CMD_STOP) {
      log::info("A2DP_CTRL_CMD_STOP result={}", ack);
    } else {
      log::warn("pending={} ignore result={}", pending_cmd, ack);
      return;
    }
    if (ack != Status::PENDING) {
      transport->ResetPendingCmd();
    }
  }
}

// Read from the FMQ of BluetoothAudio HAL
size_t read(uint8_t* p_buf, uint32_t len, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return 0;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return 0;
  }
  if (is_hal_offloading(index)) {
    log::error("session_type={} is not A2DP_SOFTWARE_ENCODING_DATAPATH",
               toString(active_hal_interface[index]->GetTransportInstance()->GetSessionType()));
    return 0;
  }
  return active_hal_interface[index]->ReadAudioData(p_buf, len);
}

// Clear the FMQ.
void flush_source(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  if (is_hal_offloading(index)) {
    log::error("session_type={} is not A2DP_SOFTWARE_ENCODING_DATAPATH",
               toString(active_hal_interface[index]->GetTransportInstance()->GetSessionType()));
    return;
  }
  active_hal_interface[index]->FlushAudioData();
}

// Update A2DP delay report to BluetoothAudio HAL
void set_remote_delay(uint16_t delay_report, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::info("not ready for DelayReport {} ms", static_cast<float>(delay_report / 10.0));
    remote_delay[index] = delay_report;
    return;
  }
  log::debug("DELAY {} ms", static_cast<float>(delay_report / 10.0));
  active_hal_interface[index]->GetTransportInstance()->SetRemoteDelay(delay_report);
}

// Set low latency buffer mode allowed or disallowed
void set_low_latency_mode_allowed(bool allowed, uint8_t index) {
  is_low_latency_mode_allowed = allowed;
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  std::vector<LatencyMode> latency_modes = {LatencyMode::FREE};
  if (is_low_latency_mode_allowed) {
    latency_modes.push_back(LatencyMode::LOW_LATENCY);
  }
  active_hal_interface[index]->SetAllowedLatencyModes(latency_modes);
}

/***
 * Lookup the codec info in the list of supported offloaded sink codecs.
 ***/
std::optional<btav_a2dp_codec_index_t> provider::sink_codec_index(const uint8_t* p_codec_info) {
  // Temporarily hardcoded to index 0 as offload is not used yet.
  // Remove hardcoding when dual A2DP offload is supported.
  // Same rationale applies to other hardcoded indices.
  return provider_info[0] ? provider_info[0]->SinkCodecIndex(p_codec_info) : std::nullopt;
}

/***
 * Lookup the codec info in the list of supported offloaded source codecs.
 ***/
std::optional<btav_a2dp_codec_index_t> provider::source_codec_index(const uint8_t* p_codec_info) {
  return provider_info[0] ? provider_info[0]->SourceCodecIndex(p_codec_info) : std::nullopt;
}

/***
 * Return the name of the codec which is assigned to the input index.
 * The codec index must be in the ranges
 * BTAV_A2DP_CODEC_INDEX_SINK_EXT_MIN..BTAV_A2DP_CODEC_INDEX_SINK_EXT_MAX or
 * BTAV_A2DP_CODEC_INDEX_SOURCE_EXT_MIN..BTAV_A2DP_CODEC_INDEX_SOURCE_EXT_MAX.
 * Returns nullopt if the codec_index is not assigned or codec extensibility
 * is not supported or enabled.
 ***/
std::optional<const char*> provider::codec_index_str(btav_a2dp_codec_index_t codec_index) {
  return provider_info[0] ? provider_info[0]->CodecIndexStr(codec_index) : std::nullopt;
}

/***
 * Return true if the codec is supported for the session type
 * A2DP_HARDWARE_ENCODING_DATAPATH or A2DP_HARDWARE_DECODING_DATAPATH.
 ***/
bool provider::supports_codec(btav_a2dp_codec_index_t codec_index) {
  return provider_info[0] ? provider_info[0]->SupportsCodec(codec_index) : false;
}

/***
 * Return the A2DP capabilities for the selected codec.
 ***/
bool provider::codec_info(btav_a2dp_codec_index_t codec_index, bluetooth::a2dp::CodecId* codec_id,
                          uint8_t* codec_info, btav_a2dp_codec_config_t* codec_config) {
  return provider_info[0]
                 ? provider_info[0]->CodecCapabilities(codec_index, codec_id, codec_info, codec_config)
                 : false;
}

/***
 * Query the codec selection fromt the audio HAL.
 * The HAL is expected to pick the best audio configuration based on the
 * discovered remote SEPs.
 ***/
std::optional<::bluetooth::audio::a2dp::provider::a2dp_configuration>
provider::get_a2dp_configuration(
        RawAddress peer_address,
        std::vector<::bluetooth::audio::a2dp::provider::a2dp_remote_capabilities> const&
                remote_seps,
        btav_a2dp_codec_config_t const& user_preferences,
        std::optional<::bluetooth::a2dp::CodecId> user_preferred_codec_id, bool is_source,
        uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return std::nullopt;
  }
  if (provider_info[index] == nullptr) {
    return std::nullopt;
  }

  using ::aidl::android::hardware::bluetooth::audio::A2dpRemoteCapabilities;
  using ::aidl::android::hardware::bluetooth::audio::CodecId;

  BluetoothAudioClientInterface* hal_interface_to_use = nullptr;

  if (com_android_bluetooth_flags_a2dp_sink_offload()) {
    if (is_source) {
      hal_interface_to_use = offloading_hal_interface[index];
      if (hal_interface_to_use == nullptr) {
        log::error("the offloading HAL interface is not opened");
        return std::nullopt;
      }
    } else {
      hal_interface_to_use = decoder_offloading_hal_interface;
      if (hal_interface_to_use == nullptr) {
        log::error("the decoder offloading HAL interface is not opened");
        return std::nullopt;
      }
    }
  } else {
    if (offloading_hal_interface[index] == nullptr) {
      log::error("the offloading HAL interface is not opened");
      return std::nullopt;
    }
  }

  // Convert the remote audio capabilities to the exchange format used
  // by the HAL.
  std::vector<A2dpRemoteCapabilities> a2dp_remote_capabilities;
  for (auto const& sep : remote_seps) {
    size_t capabilities_start = 0;
    size_t capabilities_end = 0;
    CodecId id;
    switch (sep.capabilities[2]) {
      case A2DP_MEDIA_CT_SBC:
      case A2DP_MEDIA_CT_AAC: {
        id = CodecId::make<CodecId::a2dp>(static_cast<CodecId::A2dp>(sep.capabilities[2]));
        capabilities_start = 3;
        capabilities_end = 1 + sep.capabilities[0];
        break;
      }
      case A2DP_MEDIA_CT_NON_A2DP: {
        uint32_t vendor_id = (static_cast<uint32_t>(sep.capabilities[3]) << 0) |
                             (static_cast<uint32_t>(sep.capabilities[4]) << 8) |
                             (static_cast<uint32_t>(sep.capabilities[5]) << 16) |
                             (static_cast<uint32_t>(sep.capabilities[6]) << 24);
        uint16_t codec_id = (static_cast<uint16_t>(sep.capabilities[7]) << 0) |
                            (static_cast<uint16_t>(sep.capabilities[8]) << 8);
        id = CodecId::make<CodecId::vendor>(
                CodecId::Vendor({.id = (int32_t)vendor_id, .codecId = codec_id}));
        capabilities_start = 9;
        capabilities_end = 1 + sep.capabilities[0];
        break;
      }
      default:
        continue;
    }
    A2dpRemoteCapabilities& capabilities = a2dp_remote_capabilities.emplace_back();
    capabilities.seid = sep.seid;
    capabilities.id = id;
    capabilities.capabilities.insert(capabilities.capabilities.end(),
                                     sep.capabilities + capabilities_start,
                                     sep.capabilities + capabilities_end);
  }

  // Convert the user preferences into a configuration hint.
  A2dpConfigurationHint hint;
  hint.bdAddr = peer_address.address;
  auto& codecParameters = hint.codecParameters.emplace();
  switch (user_preferences.channel_mode) {
    case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
      codecParameters.channelMode = ChannelMode::MONO;
      break;
    case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
      codecParameters.channelMode = ChannelMode::STEREO;
      break;
    default:
      break;
  }
  switch (user_preferences.sample_rate) {
    case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
      codecParameters.samplingFrequencyHz = 44100;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
      codecParameters.samplingFrequencyHz = 48000;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_88200:
      codecParameters.samplingFrequencyHz = 88200;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_96000:
      codecParameters.samplingFrequencyHz = 96000;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_176400:
      codecParameters.samplingFrequencyHz = 176400;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_192000:
      codecParameters.samplingFrequencyHz = 192000;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_16000:
      codecParameters.samplingFrequencyHz = 16000;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_24000:
      codecParameters.samplingFrequencyHz = 24000;
      break;
    default:
      break;
  }
  switch (user_preferences.bits_per_sample) {
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
      codecParameters.bitdepth = 16;
      break;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
      codecParameters.bitdepth = 24;
      break;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
      codecParameters.bitdepth = 32;
      break;
    default:
      break;
  }
  switch (user_preferences.audio_context) {
    case BTAV_A2DP_CODEC_AUDIO_CONTEXT_MEDIA:
      hint.audioContext.bitmask = AudioContext::MEDIA;
      break;
    case BTAV_A2DP_CODEC_AUDIO_CONTEXT_GAME:
      hint.audioContext.bitmask = AudioContext::GAME;
      break;
    default:
      hint.audioContext.bitmask = AudioContext::UNSPECIFIED;
      break;
  }
  hint.codecId = user_preferred_codec_id.has_value()
                         ? convertCodecId(user_preferred_codec_id.value())
                         : std::nullopt;
  log::info("local: {}, remote capabilities:", is_source ? "source" : "sink");
  for (auto const& sep : a2dp_remote_capabilities) {
    log::info("- {}", sep.toString());
  }
  log::info("hint: {}", hint.toString());

  // Invoke the HAL GetAdpCapabilities method with the
  // remote capabilities.
  std::optional<A2dpConfiguration> result = std::nullopt;
  if (com_android_bluetooth_flags_a2dp_sink_offload()) {
    result = hal_interface_to_use->GetA2dpConfiguration(a2dp_remote_capabilities, hint);
  } else {
    result = offloading_hal_interface[index]->GetA2dpConfiguration(a2dp_remote_capabilities, hint);
  }
  // Convert the result configuration back to the stack's format.
  if (!result.has_value()) {
    log::info("provider cannot resolve the a2dp configuration");
    return std::nullopt;
  }

  log::info("provider selected {}", result->toString());
  auto a2dp_configuration = convertA2dpConfiguration(result.value());
  a2dp_configuration.codec_parameters.codec_type =
          is_source ? provider_info[index]->SourceCodecIndex(result->id).value()
                    : provider_info[index]->SinkCodecIndex(result->id).value();
  return std::make_optional(a2dp_configuration);
}

/***
 * Query the codec parameters from the audio HAL.
 * The HAL is expected to parse the codec configuration
 * received from the peer and decide whether accept
 * the it or not.
 ***/
tA2DP_STATUS provider::parse_a2dp_configuration(::bluetooth::a2dp::CodecId codec_id,
                                                const uint8_t* codec_info,
                                                btav_a2dp_codec_config_t* codec_parameters,
                                                std::vector<uint8_t>* vendor_specific_parameters) {
  std::vector<uint8_t> configuration;
  CodecParameters codec_parameters_aidl;

  auto aidl_codec_id = convertCodecId(codec_id);
  log::assert_that(aidl_codec_id.has_value(), "convertCodecId failed");

  std::copy(codec_info, codec_info + AVDT_CODEC_SIZE, std::back_inserter(configuration));

  // Temporarily hardcoded to index 0 as offload is not used yet.
  // Remove hardcoding when dual A2DP offload is supported.
  if (offloading_hal_interface[0] == nullptr) {
    log::error("the offloading HAL interface is not opened");
    return A2DP_FAIL;
  }

  auto a2dp_status = offloading_hal_interface[0]->ParseA2dpConfiguration(
          aidl_codec_id.value(), configuration, &codec_parameters_aidl);

  if (!a2dp_status.has_value()) {
    log::error("provider failed to parse configuration");
    return A2DP_FAIL;
  }

  if (codec_parameters != nullptr) {
    convertCodecParameters(codec_parameters_aidl, codec_parameters);
  }

  if (vendor_specific_parameters != nullptr) {
    *vendor_specific_parameters = codec_parameters_aidl.vendorSpecificParameters;
  }

  return static_cast<tA2DP_STATUS>(a2dp_status.value());
}

/***
 * Reads the provider information from the HAL.
 * May return std::nullopt if the HAL Provider Info is empty.
 ***/
std::optional<btav_a2dp_hal_provider_info_t> get_provider_info() {
  // The provider info is a global property of the audio HAL and is not
  // peer specific: query it through the default adapter's interface.
  BluetoothAudioClientInterface* hal_interface = offloading_hal_interface[0];
  if (hal_interface == nullptr) {
    log::warn("a2dp hw offload interface is not available");
    return std::nullopt;
  }

  auto source_provider_info = hal_interface->GetProviderInfo(
          SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH, nullptr);

  auto sink_provider_info = hal_interface->GetProviderInfo(
          SessionType::A2DP_HARDWARE_OFFLOAD_DECODING_DATAPATH, nullptr);

  if (!source_provider_info.has_value() && !sink_provider_info.has_value()) {
    log::warn("the provider info is empty");
    return std::nullopt;
  }

  btav_a2dp_hal_provider_info_t codecs_info;

  if (source_provider_info.has_value()) {
    for (auto& codec_info : source_provider_info->codecInfos) {
      auto source_codec = convertCodecInfo(codec_info);
      if (source_codec.has_value()) {
        log::debug("provider source codec: {}", source_codec.value().ToString());
        codecs_info.source_codecs.push_back(source_codec.value());
      }
    }
  }

  if (sink_provider_info.has_value()) {
    for (auto& codec_info : sink_provider_info->codecInfos) {
      auto sink_codec = convertCodecInfo(codec_info);
      if (sink_codec.has_value()) {
        log::debug("provider sink codec: {}", sink_codec.value().ToString());
        codecs_info.sink_codecs.push_back(sink_codec.value());
      }
    }
  }

  log::info("successfully loaded provider info");
  return std::make_optional<btav_a2dp_hal_provider_info_t>(codecs_info);
}

}  // namespace a2dp
}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
