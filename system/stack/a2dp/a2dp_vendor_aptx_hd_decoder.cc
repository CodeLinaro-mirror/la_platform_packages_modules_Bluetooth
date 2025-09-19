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

#define LOG_TAG "a2dp_aptx_hd_decoder"

#include "a2dp_vendor_aptx_hd_decoder.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <base/logging.h>

#include "a2dp_vendor.h"
#include "a2dp_vendor_aptx_hd.h"
#include "bt_target.h"
#include "bt_types.h"
#include "osi/include/allocator.h"
#include "osi/include/compat.h"
#include "osi/include/osi.h"
#include "a2dp_vendor_aptx_decoder_auth.h"

#define APTX_COMPRSN_RATIO 4
#define DEC_CHANNELS 2

using namespace bluetooth;

//
// The aptX HD decoder shared library, and the functions to use
//
static const char* APTX_HD_DECODER_LIB_NAME = "/system/lib64/libaptX_HD_decoder.so";
static void* aptx_hd_decoder_lib_handle = NULL;
static bool aptx_hd_authentication_initiated = false;

using LockGuard = std::lock_guard<std::mutex>;
static std::mutex g_load_aptx_hd_mutex;
static std::mutex g_auth_aptx_hd_mutex;

// Prepare for aptX HD decoding.
// |state| is a pointer to the memory to save decoded data.
// The memory for |state| shall be allocated at first.
// |endian| indicates endianness of received aptX HD
// encoded data(Big endian/Little endian).
// Return zero on success, otherwise failure.
// This function does not allocate new memory.
static const char* APTX_HD_DECODER_INIT_NAME = "aptxhddec_init";
typedef int32_t (*tAPTX_HD_DECODER_INIT)(void* state, int16_t endian);

// Decode aptX HD encoded data.
// |buffer| is a pointer to aptX HD encoded data.
// |state| is a pointer to save the decoded data.
// It ouputs all zero to |state| if aptX HD decoder is not enabled properly.
// This function does not allocate new memory.
static const char* APTX_HD_DECODER_STEREO_DECODE_NAME = "aptxhddecStereoDecode";
typedef void (*tAPTX_HD_DECODER_STEREO_DECODE)(int32_t* buffer, void* state);

// Get left channel of PCM data from decoded data |state|.
// Return a pointer to the left channel of PCM data stored in |state|.
// This function does not allocate new memory.
static const char* APTX_HD_DECODER_GET_DEC_PCML_NAME = "gethdDecPcmL";
typedef int32_t* (*tAPTX_HD_DECODER_GET_DEC_PCML)(void* _state);

// Get right channel of PCM data from decoded data |state|.
// Return a pointer to the right channel of PCM data stored in |state|.
// This function does not allocate new memory.
static const char* APTX_HD_DECODER_GET_DEC_PCMR_NAME = "gethdDecPcmR";
typedef int32_t* (*tAPTX_HD_DECODER_GET_DEC_PCMR)(void* _state);

// Return the memory size of structure for storing decoded data.
// This function does not allocate new memory.
static const char* APTX_HD_DECODER_SIZEOF_PARAMS_NAME = "sizeofAptxhddec";
typedef int32_t (*tAPTX_HD_DECODER_SIZEOF_PARAMS)(void);

// Return the version of aptX HD software decoder.
// This function does not allocate new memory.
static const char* APTX_HD_DECODER_VERSION_NAME = "aptxhddec_version";
typedef char* (*tAPTX_HD_DECODER_VERSION)();

// Return the build of aptX HD software decoder.
// This function does not allocate new memory.
static const char* APTX_HD_DECODER_BUILD_NAME = "aptxhddec_build";
typedef char* (*tAPTX_HD_DECODER_BUILD)();

// Do the aptX software HD decoder authentication.
// aptX software decoder outputs all zero if authentication fails.
// This function does not allocate new memory.
static const char* APTX_HD_DECODER_AUTHENTICATE_NAME = "aptxhddec_authenticate";
typedef void (*tAPTX_HD_DECODER_AUTHENTICATE)(const char* platformName, uint32_t token);


