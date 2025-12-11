/*
 * Copyright 2015 The Android Open Source Project
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

// #define LOG_NDEBUG 0
#define LOG_TAG "bt_btif_avrcp_audio_track"

#include "btif_avrcp_audio_track.h"

#ifndef __INTRODUCED_IN
#define __INTRODUCED_IN(x)
#endif

#include <aaudio/AAudio.h>
#include <bluetooth/log.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <thread>

using namespace bluetooth;

typedef struct {
  AAudioStream* stream;
  int bitsPerSample;
  int channelCount;
  float* buffer;
  size_t bufferLength;
  float gain;
} BtifAvrcpAudioTrack;

// Maximum track gain that can be set.
constexpr float kMaxTrackGain = 1.0f;
// Minimum track gain that can be set.
constexpr float kMinTrackGain = 0.0f;

static struct AudioEngine {
  int trackFreq = 0;
  int channelCount = 0;
  std::thread* thread = nullptr;
  void* trackHandle = nullptr;
} s_AudioEngine;

static void ErrorCallback(AAudioStream* stream, void* userdata, aaudio_result_t error);
AAudioStream* buildAudioStream(int trackFreq, int bitsPerSample, int channelCount);

static void BtifAvrcpAudioErrorHandle() {
  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(s_AudioEngine.trackHandle);

  if (trackHolder == nullptr) {
    log::error("trackHolder is null in BtifAvrcpAudioErrorHandle");
    return;
  }

  // Rebuild the audio stream
  trackHolder->stream = buildAudioStream(
      s_AudioEngine.trackFreq,
      trackHolder->bitsPerSample,
      s_AudioEngine.channelCount);

  if (trackHolder->stream != nullptr) {
    log::debug("AAudio Error handle: restart A2dp Sink AudioTrack");
    AAudioStream_requestStart(trackHolder->stream);
  } else {
    log::error("Failed to rebuild audio stream in BtifAvrcpAudioErrorHandle");
  }

  // Reset the audio engine thread
  s_AudioEngine.thread = nullptr;
}

static void ErrorCallback(AAudioStream* /* stream */, void* /* userdata */, aaudio_result_t error) {
  if (error == AAUDIO_ERROR_DISCONNECTED) {
    if (s_AudioEngine.thread == nullptr) {
      s_AudioEngine.thread = new std::thread(BtifAvrcpAudioErrorHandle);
    }
  }
}

void* BtifAvrcpAudioTrackCreate(int trackFreq, int bitsPerSample, int channelCount) {
  log::info("Track.cpp: btCreateTrack freq {} bps {} channel {}", trackFreq, bitsPerSample,
            channelCount);

  // Allocate track holder
  BtifAvrcpAudioTrack* trackHolder = new BtifAvrcpAudioTrack;
  log::assert_that(trackHolder != NULL, "assert failed: trackHolder != NULL");

  // Create audio stream
  trackHolder->stream = buildAudioStream(trackFreq, bitsPerSample, channelCount);
  if (trackHolder->stream == nullptr) {
    log::error("Failed to create audio stream");
    delete trackHolder;
    return nullptr;
  }

  // Set track properties
  trackHolder->bitsPerSample = bitsPerSample;
  trackHolder->channelCount = channelCount;
  trackHolder->bufferLength =
          trackHolder->channelCount * AAudioStream_getBufferSizeInFrames(trackHolder->stream);
  trackHolder->gain = kMaxTrackGain;
  trackHolder->buffer = new float[trackHolder->bufferLength]();

  // Update global audio engine state
  s_AudioEngine.trackFreq = trackFreq;
  s_AudioEngine.channelCount = channelCount;
  s_AudioEngine.trackHandle = (void*)trackHolder;

  return (void*)trackHolder;
}

