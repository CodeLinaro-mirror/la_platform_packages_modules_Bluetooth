///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear
//
///////////////////////////////////////////////////////////////////////////////

#ifndef APTX_ADAPTIVE_DECODER_COGNITIVE_H
#define APTX_ADAPTIVE_DECODER_COGNITIVE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef uintptr_t APTX_ADAPTIVE_DECODER;
#define INVALID_APTX_ADAPTIVE_DECODER ((uintptr_t)0)


typedef struct _tagAPTX_ADAPTIVE_LOG_POSITION
{
   void* context;
   const char* file;
   int line;
   int severity;
} APTX_ADAPTIVE_LOG_POSITION;

typedef int (*APTX_ADAPTIVE_LOG_FN) (const APTX_ADAPTIVE_LOG_POSITION* pos, const char* format, va_list args);

typedef struct _tagAPTX_ADAPTIVE_CALLBACK_INFO
{
   APTX_ADAPTIVE_DECODER decoder;
   void* context;
} APTX_ADAPTIVE_CALLBACK_INFO;

typedef struct _tagAPTX_ADAPTIVE_DECODED_OUTPUT
{
   void* pcm;
   size_t octets;
   uint32_t timestamp;
} APTX_ADAPTIVE_DECODED_OUTPUT;

typedef int (*ON_DECODED_OUTPUT)(const APTX_ADAPTIVE_CALLBACK_INFO info, const APTX_ADAPTIVE_DECODED_OUTPUT* output);

typedef struct _tagAPTX_ADAPTIVE_CONFIG_CHANGE
{
   int sample_rate_hz;
   int channels_active;
   bool plc_active;
} APTX_ADAPTIVE_CONFIG_CHANGE;

typedef int (*ON_DECODER_CHANGE) (const APTX_ADAPTIVE_CALLBACK_INFO info, const APTX_ADAPTIVE_CONFIG_CHANGE* config);

typedef struct _tagAPTX_ADAPTIVE_DECODER_INPUT_CONFIG
{
   const char* profile;
   int sample_rate_hz;
} APTX_ADAPTIVE_DECODER_INPUT_CONFIG;

typedef struct _tagAPTX_ADAPTIVE_DECODER_OUTPUT_CONFIG
{
   bool duplicate_mono;
} APTX_ADAPTIVE_DECODER_OUTPUT_CONFIG;

typedef struct _tagAPTX_ADAPTIVE_DECODER_CALLBACK_CONFIG
{
   void* context;
   ON_DECODED_OUTPUT fn_decodedOutput;
   ON_DECODER_CHANGE fn_decoderChange;
} APTX_ADAPTIVE_DECODER_CALLBACK_CONFIG;

typedef struct _tagAPTX_ADAPTIVE_DECODER_CONFIG
{
   APTX_ADAPTIVE_DECODER_INPUT_CONFIG input;
   APTX_ADAPTIVE_DECODER_OUTPUT_CONFIG output;
   APTX_ADAPTIVE_DECODER_CALLBACK_CONFIG callback;
} APTX_ADAPTIVE_DECODER_CONFIG;

void aptx_adaptive_SetLogFunction(APTX_ADAPTIVE_LOG_FN fn_log, void* context);
size_t aptx_adaptive_GetDecoderSize(const char* path);
APTX_ADAPTIVE_DECODER aptx_adaptive_CreateDecoder(void* memory, const APTX_ADAPTIVE_DECODER_CONFIG* config);
void aptx_adaptive_DestroyDecoder(APTX_ADAPTIVE_DECODER decoder);
void aptx_adaptive_ResetDecoder(APTX_ADAPTIVE_DECODER decoder);
size_t aptx_adaptive_Decode(APTX_ADAPTIVE_DECODER decoder, const void* encoded, size_t octets);

#ifdef __cplusplus
}//extern "C"
#endif

#endif// APTX_ADAPTIVE_DECODER_COGNITIVE_H