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


/******************************************************************************
 *
 *  Utility functions to help build and parse the aptX-Adaptive Codec Information
 *  Element and Media Payload.
 *
 ******************************************************************************/

#define LOG_TAG "bluetooth-a2dp"

#include "a2dp_vendor_aptx_adaptive.h"

#include <bluetooth/log.h>
#include <string.h>

#include "a2dp_vendor.h"
#include "a2dp_vendor_aptx_adaptive_decoder.h"
#include "btif/include/btif_av_co.h"
#include "internal_include/bt_trace.h"
#include "osi/include/osi.h"
#include "osi/include/properties.h"
#include "stack/include/bt_hdr.h"

using namespace bluetooth;

// Data type for the aptX-Adaptive Codec Information Element
typedef struct {
  uint8_t ttp_ll_0;
  uint8_t ttp_ll_1;
  uint8_t ttp_hq_0;
  uint8_t ttp_hq_1;
  uint8_t ttp_tws_0;
  uint8_t ttp_tws_1;
  uint8_t reserved_15thbyte;
  // Additions for R2
  uint8_t cap_ext_ver_num;
  uint32_t aptx_adaptive_sup_features;
  uint8_t first_setup_pref;
  uint8_t second_setup_pref;
  uint8_t third_setup_pref;
  uint8_t fourth_setup_pref;
  uint8_t eoc0;
  uint8_t eoc1;
} tA2DP_APTX_ADAPTIVE_VENDOR_DATA;

// Codec Information Element
typedef struct {
  uint32_t vendorId;
  uint16_t codecId;
  uint8_t sampleRate;
  uint8_t sourceType;
  uint8_t channelMode;
  tA2DP_APTX_ADAPTIVE_VENDOR_DATA aptx_data;
  btav_a2dp_codec_bits_per_sample_t bits_per_sample;
  uint8_t reserved_data[A2DP_APTX_ADAPTIVE_RESERVED_DATA];
} tA2DP_APTX_ADAPTIVE_CIE;

// aptX-Adaptive Sink codec capabilities
static const tA2DP_APTX_ADAPTIVE_CIE a2dp_aptx_adaptive_sink_caps = {
  A2DP_APTX_ADAPTIVE_VENDOR_ID,
  A2DP_APTX_ADAPTIVE_CODEC_ID_BLUETOOTH,
  A2DP_APTX_ADAPTIVE_SAMPLERATE_48000,
  A2DP_APTX_ADAPTIVE_SOURCE_TYPE_1,
  (A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO |
   A2DP_APTX_ADAPTIVE_CHANNELS_STEREO),
  {
    A2DP_APTX_ADAPTIVE_TTP_LL_0,
    A2DP_APTX_ADAPTIVE_TTP_LL_1,
    A2DP_APTX_ADAPTIVE_TTP_HQ_0,
    A2DP_APTX_ADAPTIVE_TTP_HQ_1,
    A2DP_APTX_ADAPTIVE_TTP_TWS_0,
    A2DP_APTX_ADAPTIVE_TTP_TWS_1,
    A2DP_APTX_ADAPTIVE_RESERVED_15THBYTE,
    A2DP_APTX_ADAPTIVE_CAP_EXT_VER_NUM,
    A2DP_APTX_ADAPTIVE_SUPPORTED_FEATURES,
    A2DP_APTX_ADAPTIVE_FIRST_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_SECOND_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_THIRD_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_FOURTH_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_EOC0,
    A2DP_APTX_ADAPTIVE_EOC1
  },
  BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32,
  {0}
};
static const tA2DP_APTX_ADAPTIVE_CIE a2dp_aptx_adaptive_sink_r1_caps = {
  A2DP_APTX_ADAPTIVE_VENDOR_ID,          /* vendorId */
  A2DP_APTX_ADAPTIVE_CODEC_ID_BLUETOOTH, /* codecId */
  A2DP_APTX_ADAPTIVE_SAMPLERATE_48000,   /* sampleRate */
  A2DP_APTX_ADAPTIVE_SOURCE_TYPE_1,
  (A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO |
    A2DP_APTX_ADAPTIVE_CHANNELS_STEREO),      /* channelMode */
  { A2DP_APTX_ADAPTIVE_TTP_LL_0,
    A2DP_APTX_ADAPTIVE_TTP_LL_1,
    A2DP_APTX_ADAPTIVE_TTP_HQ_0,
    A2DP_APTX_ADAPTIVE_TTP_HQ_1,
    A2DP_APTX_ADAPTIVE_TTP_TWS_0,
    A2DP_APTX_ADAPTIVE_TTP_TWS_1,
    0x00,
    A2DP_APTX_ADAPTIVE_EOC0,
    A2DP_APTX_ADAPTIVE_EOC1,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00},
  BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32, /* bits_per_sample */
  {0}
};
/* Default aptX-adaptive R2.1 codec configuration */
static const tA2DP_APTX_ADAPTIVE_CIE a2dp_aptx_adaptive_sink_r2_1_caps = {
  A2DP_APTX_ADAPTIVE_VENDOR_ID,
  A2DP_APTX_ADAPTIVE_CODEC_ID_BLUETOOTH,
  A2DP_APTX_ADAPTIVE_SAMPLERATE_48000 |
  A2DP_APTX_ADAPTIVE_SAMPLERATE_96000,
  A2DP_APTX_ADAPTIVE_SOURCE_TYPE_1,
  (A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO |
    A2DP_APTX_ADAPTIVE_CHANNELS_STEREO),
  {
    A2DP_APTX_ADAPTIVE_TTP_LL_0,
    A2DP_APTX_ADAPTIVE_TTP_LL_1,
    A2DP_APTX_ADAPTIVE_TTP_HQ_0,
    A2DP_APTX_ADAPTIVE_TTP_HQ_1,
    A2DP_APTX_ADAPTIVE_TTP_TWS_0,
    A2DP_APTX_ADAPTIVE_TTP_TWS_1,
    A2DP_APTX_ADAPTIVE_RESERVED_15THBYTE,
    A2DP_APTX_ADAPTIVE_CAP_EXT_VER_NUM,
    A2DP_APTX_ADAPTIVE_R2_1_SUPPORTED_FEATURES,
    A2DP_APTX_ADAPTIVE_FIRST_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_SECOND_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_THIRD_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_FOURTH_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_EOC0,
    A2DP_APTX_ADAPTIVE_EOC1
  },
  BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32, /* bits_per_sample */
  {0}
};

