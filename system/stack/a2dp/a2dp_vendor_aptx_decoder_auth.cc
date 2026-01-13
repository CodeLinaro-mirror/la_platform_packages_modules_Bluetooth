/*
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *     Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *     Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.

 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries..
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */


#define LOG_TAG "a2dp_vendor_aptx_authenticaiton"

#include <string.h>
#include <time.h>
#include <queue>
#include <base/logging.h>
#include "stack/btm/internal/btm_api.h"
#include "bt_target.h"
#include "bt_types.h"
#include "osi/include/allocator.h"
#include "osi/include/compat.h"
#include "osi/include/alarm.h"
#include "osi/include/properties.h"
#include "a2dp_vendor_aptx_decoder_auth.h"

#define HCI_VS_OPCODE_GENERATE_TOKEN_FOR_APTX (0xFC34)
#define HCI_VS_SUBOPCODE_GENERATE_TOKEN_FOR_APTX (0x01)

#define TIME_LENGTH                   ((int32_t)sizeof("YYYYMMDDHHMMSS") + 1)
#define PLATFORM_ID_LENGTH            (PROPERTY_VALUE_MAX)
#define PLATFORM_STATE_STRING_LENGHT  (TIME_LENGTH + PLATFORM_ID_LENGTH)
#define SUB_OPCODE_LENGTH             (1)
#define PARAMETER_LENGTH_LENGTH       (1)
#define INPUT_STRING_LENGTH           (PLATFORM_STATE_STRING_LENGHT + SUB_OPCODE_LENGTH + PARAMETER_LENGTH_LENGTH)

#define PLATFORM_ID_8155P_ORIGN        ("msmnile")
#define PLATFORM_ID_8155P_TRASFORMED   ("SA8155P")
#define PLATFORM_ID_6155P_ORIGN        ("sm6150")
#define PLATFORM_ID_6155P_TRASFORMED   ("SA6155P")
#define PLATFORM_ID_8195P_ORIGN        ("sdmshrike")
#define PLATFORM_ID_8195P_TRASFORMED   ("SA8195P")
#define PLATFORM_ID_QSSI_ORIGIN        ("qssi")
#define PLATFORM_ID_QSSI_TRASFORMED    ("qssi")

char PLATFORM_ID[][PLATFORM_ID_LENGTH] = {"msmnile",   "SA8155P",
                                          "sm6150",    "SA6155P",
                                          "sdmshrike", "SA8195P",
                                          "qssi",      "SA8155P",
                                          "gen4",      "SA8155P",
                                          "gen5_gvm_gy", "SA8155P",
                                          "canoe",     "SA8155P"};

#define INVALID_TOKEN_KEY  (0)

using namespace bluetooth;

using LockGuard = std::lock_guard<std::mutex>;
static std::mutex g_mutex;

static void token_key_generate_vsc_op_cmpl(tBTM_VSC_CMPL* p_params);
static bool is_token_key_supported(void);
static bool get_platform_id();
static void get_time();

typedef struct {
  char  time[TIME_LENGTH];
  char  platform_id[PLATFORM_ID_LENGTH];
  std::queue<tA2DP_VENDOR_APTX_DECODER_AUTH_CB> callback;
} tA2DP_TOKEN_KEY_CB;

tA2DP_TOKEN_KEY_CB a2dp_token_key_cb ={"", "", std::queue<tA2DP_VENDOR_APTX_DECODER_AUTH_CB>()};

void token_key_init(tA2DP_VENDOR_APTX_DECODER_AUTH_CB callback) {
  LockGuard lock(g_mutex);
  log::info("");

  a2dp_token_key_cb.callback.push(callback);
}

/*
 * HCI_VS_LICENSED_FEATURE_CONTROL_CMD_OPCODE_GENERATE_TOKEN_FOR_APTX
 * offset  Field                        Size      Value
 * 0       OpCode                       2         0xFC34
 * 2       Parameter Length             1         0x00-0xFF
 * 3       SubOpcode                    1         0x01
 * 4       Platform_Date_String_Length  1         0x00-0xFF
 * 5       Platform_Date_String         variable  variable
 */