tAPTX_HD_DECODER_INIT aptx_hd_decoder_init_func;
tAPTX_HD_DECODER_STEREO_DECODE aptx_hd_decoder_decode_stereo_func;
tAPTX_HD_DECODER_GET_DEC_PCML aptx_hd_decoder_get_pcml_func;
tAPTX_HD_DECODER_GET_DEC_PCMR aptx_hd_decoder_get_pcmr_func;
tAPTX_HD_DECODER_SIZEOF_PARAMS aptx_hd_decoder_sizeof_params_func;
tAPTX_HD_DECODER_VERSION aptx_hd_decoder_version_func;
tAPTX_HD_DECODER_BUILD aptx_hd_decoder_build_func;
tAPTX_HD_DECODER_AUTHENTICATE aptx_hd_decoder_authenticate_func;

typedef struct {
  void* aptx_hd_xc;
  int32_t* encoded_buffer;
  uint8_t* decode_buf;
  decoded_data_callback_t decode_callback;
  bool initialized;
} tA2DP_APTX_HD_DECODER_CB;

static tA2DP_APTX_HD_DECODER_CB a2dp_aptx_hd_decoder_cb;

static void a2dp_vendor_aptx_hd_decoder_authenticate(const char *platformName, uint32_t token);

bool A2DP_VendorLoadDecoderAptxHd(void) {
  LockGuard lock(g_load_aptx_hd_mutex);

  if (aptx_hd_decoder_lib_handle != NULL) return true;  // Already loaded

  aptx_hd_decoder_lib_handle = dlopen(APTX_HD_DECODER_LIB_NAME, RTLD_NOW);
  if (aptx_hd_decoder_lib_handle == nullptr) {
    log::error("cannot open aptX HD decoder library {}: {}",
              APTX_HD_DECODER_LIB_NAME, dlerror());
    return false;
  }

  aptx_hd_decoder_init_func = (tAPTX_HD_DECODER_INIT)dlsym(aptx_hd_decoder_lib_handle,
                                                     APTX_HD_DECODER_INIT_NAME);
  if (aptx_hd_decoder_init_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_HD_DECODER_INIT_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptxHd();
    return false;
  }

  aptx_hd_decoder_decode_stereo_func = (tAPTX_HD_DECODER_STEREO_DECODE)dlsym(aptx_hd_decoder_lib_handle,
                                                              APTX_HD_DECODER_STEREO_DECODE_NAME);
  if (aptx_hd_decoder_decode_stereo_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_HD_DECODER_STEREO_DECODE_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptxHd();
    return false;
  }

  aptx_hd_decoder_get_pcml_func = (tAPTX_HD_DECODER_GET_DEC_PCML)dlsym(aptx_hd_decoder_lib_handle,
                                                         APTX_HD_DECODER_GET_DEC_PCML_NAME);
  if (aptx_hd_decoder_get_pcml_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_HD_DECODER_GET_DEC_PCML_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptxHd();
    return false;
  }

  aptx_hd_decoder_get_pcmr_func = (tAPTX_HD_DECODER_GET_DEC_PCMR)dlsym(aptx_hd_decoder_lib_handle,
                                                         APTX_HD_DECODER_GET_DEC_PCMR_NAME);
  if (aptx_hd_decoder_get_pcmr_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_HD_DECODER_GET_DEC_PCMR_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptxHd();
    return false;
  }

  aptx_hd_decoder_sizeof_params_func = (tAPTX_HD_DECODER_SIZEOF_PARAMS)dlsym(aptx_hd_decoder_lib_handle,
                                                   APTX_HD_DECODER_SIZEOF_PARAMS_NAME);
  if (aptx_hd_decoder_sizeof_params_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_HD_DECODER_SIZEOF_PARAMS_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptxHd();
    return false;
  }

  aptx_hd_decoder_version_func = (tAPTX_HD_DECODER_VERSION)dlsym(aptx_hd_decoder_lib_handle,
                                                     APTX_HD_DECODER_VERSION_NAME);
  if (aptx_hd_decoder_version_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_HD_DECODER_VERSION_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptxHd();
    return false;
  }

  aptx_hd_decoder_build_func = (tAPTX_HD_DECODER_BUILD)dlsym(aptx_hd_decoder_lib_handle,
                                                     APTX_HD_DECODER_BUILD_NAME);
  if (aptx_hd_decoder_build_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_HD_DECODER_BUILD_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptxHd();
    return false;
  }

  aptx_hd_decoder_authenticate_func = (tAPTX_HD_DECODER_AUTHENTICATE)dlsym(aptx_hd_decoder_lib_handle,
                                                     APTX_HD_DECODER_AUTHENTICATE_NAME);
  if (aptx_hd_decoder_authenticate_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_HD_DECODER_AUTHENTICATE_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptxHd();
    return false;
  }

  char *aptx_version = aptx_hd_decoder_version_func();
  char *aptx_build = aptx_hd_decoder_build_func();

  log::debug("aptX HD is loaded successfully.\n{}\n{}", aptx_version, aptx_build);

  return true;
}

