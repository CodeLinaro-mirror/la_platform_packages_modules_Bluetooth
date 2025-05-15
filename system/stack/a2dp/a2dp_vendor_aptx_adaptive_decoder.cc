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
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

#define LOG_TAG "a2dp_aptx_adaptive_decoder"

#include "a2dp_vendor_aptx_adaptive_decoder.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <iomanip>

#include <base/logging.h>

#include "a2dp_vendor.h"
#include "a2dp_vendor_aptx_adaptive.h"
#include "bt_target.h"
#include "bt_types.h"
#include "osi/include/allocator.h"
#include "osi/include/compat.h"
#include "osi/include/osi.h"
#include "osi/include/allocator.h"
#include "a2dp_vendor_aptx_decoder_auth.h"
#include "aptx-adaptive-decoder-cognitive.h"

#define APTX_COMPRSN_RATIO 4
#define DEC_CHANNELS 2

using namespace bluetooth;

//
// The aptX ADAPTIVE decoder shared library, and the functions to use
//
static const char* APTX_ADAPTIVE_DECODER_COG_LIB_NAME = "/system/lib64/libaptx-adaptive-decoder-cognitive.so";
//static const char* APTX_ADAPTIVE_DECODER_COG_LIB_NAME = "libaptx-adaptive-decoder-cognitive.so";
static const char* APTX_ADAPTIVE_DECODER_LIB_NAME = "/system/lib64/libaptXAdaptiveDec.so";
UNUSED_ATTR static const char* APTX_ADAPTIVE_DECODER3_LIB_NAME = "/system/lib64/libaptXAdaptiveDec3.so";
static void* aptx_adaptive_decoder_lib_handle = nullptr;
UNUSED_ATTR static bool aptx_adaptive_authentication_initiated = false;

using LockGuard = std::lock_guard<std::mutex>;
static std::mutex g_load_aptx_adaptive_mutex;
static std::mutex g_auth_aptx_adaptive_mutex;

// Prepare for aptX ADAPTIVE decoding.
// |state| is a pointer to the memory to save decoded data.
// The memory for |state| shall be allocated at first.
// |endian| indicates endianness of received aptX ADAPTIVE
// encoded data(Big endian/Little endian).
// Return zero on success, otherwise failure.
// This function does not allocate new memory.
static const char* APTX_ADAPTIVE_SET_LOG_NAME = "aptx_adaptive_SetLogFunction";
typedef void (*tAPTX_ADAPTIVE_SET_LOG)(APTX_ADAPTIVE_LOG_FN fn_log, void* context);

// Decode aptX ADAPTIVE encoded data.
// |buffer| is a pointer to aptX ADAPTIVE encoded data.
// |state| is a pointer to save the decoded data.
// It ouputs all zero to |state| if aptX ADAPTIVE decoder is not enabled properly.
// This function does not allocate new memory.
static const char* APTX_ADAPTIVE_GET_DECODER_SIZE_NAME = "aptx_adaptive_GetDecoderSize";
typedef size_t (*tAPTX_ADAPTIVE_GET_DECODER_SIZE)(const char* path);

// Get left channel of PCM data from decoded data |state|.
// Return a pointer to the left channel of PCM data stored in |state|.
// This function does not allocate new memory.
static const char* APTX_ADAPTIVE_CREATE_DECODER_NAME = "aptx_adaptive_CreateDecoder";
typedef APTX_ADAPTIVE_DECODER (*tAPTX_ADAPTIVE_CREATE_DECODER)(void* memory, const APTX_ADAPTIVE_DECODER_CONFIG* config);

// Get right channel of PCM data from decoded data |state|.
// Return a pointer to the right channel of PCM data stored in |state|.
// This function does not allocate new memory.
static const char* APTX_ADAPTIVE_DESTROY_DECODER_NAME = "aptx_adaptive_DestroyDecoder";
typedef void* (*tAPTX_ADAPTIVE_DESTORY_DECODER)(APTX_ADAPTIVE_DECODER decoder);