bool token_key_generate() {
  LockGuard lock(g_mutex);
  log::info("");

  if (!get_platform_id() || !is_token_key_supported()) {
    if (a2dp_token_key_cb.callback.empty()) {
      log::warn("Callback queue is empty");
      return false;
    }

    a2dp_token_key_cb.callback.pop();
    return false;
  }
  get_time();

  char input_string[INPUT_STRING_LENGTH];
  memset(input_string, 0, sizeof(input_string));

  int8_t sub_opcode = 1;
  int8_t platform_string_length = (int8_t)strlen(a2dp_token_key_cb.time)
                                  + (int8_t)strlen(a2dp_token_key_cb.platform_id);
  int8_t input_string_length = platform_string_length + (int8_t)SUB_OPCODE_LENGTH
                               + (int8_t)PARAMETER_LENGTH_LENGTH;

  input_string[0] = sub_opcode;
  input_string[1] = platform_string_length;

  strlcat(input_string, a2dp_token_key_cb.platform_id, INPUT_STRING_LENGTH);
  strlcat(input_string, a2dp_token_key_cb.time, INPUT_STRING_LENGTH);

  log::debug("token_key_generate: {}, strlen:{}", input_string, input_string_length);

  BTM_VendorSpecificCommand(HCI_VS_OPCODE_GENERATE_TOKEN_FOR_APTX, input_string_length,
                            (uint8_t *)input_string, token_key_generate_vsc_op_cmpl);

  return true;
}

static void token_key_generate_vsc_op_cmpl(tBTM_VSC_CMPL* param) {
  LockGuard lock(g_mutex);
  log::info("");

  uint8_t status = 0;
  uint8_t sub_opcode = 0;
  if (param->param_len < 0) {
    log::error("param_len = {} status = {}", param->param_len, param->p_param_buf[0]);
    status = param->p_param_buf[0];
  }

  if (a2dp_token_key_cb.callback.empty()) {
    log::warn("Callback queue is empty");
    return;
  }
  tA2DP_VENDOR_APTX_DECODER_AUTH_CB callback = a2dp_token_key_cb.callback.front();

  if (status == 0) {
    sub_opcode = param->p_param_buf[1];
    log::debug("subopcode = {}", sub_opcode);

    switch (sub_opcode) {
      case HCI_VS_SUBOPCODE_GENERATE_TOKEN_FOR_APTX:
        log::debug("Token key is generated successfully {}",
                   *((int32_t *)(param->p_param_buf+2)));
        callback(a2dp_token_key_cb.platform_id, *((int32_t *)(param->p_param_buf+2)));
        break;

      default:
        log::error("Fail to generate token key");
        break;
    }
  } else {
    log::debug("Fail to generate token key status {}",  status);
  }

  a2dp_token_key_cb.callback.pop();
}

static bool is_token_key_supported(void) {
  char value[PROPERTY_VALUE_MAX] = {0};
  osi_property_get("vendor.bt.a2dp.token_key", value, "true");

  return strcmp(value, "true") == 0 ? true : false;
}


// Retrieve platform name through the property
// "ro.product.device". E.g.
// (1) msmnile_au
// Target platform is SA8155P
// (2) msmnile_gvmq
// Target platform is SA8155P running in Hypervisor
static bool get_platform_id() {
  osi_property_get("ro.product.device", a2dp_token_key_cb.platform_id, "null");
  int PLATFORM_ID_NUM = sizeof(PLATFORM_ID) / sizeof(PLATFORM_ID[0]);

  for (int i = 0; i < PLATFORM_ID_NUM; i ++) {
    if (strncasecmp(a2dp_token_key_cb.platform_id, PLATFORM_ID[i*2],
        strlen(PLATFORM_ID[i*2])) == 0) {
        memset(a2dp_token_key_cb.platform_id, 0, PLATFORM_ID_LENGTH);
        strlcpy(a2dp_token_key_cb.platform_id, PLATFORM_ID[i*2 + 1],
                strlen(PLATFORM_ID[i*2 + 1]) + 1);
        return true;
    }
  }

  log::debug("platform_id = {}",  a2dp_token_key_cb.platform_id);
  return false;
}

static void get_time() {
  const char* TIME_STRING_FORMAT = "%Y%m%d%H%M%S";
  time_t current_time = time(NULL);
  char time_now[TIME_LENGTH];
  struct tm* time_created = localtime(&current_time);
  strftime(time_now, TIME_LENGTH, TIME_STRING_FORMAT,
           time_created);

  memset(a2dp_token_key_cb.time, 0, TIME_LENGTH);
  strlcat(a2dp_token_key_cb.time, time_now, TIME_LENGTH);

  log::debug("time = {}",  a2dp_token_key_cb.time);
}
