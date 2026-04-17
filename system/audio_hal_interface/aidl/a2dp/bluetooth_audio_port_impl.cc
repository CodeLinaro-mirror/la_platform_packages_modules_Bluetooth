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
 */

#define LOG_TAG "bluetooth-a2dp-aidl"

#ifdef TARGET_FLOSS
#include <audio_hal_interface/audio_linux.h>
#else
#include <hardware/audio.h>
#endif

#include "bluetooth_audio_port_impl.h"

#include <bluetooth/log.h>
#include <com_android_bluetooth_flags.h>
#include <hardware/audio.h>

#include <vector>

#include "android/binder_ibinder_platform.h"
#include "btif/include/btif_common.h"
#include "client_interface_aidl.h"
#include "common/stop_watch_legacy.h"

enum CONTEXT_PRIORITY { SONIFICATION = 0, MEDIA, GAME, CONVERSATIONAL };

/* Context Types */
enum class AudioContext : uint16_t {
  UNINITIALIZED = 0x0000,
  UNSPECIFIED = 0x0001,
  CONVERSATIONAL = 0x0002,
  MEDIA = 0x0004,
  GAME = 0x0008,
  INSTRUCTIONAL = 0x0010,
  VOICE_ASSISTANTS = 0x0020,
  LIVE = 0x0040,
  SOUND_EFFECTS = 0x0080,
  NOTIFICATIONS = 0x0100,
  RINGTONE = 0x0200,
  ALERTS = 0x0400,
  EMERGENCY_ALARM = 0x0800,
  RFU = 0x1000,
};