/* Default aptX-adaptive R2.2 codec configuration */
static const tA2DP_APTX_ADAPTIVE_CIE a2dp_aptx_adaptive_sink_r2_2_caps = {
  A2DP_APTX_ADAPTIVE_VENDOR_ID,          /* vendorId */
  A2DP_APTX_ADAPTIVE_CODEC_ID_BLUETOOTH, /* codecId */
  A2DP_APTX_ADAPTIVE_SAMPLERATE_44100 |
  A2DP_APTX_ADAPTIVE_SAMPLERATE_48000 |
  A2DP_APTX_ADAPTIVE_SAMPLERATE_96000,   /* sampleRate */
  A2DP_APTX_ADAPTIVE_SOURCE_TYPE_1,
  (A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO |
    A2DP_APTX_ADAPTIVE_CHANNELS_STEREO), /* channelMode */
  {
    A2DP_APTX_ADAPTIVE_TTP_LL_0,
    A2DP_APTX_ADAPTIVE_TTP_LL_1,
    A2DP_APTX_ADAPTIVE_TTP_HQ_0,
    A2DP_APTX_ADAPTIVE_TTP_HQ_1,
    A2DP_APTX_ADAPTIVE_TTP_TWS_0,
    A2DP_APTX_ADAPTIVE_TTP_TWS_1,
    A2DP_APTX_ADAPTIVE_RESERVED_15THBYTE,
    A2DP_APTX_ADAPTIVE_CAP_EXT_VER_NUM,
    A2DP_APTX_ADAPTIVE_R2_2_SUPPORTED_FEATURES,
    A2DP_APTX_ADAPTIVE_FIRST_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_SECOND_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_THIRD_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_FOURTH_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_EOC0,
    A2DP_APTX_ADAPTIVE_EOC1
  },
  BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32,
  {0}
};

/* Default aptX-Adpative sink codec configuration */
static const tA2DP_APTX_ADAPTIVE_CIE a2dp_aptx_adaptive_default_sink_config = {
  A2DP_APTX_ADAPTIVE_VENDOR_ID,
  A2DP_APTX_ADAPTIVE_CODEC_ID_BLUETOOTH,
  A2DP_APTX_ADAPTIVE_SAMPLERATE_48000,
  A2DP_APTX_ADAPTIVE_SOURCE_TYPE_1,
  A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO,
  {
    A2DP_APTX_ADAPTIVE_TTP_LL_0,
    A2DP_APTX_ADAPTIVE_TTP_LL_1,
    A2DP_APTX_ADAPTIVE_TTP_HQ_0,
    A2DP_APTX_ADAPTIVE_TTP_HQ_1,
    A2DP_APTX_ADAPTIVE_TTP_TWS_0,
    A2DP_APTX_ADAPTIVE_TTP_TWS_1,
    A2DP_APTX_ADAPTIVE_RESERVED_15THBYTE,
    A2DP_APTX_ADAPTIVE_CAP_EXT_VER_NUM,
    A2DP_APTX_ADAPTIVE_SUPPORTED_FEATURES,
    A2DP_APTX_ADAPTIVE_FIRST_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_SECOND_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_THIRD_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_FOURTH_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_EOC0,
    A2DP_APTX_ADAPTIVE_EOC1
  },
  BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32,
  {0}
};

/* Default aptX-adaptive R2.1 sink codec configuration */
static const tA2DP_APTX_ADAPTIVE_CIE a2dp_aptx_adaptive_r2_1_default_sink_config = {
  A2DP_APTX_ADAPTIVE_VENDOR_ID,
  A2DP_APTX_ADAPTIVE_CODEC_ID_BLUETOOTH,
  A2DP_APTX_ADAPTIVE_SAMPLERATE_48000,
  A2DP_APTX_ADAPTIVE_SOURCE_TYPE_1,
  A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO,
  {
    A2DP_APTX_ADAPTIVE_TTP_LL_0,
    A2DP_APTX_ADAPTIVE_TTP_LL_1,
    A2DP_APTX_ADAPTIVE_TTP_HQ_0,
    A2DP_APTX_ADAPTIVE_TTP_HQ_1,
    A2DP_APTX_ADAPTIVE_TTP_TWS_0,
    A2DP_APTX_ADAPTIVE_TTP_TWS_1,
    A2DP_APTX_ADAPTIVE_RESERVED_15THBYTE,
    A2DP_APTX_ADAPTIVE_CAP_EXT_VER_NUM,
    A2DP_APTX_ADAPTIVE_R2_1_SUPPORTED_FEATURES,
    A2DP_APTX_ADAPTIVE_FIRST_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_SECOND_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_THIRD_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_FOURTH_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_EOC0,
    A2DP_APTX_ADAPTIVE_EOC1
  },

  BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32,
  {0}
};
/* Default aptX-adaptive R2.2 sink codec configuration */
static const tA2DP_APTX_ADAPTIVE_CIE a2dp_aptx_adaptive_r2_2_default_sink_config = {
  A2DP_APTX_ADAPTIVE_VENDOR_ID,
  A2DP_APTX_ADAPTIVE_CODEC_ID_BLUETOOTH,
  A2DP_APTX_ADAPTIVE_SAMPLERATE_44100,
  A2DP_APTX_ADAPTIVE_SOURCE_TYPE_1,
  A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO,
  {
    A2DP_APTX_ADAPTIVE_TTP_LL_0,
    A2DP_APTX_ADAPTIVE_TTP_LL_1,
    A2DP_APTX_ADAPTIVE_TTP_HQ_0,
    A2DP_APTX_ADAPTIVE_TTP_HQ_1,
    A2DP_APTX_ADAPTIVE_TTP_TWS_0,
    A2DP_APTX_ADAPTIVE_TTP_TWS_1,
    A2DP_APTX_ADAPTIVE_RESERVED_15THBYTE,
    A2DP_APTX_ADAPTIVE_CAP_EXT_VER_NUM,
    A2DP_APTX_ADAPTIVE_R2_2_SUPPORTED_FEATURES,
    A2DP_APTX_ADAPTIVE_FIRST_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_SECOND_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_THIRD_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_FOURTH_SETUP_PREF,
    A2DP_APTX_ADAPTIVE_EOC0,
    A2DP_APTX_ADAPTIVE_EOC1
  },
  BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32,
  {0}
};

tA2DP_APTX_ADAPTIVE_CIE a2dp_aptx_adaptive_caps, a2dp_aptx_adaptive_default_config;

static const tA2DP_DECODER_INTERFACE a2dp_decoder_interface_aptx_adaptive = {
  a2dp_vendor_aptx_adaptive_decoder_init,
  a2dp_vendor_aptx_adaptive_decoder_cleanup,
  a2dp_vendor_aptx_adaptive_decoder_decode_packet,
  nullptr,  // decoder_start
  nullptr,  // decoder_suspend
  nullptr,  // decoder_configure
};

static tA2DP_STATUS A2DP_CodecInfoMatchesCapabilityAptxAdaptive(
  const tA2DP_APTX_ADAPTIVE_CIE* p_cap, const uint8_t* p_codec_info,
  bool is_peer_codec_info);

// Builds the aptX-adaptive Media Codec Capabilities byte sequence beginning from the
// LOSC octet. |media_type| is the media type |AVDT_MEDIA_TYPE_*|.
// |p_ie| is a pointer to the aptX-adaptive Codec Information Element information.
// The result is stored in |p_result|. Returns A2DP_SUCCESS on success,
// otherwise the corresponding A2DP error status code.
static bool A2DP_BuildInfoAptxAdaptive(uint8_t media_type, const tA2DP_APTX_ADAPTIVE_CIE* p_ie,
                                      uint8_t* p_result) {
  if (!p_ie || !p_result) {
    return false;
  }

  *p_result++ = A2DP_APTX_ADAPTIVE_CODEC_LEN;
  *p_result++ = (media_type << 4);
  *p_result++ = A2DP_MEDIA_CT_NON_A2DP;
  *p_result++ = static_cast<uint8_t>(p_ie->vendorId & 0x000000FF);
  *p_result++ = static_cast<uint8_t>((p_ie->vendorId & 0x0000FF00) >> 8);
  *p_result++ = static_cast<uint8_t>((p_ie->vendorId & 0x00FF0000) >> 16);
  *p_result++ = static_cast<uint8_t>((p_ie->vendorId & 0xFF000000) >> 24);
  *p_result++ = static_cast<uint8_t>(p_ie->codecId & 0x00FF);
  *p_result++ = static_cast<uint8_t>((p_ie->codecId & 0xFF00) >> 8);
  *p_result++ = p_ie->sampleRate | p_ie->sourceType;
  *p_result++ = p_ie->channelMode;

  memcpy(p_result, &(p_ie->aptx_data), sizeof(p_ie->aptx_data));
  p_result += sizeof(p_ie->aptx_data);
  memset(p_result, 0x0, sizeof(p_ie->reserved_data));
  p_result += sizeof(p_ie->reserved_data);

  return true;
}

