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
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#define LOG_TAG "bluetooth-a2dp"

#include "a2dp_vendor_aptx_encoder.h"

#include <bluetooth/log.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <cstdint>

#include "a2dp_codec_api.h"
#include "a2dp_vendor.h"
#include "a2dp_vendor_aptx.h"
#include "aptXbtenc.h"
#include "avdt_api.h"
#include "btif_a2dp_source.h"
#include "common/time_util.h"
#include "internal_include/bt_target.h"
#include "osi/include/allocator.h"
#include "stack/include/bt_hdr.h"

using namespace bluetooth;

//
// Encoder for aptX Source Codec
//

static const tAPTX_API aptx_api = {
        .init_func = aptxbtenc_init,
        .encode_stereo_func = aptxbtenc_encodestereo,
        .sizeof_params_func = SizeofAptxbtenc,
};

// offset
// no RTP header for aptX classic
#define A2DP_APTX_OFFSET (AVDT_MEDIA_OFFSET - AVDT_MEDIA_HDR_SIZE)

#define A2DP_APTX_MAX_PCM_BYTES_PER_READ 4096

/*******************************************************************************
 *
 * Function         A2DP_VendorLoadEncoderAptx
 *
 * Description      This function will try to load the aptx encoder library.
 *
 * Returns          LOAD_SUCCESS on success
 *                  LOAD_ERROR_MISSING_CODEC on missing library
 *                  LOAD_ERROR_VERSION_MISMATCH on symbol loading error
 *
 ******************************************************************************/
tLOADING_CODEC_STATUS A2DP_VendorLoadEncoderAptx(void) {
  // Nothing to do - the library is statically linked
  return LOAD_SUCCESS;
}

bool A2DP_VendorCopyAptxApi(tAPTX_API& external_api) {
  external_api = aptx_api;
  return true;
}

void A2DP_VendorUnloadEncoderAptx(void) {
  // nothing to do
}

void A2dpAptxEncoder::encoder_init(tA2DP_ENCODER_INIT_PEER_PARAMS* p_peer_params,
                                   A2dpCodecConfig* a2dp_codec_config,
                                   a2dp_source_read_callback_t read_callback,
                                   a2dp_source_enqueue_callback_t enqueue_callback) {
  this->a2dp_aptx_encoder_cb.stats.session_start_us = bluetooth::common::time_get_os_boottime_us();
  this->a2dp_aptx_encoder_cb.read_callback = read_callback;
  this->a2dp_aptx_encoder_cb.enqueue_callback = enqueue_callback;
  this->a2dp_aptx_encoder_cb.peer_params = *p_peer_params;
  this->a2dp_aptx_encoder_cb.timestamp = 0;

  /* aptX encoder config */
  this->a2dp_aptx_encoder_cb.use_SCMS_T = false;

  this->a2dp_aptx_encoder_cb.aptx_encoder_state = osi_malloc(aptx_api.sizeof_params_func());
  if (this->a2dp_aptx_encoder_cb.aptx_encoder_state != NULL) {
    aptx_api.init_func(this->a2dp_aptx_encoder_cb.aptx_encoder_state, 0);
  } else {
    log::error("Cannot allocate aptX encoder state");
    // TODO: Return an error?
  }

  // NOTE: Ignore the restart_input / restart_output flags - this initization
  // happens when the audio session is (re)started.
  bool restart_input = false;
  bool restart_output = false;
  bool config_updated = false;
  a2dp_vendor_aptx_encoder_update(a2dp_codec_config, &restart_input, &restart_output,
                                  &config_updated);
}