namespace bluetooth {
namespace audio {
namespace aidl {
namespace a2dp {

enum AudioContextPriority { SONIFICATION = 0, MEDIA, GAME, CONVERSATIONAL };

static btav_a2dp_codec_audio_context_t audioUsageToAudioContext(audio_usage_t usage) {
  switch (usage) {
    case AUDIO_USAGE_MEDIA:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_MEDIA;
    case AUDIO_USAGE_VOICE_COMMUNICATION:
    case AUDIO_USAGE_CALL_ASSISTANT:
    case AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_CONVERSATIONAL;
    case AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_VOICE_ASSISTANTS;
    case AUDIO_USAGE_ASSISTANCE_SONIFICATION:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_SOUND_EFFECTS;
    case AUDIO_USAGE_GAME:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_GAME;
    case AUDIO_USAGE_NOTIFICATION:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_NOTIFICATIONS;
    case AUDIO_USAGE_ALARM:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_ALERTS;
    case AUDIO_USAGE_EMERGENCY:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_EMERGENCY_ALARM;
    case AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE:
      return BTAV_A2DP_CODEC_AUDIO_CONTEXT_INSTRUCTIONAL;
    default:
      break;
  }

  LOG(INFO) << __func__ << ": Return Media when not in call by default.";
  return BTAV_A2DP_CODEC_AUDIO_CONTEXT_MEDIA;
}

static int audioContextPriority(btav_a2dp_codec_audio_context_t context) {
  switch (context) {
    case BTAV_A2DP_CODEC_AUDIO_CONTEXT_MEDIA:
      return AudioContextPriority::MEDIA;
    case BTAV_A2DP_CODEC_AUDIO_CONTEXT_GAME:
      return AudioContextPriority::GAME;
    case BTAV_A2DP_CODEC_AUDIO_CONTEXT_CONVERSATIONAL:
      return AudioContextPriority::CONVERSATIONAL;
    case BTAV_A2DP_CODEC_AUDIO_CONTEXT_SOUND_EFFECTS:
      return AudioContextPriority::SONIFICATION;
    default:
      break;
  }
  return -1;
}

using ::bluetooth::common::StopWatchLegacy;

static AudioContext AudioContentToAudioContext([[maybe_unused]] audio_content_type_t content_type,
                                               audio_source_t source_type, audio_usage_t usage) {
  switch (usage) {
    case AUDIO_USAGE_MEDIA:
      return AudioContext::MEDIA;
    case AUDIO_USAGE_VOICE_COMMUNICATION:
      return AudioContext::CONVERSATIONAL;
    case AUDIO_USAGE_CALL_ASSISTANT:
      return AudioContext::CONVERSATIONAL;
    case AUDIO_USAGE_VOICE_COMMUNICATION_SIGNALLING:
      return AudioContext::VOICE_ASSISTANTS;
    case AUDIO_USAGE_ASSISTANCE_SONIFICATION:
      return AudioContext::SOUND_EFFECTS;
    case AUDIO_USAGE_GAME:
      return AudioContext::GAME;
    case AUDIO_USAGE_NOTIFICATION:
      return AudioContext::NOTIFICATIONS;
    case AUDIO_USAGE_NOTIFICATION_TELEPHONY_RINGTONE:
      return AudioContext::CONVERSATIONAL;
    case AUDIO_USAGE_ALARM:
      return AudioContext::ALERTS;
    case AUDIO_USAGE_EMERGENCY:
      return AudioContext::EMERGENCY_ALARM;
    case AUDIO_USAGE_ASSISTANCE_NAVIGATION_GUIDANCE:
      return AudioContext::INSTRUCTIONAL;
    default:
      break;
  }

  switch (source_type) {
    case AUDIO_SOURCE_MIC:
    case AUDIO_SOURCE_HOTWORD:
    case AUDIO_SOURCE_VOICE_CALL:
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
      return AudioContext::CONVERSATIONAL;
    default:
      break;
  }

  return AudioContext::MEDIA;
}

static int getPriority(AudioContext context) {
  switch (context) {
    case AudioContext::MEDIA:
      return CONTEXT_PRIORITY::MEDIA;
    case AudioContext::GAME:
      return CONTEXT_PRIORITY::GAME;
    case AudioContext::CONVERSATIONAL:
      return CONTEXT_PRIORITY::CONVERSATIONAL;
    case AudioContext::SOUND_EFFECTS:
      return CONTEXT_PRIORITY::SONIFICATION;
    default:
      break;
  }
  return 0;
}

BluetoothAudioPortImpl::BluetoothAudioPortImpl(
        const std::shared_ptr<A2dpTransport>& transport_instance,
        const std::shared_ptr<IBluetoothAudioProvider>& provider)
    : transport_instance_(transport_instance), provider_(provider) {}

BluetoothAudioPortImpl::~BluetoothAudioPortImpl() {}

ndk::ScopedAStatus BluetoothAudioPortImpl::startStream(bool is_low_latency) {
  StopWatchLegacy stop_watch(__func__);
  auto transport = transport_instance_.lock();
  if (!transport) {
    log::error("Invalid call to dropped audio port instance");
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  }
  Status ack = transport->StartRequest(is_low_latency);
  if (ack != Status::PENDING) {
    auto aidl_retval = provider_->streamStarted(StatusToHalStatus(ack));
    if (!aidl_retval.isOk()) {
      log::error("BluetoothAudioHal failure: {}", aidl_retval.getDescription());
    }
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothAudioPortImpl::suspendStream() {
  StopWatchLegacy stop_watch(__func__);
  auto transport = transport_instance_.lock();
  if (!transport) {
    log::error("Invalid call to dropped audio port instance");
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  }
  Status ack = transport->SuspendRequest();
  if (ack != Status::PENDING) {
    auto aidl_retval = provider_->streamSuspended(StatusToHalStatus(ack));
    if (!aidl_retval.isOk()) {
      log::error("BluetoothAudioHal failure: {}", aidl_retval.getDescription());
    }
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothAudioPortImpl::stopStream() {
  StopWatchLegacy stop_watch(__func__);
  auto transport = transport_instance_.lock();
  if (!transport) {
    log::error("Invalid call to dropped audio port instance");
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  }
  transport->StopRequest();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothAudioPortImpl::getPresentationPosition(
        PresentationPosition* _aidl_return) {
  StopWatchLegacy stop_watch(__func__);
  uint64_t remote_delay_report_ns;
  uint64_t total_bytes_read;
  timespec data_position;
  auto transport = transport_instance_.lock();
  if (!transport) {
    log::error("Invalid call to dropped audio port instance");
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  }
  bool retval = transport->GetPresentationPosition(&remote_delay_report_ns, &total_bytes_read,
                                                   &data_position);

  PresentationPosition::TimeSpec transmittedOctetsTimeStamp;
  if (retval) {
    transmittedOctetsTimeStamp.tvSec = static_cast<int64_t>(data_position.tv_sec);
    transmittedOctetsTimeStamp.tvNSec = static_cast<int64_t>(data_position.tv_nsec);
  } else {
    remote_delay_report_ns = 0;
    total_bytes_read = 0;
    transmittedOctetsTimeStamp = {};
  }
  log::debug("result={}, delay={}, data={} byte(s), timestamp={}", retval, remote_delay_report_ns,
             total_bytes_read, transmittedOctetsTimeStamp.toString());
  _aidl_return->remoteDeviceAudioDelayNanos = static_cast<int64_t>(remote_delay_report_ns);
  _aidl_return->transmittedOctets = static_cast<int64_t>(total_bytes_read);
  _aidl_return->transmittedOctetsTimestamp = transmittedOctetsTimeStamp;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothAudioPortImpl::updateSourceMetadata(
        const SourceMetadata& source_metadata) {
  StopWatchLegacy stop_watch(__func__);
  log::info("{} track(s)", source_metadata.tracks.size());

  btav_a2dp_codec_audio_context_t current_context = BTAV_A2DP_CODEC_AUDIO_CONTEXT_NOTIFICATIONS;
  int current_priority = AudioContextPriority::SONIFICATION;

  for (const auto& track : source_metadata.tracks) {
    audio_usage_t usage = static_cast<audio_usage_t>(track.usage);
    btav_a2dp_codec_audio_context_t context = audioUsageToAudioContext(usage);
    int priority = audioContextPriority(context);

    if (priority > current_priority) {
      current_context = context;
      current_priority = priority;
    }
  }

  auto transport = transport_instance_.lock();
  if (!transport) {
    log::error("Invalid call to dropped audio port instance");
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  }
  transport->SourceMetadataChanged(current_context);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothAudioPortImpl::updateSinkMetadata(
        const SinkMetadata& /*sink_metadata*/) {
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothAudioPortImpl::setLatencyMode(LatencyMode latency_mode) {
  auto transport = transport_instance_.lock();
  if (!transport) {
    log::error("Invalid call to dropped audio port instance");
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  }
  transport->SetLatencyMode(latency_mode);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus BluetoothAudioPortImpl::updateSinkLatency(int64_t in_latency_ms) {
  log::debug("in_latency_ms: {}", in_latency_ms);
  auto transport = transport_instance_.lock();
  if (!transport) {
    log::error("Invalid call to dropped audio port instance");
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
  }
  transport->UpdateSinkLatency(in_latency_ms);
  return ndk::ScopedAStatus::ok();
}

// Overriding create binder and inherit RT from caller.
// In our case, the caller is the AIDL session control, so we match the priority
// of the AIDL session / AudioFlinger writer thread.
ndk::SpAIBinder BluetoothAudioPortImpl::createBinder() {
  auto binder = BnBluetoothAudioPort::createBinder();
  AIBinder_setInheritRt(binder.get(), true);
  return binder;
}

}  // namespace a2dp
}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