// Return the memory size of structure for storing decoded data.
// This function does not allocate new memory.
static const char* APTX_ADAPTIVE_RESET_DECODER_NAME = "aptx_adaptive_ResetDecoder";
typedef void (*tAPTX_ADAPTIVE_RESET_DECODER)(APTX_ADAPTIVE_DECODER decoder);

// Return the version of aptX ADAPTIVE software decoder.
// This function does not allocate new memory.
static const char* APTX_ADAPTIVE_DECODE_NAME = "aptx_adaptive_Decode";
typedef size_t (*tAPTX_ADAPTIVE_DECODE)(APTX_ADAPTIVE_DECODER decoder, const void* encoded, size_t octets);

// Return the build of aptX ADAPTIVE software decoder.
// This function does not allocate new memory.
UNUSED_ATTR static const char* APTX_ADAPTIVE_DECODER_BUILD_NAME = "aptxadaptivedec_build";
typedef char* (*tAPTX_ADAPTIVE_DECODER_BUILD)();

// Do the aptX software ADAPTIVE decoder authentication.
// aptX software decoder outputs all zero if authentication fails.
// This function does not allocate new memory.
UNUSED_ATTR static const char* APTX_ADAPTIVE_DECODER_AUTHENTICATE_NAME = "aptxadaptivedec_authenticate";
typedef void (*tAPTX_ADAPTIVE_DECODER_AUTHENTICATE)(const char* platformName, uint32_t token);


tAPTX_ADAPTIVE_SET_LOG aptx_adaptive_set_log_func;
tAPTX_ADAPTIVE_GET_DECODER_SIZE aptx_adaptive_get_decoder_size_func;
tAPTX_ADAPTIVE_CREATE_DECODER aptx_adaptive_create_decoder_func;
tAPTX_ADAPTIVE_DESTORY_DECODER aptx_adaptive_destory_decoder_func;
tAPTX_ADAPTIVE_RESET_DECODER aptx_adaptive_reset_decoder_func;
tAPTX_ADAPTIVE_DECODE aptx_adaptive_decode_func;

typedef struct {
  void* decoder_mem;
  void* aptx_adaptive_xc;
  decoded_data_callback_t decode_callback;
  bool initialized;
} tA2DP_APTX_ADAPTIVE_DECODER_CB;

static tA2DP_APTX_ADAPTIVE_DECODER_CB a2dp_aptx_adaptive_decoder_cb;

// Log callback for aptX Adaptive decoder
static int log_callback(const APTX_ADAPTIVE_LOG_POSITION *pos, const char *format, va_list args) {
  char buffer[256];
  if (!pos || !format) {
    return -1;
  }

  const char *file = pos->file ? pos->file : "[unknown file]";

  int ret = vsnprintf(buffer, sizeof(buffer), format, args);
  if (ret < 0 || ret >= (int)sizeof(buffer)) {
    buffer[sizeof(buffer) - 1] = '\0';
  }

  log::verbose("{} {}: {}: {}", pos->severity, file, pos->line, buffer);
  return 0;
}

// Decoded output callback
static int decodedOutputCallback(const APTX_ADAPTIVE_CALLBACK_INFO info, const APTX_ADAPTIVE_DECODED_OUTPUT* output) {
  (void) info;
  log::verbose("Decoded PCM: {} bytes, timestamp: {}", output->octets, output->timestamp);
  if (a2dp_aptx_adaptive_decoder_cb.decode_callback) {
    a2dp_aptx_adaptive_decoder_cb.decode_callback(reinterpret_cast<uint8_t*>(output->pcm), output->octets);
  }
  return 0;
}

