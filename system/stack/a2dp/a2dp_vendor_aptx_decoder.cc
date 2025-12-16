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

#define LOG_TAG "a2dp_aptx_decoder"

#include "a2dp_vendor_aptx_decoder.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <base/logging.h>

#include "a2dp_vendor.h"
#include "a2dp_vendor_aptx.h"
#include "bt_target.h"
#include "bt_types.h"
#include "osi/include/allocator.h"
#include "osi/include/osi.h"
#include "a2dp_vendor_aptx_decoder_auth.h"

#define APTX_COMPRSN_RATIO 4
#define DEC_CHANNELS 2

using namespace bluetooth;

//
// The aptX classic decoder shared library, and the functions to use
//
static const char* APTX_DECODER_LIB_NAME = "/system/lib64/libaptX_decoder.so";
static void* aptx_decoder_lib_handle = NULL;
static bool aptx_authentication_initiated = false;

using LockGuard = std::lock_guard<std::mutex>;
static std::mutex g_load_aptx_mutex;
static std::mutex g_auth_aptx_mutex;

// Prepare for aptX classic decoding.
// |state| is a pointer to the memory to save decoded data.
// The memory for |state| shall be allocated at first.
// |endian| indicates endianness of received aptX classic
// encoded data(Big endian/Little endian).
// Return zero on success, otherwise failure.
// This function does not allocate new memory.
static const char* APTX_DECODER_INIT_NAME = "aptxbtdec_init";
typedef int32_t (*tAPTX_DECODER_INIT)(void* state, int16_t endian);

// Decode aptX classic encoded data.
// |buffer| is a pointer to aptX encoded data.
// |state| is a pointer to save the decoded data.
// It ouputs all zero to state if aptX classic decoder is not enabled properly.
// This function does not allocate new memory.
static const char* APTX_DECODER_STEREO_DECODE_NAME = "aptxbtdecStereoDecode";
typedef void (*tAPTX_DECODER_STEREO_DECODE)(int16_t* buffer, void* state);

// Get left channel of PCM data from decoded data |state|
// Return a pointer to the left channel of PCM data stored in |state|
// This function does not allocate new memory
static const char* APTX_DECODER_GET_DEC_PCML_NAME = "getDecPcmL";
typedef int32_t* (*tAPTX_DECODER_GET_DEC_PCML)(void* _state);

// Get right channel of PCM data from decoded data |state|.
// Return a pointer to the right channel of PCM data stored in |state|.
// This function does not allocate new memory.
static const char* APTX_DECODER_GET_DEC_PCMR_NAME = "getDecPcmR";
typedef int32_t* (*tAPTX_DECODER_GET_DEC_PCMR)(void* _state);

// Return the memory size of structure for storing decoded data.
// This function does not allocate new memory.
static const char* APTX_DECODER_SIZEOF_PARAMS_NAME = "sizeofAptxbtdec";
typedef int32_t (*tAPTX_DECODER_SIZEOF_PARAMS)();

// Return the version of aptX classic software decoder.
// This function does not allocate new memory.
static const char* APTX_DECODER_VERSION_NAME = "aptxbtdec_version";
typedef char* (*tAPTX_DECODER_VERSION)();

// Return the build of aptX classic software decoder.
// This function does not allocate new memory.
static const char* APTX_DECODER_BUILD_NAME = "aptxbtdec_build";
typedef char* (*tAPTX_DECODER_BUILD)();

// Do the aptX software classic decoder authentication.
// aptX software decoder outputs all zero if authentication fails.
// This function does not allocate new memory.
static const char* APTX_DECODER_AUTHENTICATE_NAME = "aptxbtdec_authenticate";
typedef void (*tAPTX_DECODER_AUTHENTICATE)(const char* platformName, uint32_t token);


tAPTX_DECODER_INIT aptx_decoder_init_func;
tAPTX_DECODER_STEREO_DECODE aptx_decoder_decode_stereo_func;
tAPTX_DECODER_GET_DEC_PCML aptx_decoder_get_pcml_func;
tAPTX_DECODER_GET_DEC_PCMR aptx_decoder_get_pcmr_func;
tAPTX_DECODER_SIZEOF_PARAMS aptx_decoder_sizeof_params_func;
tAPTX_DECODER_VERSION aptx_decoder_version_func;
tAPTX_DECODER_BUILD aptx_decoder_build_func;
tAPTX_DECODER_AUTHENTICATE aptx_decoder_authenticate_func;

typedef struct {
  void* aptx_xc;
  int16_t* encoded_buffer;
  uint8_t* decode_buf;
  decoded_data_callback_t decode_callback;
  int8_t initialized;
} tA2DP_APTX_DECODER_CB;