void BtifAvrcpAudioTrackStart(void* handle) {
  if (handle == NULL) {
    log::error("handle is null!");
    return;
  }
  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  log::assert_that(trackHolder != nullptr, "assert failed: trackHolder != nullptr");
  log::assert_that(trackHolder->stream != nullptr, "assert failed: trackHolder->stream != nullptr");
  log::verbose("Track.cpp: btStartTrack");

  aaudio_result_t result = AAudioStream_requestStart(trackHolder->stream);

  if (result != AAUDIO_OK) {
    log::error("AAudio Error - {} in AAudioStream_requestStart()", result);
    AAudioStream_close(trackHolder->stream);

    // Restore the AAudio stream when AudioTrack doesn't restore offload/direct tracks
    log::info("AAudio re-create audio track and re-start");
    trackHolder->stream = buildAudioStream(
        s_AudioEngine.trackFreq,
        trackHolder->bitsPerSample,
        s_AudioEngine.channelCount);

    log::assert_that(trackHolder->stream != nullptr, "assert failed: trackHolder->stream != nullptr");
    trackHolder->bufferLength =
        trackHolder->channelCount * AAudioStream_getBufferSizeInFrames(trackHolder->stream);

    result = AAudioStream_requestStart(trackHolder->stream);
    if (result != AAUDIO_OK) {
      log::error("re-start failed, AAudio Error - {} in AAudioStream_requestStart()", result);
      AAudioStream_close(trackHolder->stream);
    }
  }
}

void BtifAvrcpAudioTrackStop(void* handle) {
  if (handle == NULL) {
    log::info("handle is null.");
    return;
  }
  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->stream != NULL) {
    log::verbose("Track.cpp: btStopTrack");
    AAudioStream_requestStop(trackHolder->stream);
  }
}

void BtifAvrcpAudioTrackDelete(void* handle) {
  if (handle == NULL) {
    log::info("handle is null.");
    return;
  }
  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->stream != NULL) {
    log::verbose("Track.cpp: btStartTrack");
    AAudioStream_close(trackHolder->stream);
    delete trackHolder->buffer;
    delete trackHolder;
  }
}

void BtifAvrcpAudioTrackPause(void* handle) {
  if (handle == NULL) {
    log::info("handle is null.");
    return;
  }
  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  if (trackHolder != NULL && trackHolder->stream != NULL) {
    log::verbose("Track.cpp: btPauseTrack");
    AAudioStream_requestPause(trackHolder->stream);
    AAudioStream_requestFlush(trackHolder->stream);
  }
}

void BtifAvrcpSetAudioTrackGain(void* handle, float gain) {
  if (handle == NULL) {
    log::info("handle is null.");
    return;
  }
  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  if (trackHolder != NULL) {
    const float clampedGain = std::clamp(gain, kMinTrackGain, kMaxTrackGain);
    if (clampedGain != gain) {
      log::warn("Out of bounds gain set. Clamping the gain from :{:f} to {:f}", gain, clampedGain);
    }
    trackHolder->gain = clampedGain;
    log::info("Avrcp audio track gain is set to {:f}", trackHolder->gain);
  }
}

constexpr float kScaleQ15ToFloat = 1.0f / 32768.0f;
constexpr float kScaleQ23ToFloat = 1.0f / 8388608.0f;
constexpr float kScaleQ31ToFloat = 1.0f / 2147483648.0f;

static size_t sampleSizeFor(BtifAvrcpAudioTrack* trackHolder) {
  return trackHolder->bitsPerSample / 8;
}

static size_t transcodeQ15ToFloat(uint8_t* buffer, size_t length,
                                  BtifAvrcpAudioTrack* trackHolder) {
  size_t sampleSize = sampleSizeFor(trackHolder);
  size_t i = 0;
  const float scaledGain = trackHolder->gain * kScaleQ15ToFloat;
  for (; i < std::min(trackHolder->bufferLength, length / sampleSize); i++) {
    trackHolder->buffer[i] = ((int16_t*)buffer)[i] * scaledGain;
  }
  return i * sampleSize;
}

static size_t transcodeQ23ToFloat(uint8_t* buffer, size_t length,
                                  BtifAvrcpAudioTrack* trackHolder) {
  size_t sampleSize = sampleSizeFor(trackHolder);
  size_t i = 0;
  const float scaledGain = trackHolder->gain * kScaleQ23ToFloat;
  for (; i < std::min(trackHolder->bufferLength, length / sampleSize); i++) {
    size_t offset = i * sampleSize;
    int32_t sample = *((int32_t*)(buffer + offset - 1)) & 0xFFFFFF00;
    sample = sample >> 8;
    trackHolder->buffer[i] = sample * scaledGain;
  }
  return i * sampleSize;
}

