/*
 * Copyright 2016 The Android Open Source Project
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
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

//
// Interface to the A2DP AAC Encoder
//

#ifndef A2DP_AAC_ENCODER_H
#define A2DP_AAC_ENCODER_H

#include <aacenc_lib.h>
#include "a2dp_aac_constants.h"
#include "a2dp_codec_api.h"

// A2DP AAC encoder interval in milliseconds
#define A2DP_AAC_ENCODER_INTERVAL_MS 20

// Is used in btav_a2dp_codec_config_t.codec_specific_1 when codec is AAC
enum class AacEncoderBitrateMode : int64_t {
  // Variable bitrate mode unsupported when used in a codec report, and upper
  // layer can use this value as system default (keep current settings)
  AACENC_BR_MODE_CBR = A2DP_AAC_VARIABLE_BIT_RATE_DISABLED,
  // Constant bitrate mode when Variable bitrate mode is supported. This can
  // also be used to disable Variable bitrate mode by upper layer
  AACENC_BR_MODE_VBR_C = (A2DP_AAC_VARIABLE_BIT_RATE_ENABLED | 0x00),
  // Variable bitrate mode (very low bitrate for software encoding).
  AACENC_BR_MODE_VBR_1 = (A2DP_AAC_VARIABLE_BIT_RATE_ENABLED | 0x01),
  // Variable bitrate mode (low bitrate for software encoding).
  AACENC_BR_MODE_VBR_2 = (A2DP_AAC_VARIABLE_BIT_RATE_ENABLED | 0x02),
  // Variable bitrate mode (medium bitrate for software encoding).
  AACENC_BR_MODE_VBR_3 = (A2DP_AAC_VARIABLE_BIT_RATE_ENABLED | 0x03),
  // Variable bitrate mode (high bitrate for software encoding).
  AACENC_BR_MODE_VBR_4 = (A2DP_AAC_VARIABLE_BIT_RATE_ENABLED | 0x04),
  // Variable bitrate mode (very high bitrate for software encoding).
  AACENC_BR_MODE_VBR_5 = (A2DP_AAC_VARIABLE_BIT_RATE_ENABLED | 0x05),
};

typedef struct {
  uint32_t sample_rate;
  uint8_t channel_mode;
  uint8_t bits_per_sample;
  uint32_t frame_length;         // Samples per channel in a frame
  uint8_t input_channels_n;      // Number of channels
  int max_encoded_buffer_bytes;  // Max encoded bytes per frame
} tA2DP_AAC_ENCODER_PARAMS;

typedef struct {
  float counter;
  uint32_t bytes_per_tick; /* pcm bytes read each media task tick */
  uint64_t last_frame_us;
} tA2DP_AAC_FEEDING_STATE;

typedef struct {
  uint64_t session_start_us;

  size_t media_read_total_expected_packets;
  size_t media_read_total_expected_reads_count;
  size_t media_read_total_expected_read_bytes;

  size_t media_read_total_dropped_packets;
  size_t media_read_total_actual_reads_count;
  size_t media_read_total_actual_read_bytes;
} a2dp_aac_encoder_stats_t;

typedef struct {
  a2dp_source_read_callback_t read_callback;
  a2dp_source_enqueue_callback_t enqueue_callback;
  uint16_t TxAaMtuSize;

  bool use_SCMS_T;
  tA2DP_ENCODER_INIT_PEER_PARAMS peer_params;
  uint32_t timestamp;        // Timestamp for the A2DP frames

  HANDLE_AACENCODER aac_handle;
  bool has_aac_handle;  // True if aac_handle is valid

  tA2DP_FEEDING_PARAMS feeding_params;
  tA2DP_AAC_ENCODER_PARAMS aac_encoder_params;
  tA2DP_AAC_FEEDING_STATE aac_feeding_state;

  a2dp_aac_encoder_stats_t stats;
} tA2DP_AAC_ENCODER_CB;

// Loads the A2DP AAC encoder.
// Return true on success, otherwise false.
bool A2DP_LoadEncoderAac(void);

class A2dpAacEncoder:public A2dpEncoderInterface {
public:
  A2dpAacEncoder(const RawAddress& peer_address) {
    peer_address_ = peer_address;
    memset(&a2dp_aac_encoder_cb, 0, sizeof(a2dp_aac_encoder_cb));
  };

  ~A2dpAacEncoder() {
    encoder_cleanup();
  };

  // Unloads the A2DP AAC encoder.
  void A2DP_UnloadEncoderAac(void);

  // Initialize the A2DP AAC encoder.
  // |p_peer_params| contains the A2DP peer information
  // The current A2DP codec config is in |a2dp_codec_config|.
  // |read_callback| is the callback for reading the input audio data.
  // |enqueue_callback| is the callback for enqueueing the encoded audio data.
  void encoder_init(tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
                    A2dpCodecConfig* a2dp_codec_config,
                    a2dp_source_read_callback_t read_callback,
                    a2dp_source_enqueue_callback_t enqueue_callback);

  // Cleanup the A2DP AAC encoder.
  void encoder_cleanup(void);

  // Reset the feeding for the A2DP AAC encoder.
  void feeding_flush(void);

  // Reset the feeding for the A2DP SBC encoder.
  void feeding_reset(void);

  // Get the A2DP AAC encoder interval (in milliseconds).
  uint64_t get_encoder_interval_ms(void);

  // Get the A2DP AAC encoded maximum frame size
  int get_effective_frame_size();

  // Prepare and send A2DP AAC encoded frames.
  // |timestamp_us| is the current timestamp (in microseconds).
  void send_frames(uint64_t timestamp_us);

  void a2dp_aac_encoder_update(A2dpCodecConfig* a2dp_codec_config,
                               bool* p_restart_input,
                               bool* p_restart_output,
                               bool* p_config_updated);

  tA2DP_AAC_ENCODER_CB a2dp_aac_encoder_cb;

private:
  void a2dp_aac_get_num_frame_iteration(uint8_t* num_of_iterations,
                                        uint8_t* num_of_frames,
                                        uint64_t timestamp_us);
  void a2dp_aac_encode_frames(uint8_t nb_frame);
  bool a2dp_aac_read_feeding(uint8_t* read_buffer, uint32_t* bytes_read);
  uint16_t adjust_effective_mtu(const tA2DP_ENCODER_INIT_PEER_PARAMS& peer_params);

  uint32_t a2dp_aac_encoder_interval_ms = A2DP_AAC_ENCODER_INTERVAL_MS;

};
#endif  // A2DP_AAC_ENCODER_H