bool A2DP_VendorAuthenticateAptxHd() {
  LockGuard lock(g_auth_aptx_hd_mutex);

  if (aptx_hd_authentication_initiated) return true; // Already started

  bool status = true;
  token_key_init(a2dp_vendor_aptx_hd_decoder_authenticate);
  status = token_key_generate();

  aptx_hd_authentication_initiated = true;

  return status;
}

void A2DP_VendorUnloadDecoderAptxHd(void) {
    if (aptx_hd_decoder_lib_handle != nullptr) {
      dlclose(aptx_hd_decoder_lib_handle);
      aptx_hd_decoder_lib_handle = nullptr;
    }

    aptx_hd_decoder_init_func = nullptr;

    aptx_hd_decoder_decode_stereo_func = nullptr;

    aptx_hd_decoder_get_pcml_func = nullptr;
    aptx_hd_decoder_get_pcmr_func = nullptr;

    aptx_hd_decoder_sizeof_params_func = nullptr;

    aptx_hd_decoder_version_func = nullptr;
    aptx_hd_decoder_build_func = nullptr;

    aptx_hd_decoder_authenticate_func = nullptr;
}

bool a2dp_vendor_aptx_hd_decoder_init(decoded_data_callback_t decode_callback) {
  if (a2dp_aptx_hd_decoder_cb.initialized) {
    a2dp_vendor_aptx_hd_decoder_cleanup();
  }

  a2dp_aptx_hd_decoder_cb.aptx_hd_xc = osi_malloc((size_t) aptx_hd_decoder_sizeof_params_func());
  // initialize the decoder structures for big endian operation
  int32_t result = aptx_hd_decoder_init_func(a2dp_aptx_hd_decoder_cb.aptx_hd_xc, 0);

  if(result != 0) {
    log::error("Fail to to initialize aptX HD decoder!", __func__);
    osi_free(a2dp_aptx_hd_decoder_cb.aptx_hd_xc);
    return result;
  }

  a2dp_aptx_hd_decoder_cb.encoded_buffer = reinterpret_cast<int32_t*>(osi_malloc(sizeof(int32_t) * 2));
  a2dp_aptx_hd_decoder_cb.decode_buf = reinterpret_cast<uint8_t*>(osi_malloc(BT_DEFAULT_BUFFER_SIZE));

  a2dp_aptx_hd_decoder_cb.decode_callback = decode_callback;

  a2dp_aptx_hd_decoder_cb.initialized = true;

  return true;
}

