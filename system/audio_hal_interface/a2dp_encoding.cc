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
 */

#include "a2dp_encoding.h"

#include <vector>

#include "aidl/a2dp/a2dp_encoding_aidl.h"
#include "hal_version_manager.h"
#include "hidl/a2dp_encoding_hidl.h"
#include "qti_hidl/a2dp_encoding_qti.h"
#include "btif/include/btif_av.h"
#include "btif/include/btif_av_co.h"
#include "osi/include/properties.h"

namespace bluetooth {
namespace audio {
namespace a2dp {

bool update_codec_offloading_capabilities(
        const std::vector<btav_a2dp_codec_config_t>& framework_preference,
        bool supports_a2dp_hw_offload_v2) {
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    return hidl::a2dp::update_codec_offloading_capabilities(framework_preference);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    return aidl::a2dp::update_codec_offloading_capabilities(framework_preference, false);
  }
  return aidl::a2dp::update_codec_offloading_capabilities(framework_preference,
                                                          supports_a2dp_hw_offload_v2);
}

// Check if new bluetooth_audio is enabled
bool is_hal_enabled() {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    return hidl::a2dp::is_hal_2_0_enabled();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    return aidl::a2dp::is_hal_enabled();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl is_hal_enabled";
    return qti_hidl::a2dp::is_hal_2_0_enabled();
  }
  return false;
}

bool is_aidl_enabled(){
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() ==
     BluetoothAudioHalTransport::AIDL) {
     return aidl::a2dp::is_hal_enabled();
  }
  return false;
}

bool is_offload_session_unknown() {
  LOG(INFO) << __func__;
 if (HalVersionManager::GetHalTransport() ==
      BluetoothAudioHalTransport::HIDL) {
    return hidl::a2dp::is_hal_2_0_offloading_session_unknown();
  } else if (HalVersionManager::GetHalTransport() ==
      BluetoothAudioHalTransport::AIDL) {
    return aidl::a2dp::is_hal_2_0_offloading_session_unknown();
  } else if (HalVersionManager::GetHalTransport() ==
      BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__<< ": qti_hidl is_hal_2_0_offloading_session_unknown";
    return qti_hidl::a2dp::is_hal_2_0_offloading_session_unknown();
  }
  return false;
}

// Check if new bluetooth_audio is running with offloading encoders
bool is_hal_offloading() {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    return hidl::a2dp::is_hal_2_0_offloading();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    return aidl::a2dp::is_hal_offloading();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl is_hal_offloading";
    return qti_hidl::a2dp::is_hal_2_0_offloading();
  }
  return false;
}

// Initialize BluetoothAudio HAL: openProvider
bool init(bluetooth::common::MessageLoopThread* message_loop,
          bluetooth::audio::a2dp::StreamCallbacks const* stream_callbacks, bool offload_enabled) {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    return hidl::a2dp::init(message_loop, stream_callbacks, offload_enabled);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    return aidl::a2dp::init(message_loop, stream_callbacks, offload_enabled);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl init";
    return qti_hidl::a2dp::init(message_loop);
  }
  return false;
}

// Clean up BluetoothAudio HAL
void cleanup() {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    hidl::a2dp::cleanup();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    aidl::a2dp::cleanup();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl cleanup";
    qti_hidl::a2dp::cleanup();
  }
  return;
}

// Set up the codec into BluetoothAudio HAL
bool setup_codec(A2dpCodecConfig* a2dp_config, uint16_t peer_mtu,
                 int preferred_encoding_interval_us) {
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    return hidl::a2dp::setup_codec(a2dp_config, peer_mtu, preferred_encoding_interval_us);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    return aidl::a2dp::setup_codec(a2dp_config, peer_mtu, preferred_encoding_interval_us);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl setup_codec";
    return qti_hidl::a2dp::setup_codec();
  }
  return false;
}

// Send command to the BluetoothAudio HAL: StartSession, EndSession,
// StreamStarted, StreamSuspended
void start_session() {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    hidl::a2dp::start_session();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    aidl::a2dp::start_session();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl start_session";
    qti_hidl::a2dp::start_session();
  }
  return;
}

void end_session() {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    hidl::a2dp::end_session();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    aidl::a2dp::end_session();
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl end_session";
    qti_hidl::a2dp::end_session();
  }
  return;
}

void ack_stream_started(Status status) {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    hidl::a2dp::ack_stream_started(status);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    aidl::a2dp::ack_stream_started(status);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl ack_stream_started";
    qti_hidl::a2dp::ack_stream_started(status);
  }
  return;
}

void ack_stream_suspended(Status status) {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    hidl::a2dp::ack_stream_suspended(status);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    aidl::a2dp::ack_stream_suspended(status);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl ack_stream_suspended";
    qti_hidl::a2dp::ack_stream_suspended(status);
  }
  return;
}

