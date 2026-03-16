/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "bluetooth-a2dp"

#include "a2dp_sbc_decoder.h"

#include <bluetooth/log.h>

#include <cstddef>
#include <cstdint>

#include "a2dp_codec_api.h"
#include "a2dp_sbc.h"
#include "embdrv/sbc/encoder/include/sbc_encoder.h"
#include "embdrv/sbc/decoder/include/oi_codec_sbc.h"
#include "embdrv/sbc/decoder/include/oi_cpu_dep.h"
#include "embdrv/sbc/decoder/include/oi_status.h"
#include "stack/include/bt_hdr.h"

/* Define the bitrate step when trying to match bitpool value */
#define A2DP_SBC_BITRATE_STEP 5

using namespace bluetooth;

namespace std {
template <>
struct formatter<OI_STATUS> : enum_formatter<OI_STATUS> {};
}  // namespace std

typedef struct {
  OI_CODEC_SBC_DECODER_CONTEXT decoder_context;
  uint32_t context_data[CODEC_DATA_WORDS(2, SBC_CODEC_FAST_FILTER_BUFFERS)];
  int16_t decode_buf[15 * SBC_MAX_SAMPLES_PER_FRAME * SBC_MAX_CHANNELS];
  decoded_data_callback_t decode_callback;
} tA2DP_SBC_DECODER_CB;

static tA2DP_SBC_DECODER_CB a2dp_sbc_decoder_cb;
static uint16_t offload_bitrate;

bool a2dp_sbc_decoder_init(decoded_data_callback_t decode_callback) {
  OI_STATUS status = OI_CODEC_SBC_DecoderReset(
          &a2dp_sbc_decoder_cb.decoder_context, a2dp_sbc_decoder_cb.context_data,
          sizeof(a2dp_sbc_decoder_cb.context_data), 2, 2, false);
  if (!OI_SUCCESS(status)) {
    log::error("OI_CODEC_SBC_DecoderReset failed with error code {}", status);
    return false;
  }

  a2dp_sbc_decoder_cb.decode_callback = decode_callback;
  return true;
}

void a2dp_sbc_decoder_cleanup(void) {
  // Do nothing.
}

bool a2dp_sbc_decoder_decode_packet(BT_HDR* p_buf) {
  uint8_t* data = p_buf->data + p_buf->offset;
  size_t data_size = p_buf->len;

  if (data_size == 0) {
    log::error("Empty packet");
    return false;
  }
  size_t num_frames = data[0] & 0xf;
  data += 1;
  data_size -= 1;

  const OI_BYTE* oi_data = data;
  uint32_t oi_size = data_size;
  size_t out_avail = sizeof(a2dp_sbc_decoder_cb.decode_buf);
  int16_t* out_ptr = a2dp_sbc_decoder_cb.decode_buf;

  for (size_t i = 0; i < num_frames; ++i) {
    uint32_t out_size = out_avail;
    OI_STATUS status = OI_CODEC_SBC_DecodeFrame(&a2dp_sbc_decoder_cb.decoder_context, &oi_data,
                                                &oi_size, out_ptr, &out_size);
    if (!OI_SUCCESS(status)) {
      log::error("Decoding failure: {}", status);
      return false;
    }
    out_avail -= out_size;
    out_ptr += out_size / sizeof(*out_ptr);
  }

  size_t out_used = (out_ptr - a2dp_sbc_decoder_cb.decode_buf) * sizeof(*out_ptr);
  a2dp_sbc_decoder_cb.decode_callback(reinterpret_cast<uint8_t*>(a2dp_sbc_decoder_cb.decode_buf),
                                      out_used);
  return true;
}

