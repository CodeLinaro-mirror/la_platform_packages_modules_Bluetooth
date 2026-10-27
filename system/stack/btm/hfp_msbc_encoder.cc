/*
 * Copyright (C) 2022 The Android Open Source Project
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
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "hfp_msbc_encoder"

#include "hfp_msbc_encoder.h"

#include <cstdint>
#include <cstring>

#include "embdrv/sbc/encoder/include/sbc_encoder.h"

extern uint32_t SBC_Encode(SBC_ENC_PARAMS* pstrEncParams, int16_t* input, uint8_t* output);

typedef struct {
  SBC_ENC_PARAMS sbc_encoder_params;
} tHFP_MSBC_ENCODER;

static tHFP_MSBC_ENCODER hfp_msbc_encoder = {};

void hfp_msbc_encoder_init(void) {
  SBC_ENC_PARAMS* p_encoder_params = &hfp_msbc_encoder.sbc_encoder_params;
  p_encoder_params->s16SamplingFreq = SBC_sf16000;
  p_encoder_params->s16ChannelMode = SBC_MONO;
  p_encoder_params->s16NumOfSubBands = 8;
  p_encoder_params->s16NumOfChannels = 1;
  p_encoder_params->s16NumOfBlocks = 15;
  p_encoder_params->s16AllocationMethod = SBC_LOUDNESS;
  p_encoder_params->s16BitPool = 26;
  p_encoder_params->Format = SBC_FORMAT_MSBC;
}

void hfp_msbc_encoder_cleanup(void) { hfp_msbc_encoder = {}; }

uint32_t hfp_msbc_encode_frames(int16_t* /*input*/, uint8_t* /*output*/) {
  return 0;//SBC_Encode(&hfp_msbc_encoder.sbc_encoder_params, input, output);// to be fixed
}