// decoderchange callbak
static int decoderChangeCallback(const APTX_ADAPTIVE_CALLBACK_INFO info, const APTX_ADAPTIVE_CONFIG_CHANGE *config) {
  (void) info;
  log::debug("Decoder config changed: sample_rate={} Hz, channels={}, PLC={}",
          config->sample_rate_hz, config->channels_active, config->plc_active);
  return 0;
}

// Helper to load function from decoder library
static void* load_func(const char* func_name) {
  void* func_ptr = dlsym(aptx_adaptive_decoder_lib_handle, func_name);
  if (!func_ptr) {
    log::error("Cannot find function '{}' in the decoder library: {}", func_name, dlerror());
    A2DP_VendorUnloadDecoderAptxAdaptive();
    return nullptr;
  }
  return func_ptr;
}

// Load aptX Adaptive decoder library and functions
bool A2DP_VendorLoadDecoderAptxAdaptive(void) {
  LockGuard lock(g_load_aptx_adaptive_mutex);

  if (aptx_adaptive_decoder_lib_handle != nullptr) return true;  // Already loaded

  aptx_adaptive_decoder_lib_handle = dlopen(APTX_ADAPTIVE_DECODER_COG_LIB_NAME, RTLD_NOW);
  if (!aptx_adaptive_decoder_lib_handle) {
    log::error("Cannot open aptX ADAPTIVE decoder library {}: {}",
                APTX_ADAPTIVE_DECODER_COG_LIB_NAME, dlerror());
    return false;
  }

  // Below libs requires to exist when creating decoder, otherwise crash happen in aptx-adaptive lib.
  FILE* fp = fopen(APTX_ADAPTIVE_DECODER_LIB_NAME, "rt");
  if (!fp) {
    log::error("unable to open file '{}': {}", APTX_ADAPTIVE_DECODER_LIB_NAME, strerror(errno));
    return false;
  }

  aptx_adaptive_set_log_func = reinterpret_cast<tAPTX_ADAPTIVE_SET_LOG>(load_func(APTX_ADAPTIVE_SET_LOG_NAME));
  aptx_adaptive_get_decoder_size_func = reinterpret_cast<tAPTX_ADAPTIVE_GET_DECODER_SIZE>(load_func(APTX_ADAPTIVE_GET_DECODER_SIZE_NAME));
  aptx_adaptive_create_decoder_func = reinterpret_cast<tAPTX_ADAPTIVE_CREATE_DECODER>(load_func(APTX_ADAPTIVE_CREATE_DECODER_NAME));
  aptx_adaptive_destory_decoder_func = reinterpret_cast<tAPTX_ADAPTIVE_DESTORY_DECODER>(load_func(APTX_ADAPTIVE_DESTROY_DECODER_NAME));
  aptx_adaptive_reset_decoder_func = reinterpret_cast<tAPTX_ADAPTIVE_RESET_DECODER>(load_func(APTX_ADAPTIVE_RESET_DECODER_NAME));
  aptx_adaptive_decode_func = reinterpret_cast<tAPTX_ADAPTIVE_DECODE>(load_func(APTX_ADAPTIVE_DECODE_NAME));

  if (!aptx_adaptive_set_log_func || !aptx_adaptive_get_decoder_size_func ||
      !aptx_adaptive_create_decoder_func || !aptx_adaptive_destory_decoder_func ||
      !aptx_adaptive_reset_decoder_func || !aptx_adaptive_decode_func) {
    A2DP_VendorUnloadDecoderAptxAdaptive();
    return false;
  }

  log::debug("aptX ADAPTIVE is loaded successfully.");

  return true;
}

// Unload aptX Adaptive decoder library
void A2DP_VendorUnloadDecoderAptxAdaptive(void) {
  if (aptx_adaptive_decoder_lib_handle) {
    dlclose(aptx_adaptive_decoder_lib_handle);
    aptx_adaptive_decoder_lib_handle = nullptr;
  }
  aptx_adaptive_set_log_func = nullptr;
  aptx_adaptive_get_decoder_size_func = nullptr;
  aptx_adaptive_create_decoder_func = nullptr;
  aptx_adaptive_destory_decoder_func = nullptr;
  aptx_adaptive_reset_decoder_func = nullptr;
  aptx_adaptive_decode_func = nullptr;
}

