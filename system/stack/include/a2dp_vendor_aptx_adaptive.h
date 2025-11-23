/******************************************************************************
    Copyright (c) 2018, The Linux Foundation. All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted (subject to the limitations in the
    disclaimer below) provided that the following conditions are met:

       * Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.

       * Redistributions in binary form must reproduce the above
         copyright notice, this list of conditions and the following
         disclaimer in the documentation and/or other materials provided
         with the distribution.

       * Neither the name of The Linux Foundation nor the names of its
         contributors may be used to endorse or promote products derived
         from this software without specific prior written permission.

    NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
    GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
    HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
    WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
    MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
    ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
    GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
    IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
    OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
    IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    Changes from Qualcomm Technologies, Inc. are provided under the following license:

    Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
    SPDX-License-Identifier: BSD-3-Clause-Clear.
 ******************************************************************************/

//
// A2DP Codec API for aptX-Adaptive
//

#ifndef A2DP_VENDOR_APTX_ADAPTIVE_H
#define A2DP_VENDOR_APTX_ADAPTIVE_H

#include <stddef.h>
#include <stdint.h>

#include "a2dp_codec_api.h"
#include "a2dp_vendor_aptx_adaptive_constants.h"
#include "avdt_api.h"
#include "internal_include/bt_target.h"
#include "stack/include/bt_hdr.h"

class A2dpCodecConfigAptxAdaptiveBase : public A2dpCodecConfig {
 protected:
  A2dpCodecConfigAptxAdaptiveBase(btav_a2dp_codec_index_t codec_index,
                         const std::string& name,
                         btav_a2dp_codec_priority_t codec_priority,
                         bool is_source)
      : A2dpCodecConfig(codec_index, A2DP_CODEC_ID_APTX_ADAPTIVE, name, codec_priority),
        is_source_(is_source) {}
  tA2DP_STATUS setCodecConfig(const uint8_t* p_peer_codec_info, bool is_capability,
                              uint8_t* p_result_codec_config) override;
  bool setPeerCodecCapabilities(
      const uint8_t* p_peer_codec_capabilities) override;

 private:
  bool is_source_;  // True if local is Source
};


class A2dpCodecConfigAptxAdaptiveSink : public A2dpCodecConfigAptxAdaptiveBase {
 public:
  A2dpCodecConfigAptxAdaptiveSink(btav_a2dp_codec_priority_t codec_priority);
  virtual ~A2dpCodecConfigAptxAdaptiveSink();

  bool init() override;

 private:
  bool useRtpHeaderMarkerBit() const override;
};

// Checks whether the codec capabilities contain a valid A2DP aptX-Adaptive Source
// codec.
// NOTE: only codecs that are implemented are considered valid.
// Returns true if |p_codec_info| contains information about a valid aptX-Adaptive
// codec, otherwise false.
bool A2DP_IsCodecValidAptxAdaptive(const uint8_t* p_codec_info);

// Checks whether A2DP aptX-Adaptive Sink codec is supported.
// |p_codec_info| contains information about the codec capabilities.
// Returns true if the A2DP aptX-Adaptive Sink codec is supported, otherwise false.
tA2DP_STATUS A2DP_IsSinkCodecSupportedAptxAdaptive(const uint8_t* p_codec_info);

// Checks whether the A2DP data packets should contain RTP header.
// |content_protection_enabled| is true if Content Protection is
// enabled. |p_codec_info| contains information about the codec capabilities.
// Returns true if the A2DP data packets should contain RTP header, otherwise
// false.
bool A2DP_VendorUsesRtpHeaderAptxAdaptive(bool content_protection_enabled, const uint8_t* p_codec_info);

// Gets the A2DP aptX-Adaptive codec name for a given |p_codec_info|.
const char* A2DP_VendorCodecNameAptxAdaptive(const uint8_t* p_codec_info);

// Checks whether two A2DP aptX-Adaptive codecs |p_codec_info_a| and |p_codec_info_b|
// have the same type.
// Returns true if the two codecs have the same type, otherwise false.
bool A2DP_VendorCodecTypeEqualsAptxAdaptive(const uint8_t* p_codec_info_a, const uint8_t* p_codec_info_b);

// Checks whether two A2DP aptX-Adaptive codecs |p_codec_info_a| and |p_codec_info_b|
// are exactly the same.
// Returns true if the two codecs are exactly the same, otherwise false.
// If the codec type is not aptX-Adaptive, the return value is false.
bool A2DP_VendorCodecEqualsAptxAdaptive(const uint8_t* p_codec_info_a, const uint8_t* p_codec_info_b);