// Update the A2DP aptX encoder.
// |a2dp_codec_config| is the A2DP codec to use for the update.
void A2dpAptxEncoder::a2dp_vendor_aptx_encoder_update(A2dpCodecConfig* a2dp_codec_config,
                                            bool* p_restart_input, bool* p_restart_output,
                                            bool* p_config_updated) {
  uint8_t codec_info[AVDT_CODEC_SIZE];

  *p_restart_input = false;
  *p_restart_output = false;
  *p_config_updated = false;
  if (!a2dp_codec_config->copyOutOtaCodecConfig(codec_info)) {
    log::error("Cannot update the codec encoder for {}: invalid codec config",
               a2dp_codec_config->name());
    return;
  }
  const uint8_t* p_codec_info = codec_info;

  // The feeding parameters
  tA2DP_FEEDING_PARAMS* p_feeding_params = &this->a2dp_aptx_encoder_cb.feeding_params;
  p_feeding_params->sample_rate = A2DP_VendorGetTrackSampleRateAptx(p_codec_info);
  p_feeding_params->bits_per_sample = a2dp_codec_config->getAudioBitsPerSample();
  p_feeding_params->channel_count = A2DP_VendorGetTrackChannelCountAptx(p_codec_info);
  log::info("sample_rate={} bits_per_sample={} channel_count={}", p_feeding_params->sample_rate,
            p_feeding_params->bits_per_sample, p_feeding_params->channel_count);
  feeding_reset();
}

void A2dpAptxEncoder::encoder_cleanup(void) {
  osi_free(this->a2dp_aptx_encoder_cb.aptx_encoder_state);
  memset(&this->a2dp_aptx_encoder_cb, 0, sizeof(this->a2dp_aptx_encoder_cb));
}

//
// Initialize the framing parameters, and set those that don't change
// while streaming (e.g., 'sleep_time_ns').
//
void A2dpAptxEncoder::aptx_init_framing_params(tAPTX_FRAMING_PARAMS* framing_params) {
  framing_params->sleep_time_ns = 0;
  framing_params->pcm_reads = 0;
  framing_params->pcm_bytes_per_read = 0;
  framing_params->aptx_bytes = 0;
  framing_params->frame_size_counter = 0;

  if (this->a2dp_aptx_encoder_cb.feeding_params.sample_rate == 48000) {
    if (this->a2dp_aptx_encoder_cb.use_SCMS_T) {
      framing_params->sleep_time_ns = 13000000;
    } else {
      framing_params->sleep_time_ns = 14000000;
    }
  } else {
    // Assume the sample rate is 44100
    if (this->a2dp_aptx_encoder_cb.use_SCMS_T) {
      framing_params->sleep_time_ns = 14000000;
    } else {
      framing_params->sleep_time_ns = 15000000;
    }
  }

  log::info("sleep_time_ns={}", framing_params->sleep_time_ns);
}

//
// Set frame size and transmission interval needed to stream the required
// sample rate using 2-DH5 packets for aptX and 2-DH3 packets for aptX-LL.
// With SCMS-T enabled we need to reserve room for extra headers added later.
// Packets are always sent at equals time intervals but to achieve the
// required sample rate, the frame size needs to change on occasion.
//
// Also need to specify how many of the required PCM samples are read at a
// time:
//     aptx_bytes = pcm_reads * pcm_bytes_per_read / 4
// and
//     number of aptX samples produced = pcm_bytes_per_read / 16
//
void A2dpAptxEncoder::aptx_update_framing_params(tAPTX_FRAMING_PARAMS* framing_params) {
  if (this->a2dp_aptx_encoder_cb.feeding_params.sample_rate == 48000) {
    if (this->a2dp_aptx_encoder_cb.use_SCMS_T) {
      framing_params->aptx_bytes = 624;
      framing_params->pcm_bytes_per_read = 208;
      framing_params->pcm_reads = 12;
    } else {
      framing_params->aptx_bytes = 672;
      framing_params->pcm_bytes_per_read = 224;
      framing_params->pcm_reads = 12;
    }
  } else {
    // Assume the sample rate is 44100
    if (this->a2dp_aptx_encoder_cb.use_SCMS_T) {
      if (++framing_params->frame_size_counter < 20) {
        framing_params->aptx_bytes = 616;
        framing_params->pcm_bytes_per_read = 224;
        framing_params->pcm_reads = 11;
      } else {
        framing_params->aptx_bytes = 644;
        framing_params->pcm_bytes_per_read = 368;
        framing_params->pcm_reads = 7;
        framing_params->frame_size_counter = 0;
      }
    } else {
      if (++framing_params->frame_size_counter < 8) {
        framing_params->aptx_bytes = 660;
        framing_params->pcm_bytes_per_read = 240;
        framing_params->pcm_reads = 11;
      } else {
        framing_params->aptx_bytes = 672;
        framing_params->pcm_bytes_per_read = 224;
        framing_params->pcm_reads = 12;
        framing_params->frame_size_counter = 0;
      }
    }
  }

  log::verbose(
          "sleep_time_ns={} aptx_bytes={} pcm_bytes_per_read={} pcm_reads={} "
          "frame_size_counter={}",
          framing_params->sleep_time_ns, framing_params->aptx_bytes,
          framing_params->pcm_bytes_per_read, framing_params->pcm_reads,
          framing_params->frame_size_counter);
}

