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
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#include "a2dp_codec_api.h"
#include "btif/include/btif_av_co.h"
#include "types/raw_address.h"

A2dpCodecConfig* bta_av_get_a2dp_current_codec(void) { return nullptr; }

void setA2dpSourceEncoders(const RawAddress& peer_address, A2dpEncoderInterface* encoder) {
}

A2dpEncoderInterface* findA2dpSourceEncoder(const RawAddress& peer_address) {
  return nullptr;
}

uint32_t btif_a2dp_source_read_callback(const RawAddress& peer_address,
                                                  uint8_t* p_buf, uint32_t len) {
  return 0;
}