// Gets the track sample rate value for the A2DP aptX-Adaptive codec.
// |p_codec_info| is a pointer to the aptX-Adaptive codec_info to decode.
// Returns the track sample rate on success, or -1 if |p_codec_info|
// contains invalid codec information.
int A2DP_VendorGetTrackSampleRateAptxAdaptive(const uint8_t* p_codec_info);

// Gets the track bits per sample value for the A2DP aptX-Adaptive codec.
// |p_codec_info| is a pointer to the aptX-Adaptive codec_info to decode.
// Returns the track bits per sample on success, or -1 if |p_codec_info|
// contains invalid codec information.
int A2DP_VendorGetTrackBitsPerSampleAptxAdaptive(const uint8_t* p_codec_info);

// Gets the track bitrate value for the A2DP aptX-Adaptive codec.
// |p_codec_info| is a pointer to the aptX-Adaptive codec_info to decode.
// Returns the track sample rate on success, or -1 if |p_codec_info|
// contains invalid codec information.
int A2DP_VendorGetBitRateAptxAdaptive(const uint8_t* p_codec_info);

// Gets the channel count for the A2DP aptX-Adaptive codec.
// |p_codec_info| is a pointer to the aptX-Adaptive codec_info to decode.
// Returns the channel count on success, or -1 if |p_codec_info|
// contains invalid codec information.
int A2DP_VendorGetTrackChannelCountAptxAdaptive(const uint8_t* p_codec_info);

// Gets the channel type for the A2DP aptX-Adaptive Sink codec:
// 1 for mono, or 3 for stereo
// |p_codec_info| is a pointer to the aptX-Adaptive codec_info to decode.
// Returns the channel type on success, or -1 if |p_codec_info|
// contains invalid codec information.
int A2DP_VendorGetTrackChannelTypeAptxAdaptive(const uint8_t* p_codec_info);

// Gets the A2DP aptX-Adaptive audio data timestamp from an audio packet.
// |p_codec_info| contains the codec information.
// |p_data| contains the audio data.
// The timestamp is stored in |p_timestamp|.
// Returns true on success, otherwise false.
bool A2DP_VendorGetPacketTimestampAptxAdaptive(const uint8_t* p_codec_info, const uint8_t* p_data,
                                         uint32_t* p_timestamp);

// Builds A2DP aptX-Adaptive codec header for audio data.
// |p_codec_info| contains the codec information.
// |p_buf| contains the audio data.
// |frames_per_packet| is the number of frames in this packet.
// Returns true on success, otherwise false.
bool A2DP_VendorBuildCodecHeaderAptxAdaptive(const uint8_t* p_codec_info, BT_HDR* p_buf,
                                       uint16_t frames_per_packet);

// Decodes A2DP aptX-Adaptive codec info into a human readable string.
// |p_codec_info| is a pointer to the aptX-Adaptive codec_info to decode.
// Returns a string describing the codec information.
std::string A2DP_VendorCodecInfoStringAptxAdaptive(const uint8_t* p_codec_info);

// Gets the A2DP aptX-Adaptive decoder interface that can be used to decode and prepare
// PCM packets for playing - see |tA2DP_DECODER_INTERFACE|.
// |p_codec_info| contains the codec information.
// Returns the A2DP aptX-Adaptive decoder interface if the |p_codec_info| is valid and
// supported, otherwise NULL.
const tA2DP_DECODER_INTERFACE* A2DP_VendorGetDecoderInterfaceAptxAdaptive(
    const uint8_t* p_codec_info);

// Adjusts the A2DP aptX-Adaptive codec, based on local support and Bluetooth
// specification.
// |p_codec_info| contains the codec information to adjust.
// Returns true if |p_codec_info| is valid and supported, otherwise false.
bool A2DP_VendorAdjustCodecAptxAdaptive(uint8_t* p_codec_info);

// Gets the A2DP aptX-Adaptive Sink codec index for a given |p_codec_info|.
// Returns the corresponding |btav_a2dp_codec_index_t| on success,
// otherwise |BTAV_A2DP_CODEC_INDEX_MAX|.
btav_a2dp_codec_index_t A2DP_VendorSinkCodecIndexAptxAdaptive(
    const uint8_t* p_codec_info);

// Gets the A2DP aptX-Adaptive Sink codec name.
const char* A2DP_VendorCodecIndexStrAptxAdaptiveSink(void);

// Initializes A2DP aptX-Adaptive Sink codec information into |AvdtpSepConfig|
// configuration entry pointed by |p_cfg|.
bool A2DP_VendorInitCodecConfigAptxAdaptiveSink(AvdtpSepConfig* p_cfg);

#endif  // A2DP_VENDOR_APTX_ADAPTIVE_H