void A2dpAptxEncoder::feeding_reset(void) {
  aptx_init_framing_params(&this->a2dp_aptx_encoder_cb.framing_params);
}

void A2dpAptxEncoder::feeding_flush(void) {
  aptx_init_framing_params(&this->a2dp_aptx_encoder_cb.framing_params);
}

uint64_t A2dpAptxEncoder::get_encoder_interval_ms(void) {
  return this->a2dp_aptx_encoder_cb.framing_params.sleep_time_ns / (1000 * 1000);
}

int A2dpAptxEncoder::get_effective_frame_size() {
  return this->a2dp_aptx_encoder_cb.peer_params.peer_mtu;
}

void A2dpAptxEncoder::send_frames(uint64_t /* timestamp_us */) {
  tAPTX_FRAMING_PARAMS* framing_params = &this->a2dp_aptx_encoder_cb.framing_params;

  // Prepare the packet to send
  BT_HDR* p_buf = (BT_HDR*)osi_malloc(BT_DEFAULT_BUFFER_SIZE);
  p_buf->offset = A2DP_APTX_OFFSET;
  p_buf->len = 0;
  p_buf->layer_specific = 0;

  uint8_t* encoded_ptr = (uint8_t*)(p_buf + 1);
  encoded_ptr += p_buf->offset;

  aptx_update_framing_params(framing_params);

  //
  // Read the PCM data and encode it
  //
  uint16_t read_buffer16[A2DP_APTX_MAX_PCM_BYTES_PER_READ / sizeof(uint16_t)];
  uint32_t expected_read_bytes = framing_params->pcm_reads * framing_params->pcm_bytes_per_read;
  size_t encoded_ptr_index = 0;
  size_t pcm_bytes_encoded = 0;
  uint32_t bytes_read = 0;

  this->a2dp_aptx_encoder_cb.stats.media_read_total_expected_packets++;
  this->a2dp_aptx_encoder_cb.stats.media_read_total_expected_reads_count++;
  this->a2dp_aptx_encoder_cb.stats.media_read_total_expected_read_bytes += expected_read_bytes;

  log::verbose("PCM read of size {}", expected_read_bytes);
  bytes_read = btif_a2dp_source_read_callback(get_peer_address(), (uint8_t*)read_buffer16, expected_read_bytes);
  this->a2dp_aptx_encoder_cb.stats.media_read_total_actual_read_bytes += bytes_read;
  if (bytes_read < expected_read_bytes) {
    log::warn("underflow at PCM reading: read {} bytes instead of {}", bytes_read,
              expected_read_bytes);
    this->a2dp_aptx_encoder_cb.stats.media_read_total_dropped_packets++;
    osi_free(p_buf);
    return;
  }
  this->a2dp_aptx_encoder_cb.stats.media_read_total_actual_reads_count++;

  for (uint32_t reads = 0, offset = 0; reads < framing_params->pcm_reads;
       reads++, offset += (framing_params->pcm_bytes_per_read / sizeof(uint16_t))) {
    pcm_bytes_encoded += aptx_encode_16bit(framing_params, &encoded_ptr_index,
                                           read_buffer16 + offset, encoded_ptr);
  }

  // Compute the number of encoded bytes
  const int COMPRESSION_RATIO = 4;
  size_t encoded_bytes = pcm_bytes_encoded / COMPRESSION_RATIO;
  p_buf->len += encoded_bytes;
  log::verbose("encoded {} PCM bytes to {}", pcm_bytes_encoded, encoded_bytes);

  // Update the RTP timestamp
  *((uint32_t*)(p_buf + 1)) = this->a2dp_aptx_encoder_cb.timestamp;

  const uint8_t BYTES_PER_FRAME = 2;
  uint32_t rtp_timestamp =
          (pcm_bytes_encoded / this->a2dp_aptx_encoder_cb.feeding_params.channel_count) / BYTES_PER_FRAME;

  // Timestamp will wrap over to 0 if stream continues on long enough
  // (>25H @ 48KHz). The parameters are promoted to 64bit to ensure that
  // no unsigned overflow is triggered as ubsan is always enabled.
  this->a2dp_aptx_encoder_cb.timestamp =
          ((uint64_t)this->a2dp_aptx_encoder_cb.timestamp + rtp_timestamp) & UINT32_MAX;

  if (p_buf->len > 0) {
    btif_a2dp_source_enqueue_callback(get_peer_address(), p_buf, 1, bytes_read);
  } else {
    this->a2dp_aptx_encoder_cb.stats.media_read_total_dropped_packets++;
    osi_free(p_buf);
  }
}