static tA2DP_APTX_DECODER_CB a2dp_aptx_decoder_cb;

static void a2dp_vendor_aptx_decoder_authenticate(const char *platformName, uint32_t token);

bool A2DP_VendorLoadDecoderAptx(void) {
  LockGuard lock(g_load_aptx_mutex);

  if (aptx_decoder_lib_handle != NULL) return true;  // Already loaded

  aptx_decoder_lib_handle = dlopen(APTX_DECODER_LIB_NAME, RTLD_NOW);
  if (aptx_decoder_lib_handle == nullptr) {
    log::error("cannot open aptX classic decoder library {}: {}",
              APTX_DECODER_LIB_NAME, dlerror());
    return false;
  }

  aptx_decoder_init_func = (tAPTX_DECODER_INIT)dlsym(aptx_decoder_lib_handle,
                                                     APTX_DECODER_INIT_NAME);
  if (aptx_decoder_init_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_DECODER_INIT_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptx();
    return false;
  }

  aptx_decoder_decode_stereo_func = (tAPTX_DECODER_STEREO_DECODE)dlsym(aptx_decoder_lib_handle,
                                                              APTX_DECODER_STEREO_DECODE_NAME);
  if (aptx_decoder_decode_stereo_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_DECODER_STEREO_DECODE_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptx();
    return false;
  }

  aptx_decoder_get_pcml_func = (tAPTX_DECODER_GET_DEC_PCML)dlsym(aptx_decoder_lib_handle,
                                                         APTX_DECODER_GET_DEC_PCML_NAME);
  if (aptx_decoder_get_pcml_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_DECODER_GET_DEC_PCML_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptx();
    return false;
  }

  aptx_decoder_get_pcmr_func = (tAPTX_DECODER_GET_DEC_PCMR)dlsym(aptx_decoder_lib_handle,
                                                         APTX_DECODER_GET_DEC_PCMR_NAME);
  if (aptx_decoder_get_pcmr_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_DECODER_GET_DEC_PCMR_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptx();
    return false;
  }

  aptx_decoder_sizeof_params_func = (tAPTX_DECODER_SIZEOF_PARAMS)dlsym(aptx_decoder_lib_handle,
                                                   APTX_DECODER_SIZEOF_PARAMS_NAME);
  if (aptx_decoder_sizeof_params_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_DECODER_SIZEOF_PARAMS_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptx();
    return false;
  }

  aptx_decoder_version_func = (tAPTX_DECODER_VERSION)dlsym(aptx_decoder_lib_handle,
                                                     APTX_DECODER_VERSION_NAME);
  if (aptx_decoder_version_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_DECODER_VERSION_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptx();
    return false;
  }

  aptx_decoder_build_func = (tAPTX_DECODER_BUILD)dlsym(aptx_decoder_lib_handle,
                                                     APTX_DECODER_BUILD_NAME);
  if (aptx_decoder_build_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_DECODER_BUILD_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptx();
    return false;
  }

  aptx_decoder_authenticate_func = (tAPTX_DECODER_AUTHENTICATE)dlsym(aptx_decoder_lib_handle,
                                                     APTX_DECODER_AUTHENTICATE_NAME);
  if (aptx_decoder_authenticate_func == NULL) {
    log::error("cannot find function '{}' in the decoder library: {}",
               APTX_DECODER_AUTHENTICATE_NAME, dlerror());
    A2DP_VendorUnloadDecoderAptx();
    return false;
  }

  char *aptx_version = aptx_decoder_version_func();
  char *aptx_build = aptx_decoder_build_func();

  log::debug("aptX classic is loaded successfully.{}{}",
             aptx_version, aptx_build);

  return true;
}

bool A2DP_VendorAuthenticateAptx() {
  LockGuard lock(g_auth_aptx_mutex);

  if (aptx_authentication_initiated) return true; // Already started

  bool status = false;
  token_key_init(a2dp_vendor_aptx_decoder_authenticate);
  status = token_key_generate();

  aptx_authentication_initiated = true;

  return status;
}

void A2DP_VendorUnloadDecoderAptx(void) {
    if (aptx_decoder_lib_handle != nullptr) {
      dlclose(aptx_decoder_lib_handle);
      aptx_decoder_lib_handle = nullptr;
    }

    aptx_decoder_init_func = nullptr;

    aptx_decoder_decode_stereo_func = nullptr;

    aptx_decoder_get_pcml_func = nullptr;
    aptx_decoder_get_pcmr_func = nullptr;

    aptx_decoder_sizeof_params_func = nullptr;

    aptx_decoder_version_func = nullptr;
    aptx_decoder_build_func = nullptr;

    aptx_decoder_authenticate_func = nullptr;
}

