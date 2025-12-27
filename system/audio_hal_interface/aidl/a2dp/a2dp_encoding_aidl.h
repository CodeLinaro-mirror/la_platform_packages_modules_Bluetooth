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

#pragma once

#include <bluetooth/types/address.h>

#include <vector>

#include "a2dp_constants.h"
#include "a2dp_encoding.h"
#include "a2dp_sbc_constants.h"
#include "common/message_loop_thread.h"
#include "hardware/bt_av.h"
#include "osi/include/properties.h"

namespace bluetooth {
namespace audio {
namespace aidl {
namespace a2dp {

bool is_index_valid(uint8_t index);

bool update_codec_offloading_capabilities(
        const std::vector<btav_a2dp_codec_config_t>& framework_preference,
        bool supports_a2dp_hw_offload_v2, uint8_t index);

/***
 * Check if new bluetooth_audio is enabled
 ***/
bool is_hal_enabled(uint8_t index);

/***
 * Check if new bluetooth_audio is running with offloading encoders
 ***/
bool is_hal_offloading(uint8_t index);

/***
 * Initialize BluetoothAudio HAL: openProvider
 ***/
bool init(bluetooth::common::MessageLoopThread* message_loop,
          bluetooth::audio::a2dp::StreamCallbacks const* stream_callbacks, bool offload_enabled,
          uint8_t index);

/***
 * Clean up BluetoothAudio HAL
 ***/
void cleanup();

/***
 * Clean up BluetoothAudio HAL
 ***/
void cleanup(uint8_t index);

/***
 * Set up the codec into BluetoothAudio HAL
 ***/
bool setup_codec(const ::bluetooth::audio::a2dp::ahal_codec_configuration& config, uint8_t index);

/***
 * Send command to the BluetoothAudio HAL: StartSession, EndSession,
 * StreamStarted, StreamSuspended
 ***/
void start_session(uint8_t index);
void end_session(uint8_t index);
void ack_stream_started(::bluetooth::audio::a2dp::Status status, uint8_t index);
void ack_stream_suspended(::bluetooth::audio::a2dp::Status status, uint8_t index);

/***
 * Read from the FMQ of BluetoothAudio HAL
 ***/
size_t read(uint8_t* p_buf, uint32_t len, uint8_t index);

/***
 * Update A2DP delay report to BluetoothAudio HAL
 ***/
void set_remote_delay(uint16_t delay_report, uint8_t index);

/***
 * Set low latency buffer mode allowed or disallowed
 ***/
void set_low_latency_mode_allowed(bool allowed, uint8_t index);

namespace provider {

/***
 * Lookup the codec info in the list of supported offloaded sink codecs.
 * Should not be called before update_codec_offloading_capabilities.
 ***/
std::optional<btav_a2dp_codec_index_t> sink_codec_index(const uint8_t* p_codec_info);

/***
 * Lookup the codec info in the list of supported offloaded source codecs.
 * Should not be called before update_codec_offloading_capabilities.
 ***/
std::optional<btav_a2dp_codec_index_t> source_codec_index(const uint8_t* p_codec_info);

/***
 * Return the name of the codec which is assigned to the input index.
 * The codec index must be in the ranges
 * BTAV_A2DP_CODEC_INDEX_SINK_EXT_MIN..BTAV_A2DP_CODEC_INDEX_SINK_EXT_MAX or
 * BTAV_A2DP_CODEC_INDEX_SOURCE_EXT_MIN..BTAV_A2DP_CODEC_INDEX_SOURCE_EXT_MAX.
 * Returns nullopt if the codec_index is not assigned or codec extensibility
 * is not supported or enabled.
 * Should not be called before update_codec_offloading_capabilities.
 ***/
std::optional<const char*> codec_index_str(btav_a2dp_codec_index_t codec_index);

/***
 * Return true if the codec is supported for the session type
 * A2DP_HARDWARE_ENCODING_DATAPATH or A2DP_HARDWARE_DECODING_DATAPATH.
 ***/
bool supports_codec(btav_a2dp_codec_index_t codec_index);

/***
 * Return the A2DP capabilities for the selected codec.
 ***/
bool codec_info(btav_a2dp_codec_index_t codec_index, bluetooth::a2dp::CodecId* codec_id,
                uint8_t* codec_info, btav_a2dp_codec_config_t* codec_config);

/***
 * Query the codec selection fromt the audio HAL.
 * The HAL is expected to pick the best audio configuration based on the
 * discovered remote SEPs.
 ***/
std::optional<::bluetooth::audio::a2dp::provider::a2dp_configuration> get_a2dp_configuration(
        RawAddress peer_address,
        std::vector<::bluetooth::audio::a2dp::provider::a2dp_remote_capabilities> const&
                remote_seps,
        btav_a2dp_codec_config_t const& user_preferences,
        ::bluetooth::a2dp::CodecId user_preferred_codec_id,
        uint8_t index);

/***
 * Query the codec parameters from the audio HAL.
 * The HAL is expected to parse the codec configuration
 * received from the peer and decide whether accept
 * the it or not.
 ***/
tA2DP_STATUS parse_a2dp_configuration(::bluetooth::a2dp::CodecId codec_id,
                                      const uint8_t* codec_info,
                                      btav_a2dp_codec_config_t* codec_parameters,
                                      std::vector<uint8_t>* vendor_specific_parameters);

}  // namespace provider

/***
 * Reads the provider information from the HAL.
 * May return std::nullopt if the HAL Provider Info is empty.
 ***/
std::optional<btav_a2dp_hal_provider_info_t> get_provider_info();

}  // namespace a2dp
}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
