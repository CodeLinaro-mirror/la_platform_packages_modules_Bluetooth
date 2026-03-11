/*
 * Copyright (C) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#undef LOG_TAG
#define LOG_TAG "BTAudioQtiHidlLeAudioBroadcast"

#include "le_audio_broadcast_encoding.h"

#include <android/log.h>
#include <mutex>
#include <vector>

#include "client_interface.h"
#include "osi/include/properties.h"

namespace {

using ::bluetooth::audio::qti_hidl::BluetoothAudioClientInterface;
using ::bluetooth::audio::qti_hidl::BluetoothAudioCtrlAck;
using ::bluetooth::audio::qti_hidl::IBluetoothTransportInstance_2_1;
using AudioConfiguration_2_1   = vendor::qti::hardware::bluetooth_audio::V2_1::AudioConfiguration;
using CodecConfiguration_2_1   = vendor::qti::hardware::bluetooth_audio::V2_1::CodecConfiguration;
using CodecType_2_1             = vendor::qti::hardware::bluetooth_audio::V2_1::CodecType;
using vendor::qti::hardware::bluetooth_audio::V2_0::BitsPerSample;
using vendor::qti::hardware::bluetooth_audio::V2_0::ChannelMode;
using vendor::qti::hardware::bluetooth_audio::V2_0::PcmParameters;
using vendor::qti::hardware::bluetooth_audio::V2_0::SampleRate;
using vendor::qti::hardware::bluetooth_audio::V2_0::SessionType;
using vendor::qti::hardware::bluetooth_audio::V2_1::ExtSampleRate;
using vendor::qti::hardware::bluetooth_audio::V2_1::LC3ChannelMode;

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
std::mutex internal_mutex_;
BluetoothAudioClientInterface* broadcast_hal_clientif = nullptr;
SessionType broadcast_session_type = SessionType::UNKNOWN;
bool session_started = false;

// ---------------------------------------------------------------------------
// Duplex direction tracking (stack / vector)
//
// Start order : TX pushed first, RX pushed second.
// Stop  order : LIFO — RX popped first, TX popped second.
//
// Each StartRequest / SuspendRequest is forwarded to the upper layer
// immediately (no waiting for the other direction).  The stack is used
// purely to record which direction is being started / stopped so that
// log messages are accurate and the LIFO invariant is maintained.
// ---------------------------------------------------------------------------
enum class DuplexDirection { TX = 0, RX = 1 };

// Active-direction stack.  Cleared before every new StartSession and on
// end_session().  Thread-safety note: StartRequest / SuspendRequest are
// called from the HAL binder thread; the stack is only cleared from the
// BT main thread (start_session / end_session) before any HAL callbacks
// can arrive, so no additional lock is required.
std::vector<DuplexDirection> active_direction_stack_;

// Stores the current audio config.
// Initialised with a default PCM config in init().
// Updated with the actual codec PCM params in setup_codec() once both
// TX and RX configs are ready.
// Used by start_session() to call UpdateAudioConfig_2_1 before StartSession_2_1.
AudioConfiguration_2_1 current_audio_config_{};

// ---------------------------------------------------------------------------
// Duplex codec config state
// Tracks whether TX (encoder) and RX (decoder) params have been received.
// The HAL config is only pushed once BOTH are ready.
// ---------------------------------------------------------------------------
struct DuplexCodecState {
  bool encoder_configured = false;
  bool decoder_configured = false;

  uint32_t encoder_sample_rate      = 0;
  uint8_t  encoder_bits_per_sample  = 0;
  uint8_t  encoder_channel_count    = 0;
  uint32_t encoder_data_interval_us = 0;

  uint32_t decoder_sample_rate      = 0;
  uint8_t  decoder_bits_per_sample  = 0;
  uint8_t  decoder_channel_count    = 0;
  uint32_t decoder_data_interval_us = 0;

  bool both_configs_ready() const { return encoder_configured && decoder_configured; }

  void reset() {
    encoder_configured        = false;
    decoder_configured        = false;
    encoder_sample_rate       = 0;
    encoder_bits_per_sample   = 0;
    encoder_channel_count     = 0;
    encoder_data_interval_us  = 0;
    decoder_sample_rate       = 0;
    decoder_bits_per_sample   = 0;
    decoder_channel_count     = 0;
    decoder_data_interval_us  = 0;
  }
};

DuplexCodecState duplex_codec_state_;

// ---------------------------------------------------------------------------
// Separate callbacks for Source (encoder/TX) and Sink (decoder/RX)
// ---------------------------------------------------------------------------
struct BroadcastCallbacks {
  std::function<bool(bool)> on_resume_;
  std::function<bool()>     on_suspend_;
  std::function<bool()>     on_stop_;
};

BroadcastCallbacks source_callbacks_;  // encoder / TX
BroadcastCallbacks sink_callbacks_;    // decoder / RX

// Polling gate: HAL StartSession called only when BOTH source+sink have requested start.
bool source_session_requested_ = false;
bool sink_session_requested_   = false;

// ---------------------------------------------------------------------------
// Transport implementation
// ---------------------------------------------------------------------------
class LeAudioBroadcastTransport_2_1 : public IBluetoothTransportInstance_2_1 {
 public:
  LeAudioBroadcastTransport_2_1(SessionType sessionType, AudioConfiguration_2_1 audioConfig)
      : IBluetoothTransportInstance_2_1(sessionType, std::move(audioConfig)),
        remote_delay_report_(0),
        total_bytes_written_(0),
        data_position_({}) {}

  BluetoothAudioCtrlAck StartRequest() override {
    // Determine direction: TX is always started first, RX second.
    DuplexDirection dir = active_direction_stack_.empty()
                                  ? DuplexDirection::TX
                                  : DuplexDirection::RX;
    active_direction_stack_.push_back(dir);

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "%s: HAL %s start request (stack size: %zu)", __func__,
                        dir == DuplexDirection::TX ? "TX" : "RX",
                        active_direction_stack_.size());

    // Route to source (TX) or sink (RX) callbacks based on direction.
    BroadcastCallbacks& cb = (dir == DuplexDirection::TX) ? source_callbacks_ : sink_callbacks_;
    if (cb.on_resume_) {
      if (cb.on_resume_(true)) {
        return BluetoothAudioCtrlAck::PENDING;;
      }
    }
    return BluetoothAudioCtrlAck::PENDING;
  }

  BluetoothAudioCtrlAck SuspendRequest() override {
    if (active_direction_stack_.empty()) {
      __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                          "%s: SuspendRequest with empty direction stack", __func__);
      return BluetoothAudioCtrlAck::SUCCESS_FINISHED;
    }

    // LIFO: pop the last-started direction (RX first, then TX).
    DuplexDirection dir = active_direction_stack_.back();
    active_direction_stack_.pop_back();

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "%s: HAL %s suspend request (LIFO, remaining: %zu)", __func__,
                        dir == DuplexDirection::TX ? "TX" : "RX",
                        active_direction_stack_.size());

    // Route to source (TX) or sink (RX) callbacks based on LIFO direction.
    BroadcastCallbacks& cb = (dir == DuplexDirection::TX) ? source_callbacks_ : sink_callbacks_;
    if (cb.on_suspend_) {
      if (cb.on_suspend_()) {
        return BluetoothAudioCtrlAck::PENDING;
      }
    }
    return BluetoothAudioCtrlAck::PENDING;
  }

  void StopRequest() override {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: HAL initiated stop request", __func__);
    // Route stop to both source and sink callbacks.
    if (source_callbacks_.on_stop_) source_callbacks_.on_stop_();
    if (sink_callbacks_.on_stop_)   sink_callbacks_.on_stop_();
  }

  bool GetPresentationPosition(uint64_t* remote_delay_report_ns, uint64_t* total_bytes_written,
                               timespec* data_position) override {
    *remote_delay_report_ns = remote_delay_report_ * 100000ULL;
    *total_bytes_written    = total_bytes_written_;
    *data_position          = data_position_;
    return true;
  }

  void ResetPresentationPosition() override {
    remote_delay_report_ = 0;
    total_bytes_written_ = 0;
    data_position_       = {};
  }

  void LogBytesRead(size_t bytes_written) override {
    if (bytes_written != 0) {
      total_bytes_written_ += bytes_written;
      clock_gettime(CLOCK_MONOTONIC, &data_position_);
    }
  }

  void SetRemoteDelay(uint16_t delay_report) { remote_delay_report_ = delay_report; }

 private:
  uint16_t remote_delay_report_;
  uint64_t total_bytes_written_;
  timespec data_position_;
};

LeAudioBroadcastTransport_2_1* broadcast_transport = nullptr;

// ---------------------------------------------------------------------------
// HAL enum conversion helpers
// ---------------------------------------------------------------------------

// V2.1 extended sample rate (used in LC3 codec config)
ExtSampleRate le_audio_sample_rate_to_ext_hal(uint32_t sample_rate) {
  switch (sample_rate) {
    case 8000:  return ExtSampleRate::RATE_8000;
    case 16000: return ExtSampleRate::RATE_16000;
    case 24000: return ExtSampleRate::RATE_24000;
    case 32000: return ExtSampleRate::RATE_32000;
    case 44100: return ExtSampleRate::RATE_44100;
    case 48000: return ExtSampleRate::RATE_48000;
    default:
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: Unknown sample rate %u (ext)",
                          __func__, sample_rate);
      return ExtSampleRate::RATE_UNKNOWN;
  }
}

// V2.0 sample rate (kept for PcmParameters compatibility if needed)
SampleRate le_audio_sample_rate_to_hal(uint32_t sample_rate) {
  switch (sample_rate) {
    case 16000: return SampleRate::RATE_16000;
    case 24000: return SampleRate::RATE_24000;
    case 32000: return SampleRate::RATE_32000;
    case 44100: return SampleRate::RATE_44100;
    case 48000: return SampleRate::RATE_48000;
    default:
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: Unknown sample rate %u",
                          __func__, sample_rate);
      return SampleRate::RATE_UNKNOWN;
  }
}

BitsPerSample le_audio_bits_per_sample_to_hal(uint8_t bits_per_sample) {
  switch (bits_per_sample) {
    case 16: return BitsPerSample::BITS_16;
    case 24: return BitsPerSample::BITS_24;
    case 32: return BitsPerSample::BITS_32;
    default:
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: Unknown bits per sample %d",
                          __func__, static_cast<int>(bits_per_sample));
      return BitsPerSample::BITS_UNKNOWN;
  }
}

ChannelMode le_audio_channel_mode_to_hal(uint8_t channels) {
  switch (channels) {
    case 1: return ChannelMode::MONO;
    case 2: return ChannelMode::STEREO;
    default:
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: Unknown channel count %d",
                          __func__, static_cast<int>(channels));
      return ChannelMode::UNKNOWN;
  }
}

// V2.1 LC3-specific channel mode (used in lc3Config.txConfig/rxConfig.channelMode)
LC3ChannelMode le_audio_channel_mode_to_lc3_hal(uint8_t channels) {
  switch (channels) {
    case 1: return LC3ChannelMode::MONO;
    case 2: return LC3ChannelMode::STEREO;
    default:
      __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: Unknown channel count %d (lc3)",
                          __func__, static_cast<int>(channels));
      return LC3ChannelMode::UNKNOWN;
  }
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================
namespace bluetooth {
namespace audio {
namespace qti_hidl {
namespace le_audio_broadcast {

bool is_duplex_broadcast_enabled() {
  bool enabled = osi_property_get_bool("persist.bluetooth.aurachat.enabled", false);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Aurachat (duplex broadcast) %s", __func__,
                      enabled ? "enabled" : "disabled");
  return enabled;
}

bool is_hal_enabled() {
  return (broadcast_hal_clientif != nullptr && broadcast_transport != nullptr);
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
bool init(bluetooth::common::MessageLoopThread* message_loop) {
  std::unique_lock<std::mutex> guard(internal_mutex_);

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "%s: Initializing LE Audio broadcast transport", __func__);

  if (broadcast_transport != nullptr) {
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "%s: Transport already initialized", __func__);
    return true;
  }

  broadcast_session_type = SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH;

  // Placeholder LC3 codec config.
  // The HAL's IsRepurposedSoftwareSessionForAchat() check requires:
  //   codecType == LC3  AND  rxConfigSet == TX_RX_BOTH_CONFIG (0x3)
  // Using PcmParameters here causes the check to fail with
  // "IsSoftwarePcmConfigurationValid: Invalid PCM Configuration".
  // The actual LC3 params (sample rate, bitrate, etc.) are filled in
  // by setup_codec() which is called from SetPcmParameters() before
  // StartSession().
  CodecConfiguration_2_1 codec_config{};
  codec_config.codecType = CodecType_2_1::LC3;
  // Set rxConfigSet = TX_RX_BOTH_CONFIG (0x3) so the HAL recognises
  // this as an Aurachat duplex session even before setup_codec() runs.
  codec_config.config.lc3Config.rxConfigSet = 0x3;

  current_audio_config_              = AudioConfiguration_2_1{};
  current_audio_config_.codecConfig  = codec_config;

  broadcast_transport =
          new LeAudioBroadcastTransport_2_1(broadcast_session_type, current_audio_config_);

  if (broadcast_hal_clientif == nullptr) {
    broadcast_hal_clientif =
            new BluetoothAudioClientInterface(broadcast_transport, message_loop, &internal_mutex_);
  }

  session_started = false;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "%s: LE Audio broadcast transport initialized", __func__);
  return true;
}

// ---------------------------------------------------------------------------
// cleanup
// ---------------------------------------------------------------------------
void cleanup() {
  std::unique_lock<std::mutex> guard(internal_mutex_);

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "%s: Cleaning up LE Audio broadcast transport", __func__);

  if (broadcast_hal_clientif != nullptr) {
    broadcast_hal_clientif->EndSession();
    broadcast_hal_clientif = nullptr;
  }

  if (broadcast_transport != nullptr) {
    delete broadcast_transport;
    broadcast_transport = nullptr;
  }

  broadcast_session_type = SessionType::UNKNOWN;
  source_callbacks_      = {};
  sink_callbacks_        = {};
  current_audio_config_  = AudioConfiguration_2_1{};
  session_started        = false;
  source_session_requested_ = false;
  sink_session_requested_   = false;
  duplex_codec_state_.reset();

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "%s: LE Audio broadcast transport cleaned up", __func__);
}

// ---------------------------------------------------------------------------
// setup_codec
//
// Called twice by UpdateBroadcastAudioConfigToHal():
//   1st call: is_encoder=true  (TX / encoder direction)
//   2nd call: is_encoder=false (RX / decoder direction)
//
// The HAL config is only pushed once BOTH directions are ready.
// The resulting PCM config is also stored in current_audio_config_ so that
// start_session() can use it if it is called after setup_codec().
// ---------------------------------------------------------------------------
bool setup_codec(uint32_t sample_rate, uint8_t bits_per_sample, uint8_t channel_count,
                 uint32_t data_interval_us, bool is_encoder) {
  std::unique_lock<std::mutex> guard(internal_mutex_);

  const char* direction = is_encoder ? "TX (encoder)" : "RX (decoder)";
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "%s: Setting up codec for %s (rate=%u, bits=%d, ch=%d, interval_us=%u)",
                      __func__, direction, sample_rate,
                      static_cast<int>(bits_per_sample), static_cast<int>(channel_count),
                      data_interval_us);

  if (broadcast_transport == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: Transport not initialized", __func__);
    return false;
  }

  // Store parameters for this direction
  if (is_encoder) {
    duplex_codec_state_.encoder_sample_rate      = sample_rate;
    duplex_codec_state_.encoder_bits_per_sample  = bits_per_sample;
    duplex_codec_state_.encoder_channel_count    = channel_count;
    duplex_codec_state_.encoder_data_interval_us = data_interval_us;
    duplex_codec_state_.encoder_configured       = true;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: TX (encoder) params stored", __func__);
  } else {
    duplex_codec_state_.decoder_sample_rate      = sample_rate;
    duplex_codec_state_.decoder_bits_per_sample  = bits_per_sample;
    duplex_codec_state_.decoder_channel_count    = channel_count;
    duplex_codec_state_.decoder_data_interval_us = data_interval_us;
    duplex_codec_state_.decoder_configured       = true;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: RX (decoder) params stored", __func__);
  }

  // Wait until both TX and RX configs are available before updating HAL
  if (!duplex_codec_state_.both_configs_ready()) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "%s: Waiting for both configs (encoder: %s, decoder: %s)", __func__,
                        duplex_codec_state_.encoder_configured ? "ready" : "pending",
                        duplex_codec_state_.decoder_configured ? "ready" : "pending");
    return true;
  }

  // ---------------------------------------------------------------------------
  // Both TX and RX configs are ready.
  // Build a CodecConfiguration_2_1 with codecType = LC3.
  //
  // WHY LC3 and NOT PcmParameters:
  //   The QTI HAL's startSession_2_1() calls IsRepurposedSoftwareSessionForAchat()
  //   which checks:  codecType == LC3  AND  rxConfigSet == TX_RX_BOTH_CONFIG (0x3)
  //   If PcmParameters is used instead, the check returns false and the HAL
  //   falls through to IsSoftwarePcmConfigurationValid() which rejects the
  //   session with UNSUPPORTED_CODEC_CONFIGURATION.
  // ---------------------------------------------------------------------------
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "%s: Both TX and RX configs ready, building LC3 CodecConfiguration_2_1",
                      __func__);

  // Derive LC3 bitrate from sample rate (standard Aurachat values)
  uint32_t bitrate_bps = 32000;  // 32 kbps default (16 kHz Aurachat)
  switch (duplex_codec_state_.encoder_sample_rate) {
    case 16000: bitrate_bps = 32000;  break;
    case 24000: bitrate_bps = 48000;  break;
    case 32000: bitrate_bps = 64000;  break;
    case 48000: bitrate_bps = 96000;  break;
    default:    bitrate_bps = 32000;  break;
  }

  // octetsPerFrame = (bitrate_bps * frame_duration_us) / (8 * 1_000_000)
  uint32_t enc_interval = duplex_codec_state_.encoder_data_interval_us
                          ? duplex_codec_state_.encoder_data_interval_us : 7500;
  uint16_t octets_per_frame = static_cast<uint16_t>(
      (static_cast<uint64_t>(bitrate_bps) * enc_interval) / (8ULL * 1000000ULL));
  // ---------------------------------------------------------------------------
  // Channel count handling for multi-BIS broadcast (e.g. Aurachat 4-BIS mode)
  //
  // The channel_count stored in duplex_codec_state_ is the TOTAL number of
  // channels across ALL BIS streams (numChannelsTotal from the broadcaster).
  // For example, a 4-BIS broadcast with 1 channel per BIS has
  // encoder_channel_count = 4.
  //
  // The QTI HAL's ChannelMode / LC3ChannelMode enums only support MONO (1)
  // and STEREO (2).  Passing the total count (4) causes
  // le_audio_channel_mode_to_hal() to return UNKNOWN, which then fails the
  // validation below and leaves current_audio_config_ with the placeholder
  // zeros from init() — ultimately causing a2dp_codec_parser to report
  // "unknown sampling rate: 0".
  //
  // Fix: derive the per-BIS channel count.
  //   • total_ch <= 2  → use total_ch directly (MONO or STEREO per BIS)
  //   • total_ch >  2  → each BIS carries 1 channel (MONO); the full channel
  //                       count is preserved in lc3.decoderOuputChannels so
  //                       the HAL still knows the aggregate output width.
  // ---------------------------------------------------------------------------
  uint8_t enc_per_bis_ch = (duplex_codec_state_.encoder_channel_count > 2)
                               ? 1u
                               : duplex_codec_state_.encoder_channel_count;
  uint8_t dec_per_bis_ch = (duplex_codec_state_.decoder_channel_count > 2)
                               ? 1u
                               : duplex_codec_state_.decoder_channel_count;

  if (duplex_codec_state_.encoder_channel_count > 2) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "%s: Multi-BIS broadcast: total_enc_ch=%d, using per-BIS ch=%d (MONO)",
                        __func__,
                        static_cast<int>(duplex_codec_state_.encoder_channel_count),
                        static_cast<int>(enc_per_bis_ch));
  }
  if (duplex_codec_state_.decoder_channel_count > 2) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "%s: Multi-BIS broadcast: total_dec_ch=%d, using per-BIS ch=%d (MONO)",
                        __func__,
                        static_cast<int>(duplex_codec_state_.decoder_channel_count),
                        static_cast<int>(dec_per_bis_ch));
  }

  // Validate converted enum values
  ExtSampleRate  enc_sr      = le_audio_sample_rate_to_ext_hal(duplex_codec_state_.encoder_sample_rate);
  BitsPerSample  enc_bps     = le_audio_bits_per_sample_to_hal(duplex_codec_state_.encoder_bits_per_sample);
  ChannelMode    enc_ch      = le_audio_channel_mode_to_hal(enc_per_bis_ch);
  LC3ChannelMode enc_lc3_ch  = le_audio_channel_mode_to_lc3_hal(enc_per_bis_ch);
  ExtSampleRate  dec_sr      = le_audio_sample_rate_to_ext_hal(duplex_codec_state_.decoder_sample_rate);
  BitsPerSample  dec_bps     = le_audio_bits_per_sample_to_hal(duplex_codec_state_.decoder_bits_per_sample);
  ChannelMode    dec_ch      = le_audio_channel_mode_to_hal(duplex_codec_state_.decoder_channel_count);
  LC3ChannelMode dec_lc3_ch  = le_audio_channel_mode_to_lc3_hal(duplex_codec_state_.decoder_channel_count);

  if (enc_sr  == ExtSampleRate::RATE_UNKNOWN ||
      enc_bps == BitsPerSample::BITS_UNKNOWN ||
      enc_ch  == ChannelMode::UNKNOWN) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "%s: Invalid TX codec parameters (rate=%u, bits=%d, per_bis_ch=%d)",
                        __func__,
                        duplex_codec_state_.encoder_sample_rate,
                        static_cast<int>(duplex_codec_state_.encoder_bits_per_sample),
                        static_cast<int>(enc_per_bis_ch));
  }

  CodecConfiguration_2_1 codec_config{};
  codec_config.codecType = CodecType_2_1::LC3;

  auto& lc3 = codec_config.config.lc3Config;

  // TX (encoder) direction
  lc3.txConfig.sampleRate     = enc_sr;
  lc3.txConfig.channelMode    = enc_lc3_ch;
  lc3.txConfig.bitsPerSample  = enc_bps;
  lc3.txConfig.octetsPerFrame = octets_per_frame;
  lc3.txConfig.frameDuration  = enc_interval;
  lc3.txConfig.bitrate        = bitrate_bps;
  lc3.txConfig.numBlocks      = 1;
  

  // RX (decoder) direction — symmetric for Aurachat
  uint32_t dec_interval = duplex_codec_state_.decoder_data_interval_us
                          ? duplex_codec_state_.decoder_data_interval_us : enc_interval;
  lc3.rxConfig.sampleRate     = (dec_sr != ExtSampleRate::RATE_UNKNOWN) ? dec_sr : enc_sr;
  lc3.rxConfig.channelMode    = (dec_ch != ChannelMode::UNKNOWN) ? dec_lc3_ch : enc_lc3_ch;
  lc3.rxConfig.bitsPerSample  = (dec_bps != BitsPerSample::BITS_UNKNOWN)? dec_bps : enc_bps;
  lc3.rxConfig.octetsPerFrame = octets_per_frame;
  lc3.rxConfig.frameDuration  = dec_interval;
  lc3.rxConfig.bitrate        = bitrate_bps;
  lc3.rxConfig.numBlocks      = 1;

  // CRITICAL: rxConfigSet = TX_RX_BOTH_CONFIG (0x3)
  // This is the flag that IsRepurposedSoftwareSessionForAchat() checks.
  lc3.rxConfigSet          = 0x3;
  lc3.mode                 = 4;
  lc3.decoderOuputChannels = static_cast<uint8_t>(duplex_codec_state_.decoder_channel_count);
  lc3.defaultQlevel        = 3;

  // ---------------------------------------------------------------------------
  // Stream map population (CRITICAL FIX for A16 port)
  //
  // IMPORTANT: NumStreamIDGroup must be set to indicate how many valid entries
  // are in the streamMap array. The HAL uses this field to determine how many
  // stream map entries to process.
  //
  // The streamMap field is a fixed-size array uint32_t[48] at the LC3Parameters
  // level (not in txConfig/rxConfig). It encodes stream information for both
  // TX and RX directions. Based on A14 logs:
  //   - TX (out): 1 entry with stream_id=0, audio_location=0, direction=0
  //   - RX (in):  decoderOuputChannels entries (e.g., 4) with stream_ids 0-3,
  //               audio_location=0, direction=1
  //
  // The encoding format packs stream_id, audio_location, and direction into
  // a single uint32_t:
  //   bits [7:0]   = stream_id
  //   bits [23:8]  = audio_location (0 = front center/mono)
  //   bits [31:24] = direction (0 = TX/out, 1 = RX/in)
  //
  // The streamMap array layout:
  //   [0]       = TX stream (direction=0, stream_id=0)
  //   [1..N]    = RX streams (direction=1, stream_ids=0..N-1)
  //   [N+1..47] = unused (set to 0)
  // ---------------------------------------------------------------------------

  // Hardcoded: 1 TX + 4 RX = 5 streams (standard Aurachat 4-BIS config)
  lc3.NumStreamIDGroup = 5;
  uint8_t total_streams = 5;

  // Initialize entire streamMap array to 0
  for (uint8_t i = 0; i < 48; i++) {
    lc3.streamMap[i] = 0;
  }

  // TX stream (stream 0): entries [0..2]
  // Format per entry: {audio_location, stream_id, direction}
  lc3.streamMap[0] = 0;  // audio_location = 0
  lc3.streamMap[1] = 0;  // stream_id = 0
  lc3.streamMap[2] = 0;  // direction = 0 (TX / out)

  // RX streams (streams 1..N): entries [3..3+N*3-1]
  // Format per entry: {audio_location, stream_id, direction}
  for (uint8_t i = 0; i < duplex_codec_state_.decoder_channel_count; i++) {
    uint8_t base = static_cast<uint8_t>((1 + i) * 3);
    lc3.streamMap[base + 0] = 0;  // audio_location = 0
    lc3.streamMap[base + 1] = i;  // stream_id = 0, 1, 2, 3, ...
    lc3.streamMap[base + 2] = 1;  // direction = 1 (RX / in)
  }

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "%s: Stream map populated ENCODING: NumStreamIDGroup=%d, total=%d "
                      "(1 TX + %d RX), format=3-per-stream",
                      __func__,
                      static_cast<int>(lc3.NumStreamIDGroup),
                      static_cast<int>(total_streams),
                      static_cast<int>(duplex_codec_state_.decoder_channel_count));

  AudioConfiguration_2_1 audio_config{};
  audio_config.codecConfig = codec_config;

  // Persist so start_session() uses the correct config
  current_audio_config_ = audio_config;

  // Update transport's internal config so start_session() can use current_audio_config_.
  // Do NOT call UpdateAudioConfig_2_1 here — start_session() will call it exactly once
  // (after the polling gate confirms both source and sink have requested start).
  broadcast_transport->UpdateAudioConfiguration(audio_config);

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                      "%s: LC3 CodecConfig stored (HAL update deferred to start_session): "
                      "(enc: rate=%u bits=%d ch=%d interval=%u octets=%u "
                      "dec: rate=%u bits=%d ch=%d rxConfigSet=0x3)",
                      __func__,
                      duplex_codec_state_.encoder_sample_rate,
                      static_cast<int>(duplex_codec_state_.encoder_bits_per_sample),
                      static_cast<int>(duplex_codec_state_.encoder_channel_count),
                      enc_interval, octets_per_frame,
                      duplex_codec_state_.decoder_sample_rate,
                      static_cast<int>(duplex_codec_state_.decoder_bits_per_sample),
                      static_cast<int>(duplex_codec_state_.decoder_channel_count));
  return true;
}

// ---------------------------------------------------------------------------
// start_session (with polling gate)
// Source (encoder, is_encoder=true) calls first, Sink (decoder, is_encoder=false) calls second.
// HAL StartSession is issued only when BOTH have called start_session.
// ---------------------------------------------------------------------------
bool start_session(bool is_encoder) {
  std::unique_lock<std::mutex> guard(internal_mutex_);

  const char* direction = is_encoder ? "Source (encoder)" : "Sink (decoder)";
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: %s requesting start", __func__, direction);

  if (broadcast_hal_clientif == nullptr || broadcast_transport == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: HAL not initialized", __func__);
    return false;
  }

  // Record that this side has requested start
  if (is_encoder) {
    source_session_requested_ = true;
  } else {
    sink_session_requested_ = true;
  }

  // Wait for BOTH source and sink to request start before starting HAL session
  if (!source_session_requested_ || !sink_session_requested_) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "%s: Waiting for both sides (source: %s, sink: %s)", __func__,
                        source_session_requested_ ? "ready" : "pending",
                        sink_session_requested_ ? "ready" : "pending");
    return true;
  }

  if (session_started) {
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "%s: Session already started", __func__);
    return true;
  }

  // Both source and sink ready - start HAL session
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Both sides ready, starting HAL session", __func__);

  active_direction_stack_.clear();

  if (!broadcast_hal_clientif->UpdateAudioConfig_2_1(current_audio_config_)) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                        "%s: Failed to update audio config before starting session", __func__);
    return false;
  }

  int result = broadcast_hal_clientif->StartSession();

  if (result == 0) {
    session_started = true;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "%s: Duplex broadcast session started successfully", __func__);
  } else {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: Session start failed", __func__);
  }

  return (result == 0);
}

// ---------------------------------------------------------------------------
// stop_session (with polling gate)
// ---------------------------------------------------------------------------
bool stop_session(bool is_encoder) {
  std::unique_lock<std::mutex> guard(internal_mutex_);

  const char* direction = is_encoder ? "Source (encoder)" : "Sink (decoder)";
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: %s requesting stop", __func__, direction);

  if (broadcast_hal_clientif == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: HAL not initialized", __func__);
    return false;
  }

  // Record that this side has requested stop
  if (is_encoder) {
    source_session_requested_ = false;
  } else {
    sink_session_requested_ = false;
  }

  // End HAL session when BOTH sides have stopped
  if (!source_session_requested_ && !sink_session_requested_) {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Both sides stopped, ending HAL session", __func__);
    if (broadcast_hal_clientif != nullptr) {
      broadcast_hal_clientif->EndSession();
    }
    if (broadcast_transport != nullptr) {
      broadcast_transport->ResetPresentationPosition();
    }
    session_started = false;
    duplex_codec_state_.reset();
    active_direction_stack_.clear();
  } else {
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                        "%s: Waiting for both sides to stop (source: %s, sink: %s)", __func__,
                        source_session_requested_ ? "still active" : "stopped",
                        sink_session_requested_ ? "still active" : "stopped");
  }

  return true;
}

// ---------------------------------------------------------------------------
// end_session
// ---------------------------------------------------------------------------
void end_session() {
  std::unique_lock<std::mutex> guard(internal_mutex_);

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Ending broadcast session", __func__);

  if (broadcast_hal_clientif != nullptr) {
    broadcast_hal_clientif->EndSession();
  }

  if (broadcast_transport != nullptr) {
    broadcast_transport->ResetPresentationPosition();
  }

  session_started = false;
  duplex_codec_state_.reset();
  active_direction_stack_.clear();

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Session ended", __func__);
}

// ---------------------------------------------------------------------------
// Stream acknowledgments (direction-aware)
// ---------------------------------------------------------------------------
void confirm_streaming_request(bool is_encoder) {
  std::unique_lock<std::mutex> guard(internal_mutex_);
  const char* direction = is_encoder ? "encoder (TX)" : "decoder (RX)";
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Confirming streaming request for %s", __func__, direction);
  if (broadcast_hal_clientif != nullptr) {
    broadcast_hal_clientif->StreamStarted(BluetoothAudioCtrlAck::SUCCESS_FINISHED);
  }
}

void confirm_suspend_request(bool is_encoder) {
  std::unique_lock<std::mutex> guard(internal_mutex_);
  const char* direction = is_encoder ? "encoder (TX)" : "decoder (RX)";
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Confirming suspend request for %s", __func__, direction);
  if (broadcast_hal_clientif != nullptr) {
    broadcast_hal_clientif->StreamSuspended(BluetoothAudioCtrlAck::SUCCESS_FINISHED);
  }
}

void cancel_streaming_request(bool is_encoder) {
  std::unique_lock<std::mutex> guard(internal_mutex_);
  const char* direction = is_encoder ? "encoder (TX)" : "decoder (RX)";
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Canceling streaming request for %s", __func__, direction);
  if (broadcast_hal_clientif != nullptr) {
    broadcast_hal_clientif->StreamStarted(BluetoothAudioCtrlAck::FAILURE);
  }
}

// ---------------------------------------------------------------------------
// Audio data
// ---------------------------------------------------------------------------
size_t read(uint8_t* p_buf, uint32_t len) {
  std::unique_lock<std::mutex> guard(internal_mutex_);

  if (broadcast_hal_clientif == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: HAL not initialized", __func__);
    return 0;
  }

  return broadcast_hal_clientif->ReadAudioData(p_buf, len);
}

size_t write(const uint8_t* p_buf, uint32_t len) {
  std::unique_lock<std::mutex> guard(internal_mutex_);

  if (broadcast_hal_clientif == nullptr) {
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "%s: HAL not initialized", __func__);
    return 0;
  }

  size_t bytes_written =
          broadcast_hal_clientif->WriteAudioData(const_cast<uint8_t*>(p_buf), len);

  if (broadcast_transport != nullptr && bytes_written > 0) {
    broadcast_transport->LogBytesRead(bytes_written);
  }

  return bytes_written;
}

// ---------------------------------------------------------------------------
// Delay reporting
// ---------------------------------------------------------------------------
void set_remote_delay(uint16_t delay_report_ms) {
  std::unique_lock<std::mutex> guard(internal_mutex_);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Setting remote delay to %d ms", __func__,
                      static_cast<int>(delay_report_ms));
  if (broadcast_transport != nullptr) {
    broadcast_transport->SetRemoteDelay(delay_report_ms);
  }
}

// ---------------------------------------------------------------------------
// Callback registration (separate for source and sink)
// ---------------------------------------------------------------------------
void register_source_callbacks(std::function<bool(bool)> on_resume, std::function<bool()> on_suspend,
                               std::function<bool()> on_stop) {
  std::unique_lock<std::mutex> guard(internal_mutex_);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Registering source (encoder/TX) callbacks", __func__);
  source_callbacks_.on_resume_  = std::move(on_resume);
  source_callbacks_.on_suspend_ = std::move(on_suspend);
  source_callbacks_.on_stop_    = std::move(on_stop);
}

void register_sink_callbacks(std::function<bool(bool)> on_resume, std::function<bool()> on_suspend,
                             std::function<bool()> on_stop) {
  std::unique_lock<std::mutex> guard(internal_mutex_);
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s: Registering sink (decoder/RX) callbacks", __func__);
  sink_callbacks_.on_resume_  = std::move(on_resume);
  sink_callbacks_.on_suspend_ = std::move(on_suspend);
  sink_callbacks_.on_stop_    = std::move(on_stop);
}

}  // namespace le_audio_broadcast
}  // namespace qti_hidl
}  // namespace audio
}  // namespace bluetooth