// Parses the aptX-adaptive Media Codec Capabilities byte sequence beginning from the
// LOSC octet. The result is stored in |p_ie|. The byte sequence to parse is
// |p_codec_info|. If |is_capability| is true, the byte sequence is
// codec capabilities, otherwise is codec configuration.
// Returns A2DP_SUCCESS on success, otherwise the corresponding A2DP error
// status code.
static tA2DP_STATUS A2DP_ParseInfoAptxAdaptive(tA2DP_APTX_ADAPTIVE_CIE* p_ie, const uint8_t* p_codec_info,
                                         bool is_capability) {
  uint8_t losc;
  uint8_t media_type;
  tA2DP_CODEC_TYPE codec_type;

  if (!p_ie || !p_codec_info) {
    return AVDTP_UNSUPPORTED_CONFIGURATION;
  }

  // Check the codec capability length
  losc = *p_codec_info++;

  if (losc != A2DP_APTX_ADAPTIVE_CODEC_LEN) {
    log::error("A2DP_APTX_ADAPTIVE_CODEC_LEN fail");
    return AVDTP_UNSUPPORTED_CONFIGURATION;
  }

  media_type = (*p_codec_info++) >> 4;
  codec_type = static_cast<tA2DP_CODEC_TYPE>(*p_codec_info++);
  /* Check the Media Type and Media Codec Type */
  if (media_type != AVDT_MEDIA_TYPE_AUDIO || codec_type != A2DP_MEDIA_CT_NON_A2DP) {
    log::error("A2DP_MEDIA_CT_NON_A2DP ID");
    return AVDTP_UNSUPPORTED_CONFIGURATION;
  }

  p_ie->vendorId =
    (static_cast<uint32_t>(p_codec_info[0])      ) |
    (static_cast<uint32_t>(p_codec_info[1]) << 8 ) |
    (static_cast<uint32_t>(p_codec_info[2]) << 16) |
    (static_cast<uint32_t>(p_codec_info[3]) << 24);
  p_codec_info += 4;

  p_ie->codecId =
    (static_cast<uint16_t>(p_codec_info[0])      ) |
    (static_cast<uint16_t>(p_codec_info[1]) << 8 );
  p_codec_info += 2;

  if (p_ie->vendorId != A2DP_APTX_ADAPTIVE_VENDOR_ID ||
      p_ie->codecId != A2DP_APTX_ADAPTIVE_CODEC_ID_BLUETOOTH) {
      log::error("A2DP_APTX_ADAPTIVE ID WRONG CODEC");
    return AVDTP_UNSUPPORTED_CONFIGURATION;
  }

  p_ie->sampleRate = *p_codec_info & 0xF8;
  p_ie->sourceType = *p_codec_info & 0x07;
  p_codec_info++;

  p_ie->channelMode = *p_codec_info & 0x3F;
  p_codec_info++;

  memcpy(&(p_ie->aptx_data), p_codec_info, sizeof(p_ie->aptx_data));
  p_codec_info += sizeof(p_ie->aptx_data);

  if (is_capability) return A2DP_SUCCESS;

  if (A2DP_BitsSet(p_ie->sampleRate) != A2DP_SET_ONE_BIT)
    return A2DP_INVALID_SAMPLING_FREQUENCY;
  if (A2DP_BitsSet(p_ie->channelMode) != A2DP_SET_ONE_BIT)
    return A2DP_INVALID_CHANNEL_MODE;

  return A2DP_SUCCESS;

}

static bool A2DP_IsVendorCodecAptxAdaptiveEnabled() {
  return A2DP_IsCodecSupported(BTAV_A2DP_CODEC_INDEX_SINK_APTX_ADAPTIVE);
}

bool A2DP_IsCodecValidAptxAdaptive(const uint8_t* p_codec_info) {
  tA2DP_APTX_ADAPTIVE_CIE cfg_cie;
  /* Use a liberal check when parsing the codec info */
  return (A2DP_ParseInfoAptxAdaptive(&cfg_cie, p_codec_info, false) == A2DP_SUCCESS) ||
         (A2DP_ParseInfoAptxAdaptive(&cfg_cie, p_codec_info, true) == A2DP_SUCCESS);
}

// Checks whether A2DP aptX-Adaptive codec configuration matches with a device's
// codec capabilities. |p_cap| is the aptX-Adaptive codec configuration.
// |p_codec_info| is the device's codec capabilities.
// If |is_capability| is true, the byte sequence is codec capabilities,
// otherwise is codec configuration.
// |p_codec_info| contains the codec capabilities for a peer device that
// is acting as an A2DP source.
// Returns A2DP_SUCCESS if the codec configuration matches with capabilities,
// otherwise the corresponding A2DP error status code.
UNUSED_ATTR static tA2DP_STATUS A2DP_CodecInfoMatchesCapabilityAptxAdaptive(
        const tA2DP_APTX_ADAPTIVE_CIE* p_cap, const uint8_t* p_codec_info, bool is_capability) {
  tA2DP_STATUS status;
  tA2DP_APTX_ADAPTIVE_CIE cfg_cie;

  /* parse configuration */
  status = A2DP_ParseInfoAptxAdaptive(&cfg_cie, p_codec_info, is_capability);
  if (status != A2DP_SUCCESS) {
    log::error("parsing failed {}", status);
    return status;
  }

  /* sampling frequency */
  if ((cfg_cie.sampleRate & p_cap->sampleRate) == 0) {
    return A2DP_NOT_SUPPORTED_SAMPLING_FREQUENCY;
  }

  /* channel mode */
  if ((cfg_cie.channelMode & p_cap->channelMode) == 0) {
    return A2DP_NOT_SUPPORTED_CHANNEL_MODE;
  }

  return A2DP_SUCCESS;
}

tA2DP_STATUS A2DP_IsSinkCodecSupportedAptxAdaptive(const uint8_t* p_codec_info) {
  if (!A2DP_IsVendorCodecAptxAdaptiveEnabled()) {
    return A2DP_INVALID_CODEC_TYPE;
  }
  return A2DP_CodecInfoMatchesCapabilityAptxAdaptive(&a2dp_aptx_adaptive_sink_caps, p_codec_info, false);
}

bool A2DP_VendorUsesRtpHeaderAptxAdaptive(bool /* content_protection_enabled */,
                                          const uint8_t* /* p_codec_info */) {
  return true;
}