size_t A2dpAptxEncoder::aptx_encode_16bit(tAPTX_FRAMING_PARAMS* framing_params, size_t* data_out_index,
                                uint16_t* data16_in, uint8_t* data_out) {
  size_t pcm_bytes_encoded = 0;
  size_t frame = 0;

  for (size_t aptx_samples = 0; aptx_samples < framing_params->pcm_bytes_per_read / 16;
       aptx_samples++) {
    uint32_t pcmL[4];
    uint32_t pcmR[4];
    uint16_t encoded_sample[2];

    for (size_t i = 0, j = frame; i < 4; i++, j++) {
      pcmL[i] = (uint16_t)*(data16_in + (2 * j));
      pcmR[i] = (uint16_t)*(data16_in + ((2 * j) + 1));
    }

    aptx_api.encode_stereo_func(this->a2dp_aptx_encoder_cb.aptx_encoder_state, &pcmL, &pcmR,
                                &encoded_sample);

    data_out[*data_out_index + 0] = (uint8_t)((encoded_sample[0] >> 8) & 0xff);
    data_out[*data_out_index + 1] = (uint8_t)((encoded_sample[0] >> 0) & 0xff);
    data_out[*data_out_index + 2] = (uint8_t)((encoded_sample[1] >> 8) & 0xff);
    data_out[*data_out_index + 3] = (uint8_t)((encoded_sample[1] >> 0) & 0xff);

    frame += 4;
    pcm_bytes_encoded += 16;
    *data_out_index += 4;
  }

  return pcm_bytes_encoded;
}

void A2dpCodecConfigAptxSource::debug_codec_dump(int fd) {
  A2dpAptxEncoder* encoder = (A2dpAptxEncoder*)findA2dpSourceEncoder(getPeerAddress());
  if (encoder == nullptr) {
    log::error("failed to find encoder for peer_address:{}",
               getPeerAddress().ToString().c_str());
    return;
  }

  a2dp_aptx_encoder_stats_t* stats = &encoder->a2dp_aptx_encoder_cb.stats;

  A2dpCodecConfig::debug_codec_dump(fd);

  dprintf(fd, "  Encoder interval (ms): %" PRIu64 "\n", encoder->get_encoder_interval_ms());
  dprintf(fd, "  Effective MTU: %d\n", encoder->get_effective_frame_size());
  dprintf(fd,
          "  Packet counts (expected/dropped)                        : %zu / "
          "%zu\n",
          stats->media_read_total_expected_packets, stats->media_read_total_dropped_packets);

  dprintf(fd,
          "  PCM read counts (expected/actual)                       : %zu / "
          "%zu\n",
          stats->media_read_total_expected_reads_count, stats->media_read_total_actual_reads_count);

  dprintf(fd,
          "  PCM read bytes (expected/actual)                        : %zu / "
          "%zu\n",
          stats->media_read_total_expected_read_bytes, stats->media_read_total_actual_read_bytes);
}