void a2dp_sbc_decoder_configure(const uint8_t* p_codec_info) {
  uint16_t s16SamplingFreq,sample_rate;
  int16_t s16BitPool = 0;
  int16_t s16BitRate;
  int16_t s16FrameLen;
  uint8_t protect = 0;
  int min_bitpool;
  int max_bitpool;
  uint8_t bits_per_sample = 16,channel_count;
  uint16_t s16ChannelMode, s16NumOfSubBands, s16NumOfBlocks;
  uint16_t s16AllocationMethod, s16NumOfChannels;

  min_bitpool = A2DP_GetMinBitpoolSbc(p_codec_info);
  max_bitpool = A2DP_GetMaxBitpoolSbc(p_codec_info);
  // The feeding parameters
  sample_rate = A2DP_GetTrackSampleRateSbc(p_codec_info);
  channel_count = A2DP_GetTrackChannelCountSbc(p_codec_info);
  log::info("sample_rate {} bits_per_sample {} channel_count {} min_bitpool {} \
             max_bitpool {}", sample_rate, bits_per_sample, channel_count,
             min_bitpool, max_bitpool);

 // The codec parameters
  s16ChannelMode = A2DP_GetChannelModeCodeSbc(p_codec_info);
  s16NumOfSubBands =
       A2DP_GetNumberOfSubbandsSbc(p_codec_info);
  s16NumOfBlocks = A2DP_GetNumberOfBlocksSbc(p_codec_info);
  s16AllocationMethod =
       A2DP_GetAllocationMethodCodeSbc(p_codec_info);
  s16SamplingFreq =
       A2DP_GetSamplingFrequencyCodeSbc(p_codec_info);
  s16NumOfChannels =
       A2DP_GetTrackChannelCountSbc(p_codec_info);

 // Reset invalid parameters
  if (!s16NumOfSubBands) {
    log::warn("SubBands are set to 0, resetting to max ({})",
                                                      SBC_MAX_NUM_OF_SUBBANDS);
    s16NumOfSubBands = SBC_MAX_NUM_OF_SUBBANDS;
  }
  if (!s16NumOfBlocks) {
     log::warn("Blocks are set to 0, resetting to max ({})",
                                                        SBC_MAX_NUM_OF_BLOCKS);
    s16NumOfBlocks = SBC_MAX_NUM_OF_BLOCKS;
  }
  if (!s16NumOfChannels) {
    log::warn("Channels are set to 0, resetting to max ({})",
                                                      SBC_MAX_NUM_OF_CHANNELS);
    s16NumOfChannels = SBC_MAX_NUM_OF_CHANNELS;
  }

  if (s16SamplingFreq == SBC_sf16000)
    s16SamplingFreq = 16000;
  else if (s16SamplingFreq == SBC_sf32000)
    s16SamplingFreq = 32000;
  else if (s16SamplingFreq == SBC_sf44100)
    s16SamplingFreq = 44100;
  else
    s16SamplingFreq = 48000;

  //TO-DO: for edr it is 328 and for non-edr 229
  //fetching edr status unknown
  offload_bitrate = 328 ;
  log::warn("initial bitrate = {}", offload_bitrate);
  do {
    if ((s16ChannelMode == SBC_JOINT_STEREO) ||
        (s16ChannelMode == SBC_STEREO)) {
      s16BitPool = (int16_t)((offload_bitrate *
                              s16NumOfSubBands * 1000 /
                              s16SamplingFreq) -
                             ((32 + (4 * s16NumOfSubBands *
                                     s16NumOfChannels) +
                               (s16ChannelMode - 2) *
                                s16NumOfSubBands)) /
                              s16NumOfBlocks);

      s16FrameLen = 4 +
                    (4 * s16NumOfSubBands *
                     s16NumOfChannels) /
                        8 +
                    (((s16ChannelMode - 2) *
                      s16NumOfSubBands) +
                     (s16NumOfBlocks * s16BitPool)) /
                        8;

      s16BitRate = (8 * s16FrameLen * s16SamplingFreq) /
                   (s16NumOfSubBands *
                    s16NumOfBlocks * 1000);

      if (s16BitRate > offload_bitrate) s16BitPool--;

      if (s16NumOfSubBands == 8)
        s16BitPool = (s16BitPool > 255) ? 255 : s16BitPool;
      else
        s16BitPool = (s16BitPool > 128) ? 128 : s16BitPool;
    } else {
      s16BitPool =
          (int16_t)(((s16NumOfSubBands *
                      offload_bitrate * 1000) /
                     (s16SamplingFreq * s16NumOfChannels)) -
                    (((32 / s16NumOfChannels) +
                      (4 * s16NumOfSubBands)) /
                     s16NumOfBlocks));
    }

    if (s16BitPool < 0) s16BitPool = 0;

    log::verbose("bitpool candidate: {} ({} kbps)",
              s16BitPool, offload_bitrate);

    if (s16BitPool > max_bitpool) {
      log::verbose("computed bitpool too large ({})", s16BitPool);
      /* Decrease bitrate */
      offload_bitrate -= A2DP_SBC_BITRATE_STEP;
      /* Record that we have decreased the bitrate */
      protect |= 1;
    } else if (s16BitPool < min_bitpool) {
      log::warn("computed bitpool too small ({})", s16BitPool);
      /* Increase bitrate */
      uint16_t previous_u16BitRate = offload_bitrate;
      offload_bitrate += A2DP_SBC_BITRATE_STEP;
      /* Record that we have increased the bitrate */
      protect |= 2;
      /* Check over-flow */
      if (offload_bitrate < previous_u16BitRate) protect |= 3;
    } else {
      break;
    }
    /* In case we have already increased and decreased the bitrate, just stop */
    if (protect == 3) {
      log::error("could not find bitpool in range");
      break;
    }
  } while (true);

  log::info("final bit rate {}",offload_bitrate);

}

uint32_t a2dp_sbc_sink_get_bitrate() {
    return offload_bitrate * 1000;
}