const char* A2DP_VendorCodecNameAptxAdaptive(const uint8_t* /* p_codec_info */) { return "aptX-Adaptive"; }

bool A2DP_VendorCodecTypeEqualsAptxAdaptive(const uint8_t* p_codec_info_a,
                                      const uint8_t* p_codec_info_b) {
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie_a;
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie_b;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie_a, p_codec_info_a, true);
  if (a2dp_status != A2DP_SUCCESS) {
    log::error("cannot decode codec information: {}", a2dp_status);
    return false;
  }
  a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie_b, p_codec_info_b, true);
  if (a2dp_status != A2DP_SUCCESS) {
    log::error("cannot decode codec information: {}", a2dp_status);
    return false;
  }

  return true;
}

bool A2DP_VendorCodecEqualsAptxAdaptive(const uint8_t* p_codec_info_a, const uint8_t* p_codec_info_b) {
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie_a;
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie_b;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie_a, p_codec_info_a, true);
  if (a2dp_status != A2DP_SUCCESS) {
    log::error("cannot decode codec information: {}", a2dp_status);
    return false;
  }
  a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie_b, p_codec_info_b, true);
  if (a2dp_status != A2DP_SUCCESS) {
    log::error("cannot decode codec information: {}", a2dp_status);
    return false;
  }

  return (aptx_adaptive_cie_a.sampleRate == aptx_adaptive_cie_b.sampleRate) &&
         (aptx_adaptive_cie_a.channelMode == aptx_adaptive_cie_b.channelMode);
}

int A2DP_VendorGetBitRateAptxAdaptive(const uint8_t* p_codec_info) {
  A2dpCodecConfig* CodecConfig = bta_av_get_a2dp_current_codec();
  tA2DP_BITS_PER_SAMPLE bits_per_sample = CodecConfig->getAudioBitsPerSample();
  uint16_t samplerate = A2DP_GetTrackSampleRate(p_codec_info);
  return (samplerate * bits_per_sample * 2) / 4;
}

int A2DP_VendorGetTrackSampleRateAptxAdaptive(const uint8_t* p_codec_info) {
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    log::error("cannot decode codec information: {}", a2dp_status);
    return -1;
  }

  if (aptx_adaptive_cie.sampleRate == A2DP_APTX_ADAPTIVE_SAMPLERATE_44100) return 44100;
  if (aptx_adaptive_cie.sampleRate == A2DP_APTX_ADAPTIVE_SAMPLERATE_48000) return 48000;
  if (aptx_adaptive_cie.sampleRate == A2DP_APTX_ADAPTIVE_SAMPLERATE_96000) return 96000;

  return -1;
}

int A2DP_VendorGetTrackBitsPerSampleAptxAdaptive(const uint8_t* p_codec_info) {
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    log::error("cannot decode codec information: {}", a2dp_status);
    return -1;
  }

  // NOTE: The bits per sample never changes for aptX-Adaptive
  return 32;
}

int A2DP_VendorGetTrackChannelCountAptxAdaptive(const uint8_t* p_codec_info) {
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    log::error("cannot decode codec information: {}", a2dp_status);
    return -1;
  }

  log::debug("channelMode is {}", aptx_adaptive_cie.channelMode);

  switch (aptx_adaptive_cie.channelMode) {
    case A2DP_APTX_ADAPTIVE_CHANNELS_MONO:
      return 1;
    case A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO:
      return 1;
    case A2DP_APTX_ADAPTIVE_CHANNELS_STEREO:
      return 2;
    case A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO:
      return 2;
    case A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO:
    case A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS:
      return 2;
  }

  return -1;
}

bool A2DP_VendorGetPacketTimestampAptxAdaptive(const uint8_t* /* p_codec_info */, const uint8_t* p_data,
                                         uint32_t* p_timestamp) {
  // TODO: Is this function really codec-specific?
  *p_timestamp = *(const uint32_t*)p_data;
  return true;
}

int A2DP_VendorGetTrackChannelTypeAptxAdaptive(const uint8_t* p_codec_info) {
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie;

  // Check whether the codec info contains valid data
  tA2DP_STATUS a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie, p_codec_info, false);
  if (a2dp_status != A2DP_SUCCESS) {
    log::error("cannot decode codec information: {}", a2dp_status);
    return -1;
  }

  switch (aptx_adaptive_cie.channelMode) {
    case A2DP_APTX_ADAPTIVE_CHANNELS_MONO:
    case A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO:
      return 1; // Mono channel
    case A2DP_APTX_ADAPTIVE_CHANNELS_STEREO:
    case A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO:
    case A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO:
    case A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS:
      return 3; // Stereo or TWS channel
  }

  return -1;
}

bool A2DP_VendorBuildCodecHeaderAptxAdaptive(const uint8_t* /* p_codec_info */,
                                       BT_HDR* /* p_buf */,
                                       uint16_t /* frames_per_packet */) {
  // Nothing to do
  return true;
}

std::string A2DP_VendorCodecInfoStringAptxAdaptive(const uint8_t* p_codec_info) {
  std::stringstream res;
  std::string field;
  tA2DP_STATUS a2dp_status;
  tA2DP_APTX_ADAPTIVE_CIE aptx_adaptive_cie;

  a2dp_status = A2DP_ParseInfoAptxAdaptive(&aptx_adaptive_cie, p_codec_info, true);
  if (a2dp_status != A2DP_SUCCESS) {
    res << "A2DP_ParseInfoAptxAdaptive fail: " << loghex(static_cast<uint8_t>(a2dp_status));
    return res.str();
  }

  res << "\tname: aptX-Adaptive\n";

  // Sample frequency
  field.clear();
  AppendField(&field, (aptx_adaptive_cie.sampleRate == 0), "NONE");
  AppendField(&field, (aptx_adaptive_cie.sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100), "44100");
  AppendField(&field, (aptx_adaptive_cie.sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_48000), "48000");
  res << "\tsamp_freq: " << field << " (" << loghex(aptx_adaptive_cie.sampleRate) << ")\n";

  // Channel mode
  field.clear();
  AppendField(&field, (aptx_adaptive_cie.channelMode == 0), "NONE");
  AppendField(&field, (aptx_adaptive_cie.channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_MONO), "Mono");
  AppendField(&field, (aptx_adaptive_cie.channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_STEREO), "Stereo");
  res << "\tch_mode: " << field << " (" << loghex(aptx_adaptive_cie.channelMode) << ")\n";

  return res.str();
}

const tA2DP_DECODER_INTERFACE* A2DP_VendorGetDecoderInterfaceAptxAdaptive(
    const uint8_t* p_codec_info) {
  if (!A2DP_IsCodecValidAptxAdaptive(p_codec_info) ||
    !A2DP_IsVendorCodecAptxAdaptiveEnabled()) {
    return NULL;
  }

  return &a2dp_decoder_interface_aptx_adaptive;
}

bool A2DP_VendorAdjustCodecAptxAdaptive(uint8_t* p_codec_info) {
  tA2DP_APTX_ADAPTIVE_CIE cfg_cie;

  // Nothing to do: just verify the codec info is valid
  if (A2DP_ParseInfoAptxAdaptive(&cfg_cie, p_codec_info, true) != A2DP_SUCCESS) {
    return false;
  }

  return true;
}