static size_t transcodeQ31ToFloat(uint8_t* buffer, size_t length,
                                  BtifAvrcpAudioTrack* trackHolder) {
  size_t sampleSize = sampleSizeFor(trackHolder);
  size_t i = 0;
  const float scaledGain = trackHolder->gain * kScaleQ31ToFloat;
  for (; i < std::min(trackHolder->bufferLength, length / sampleSize); i++) {
    trackHolder->buffer[i] = ((int32_t*)buffer)[i] * scaledGain;
  }
  return i * sampleSize;
}

static size_t transcodeToPcmFloat(uint8_t* buffer, size_t length,
                                  BtifAvrcpAudioTrack* trackHolder) {
  switch (trackHolder->bitsPerSample) {
    case 16:
      return transcodeQ15ToFloat(buffer, length, trackHolder);
    case 24:
      return transcodeQ23ToFloat(buffer, length, trackHolder);
    case 32:
      return transcodeQ31ToFloat(buffer, length, trackHolder);
  }
  return -1;
}

constexpr int64_t kTimeoutNanos = 100 * 1000 * 1000;  // 100 ms

int BtifAvrcpAudioTrackWriteData(void* handle, void* audioBuffer, int bufferLength) {
  BtifAvrcpAudioTrack* trackHolder = static_cast<BtifAvrcpAudioTrack*>(handle);
  log::assert_that(trackHolder != NULL, "assert failed: trackHolder != NULL");
  log::assert_that(trackHolder->stream != NULL, "assert failed: trackHolder->stream != NULL");
  aaudio_result_t retval = -1;

  size_t sampleSize = sampleSizeFor(trackHolder);
  int transcodedCount = 0;
  do {
    transcodedCount += transcodeToPcmFloat(((uint8_t*)audioBuffer) + transcodedCount,
                                           bufferLength - transcodedCount, trackHolder);

    retval = AAudioStream_write(trackHolder->stream, trackHolder->buffer,
                                transcodedCount / (sampleSize * trackHolder->channelCount),
                                kTimeoutNanos);
    log::verbose("Track.cpp: btWriteData len = {} ret = {}", bufferLength, retval);
  } while (transcodedCount < bufferLength);

  return transcodedCount;
}

AAudioStream* buildAudioStream(int trackFreq, int bitsPerSample, int channelCount) {
  AAudioStreamBuilder* builder = nullptr;
  AAudioStream* stream = nullptr;

  // Create stream builder
  aaudio_result_t result = AAudio_createStreamBuilder(&builder);
  if (result != AAUDIO_OK || builder == nullptr) {
    log::error("Failed to create AAudioStreamBuilder: {}", result);
    return nullptr;
  }

  // Set stream parameters
  AAudioStreamBuilder_setSampleRate(builder, trackFreq);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setChannelCount(builder, channelCount);
  AAudioStreamBuilder_setSessionId(builder, AAUDIO_SESSION_ID_ALLOCATE);

  // Choose performance mode based on bitsPerSample
  aaudio_performance_mode_t mode = (bitsPerSample >= 24)
      ? AAUDIO_PERFORMANCE_MODE_HD_APTX
      : AAUDIO_PERFORMANCE_MODE_LOW_LATENCY;
  log::debug("Selected performance mode: {}", mode);
  AAudioStreamBuilder_setPerformanceMode(builder, mode);

  // Set error callback
  AAudioStreamBuilder_setErrorCallback(builder, ErrorCallback, nullptr);

  // Open stream
  result = AAudioStreamBuilder_openStream(builder, &stream);
  if (result != AAUDIO_OK || stream == nullptr) {
    log::error("Failed to open AAudioStream: {}", result);
    AAudioStreamBuilder_delete(builder);
    return nullptr;
  }

  // Clean up builder
  AAudioStreamBuilder_delete(builder);

  return stream;
}