void a2dp_vendor_aptx_hd_decoder_cleanup(void) {
  osi_free(a2dp_aptx_hd_decoder_cb.aptx_hd_xc);

  osi_free(a2dp_aptx_hd_decoder_cb.encoded_buffer);
  osi_free(a2dp_aptx_hd_decoder_cb.decode_buf);

  memset(&a2dp_aptx_hd_decoder_cb, 0, sizeof(a2dp_aptx_hd_decoder_cb));

  a2dp_aptx_hd_decoder_cb.initialized = false;
}

bool a2dp_vendor_aptx_hd_decoder_decode_packet(BT_HDR* p_buf) {
  uint8_t* p_buffer = p_buf->data + p_buf->offset;
  int32_t bytes_valid = static_cast<int32_t>(p_buf->len);

  uint16_t buf_idx   = 0;
  uint16_t pcm_idx   = 0;
  size_t   frame_len = 0;

  memset(a2dp_aptx_hd_decoder_cb.decode_buf, 0, BT_DEFAULT_BUFFER_SIZE);
  memset(a2dp_aptx_hd_decoder_cb.encoded_buffer, 0, sizeof(int32_t) * 2);

  while (bytes_valid >= 6 ) {
    // Get encoded buffer
    uint8_t* encoded_ptr = reinterpret_cast<uint8_t*>(a2dp_aptx_hd_decoder_cb.encoded_buffer);
    *(encoded_ptr)     = *(p_buffer + 2);
    *(encoded_ptr + 1) = *(p_buffer + 1);
    *(encoded_ptr + 2) = *(p_buffer);
    *(encoded_ptr + 4) = *(p_buffer + 5);
    *(encoded_ptr + 5) = *(p_buffer + 4);
    *(encoded_ptr + 6) = *(p_buffer + 3);

    aptx_hd_decoder_decode_stereo_func(a2dp_aptx_hd_decoder_cb.encoded_buffer, a2dp_aptx_hd_decoder_cb.aptx_hd_xc);

    int32_t *pcm_lc = aptx_hd_decoder_get_pcml_func(a2dp_aptx_hd_decoder_cb.aptx_hd_xc);
    int32_t *pcm_rc = aptx_hd_decoder_get_pcmr_func(a2dp_aptx_hd_decoder_cb.aptx_hd_xc);

    for (pcm_idx = 0; pcm_idx < 4; pcm_idx++, buf_idx += 6) {
      int32_t lc = pcm_lc[pcm_idx];
      int32_t rc = pcm_rc[pcm_idx];
      a2dp_aptx_hd_decoder_cb.decode_buf[buf_idx]     = static_cast<uint8_t>((lc >> 8) & 0xFF);
      a2dp_aptx_hd_decoder_cb.decode_buf[buf_idx + 1] = static_cast<uint8_t>((lc >> 16) & 0xFF);
      a2dp_aptx_hd_decoder_cb.decode_buf[buf_idx + 2] = static_cast<uint8_t>((lc >> 24) & 0xFF);
      a2dp_aptx_hd_decoder_cb.decode_buf[buf_idx + 3] = static_cast<uint8_t>((rc >> 8) & 0xFF);
      a2dp_aptx_hd_decoder_cb.decode_buf[buf_idx + 4] = static_cast<uint8_t>((rc >> 16) & 0xFF);
      a2dp_aptx_hd_decoder_cb.decode_buf[buf_idx + 5] = static_cast<uint8_t>((rc >> 24) & 0xFF);
    }

    frame_len += 24 * sizeof(int8_t);
    p_buffer += 6;
    bytes_valid -= 6;
  }

  a2dp_aptx_hd_decoder_cb.decode_callback(a2dp_aptx_hd_decoder_cb.decode_buf, frame_len);
  return true;
}

void a2dp_vendor_aptx_hd_decoder_authenticate(const char *platformName, uint32_t token) {
  log::debug("enter {} {}",   platformName, token);

  aptx_hd_decoder_authenticate_func(platformName, token);

  log::debug("leave");
}