btav_a2dp_codec_index_t A2DP_VendorSinkCodecIndexAptxAdaptive(
    const uint8_t* p_codec_info) {
  return BTAV_A2DP_CODEC_INDEX_SINK_APTX_ADAPTIVE;
}

const char* A2DP_VendorCodecIndexStrAptxAdaptiveSink(void) { return "aptX-Adaptive Sink"; }

bool A2DP_VendorInitCodecConfigAptxAdaptiveSink(AvdtpSepConfig* p_cfg) {
  if (!A2DP_IsVendorCodecAptxAdaptiveEnabled()) {
    return false;
  }
  log::debug("aptx-adaptive size is {}", sizeof(a2dp_aptx_adaptive_caps));
  if (!A2DP_BuildInfoAptxAdaptive(AVDT_MEDIA_TYPE_AUDIO, &a2dp_aptx_adaptive_caps, p_cfg->codec_info)) {
    return false;
  }

#if (BTA_AV_CO_CP_SCMS_T == TRUE)
  /* Content protection info - support SCMS-T */
  uint8_t* p = p_cfg->protect_info;
  *p++ = AVDT_CP_LOSC;
  UINT16_TO_STREAM(p, AVDT_CP_SCMS_T_ID);
  p_cfg->num_protect = 1;
#endif

  return true;
}

//
// Selects the best sample rate from |sampleRate|.
// The result is stored in |p_result| and p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_best_sample_rate(uint8_t sampleRate, tA2DP_APTX_ADAPTIVE_CIE* p_result,
                                    btav_a2dp_codec_config_t* p_codec_config) {
  log::debug("Sample rate: {}", sampleRate);
  if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100) {
    p_result->sampleRate = A2DP_APTX_ADAPTIVE_SAMPLERATE_44100;
    p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
    return true;
  }
  if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_48000) {
    p_result->sampleRate = A2DP_APTX_ADAPTIVE_SAMPLERATE_48000;
    p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
    return true;
  }
  if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_96000) {
    p_result->sampleRate = A2DP_APTX_ADAPTIVE_SAMPLERATE_96000;
    p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
    return true;
  }
  return false;
}

//
// Selects the audio sample rate from |p_codec_audio_config|.
// |sampleRate| contains the capability.
// The result is stored in |p_result| and |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_audio_sample_rate(const btav_a2dp_codec_config_t* p_codec_audio_config,
                                     uint8_t sampleRate, tA2DP_APTX_ADAPTIVE_CIE* p_result,
                                     btav_a2dp_codec_config_t* p_codec_config) {
  log::debug("Sample rate: {}", sampleRate);
  switch (p_codec_audio_config->sample_rate) {
    case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
      if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100) {
        p_result->sampleRate = A2DP_APTX_ADAPTIVE_SAMPLERATE_44100;
        p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
      if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_48000) {
        p_result->sampleRate = A2DP_APTX_ADAPTIVE_SAMPLERATE_48000;
        p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_96000:
      if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_96000) {
        p_result->sampleRate = A2DP_APTX_ADAPTIVE_SAMPLERATE_96000;
        p_codec_config->sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_88200:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_176400:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_192000:
    case BTAV_A2DP_CODEC_SAMPLE_RATE_NONE:
    default:
      break;
  }
  return false;
}

//
// Selects the best bits per sample.
// The result is stored in |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_best_bits_per_sample(btav_a2dp_codec_config_t* p_codec_config) {
  p_codec_config->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32;
  return true;
}

//
// Selects the audio bits per sample from |p_codec_audio_config|.
// The result is stored in |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_audio_bits_per_sample(const btav_a2dp_codec_config_t* p_codec_audio_config,
                                         btav_a2dp_codec_config_t* p_codec_config) {
  switch (p_codec_audio_config->bits_per_sample) {
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
       p_codec_config->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24;
       return true;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
       p_codec_config->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32;
       return true;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
      p_codec_config->bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16;
      return true;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE:
      break;
  }
  return false;
}

//
// Selects the best channel mode from |channelMode|.
// The result is stored in |p_result| and |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_best_channel_mode(uint8_t channelMode, tA2DP_APTX_ADAPTIVE_CIE* p_result,
                                     btav_a2dp_codec_config_t* p_codec_config) {
  log::debug("Channel Mode: {}", channelMode);
  if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS) {
    p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS;
    p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
    return true;
  }

  if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_STEREO) {
    p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_STEREO;
    p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
    return true;
  }


  if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO) {
    p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO;
    p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
    return true;
  }

  if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO) {
    p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO;
    p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
    return true;
  }

  if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_MONO) {
    p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_MONO;
    p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
    return true;
  }

  if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO) {
    p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO;
    p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
    return true;
  }
  return false;
}

//
// Selects the audio channel mode from |p_codec_audio_config|.
// |channelMode| contains the capability.
// The result is stored in |p_result| and |p_codec_config|.
// Returns true if a selection was made, otherwise false.
//
static bool select_audio_channel_mode(const btav_a2dp_codec_config_t* p_codec_audio_config,
                                      uint8_t channelMode, tA2DP_APTX_ADAPTIVE_CIE* p_result,
                                      btav_a2dp_codec_config_t* p_codec_config) {
  log::error("p_codec_audio_config->channel_mode: {}, channelMode: {}",
              p_codec_audio_config->channel_mode, channelMode);
  switch (p_codec_audio_config->channel_mode) {
    case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
      if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_MONO) {
        p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_MONO;
        p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
        return true;
      }
      if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO) {
        p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO;
        p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
      if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS) {
        p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS;
        p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
        return true;
      }
      if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_STEREO) {
        p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_STEREO;
        p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
        return true;
      }
      if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO) {
        p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO;
        p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
        return true;
      }
      if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO) {
        p_result->channelMode = A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO;
        p_codec_config->channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
        return true;
      }
      break;
    case BTAV_A2DP_CODEC_CHANNEL_MODE_NONE:
      break;
  }

  return false;
}