bool a2dp_vendor_aptx_decoder_init(decoded_data_callback_t decode_callback) {
  if (a2dp_aptx_decoder_cb.initialized) {
    a2dp_vendor_aptx_decoder_cleanup();
  }

  a2dp_aptx_decoder_cb.aptx_xc = osi_malloc((size_t) aptx_decoder_sizeof_params_func());
  // Initialize the decoder structures for Big Endian operation
  int32_t result = aptx_decoder_init_func(a2dp_aptx_decoder_cb.aptx_xc, 0);

  if(result != 0) {
    log::error("Fail to to initialize aptX classic decoder!");
    osi_free(a2dp_aptx_decoder_cb.aptx_xc);
    return result;
  }

  a2dp_aptx_decoder_cb.encoded_buffer = reinterpret_cast<int16_t*>(osi_malloc(sizeof(int16_t) * 2));
  a2dp_aptx_decoder_cb.decode_buf = reinterpret_cast<uint8_t*>(osi_malloc(BT_DEFAULT_BUFFER_SIZE));

  a2dp_aptx_decoder_cb.decode_callback = decode_callback;

  a2dp_aptx_decoder_cb.initialized = true;

  return true;
}

void a2dp_vendor_aptx_decoder_cleanup(void) {
  osi_free(a2dp_aptx_decoder_cb.aptx_xc);

  osi_free(a2dp_aptx_decoder_cb.encoded_buffer);
  osi_free(a2dp_aptx_decoder_cb.decode_buf);

  memset(&a2dp_aptx_decoder_cb, 0, sizeof(a2dp_aptx_decoder_cb));

  a2dp_aptx_decoder_cb.initialized = false;
}

bool a2dp_vendor_aptx_decoder_decode_packet(BT_HDR* p_buf) {
  uint8_t* p_buffer = p_buf->data + p_buf->offset;
  int32_t bytes_valid = static_cast<int32_t>(p_buf->len);

  uint16_t buf_idx   = 0;
  uint16_t pcm_idx   = 0;
  size_t   frame_len = 0;

  memset(a2dp_aptx_decoder_cb.decode_buf, 0, BT_DEFAULT_BUFFER_SIZE);
  memset(a2dp_aptx_decoder_cb.encoded_buffer, 0, sizeof(int16_t) * 2);

  while (bytes_valid >= 4 ) {
    /* Get encoded buffer
     * The data received is stored in Little Endian.
     * Do the transformation before decoding
     */
    uint8_t* encoded_ptr = reinterpret_cast<uint8_t*>(a2dp_aptx_decoder_cb.encoded_buffer);
    *(encoded_ptr)     = *(p_buffer + 1);
    *(encoded_ptr + 1) = *(p_buffer + 0);
    *(encoded_ptr + 2) = *(p_buffer + 3);
    *(encoded_ptr + 3) = *(p_buffer + 2);

    aptx_decoder_decode_stereo_func(a2dp_aptx_decoder_cb.encoded_buffer, a2dp_aptx_decoder_cb.aptx_xc);

    int32_t *pcm_lc = aptx_decoder_get_pcml_func(a2dp_aptx_decoder_cb.aptx_xc);
    int32_t *pcm_rc = aptx_decoder_get_pcmr_func(a2dp_aptx_decoder_cb.aptx_xc);

    for (pcm_idx = 0; pcm_idx < 4; pcm_idx++, buf_idx += 4) {
      int32_t lc = pcm_lc[pcm_idx];
      int32_t rc = pcm_rc[pcm_idx];
      a2dp_aptx_decoder_cb.decode_buf[buf_idx]     = static_cast<uint8_t>(lc & 0xFF);
      a2dp_aptx_decoder_cb.decode_buf[buf_idx + 1] = static_cast<uint8_t>((lc >> 8) & 0xFF);
      a2dp_aptx_decoder_cb.decode_buf[buf_idx + 2] = static_cast<uint8_t>(rc & 0xFF);
      a2dp_aptx_decoder_cb.decode_buf[buf_idx + 3] = static_cast<uint8_t>((rc >> 8) & 0xFF);
    }

    frame_len += 8 * sizeof(int16_t);
    p_buffer += 4;
    bytes_valid -= 4;
  }

  a2dp_aptx_decoder_cb.decode_callback(a2dp_aptx_decoder_cb.decode_buf, frame_len);

  return true;
}

void a2dp_vendor_aptx_decoder_authenticate(const char *platformName, uint32_t token) {
  log::debug("enter {} {}",   platformName, token);

  aptx_decoder_authenticate_func(platformName, token);

  log::debug("leave",  __func__);
}
