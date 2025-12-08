/******************************************************************************
 *
 *  Copyright 2016 The Android Open Source Project
 *  Copyright 2009-2012 Broadcom Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear.
 *
 ******************************************************************************/

//
// Interface to the A2DP SBC Encoder
//

#ifndef A2DP_SBC_ENCODER_H
#define A2DP_SBC_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#include "a2dp_codec_api.h"
#include "embdrv/sbc/encoder/include/sbc_encoder.h"
#include "a2dp_sbc_up_sample.h"

typedef struct {
  uint32_t aa_frame_counter;
  int32_t aa_feed_counter;
  int32_t aa_feed_residue;
  float counter;
  uint32_t bytes_per_tick; /* pcm bytes read each media task tick */
  uint64_t last_frame_us;
} tA2DP_SBC_FEEDING_STATE;

typedef struct {
  uint64_t session_start_us;
  size_t media_read_total_expected_packets;
  size_t media_read_total_expected_reads_count;
  size_t media_read_total_expected_read_bytes;
  size_t media_read_total_dropped_packets;
  size_t media_read_total_actual_reads_count;
  size_t media_read_total_actual_read_bytes;
  size_t media_read_total_expected_frames;
  size_t media_read_total_dropped_frames;
} a2dp_sbc_encoder_stats_t;

class A2dpSbcEncoder:public A2dpEncoderInterface {
public:
  A2dpSbcEncoder(const RawAddress& peer_address) {
    peer_address_ = peer_address;
  };

  ~A2dpSbcEncoder() {
    encoder_cleanup();
  };

  // Loads the A2DP SBC encoder.
  // Return true on success, otherwise false.
  // Nothing to do - the library is statically linked
  static bool A2DP_LoadEncoderSbc(void) { return true; };

  // Unloads the A2DP SBC encoder.
  // Nothing to do - the library is statically linked
  static void A2DP_UnloadEncoderSbc(void) { };

  // Initialize the A2DP SBC encoder.
  // |p_peer_params| contains the A2DP peer information
  // The current A2DP codec config is in |a2dp_codec_config|.
  // |read_callback| is the callback for reading the input audio data.
  // |enqueue_callback| is the callback for enqueueing the encoded audio data.
  void encoder_init(tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
                             A2dpCodecConfig* a2dp_codec_config,
                             a2dp_source_read_callback_t read_callback,
                             a2dp_source_enqueue_callback_t enqueue_callback);

  // Cleanup the A2DP SBC encoder.
  void encoder_cleanup(void);

  // Reset the feeding for the A2DP SBC encoder.
  void feeding_reset(void);

  // Flush the feeding for the A2DP SBC encoder.
  void feeding_flush(void);

  // Get the A2DP SBC encoder interval (in milliseconds).
  uint64_t get_encoder_interval_ms(void);

  // Get the A2DP encoded maximum frame size (similar to MTU).
  int get_effective_frame_size(void);

  // Prepare and send A2DP SBC encoded frames.
  // |timestamp_us| is the current timestamp (in microseconds).
  void send_frames(uint64_t timestamp_us);

  // Get SBC bitrate
  // Returns |uint32_t| bitrate in bits per second
  uint32_t get_bitrate();

  void a2dp_sbc_encoder_update(A2dpCodecConfig* a2dp_codec_config,
                               bool* p_restart_input,
                               bool* p_restart_output,
                               bool* p_config_updated);

  /* public vairable */
  bool is_peer_edr;         /* True if the peer device supports EDR */
  bool peer_supports_3mbps; /* True if the peer device supports 3Mbps EDR */
  uint16_t peer_mtu;        /* MTU of the A2DP peer */
  uint32_t timestamp;       /* Timestamp for the A2DP frames */
  uint16_t TxAaMtuSize;
  a2dp_sbc_encoder_stats_t stats;

private:
  void a2dp_sbc_get_num_frame_iteration(uint8_t* num_of_iterations,
                                        uint8_t* num_of_frames,
                                        uint64_t timestamp_us);

  void a2dp_sbc_encode_frames(uint8_t nb_frame);

  uint16_t a2dp_sbc_source_rate(bool is_peer_edr);

  uint8_t calculate_max_frames_per_packet(void);

  bool a2dp_sbc_read_feeding(uint32_t* bytes_read);

  uint32_t a2dp_sbc_frame_length(void);

  uint16_t adjust_effective_mtu(    const tA2DP_ENCODER_INIT_PEER_PARAMS& peer_params);

  /* Member variables */
  uint8_t tx_sbc_frames;
  SBC_ENC_PARAMS sbc_encoder_params;
  tA2DP_FEEDING_PARAMS feeding_params;
  tA2DP_SBC_FEEDING_STATE feeding_state;
  int16_t pcmBuffer[SBC_MAX_PCM_BUFFER_SIZE];
  void* sbc_encoder_lib_handle;
  A2DP_SBC_UPS_CB* mSbcUpSample;
};
#endif  // A2DP_SBC_ENCODER_H