tA2DP_STATUS A2dpCodecConfigAptxAdaptiveBase::setCodecConfig(const uint8_t* p_peer_codec_info,
                                                       bool is_capability,
                                                       uint8_t* p_result_codec_config) {
  // Serialize access to codec state.
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);

  tA2DP_APTX_ADAPTIVE_CIE peer_info_cie{};
  tA2DP_APTX_ADAPTIVE_CIE result_config_cie{};
  uint8_t sampleRate = 0;
  uint8_t channelMode = 0;

  // 17th-byte negotiation helpers (keep exact behavior).
  uint8_t src_byts_17th = 0x00, sink_byts_17th = 0x00, byte_negotiated_17th = 0x00;

  // --- Save current internal state so we can restore on failure ---
  btav_a2dp_codec_config_t saved_codec_config                = codec_config_;
  btav_a2dp_codec_config_t saved_codec_capability           = codec_capability_;
  btav_a2dp_codec_config_t saved_codec_selectable_capability = codec_selectable_capability_;
  btav_a2dp_codec_config_t saved_codec_user_config          = codec_user_config_;
  btav_a2dp_codec_config_t saved_codec_audio_config         = codec_audio_config_;

  uint8_t saved_ota_codec_config[AVDT_CODEC_SIZE];
  uint8_t saved_ota_codec_peer_capability[AVDT_CODEC_SIZE];
  uint8_t saved_ota_codec_peer_config[AVDT_CODEC_SIZE];
  memcpy(saved_ota_codec_config, ota_codec_config_, sizeof(ota_codec_config_));
  memcpy(saved_ota_codec_peer_capability, ota_codec_peer_capability_, sizeof(ota_codec_peer_capability_));
  memcpy(saved_ota_codec_peer_config, ota_codec_peer_config_, sizeof(ota_codec_peer_config_));

  // --- Parse peer capability/config ---
  tA2DP_STATUS status = A2DP_ParseInfoAptxAdaptive(&peer_info_cie, p_peer_codec_info, is_capability);
  if (status != A2DP_SUCCESS) {
   log::error("Can't parse peer capabilities: {}", status);
   goto fail;
  }

  // --- Build base of our preferred configuration ---
  memset(&result_config_cie, 0, sizeof(result_config_cie));
  result_config_cie.vendorId = a2dp_aptx_adaptive_caps.vendorId;
  result_config_cie.codecId  = a2dp_aptx_adaptive_caps.codecId;
  log::info("Source cap ext ver num(16th bytes): 0x{:x}", peer_info_cie.aptx_data.cap_ext_ver_num);
  log::info("Source additional supported features(17th-20th bytes): 0x{:x}",
          peer_info_cie.aptx_data.aptx_adaptive_sup_features);
  if (peer_info_cie.aptx_data.cap_ext_ver_num == 0) {
    result_config_cie.aptx_data = a2dp_aptx_adaptive_sink_r1_caps.aptx_data;
    log::info("Select Aptx Adaptive R1 config");
  } else {
  // Negotiate 17th byte of features.
  src_byts_17th  = static_cast<uint8_t>(peer_info_cie.aptx_data.aptx_adaptive_sup_features & 0xFF);
  sink_byts_17th = static_cast<uint8_t>(a2dp_aptx_adaptive_caps.aptx_data.aptx_adaptive_sup_features & 0xFF);
  byte_negotiated_17th =
   static_cast<uint8_t>((((src_byts_17th >> 4) | (sink_byts_17th >> 4)) << 4) |
                        ((src_byts_17th & sink_byts_17th) & 0x0F));

  log::info("Peer byte: 0x{:x}, sink byte: 0x{:x}, negotiated: 0x{:x}",
           src_byts_17th, sink_byts_17th, byte_negotiated_17th);

  // --- Select R2.x profile variant and adjust capabilities accordingly ---
  if (sink_byts_17th & APTX_ADAPTIVE_SINK_R2_2_SUPPORT_CAP &&
        (peer_info_cie.sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100)) {
    log::info("Sink supports R2.2 decoder, and Source supports 44.1k");
    result_config_cie.aptx_data = a2dp_aptx_adaptive_sink_r2_2_caps.aptx_data;
    // Allow 44.1k when R2.2 is available; keep default at 44.1k.
    a2dp_aptx_adaptive_caps.sampleRate |= A2DP_APTX_ADAPTIVE_SAMPLERATE_44100;
    a2dp_aptx_adaptive_default_config.sampleRate = A2DP_APTX_ADAPTIVE_SAMPLERATE_44100;

    codec_config_.codec_specific_3 &= ~(int64_t)APTX_ADAPTIVE_R2_2_SUPPORT_MASK;
    codec_config_.codec_specific_3 |=  (int64_t)APTX_ADAPTIVE_R2_2_SUPPORT_AVAILABLE;
  } else {
    log::info("Not support R2.2 decoder; limit local sample rate caps (use R2.1)");
    a2dp_aptx_adaptive_caps.sampleRate &= ~A2DP_APTX_ADAPTIVE_SAMPLERATE_44100;
    a2dp_aptx_adaptive_default_config.sampleRate = A2DP_APTX_ADAPTIVE_SAMPLERATE_48000;
    result_config_cie.aptx_data = a2dp_aptx_adaptive_sink_r2_1_caps.aptx_data;
    codec_config_.codec_specific_3 &= ~(int64_t) APTX_ADAPTIVE_R2_2_SUPPORT_MASK;
    codec_config_.codec_specific_3 |= (int64_t) APTX_ADAPTIVE_R2_2_SUPPORT_NOT_AVAILABLE;
  }
  result_config_cie.aptx_data.aptx_adaptive_sup_features =
    (peer_info_cie.aptx_data.aptx_adaptive_sup_features & 0xFFFFFF00) | byte_negotiated_17th;

  log::info("Resulting config sub-features: 0x{:x}",
           result_config_cie.aptx_data.aptx_adaptive_sup_features);
  }
  // =========================
  // Sample-rate negotiation
  // =========================
  sampleRate = static_cast<uint8_t>(a2dp_aptx_adaptive_caps.sampleRate & peer_info_cie.sampleRate);
  log::debug("Sample rate: sink caps = 0x{:x}, peer info = 0x{:x}",
            a2dp_aptx_adaptive_caps.sampleRate, peer_info_cie.sampleRate);

  codec_config_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_NONE;
  log::info("codec_user_config_.sample_rate = 0x{:x}", codec_user_config_.sample_rate);

  switch (codec_user_config_.sample_rate) {
   case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
     if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100) {
       result_config_cie.sampleRate  = A2DP_APTX_ADAPTIVE_SAMPLERATE_44100;
       codec_capability_.sample_rate = codec_user_config_.sample_rate;
       codec_config_.sample_rate     = codec_user_config_.sample_rate;
     }
     break;
   case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
     if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_48000) {
       result_config_cie.sampleRate  = A2DP_APTX_ADAPTIVE_SAMPLERATE_48000;
       codec_capability_.sample_rate = codec_user_config_.sample_rate;
       codec_config_.sample_rate     = codec_user_config_.sample_rate;
     }
     break;
   case BTAV_A2DP_CODEC_SAMPLE_RATE_96000:
     if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_96000) {
       result_config_cie.sampleRate  = A2DP_APTX_ADAPTIVE_SAMPLERATE_96000;
       codec_capability_.sample_rate = codec_user_config_.sample_rate;
       codec_config_.sample_rate     = codec_user_config_.sample_rate;
     }
     break;
   case BTAV_A2DP_CODEC_SAMPLE_RATE_88200:
   case BTAV_A2DP_CODEC_SAMPLE_RATE_176400:
   case BTAV_A2DP_CODEC_SAMPLE_RATE_192000:
   case BTAV_A2DP_CODEC_SAMPLE_RATE_NONE:
   default:
     codec_capability_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_NONE;
     codec_config_.sample_rate     = BTAV_A2DP_CODEC_SAMPLE_RATE_NONE;
     break;
  }

  // If no explicit user preference matched, derive from selectable/common/default/best.
  do {
   // Selectable capability
   codec_selectable_capability_.sample_rate = BTAV_A2DP_CODEC_SAMPLE_RATE_NONE;
   if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100)
     codec_selectable_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
   if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_48000)
     codec_selectable_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
   if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_96000)
     codec_selectable_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_96000;

   if (codec_config_.sample_rate != BTAV_A2DP_CODEC_SAMPLE_RATE_NONE) break;

   // Common capability (union we can offer)
   if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100)
     codec_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
   if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_48000)
     codec_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
   if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_96000)
     codec_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_96000;

   // Try codec audio config
   if (select_audio_sample_rate(&codec_audio_config_, sampleRate, &result_config_cie, &codec_config_))
     break;

   // Try default config
   if (select_best_sample_rate(a2dp_aptx_adaptive_default_config.sampleRate & peer_info_cie.sampleRate,
                               &result_config_cie, &codec_config_))
     break;

   // Fallback: best match
   if (select_best_sample_rate(sampleRate, &result_config_cie, &codec_config_))
     break;
  } while (false);

  if (codec_config_.sample_rate == BTAV_A2DP_CODEC_SAMPLE_RATE_NONE) {
   log::error("Cannot match sample frequency: sink caps = 0x{:x}, peer info = 0x{:x}",
              a2dp_aptx_adaptive_caps.sampleRate, peer_info_cie.sampleRate);
   goto fail;
  }

  // =========================
  // Bits-per-sample negotiation
  // =========================
  // NOTE: bits-per-sample is not in the aptX-Adaptive OTA descriptor.
  codec_config_.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;

  switch (codec_user_config_.bits_per_sample) {
   case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
     codec_capability_.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
     codec_config_.bits_per_sample     = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
     break;
   case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
     codec_capability_.bits_per_sample = codec_user_config_.bits_per_sample;
     codec_config_.bits_per_sample     = codec_user_config_.bits_per_sample;
     break;
   case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
   case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE:
     codec_capability_.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
     codec_config_.bits_per_sample     = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE;
     break;
  }

  do {
   // Selectable capability mirrors local caps.
   codec_selectable_capability_.bits_per_sample = a2dp_aptx_adaptive_caps.bits_per_sample;

   if (codec_config_.bits_per_sample != BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE) break;

   // Common capability: this implementation supports 32-bit.
   codec_capability_.bits_per_sample = BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32;

   // Try codec audio config
   if (select_audio_bits_per_sample(&codec_audio_config_, &codec_config_)) break;

   // Try default/best
   if (select_best_bits_per_sample(&codec_config_)) break;

   // (No-op duplicate kept for symmetry with other codecs)
   if (select_best_bits_per_sample(&codec_config_)) break;
  } while (false);

  if (codec_config_.bits_per_sample == BTAV_A2DP_CODEC_BITS_PER_SAMPLE_NONE) {
   log::error("Cannot match bits per sample: user preference = 0x{:x}",
              codec_user_config_.bits_per_sample);
   goto fail;
  }

  // =========================
  // Channel-mode negotiation
  // =========================
  channelMode = static_cast<uint8_t>(a2dp_aptx_adaptive_caps.channelMode & peer_info_cie.channelMode);
  codec_config_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;

  log::debug("codec_user_config_.channel_mode=0x{:x}, channelMode=0x{:x}",
            codec_user_config_.channel_mode, channelMode);

  switch (codec_user_config_.channel_mode) {
   case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
     if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_MONO) {
       result_config_cie.channelMode   = A2DP_APTX_ADAPTIVE_CHANNELS_MONO;
       codec_capability_.channel_mode  = codec_user_config_.channel_mode;
       codec_config_.channel_mode      = codec_user_config_.channel_mode;
       break;
     }
     if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO) {
       result_config_cie.channelMode   = A2DP_APTX_ADAPTIVE_CHANNELS_MONO;
       codec_capability_.channel_mode  = codec_user_config_.channel_mode;
       codec_config_.channel_mode      = codec_user_config_.channel_mode;
       break;
     }
     break;

   case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
     if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS) {
       result_config_cie.channelMode   = A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS;
       codec_capability_.channel_mode  = codec_user_config_.channel_mode;
       codec_config_.channel_mode      = codec_user_config_.channel_mode;
       break;
     }
     if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_STEREO) {
       result_config_cie.channelMode   = A2DP_APTX_ADAPTIVE_CHANNELS_STEREO;
       codec_capability_.channel_mode  = codec_user_config_.channel_mode;
       codec_config_.channel_mode      = codec_user_config_.channel_mode;
       break;
     }
     if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO) {
       result_config_cie.channelMode   = A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO;
       codec_capability_.channel_mode  = codec_user_config_.channel_mode;
       codec_config_.channel_mode      = codec_user_config_.channel_mode;
       break;
     }
     if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO) {
       result_config_cie.channelMode   = A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO;
       codec_capability_.channel_mode  = codec_user_config_.channel_mode;
       codec_config_.channel_mode      = codec_user_config_.channel_mode;
       break;
     }
     break;

   case BTAV_A2DP_CODEC_CHANNEL_MODE_NONE:
     codec_capability_.channel_mode = BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;
     codec_config_.channel_mode     = BTAV_A2DP_CODEC_CHANNEL_MODE_NONE;
     break;
  }

  // If still not decided, compute selectable/common, then try audio/default/best.
  do {
   // Selectable capability
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_MONO)
     codec_selectable_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO)
     codec_selectable_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO)
     codec_selectable_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO)
     codec_selectable_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_STEREO)
     codec_selectable_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS)
     codec_selectable_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;

   if (codec_config_.channel_mode != BTAV_A2DP_CODEC_CHANNEL_MODE_NONE) break;

   // Common capability
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_MONO)
     codec_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO)
     codec_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_STEREO)
     codec_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO)
     codec_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO)
     codec_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
   if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_PLUS)
     codec_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;

   // Try codec audio config
   if (select_audio_channel_mode(&codec_audio_config_, channelMode, &result_config_cie, &codec_config_))
     break;

   // Try default config
   if (select_best_channel_mode(a2dp_aptx_adaptive_default_config.channelMode & peer_info_cie.channelMode,
                                &result_config_cie, &codec_config_))
     break;

   // Fallback: best match
   if (select_best_channel_mode(channelMode, &result_config_cie, &codec_config_))
     break;
  } while (false);

  if (codec_config_.channel_mode == BTAV_A2DP_CODEC_CHANNEL_MODE_NONE) {
   log::error("Cannot match channel mode: source caps = 0x{:x}, peer info = 0x{:x}",
              a2dp_aptx_adaptive_caps.channelMode, peer_info_cie.channelMode);
   goto fail;
  }

  log::debug("Channel mode resolved: source caps = 0x{:x}, peer info = 0x{:x}",
            a2dp_aptx_adaptive_caps.channelMode, peer_info_cie.channelMode);

  // Finalize the resulting CIE.
  result_config_cie.sourceType = a2dp_aptx_adaptive_caps.sourceType;
  memset(result_config_cie.reserved_data, 0, sizeof(result_config_cie.reserved_data));

  // Build the outbound codec info; BuildInfo returns bool in this tree.
  if (!A2DP_BuildInfoAptxAdaptive(AVDT_MEDIA_TYPE_AUDIO, &result_config_cie, p_result_codec_config)) {
   goto fail;
  }

  // Keep codec-specific knobs in sync.
  if (codec_user_config_.codec_specific_2 != codec_config_.codec_specific_2) {
   codec_config_.codec_specific_2 = codec_user_config_.codec_specific_2;
  }
  if (codec_user_config_.codec_specific_3 != codec_config_.codec_specific_3) {
   codec_user_config_.codec_specific_3 = codec_config_.codec_specific_3;
  }

  // Store the negotiated channelMode in spare field (top byte), preserving back-channel bits.
  log::debug("Set codec_config_.codec_specific_4 channelMode: 0x{:x}", channelMode);
  codec_config_.codec_specific_4 &= (int64_t)CHANNEL_MODE_BACK_CHANNEL_MASK;
  codec_config_.codec_specific_4 |= (int64_t)channelMode << 24;

  // Build local copies of peer capability/config and our result for later inspection.
  if (is_capability) {
   log::assert_that(
     A2DP_BuildInfoAptxAdaptive(AVDT_MEDIA_TYPE_AUDIO, &peer_info_cie, ota_codec_peer_capability_),
     "Failed to build media codec capabilities");
  } else {
   log::assert_that(
     A2DP_BuildInfoAptxAdaptive(AVDT_MEDIA_TYPE_AUDIO, &peer_info_cie, ota_codec_peer_config_),
     "Failed to build media codec capabilities");
  }
  log::assert_that(
   A2DP_BuildInfoAptxAdaptive(AVDT_MEDIA_TYPE_AUDIO, &result_config_cie, ota_codec_config_),
   "Failed to build media codec capabilities");

  return A2DP_SUCCESS;

  fail:
  // --- Restore internal state on failure ---
  codec_config_                = saved_codec_config;
  codec_capability_            = saved_codec_capability;
  codec_selectable_capability_ = saved_codec_selectable_capability;
  codec_user_config_           = saved_codec_user_config;
  codec_audio_config_          = saved_codec_audio_config;

  memcpy(ota_codec_config_,          saved_ota_codec_config,          sizeof(ota_codec_config_));
  memcpy(ota_codec_peer_capability_, saved_ota_codec_peer_capability, sizeof(ota_codec_peer_capability_));
  memcpy(ota_codec_peer_config_,     saved_ota_codec_peer_config,     sizeof(ota_codec_peer_config_));

  return status;
}