// Initialize aptX Adaptive decoder
bool a2dp_vendor_aptx_adaptive_decoder_init(decoded_data_callback_t decode_callback) {
  if (a2dp_aptx_adaptive_decoder_cb.initialized) {
    a2dp_vendor_aptx_adaptive_decoder_cleanup();
  }

  log::debug("Initializing aptX Adaptive decoder");

  // Set log callback
  aptx_adaptive_set_log_func(log_callback, nullptr);

  // Obtain decoder size and allocate memory
  size_t decoder_size = static_cast<size_t>(aptx_adaptive_get_decoder_size_func(nullptr));
  a2dp_aptx_adaptive_decoder_cb.decoder_mem = osi_malloc(decoder_size);
  if (!a2dp_aptx_adaptive_decoder_cb.decoder_mem) {
    log::error("Decoder allocation failed");
    return false;
  }

  // Configure decoder
  APTX_ADAPTIVE_DECODER_CONFIG config = {};
  config.input.profile = nullptr;
  config.input.sample_rate_hz = 48000;
  config.output.duplicate_mono = false;
  config.callback.context = nullptr;
  config.callback.fn_decodedOutput = decodedOutputCallback;
  config.callback.fn_decoderChange = decoderChangeCallback;

  a2dp_aptx_adaptive_decoder_cb.aptx_adaptive_xc =
    (void*)aptx_adaptive_create_decoder_func(a2dp_aptx_adaptive_decoder_cb.decoder_mem, &config);

  if (!a2dp_aptx_adaptive_decoder_cb.aptx_adaptive_xc) {
    log::error("Failed to initialize aptX ADAPTIVE decoder!");
    osi_free(a2dp_aptx_adaptive_decoder_cb.decoder_mem);
    a2dp_aptx_adaptive_decoder_cb.decoder_mem = nullptr;
    return false;
  }

  a2dp_aptx_adaptive_decoder_cb.decode_callback = decode_callback;
  a2dp_aptx_adaptive_decoder_cb.initialized = true;

  return true;
}

// Cleanup aptX Adaptive decoder resources
void a2dp_vendor_aptx_adaptive_decoder_cleanup(void) {
  if (!a2dp_aptx_adaptive_decoder_cb.decoder_mem) {
    osi_free(a2dp_aptx_adaptive_decoder_cb.decoder_mem);
    a2dp_aptx_adaptive_decoder_cb.decoder_mem = nullptr;
  }

  if (!a2dp_aptx_adaptive_decoder_cb.aptx_adaptive_xc) {
    osi_free(a2dp_aptx_adaptive_decoder_cb.aptx_adaptive_xc);
    a2dp_aptx_adaptive_decoder_cb.aptx_adaptive_xc = nullptr;
  }

  memset(&a2dp_aptx_adaptive_decoder_cb, 0, sizeof(a2dp_aptx_adaptive_decoder_cb));
  a2dp_aptx_adaptive_decoder_cb.initialized = false;
}

// Decode aptX Adaptive packet
bool a2dp_vendor_aptx_adaptive_decoder_decode_packet(BT_HDR* p_buf) {
  log::debug("Decoding aptX Adaptive packet");

  uint8_t* p_buffer = p_buf->data;
  int32_t bytes_valid = static_cast<int32_t>(p_buf->len + A2DP_APTX_ADAPTIVE_RTP_HEADER_LEN);

  int consumed = aptx_adaptive_decode_func(
    reinterpret_cast<uintptr_t>(a2dp_aptx_adaptive_decoder_cb.aptx_adaptive_xc),
    p_buffer, bytes_valid);

  log::debug("bytes_valid = {}, consumed = {}", bytes_valid, consumed);

  return true;
}