// Read from the FMQ of BluetoothAudio HAL
size_t read(uint8_t* p_buf, uint32_t len) {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    return hidl::a2dp::read(p_buf, len);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    return aidl::a2dp::read(p_buf, len);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl read";
    return qti_hidl::a2dp::read(p_buf, len);
  }
  return 0;
}
static uint16_t a2dp_get_peer_mtu(A2dpCodecConfig* a2dp_config) {
   uint8_t codec_info[AVDT_CODEC_SIZE];
   a2dp_config->copyOutOtaCodecConfig(codec_info);

   RawAddress peer_addr = btif_av_source_active_peer();
   tA2DP_ENCODER_INIT_PEER_PARAMS peer_params;
   bta_av_co_get_peer_params(peer_addr, &peer_params);
   uint16_t peer_mtu = peer_params.peer_mtu;
   uint16_t effective_mtu = bta_av_co_get_encoder_effective_frame_size(peer_addr);

   if (effective_mtu > 0 && effective_mtu < peer_mtu) {
     peer_mtu = effective_mtu;
   }
   // b/188020925
   // When SBC headsets report middle quality bitpool under a larger MTU, we
   // reduce the packet size to prevent the hardware encoder from putting too
   // many frames in one packet.
   if (a2dp_config->codecIndex() == BTAV_A2DP_CODEC_INDEX_SOURCE_SBC &&
       codec_info[2] /* maxBitpool */ <= A2DP_SBC_BITPOOL_MIDDLE_QUALITY) {
     peer_mtu = MAX_2MBPS_AVDTP_MTU;
   }
   // b/177205770
   // Fix the MTU value not to be greater than an AVDTP packet, so the data
   // encoded by A2DP hardware encoder can be fitted into one AVDTP packet
   // without fragmented
   if (peer_mtu > MAX_3MBPS_AVDTP_MTU) {
     peer_mtu = MAX_3MBPS_AVDTP_MTU;
   }
   return peer_mtu;
 }
// Update A2DP delay report to BluetoothAudio HAL
void set_remote_delay(uint16_t delay_report) {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::HIDL) {
    hidl::a2dp::set_remote_delay(delay_report);
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    aidl::a2dp::set_remote_delay(delay_report);
    setup_codec(bta_av_get_a2dp_current_codec(),
                a2dp_get_peer_mtu(bta_av_get_a2dp_current_codec()),
                bta_av_co_get_encoder_preferred_interval_us());
  } else if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::QTI_HIDL) {
    LOG(INFO) << __func__ << ": qti_hidl set_remote_delay";
    qti_hidl::a2dp::set_remote_delay(delay_report);
  }
  return;
}

// Set low latency buffer mode allowed or disallowed
void set_audio_low_latency_mode_allowed(bool allowed) {
  LOG(INFO) << __func__;
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    aidl::a2dp::set_low_latency_mode_allowed(allowed);
  }
}

// Check if OPUS codec is supported
bool is_opus_supported() {
  // OPUS codec was added after HIDL HAL was frozen
  if (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL) {
    return true;
  }
  return false;
}

namespace provider {

// Lookup the codec info in the list of supported offloaded sink codecs.
std::optional<btav_a2dp_codec_index_t> sink_codec_index(const uint8_t* p_codec_info) {
  return (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL)
                 ? aidl::a2dp::provider::sink_codec_index(p_codec_info)
                 : std::nullopt;
}

// Lookup the codec info in the list of supported offloaded source codecs.
std::optional<btav_a2dp_codec_index_t> source_codec_index(const uint8_t* p_codec_info) {
  return (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL)
                 ? aidl::a2dp::provider::source_codec_index(p_codec_info)
                 : std::nullopt;
}

// Return the name of the codec which is assigned to the input index.
// The codec index must be in the ranges
// BTAV_A2DP_CODEC_INDEX_SINK_EXT_MIN..BTAV_A2DP_CODEC_INDEX_SINK_EXT_MAX or
// BTAV_A2DP_CODEC_INDEX_SOURCE_EXT_MIN..BTAV_A2DP_CODEC_INDEX_SOURCE_EXT_MAX.
// Returns nullopt if the codec_index is not assigned or codec extensibility
// is not supported or enabled.
std::optional<const char*> codec_index_str(btav_a2dp_codec_index_t codec_index) {
  return (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL)
                 ? aidl::a2dp::provider::codec_index_str(codec_index)
                 : std::nullopt;
}

// Return true if the codec is supported for the session type
// A2DP_HARDWARE_ENCODING_DATAPATH or A2DP_HARDWARE_DECODING_DATAPATH.
bool supports_codec(btav_a2dp_codec_index_t codec_index) {
  LOG(INFO) << __func__ << ": codec_index: " << codec_index;
  return (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL)
                 ? aidl::a2dp::provider::supports_codec(codec_index)
                 : false;
}

// Return the A2DP capabilities for the selected codec.
bool codec_info(btav_a2dp_codec_index_t codec_index, bluetooth::a2dp::CodecId* codec_id,
                uint8_t* codec_info, btav_a2dp_codec_config_t* codec_config) {
  return (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL)
                 ? aidl::a2dp::provider::codec_info(codec_index, codec_id, codec_info, codec_config)
                 : false;
}

// Query the codec selection fromt the audio HAL.
// The HAL is expected to pick the best audio configuration based on the
// discovered remote SEPs.
std::optional<a2dp_configuration> get_a2dp_configuration(
        RawAddress peer_address, std::vector<a2dp_remote_capabilities> const& remote_seps,
        btav_a2dp_codec_config_t const& user_preferences) {
  return (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL)
                 ? aidl::a2dp::provider::get_a2dp_configuration(peer_address, remote_seps,
                                                                user_preferences)
                 : std::nullopt;
}

// Query the codec parameters from the audio HAL.
// The HAL performs a two part validation:
//  - check if the configuration is valid
//  - check if the configuration is supported by the audio provider
// In case any of these checks fails, the corresponding A2DP
// status is returned. If the configuration is valid and supported,
// A2DP_OK is returned.
tA2DP_STATUS parse_a2dp_configuration(btav_a2dp_codec_index_t codec_index,
                                      const uint8_t* codec_info,
                                      btav_a2dp_codec_config_t* codec_parameters,
                                      std::vector<uint8_t>* vendor_specific_parameters) {
  return (HalVersionManager::GetHalTransport() == BluetoothAudioHalTransport::AIDL)
                 ? aidl::a2dp::provider::parse_a2dp_configuration(
                           codec_index, codec_info, codec_parameters, vendor_specific_parameters)
                 : A2DP_FAIL;
}

}  // namespace provider
}  // namespace a2dp
}  // namespace audio
}  // namespace bluetooth