bool A2dpCodecConfigAptxAdaptiveBase::setPeerCodecCapabilities(
  const uint8_t* p_peer_codec_capabilities) {
  // Serialize access to codec state.
  std::lock_guard<std::recursive_mutex> lock(codec_mutex_);
  tA2DP_APTX_ADAPTIVE_CIE peer_info_cie{};
  uint8_t sampleRate = 0;
  uint8_t channelMode = 0;

  if (is_source_) {
    log::error("Not support aptx adaptive source");
    return false;
  }

  // Select capability set based on source/sink role.
  const tA2DP_APTX_ADAPTIVE_CIE* p_a2dp_aptx_adaptive_cap = &a2dp_aptx_adaptive_sink_caps;

  // Save the internal state for rollback on failure.
  btav_a2dp_codec_config_t saved_codec_selectable_capability = codec_selectable_capability_;
  uint8_t saved_ota_codec_peer_capability[AVDT_CODEC_SIZE];
  memcpy(saved_ota_codec_peer_capability, ota_codec_peer_capability_, sizeof(ota_codec_peer_capability_));

  // Parse peer capabilities.
  tA2DP_STATUS status = A2DP_ParseInfoAptxAdaptive(&peer_info_cie, p_peer_codec_capabilities, true);
  if (status != A2DP_SUCCESS) {
    log::error("Can't parse peer's capabilities: error = {}", status);
    goto fail;
  }

  // Compute selectable capability - sample rate.
  sampleRate = p_a2dp_aptx_adaptive_cap->sampleRate & peer_info_cie.sampleRate;
  if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100) {
    codec_selectable_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
  }
  if (sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_48000) {
    codec_selectable_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
  }

  // Compute selectable capability - bits per sample.
  codec_selectable_capability_.bits_per_sample = p_a2dp_aptx_adaptive_cap->bits_per_sample;

  // Compute selectable capability - channel mode.
  channelMode = p_a2dp_aptx_adaptive_cap->channelMode & peer_info_cie.channelMode;
  if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_MONO) {
    codec_selectable_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
  }
  if (channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_STEREO) {
    codec_selectable_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
  }

  // Build the peer capability info for later inspection.
  log::assert_that(
    A2DP_BuildInfoAptxAdaptive(AVDT_MEDIA_TYPE_AUDIO, &peer_info_cie, ota_codec_peer_capability_),
    "Failed to build media codec capabilities");
  return true;

fail:
  // Restore the internal state on failure.
  codec_selectable_capability_ = saved_codec_selectable_capability;
  memcpy(ota_codec_peer_capability_, saved_ota_codec_peer_capability, sizeof(ota_codec_peer_capability_));
  return false;
}

A2dpCodecConfigAptxAdaptiveSink::A2dpCodecConfigAptxAdaptiveSink(
    btav_a2dp_codec_priority_t codec_priority)
    : A2dpCodecConfigAptxAdaptiveBase(BTAV_A2DP_CODEC_INDEX_SINK_APTX_ADAPTIVE, "aptX-Adaptive Sink",
                      codec_priority, false) {
  // Compute the local capability
  // Using R2_1 by default
  a2dp_aptx_adaptive_caps = a2dp_aptx_adaptive_sink_r2_1_caps;
  a2dp_aptx_adaptive_default_config = a2dp_aptx_adaptive_r2_2_default_sink_config;

  if (a2dp_aptx_adaptive_caps.sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_44100) {
    codec_local_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_44100;
  }
  if (a2dp_aptx_adaptive_caps.sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_48000) {
    codec_local_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_48000;
  }
  if (a2dp_aptx_adaptive_caps.sampleRate & A2DP_APTX_ADAPTIVE_SAMPLERATE_96000) {
    codec_local_capability_.sample_rate |= BTAV_A2DP_CODEC_SAMPLE_RATE_96000;
  }
  codec_local_capability_.bits_per_sample = a2dp_aptx_adaptive_caps.bits_per_sample;
  if (a2dp_aptx_adaptive_caps.channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_MONO) {
    codec_local_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
  }
  if (a2dp_aptx_adaptive_caps.channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_MONO) {
    codec_local_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_MONO;
  }
  if (a2dp_aptx_adaptive_caps.channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_STEREO) {
    codec_local_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
  }

  if (a2dp_aptx_adaptive_caps.channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_JOINT_STEREO) {
    codec_local_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
  }
  if (a2dp_aptx_adaptive_caps.channelMode & A2DP_APTX_ADAPTIVE_CHANNELS_TWS_STEREO) {
    codec_local_capability_.channel_mode |= BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO;
  }
}

A2dpCodecConfigAptxAdaptiveSink::~A2dpCodecConfigAptxAdaptiveSink() {}

bool A2dpCodecConfigAptxAdaptiveSink::init() {
  if (!A2DP_IsVendorCodecAptxAdaptiveEnabled()) return false;

  if (A2DP_GetCodecLocation(BTAV_A2DP_CODEC_INDEX_SINK_APTX_ADAPTIVE) !=
      BTAV_A2DP_CODEC_LOCATION_SOFTWARE) {
    log::info("non software decoder");
    return true;
  }

  // Load the decoder
  if (!A2DP_VendorLoadDecoderAptxAdaptive()) {
    log::error("cannot load the decoder");
    return false;
  }

  return true;
}

bool A2dpCodecConfigAptxAdaptiveSink::useRtpHeaderMarkerBit() const { return false; }
