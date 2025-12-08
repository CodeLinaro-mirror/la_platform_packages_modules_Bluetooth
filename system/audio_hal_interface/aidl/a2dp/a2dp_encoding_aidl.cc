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
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#define LOG_TAG "BTAudioA2dpAIDL"

#include "a2dp_encoding_aidl.h"

#include <bluetooth/log.h>
#include <com_android_bluetooth_flags.h>

#include <vector>

#include "a2dp_encoding_aidl_utils.h"
#include "a2dp_provider_info.h"
#include "audio_aidl_interfaces.h"
#include "client_interface_aidl.h"
#include "codec_status_aidl.h"
#include "transport_instance.h"

typedef enum {
  A2DP_CTRL_CMD_NONE,
  A2DP_CTRL_CMD_CHECK_READY,
  A2DP_CTRL_CMD_START,
  A2DP_CTRL_CMD_STOP,
  A2DP_CTRL_CMD_SUSPEND,
  A2DP_CTRL_GET_INPUT_AUDIO_CONFIG,
  A2DP_CTRL_GET_OUTPUT_AUDIO_CONFIG,
  A2DP_CTRL_SET_OUTPUT_AUDIO_CONFIG,
  A2DP_CTRL_GET_PRESENTATION_POSITION,
} tA2DP_CTRL_CMD;

namespace std {
template <>
struct formatter<tA2DP_CTRL_CMD> : enum_formatter<tA2DP_CTRL_CMD> {};
template <>
struct formatter<audio_usage_t> : enum_formatter<audio_usage_t> {};
template <>
struct formatter<audio_content_type_t> : enum_formatter<audio_content_type_t> {};
}  // namespace std

namespace bluetooth {
namespace audio {
namespace aidl {
namespace a2dp {

using ::bluetooth::audio::a2dp::ahal_codec_configuration;

namespace {

using ::bluetooth::audio::a2dp::Status;
using ::bluetooth::audio::a2dp::StreamCallbacks;
using ::bluetooth::audio::aidl::a2dp::LatencyMode;

// Provide call-in APIs for the Bluetooth Audio HAL
class A2dpTransport : public ::bluetooth::audio::aidl::a2dp::IBluetoothTransportInstance {
public:
  A2dpTransport(SessionType sessionType, StreamCallbacks const* stream_callbacks);

  ~A2dpTransport() override;

  Status StartRequest(bool is_low_latency) override;

  Status SuspendRequest() override;

  void StopRequest() override;

  void SetLatencyMode(LatencyMode latency_mode) override;

  bool GetPresentationPosition(uint64_t* remote_delay_report_ns, uint64_t* total_bytes_read,
                               timespec* data_position) override;

  tA2DP_CTRL_CMD GetPendingCmd() const;

  void ResetPendingCmd();

  void ResetPresentationPosition();

  void LogBytesRead(size_t bytes_read) override;

  // delay reports from AVDTP is based on 1/10 ms (100us)
  void SetRemoteDelay(uint16_t delay_report);

private:
  tA2DP_CTRL_CMD a2dp_pending_cmd_;
  uint16_t remote_delay_report_;
  uint64_t total_bytes_read_;
  timespec data_position_;
  StreamCallbacks const* stream_callbacks_;
};

}  // namespace

using ::bluetooth::audio::a2dp::Status;
using ::bluetooth::audio::a2dp::StreamCallbacks;
using bluetooth::audio::a2dp::MAX_A2DP_CONN;

static StreamCallbacks null_stream_callbacks_;

namespace {

using ::aidl::android::hardware::bluetooth::audio::A2dpStreamConfiguration;
using ::aidl::android::hardware::bluetooth::audio::AudioConfiguration;
using ::aidl::android::hardware::bluetooth::audio::ChannelMode;
using ::aidl::android::hardware::bluetooth::audio::CodecConfiguration;
using ::aidl::android::hardware::bluetooth::audio::PcmConfiguration;
using ::aidl::android::hardware::bluetooth::audio::SessionType;

using ::bluetooth::audio::aidl::a2dp::BluetoothAudioClientInterface;
using ::bluetooth::audio::aidl::a2dp::codec::getHalCodecConfiguration;
using ::bluetooth::audio::aidl::a2dp::codec::getHalPcmConfiguration;

/***
 *
 * A2dpTransport functions and variables
 *
 ***/

A2dpTransport::A2dpTransport(SessionType sessionType, StreamCallbacks const* stream_callbacks)
    : IBluetoothTransportInstance(sessionType, (AudioConfiguration){}),
      total_bytes_read_(0),
      data_position_({}),
      stream_callbacks_(stream_callbacks ? stream_callbacks : &null_stream_callbacks_){
  a2dp_pending_cmd_ = A2DP_CTRL_CMD_NONE;
  remote_delay_report_ = 0;
}

A2dpTransport::~A2dpTransport() {
    stream_callbacks_ = nullptr;
}

Status A2dpTransport::StartRequest(bool is_low_latency) {
  // Check if a previous Start request is ongoing.
  if (a2dp_pending_cmd_ == A2DP_CTRL_CMD_START) {
    log::warn("unable to start stream: already pending");
    return Status::PENDING;
  }

  // Check if a different request is ongoing.
  if (a2dp_pending_cmd_ != A2DP_CTRL_CMD_NONE) {
    log::warn("unable to start stream: busy with pending command {}", a2dp_pending_cmd_);
    return Status::FAILURE;
  }

  log::info("");

  auto status = stream_callbacks_->StartStream(is_low_latency);
  a2dp_pending_cmd_ = status == Status::PENDING ? A2DP_CTRL_CMD_START : A2DP_CTRL_CMD_NONE;

  return status;
}

Status A2dpTransport::SuspendRequest() {
  // Check if a previous Suspend request is ongoing.
  if (a2dp_pending_cmd_ == A2DP_CTRL_CMD_SUSPEND) {
    log::warn("unable to suspend stream: already pending");
    return Status::PENDING;
  }

  // Check if a different request is ongoing.
  if (a2dp_pending_cmd_ != A2DP_CTRL_CMD_NONE) {
    log::warn("unable to suspend stream: busy with pending command {}", a2dp_pending_cmd_);
    return Status::FAILURE;
  }

  log::info("");

  auto status = stream_callbacks_->SuspendStream();
  a2dp_pending_cmd_ = status == Status::PENDING ? A2DP_CTRL_CMD_SUSPEND : A2DP_CTRL_CMD_NONE;

  return status;
}

void A2dpTransport::StopRequest() {
  log::info("");

  auto status = stream_callbacks_->StopStream();
  a2dp_pending_cmd_ = status == Status::PENDING ? A2DP_CTRL_CMD_STOP : A2DP_CTRL_CMD_NONE;
}

void A2dpTransport::SetLatencyMode(LatencyMode latency_mode) {
  stream_callbacks_->SetLatencyMode(latency_mode == LatencyMode::LOW_LATENCY);
}

bool A2dpTransport::GetPresentationPosition(uint64_t* remote_delay_report_ns,
                                            uint64_t* total_bytes_read, timespec* data_position) {
  *remote_delay_report_ns = remote_delay_report_ * 100000u;
  *total_bytes_read = total_bytes_read_;
  *data_position = data_position_;
  log::verbose("delay={}/10ms, data={} byte(s), timestamp={}.{}s", remote_delay_report_,
               total_bytes_read_, data_position_.tv_sec, data_position_.tv_nsec);
  return true;
}

tA2DP_CTRL_CMD A2dpTransport::GetPendingCmd() const { return a2dp_pending_cmd_; }

void A2dpTransport::ResetPendingCmd() { a2dp_pending_cmd_ = A2DP_CTRL_CMD_NONE; }

void A2dpTransport::ResetPresentationPosition() {
  remote_delay_report_ = 0;
  total_bytes_read_ = 0;
  data_position_ = {};
}

void A2dpTransport::LogBytesRead(size_t bytes_read) {
  if (bytes_read != 0) {
    total_bytes_read_ += bytes_read;
    clock_gettime(CLOCK_MONOTONIC, &data_position_);
  }
}

// delay reports from AVDTP is based on 1/10 ms (100us)
void A2dpTransport::SetRemoteDelay(uint16_t delay_report) {
  remote_delay_report_ = delay_report;
}

/***
 *
 * Global functions and variables
 *
 ***/

// Common interface to call-out into Bluetooth Audio HAL
BluetoothAudioClientInterface* software_hal_interface[MAX_A2DP_CONN];
BluetoothAudioClientInterface* offloading_hal_interface[MAX_A2DP_CONN];
BluetoothAudioClientInterface* active_hal_interface[MAX_A2DP_CONN];

// ProviderInfo for A2DP hardware offload encoding and decoding data paths,
// if supported by the HAL and enabled. nullptr if not supported
// or disabled.
std::array<std::unique_ptr<::bluetooth::audio::aidl::a2dp::ProviderInfo>, MAX_A2DP_CONN> provider_info;

// Save the value if the remote reports its delay before this interface is
// initialized
uint16_t remote_delay[MAX_A2DP_CONN] = {0};

bool is_low_latency_mode_allowed = false;

}  // namespace

// Check if a2dp source index is valid
bool is_index_valid(uint8_t index) {
  return index < MAX_A2DP_CONN;
}

bool update_codec_offloading_capabilities(
        const std::vector<btav_a2dp_codec_config_t>& framework_preference,
        bool supports_a2dp_hw_offload_v2, uint8_t index) {
  /* Load the provider information if supported by the HAL. */
  provider_info[index] = ::bluetooth::audio::aidl::a2dp::ProviderInfo::GetProviderInfo(
          supports_a2dp_hw_offload_v2, offloading_hal_interface[index]);
  return ::bluetooth::audio::aidl::a2dp::codec::UpdateOffloadingCapabilities(framework_preference,
                                                                             offloading_hal_interface[index]);
}

// Checking if new bluetooth_audio is enabled
bool is_hal_enabled(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }
  return active_hal_interface[index] != nullptr;
}

// Check if new bluetooth_audio is running with offloading encoders
bool is_hal_offloading(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }
  if (!is_hal_enabled(index)) {
    return false;
  }
  return active_hal_interface[index]->GetTransportInstance()->GetSessionType() ==
         SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH;
}

// Opens the HAL client interface of the specified session type and check
// that is is valid. Returns nullptr if the client interface did not open
// properly.
static BluetoothAudioClientInterface* new_hal_interface(SessionType session_type,
                                                        StreamCallbacks const* stream_callbacks,
                                                        uint8_t index) {
  auto a2dp_transport = new A2dpTransport(session_type, stream_callbacks);
  auto hal_interface = new BluetoothAudioClientInterface(a2dp_transport, index);
  if (hal_interface->IsValid()) {
    return hal_interface;
  } else {
    log::error("BluetoothAudio HAL for a2dp is invalid");
    delete a2dp_transport;
    delete hal_interface;
    return nullptr;
  }
}

/// Delete the selected HAL client interface.
static void delete_hal_interface(BluetoothAudioClientInterface* hal_interface) {
  if (hal_interface == nullptr) {
    return;
  }
  auto a2dp_transport = static_cast<A2dpTransport*>(hal_interface->GetTransportInstance());
  delete a2dp_transport;
  delete hal_interface;
}

// Initialize BluetoothAudio HAL: openProvider
bool init(bluetooth::common::MessageLoopThread* /*message_loop*/,
          StreamCallbacks const* stream_callbacks, bool offload_enabled,
          uint8_t index) {
  log::info("");
  log::assert_that(stream_callbacks != nullptr, "stream_callbacks != nullptr");

  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }

  if (software_hal_interface[index] != nullptr) {
    return true;
  }

  software_hal_interface[index] = new_hal_interface(SessionType::A2DP_SOFTWARE_ENCODING_DATAPATH,
                                                    stream_callbacks, index);
  if (software_hal_interface[index] == nullptr) {
    return false;
  }

  if (!software_hal_interface[index]->is_aidl_available()) {
    log::error("BluetoothAudio AIDL implementation does not exist");
    return false;
  }

  if (offload_enabled && offloading_hal_interface[index] == nullptr) {
    offloading_hal_interface[index] =
            new_hal_interface(SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH,
                              stream_callbacks, index);
    if (offloading_hal_interface[index] == nullptr) {
      delete_hal_interface(software_hal_interface[index]);
      software_hal_interface[index] = nullptr;
      return false;
    }
  }

  active_hal_interface[index] =
          (offloading_hal_interface[index] != nullptr ? offloading_hal_interface[index] : software_hal_interface[index]);

  if (remote_delay[index] != 0) {
    log::info("restore DELAY {} ms", static_cast<float>(remote_delay[index] / 10.0));
    static_cast<A2dpTransport*>(active_hal_interface[index]->GetTransportInstance())
            ->SetRemoteDelay(remote_delay[index]);
    remote_delay[index] = 0;
  }
  return true;
}

// Clean up BluetoothAudio HAL
void cleanup() {
  for (int i = 0; i < MAX_A2DP_CONN; i++) {
    cleanup(i);
  }
}

void cleanup(uint8_t index) {
  if (!is_hal_enabled(index)) {
    return;
  }
  end_session(index);

  auto a2dp_sink = active_hal_interface[index]->GetTransportInstance();
  static_cast<A2dpTransport*>(a2dp_sink)->ResetPendingCmd();
  static_cast<A2dpTransport*>(a2dp_sink)->ResetPresentationPosition();
  active_hal_interface[index] = nullptr;

  a2dp_sink = software_hal_interface[index]->GetTransportInstance();
  delete software_hal_interface[index];
  software_hal_interface[index] = nullptr;
  delete a2dp_sink;
  if (offloading_hal_interface[index] != nullptr) {
    a2dp_sink = offloading_hal_interface[index]->GetTransportInstance();
    delete offloading_hal_interface[index];
    offloading_hal_interface[index] = nullptr;
    delete a2dp_sink;
  }

  remote_delay[index] = 0;
}

// Set up the codec into BluetoothAudio HAL
bool setup_codec(const ahal_codec_configuration& config, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return false;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return false;
  }

  if (provider::supports_codec(config.codec_config.codec_type)) {
    // The codec is supported in the provider info (AIDL v4).
    // In this case, the codec is offloaded, and the configuration passed
    // as A2dpStreamConfiguration to the UpdateAudioConfig() interface
    // method.
    A2dpStreamConfiguration a2dp_stream_configuration;

    a2dp_stream_configuration.peerMtu = config.peer_mtu;
    a2dp_stream_configuration.codecId =
            provider_info[index]->GetCodec(config.codec_config.codec_type).value()->id;

    size_t parameters_start = 0;
    size_t parameters_end = 0;
    size_t codec_info_length = static_cast<size_t>(config.codec_specific_information_elements[0]);
    switch (config.codec_config.codec_type) {
      case BTAV_A2DP_CODEC_INDEX_SOURCE_SBC:
      case BTAV_A2DP_CODEC_INDEX_SOURCE_AAC:
        parameters_start = 3;
        parameters_end = 1 + codec_info_length;
        break;
      default:
        parameters_start = 9;
        parameters_end = 1 + codec_info_length;
        break;
    }

    a2dp_stream_configuration.configuration.insert(
            a2dp_stream_configuration.configuration.end(),
            config.codec_specific_information_elements + parameters_start,
            config.codec_specific_information_elements + parameters_end);

    if (!is_hal_offloading(index)) {
      log::warn("Switching BluetoothAudio HAL to Hardware");
      end_session(index);
      active_hal_interface[index] = offloading_hal_interface[index];
    }

    return active_hal_interface[index]->UpdateAudioConfig(AudioConfiguration(a2dp_stream_configuration));
  }

  // Fallback to legacy offloading path.
  AudioConfiguration audio_config{};
  CodecConfiguration codec_config{};
  PcmConfiguration pcm_config{};

  // Compute the codec configuration for the hardware encoding session and
  // check if the parameters are supported.
  if (getHalCodecConfiguration(config, &codec_config)) {
    if (!is_hal_offloading(index)) {
      log::info("Switching BluetoothAudio HAL to Hardware");
      end_session(index);
      active_hal_interface[index] = offloading_hal_interface[index];
    }

    audio_config.set<AudioConfiguration::a2dpConfig>(codec_config);
    return active_hal_interface[index]->UpdateAudioConfig(audio_config);
  }

  // Compute the PCM configuration for the software encoding session and
  // check if the parameters are supported.
  if (getHalPcmConfiguration(config, &pcm_config)) {
    if (is_hal_offloading(index)) {
      log::info("Switching BluetoothAudio HAL to Software");
      end_session(index);
      active_hal_interface[index] = software_hal_interface[index];
    }
    audio_config.set<AudioConfiguration::pcmConfig>(pcm_config);
    return active_hal_interface[index]->UpdateAudioConfig(audio_config);
  }

  log::error(
          "The codec configuration cannot be set for either"
          " software or hardware sessions:\n{}",
          config.ToString());
  return false;
}

void start_session(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  std::vector<LatencyMode> latency_modes = {LatencyMode::FREE};
  if (is_low_latency_mode_allowed) {
    latency_modes.push_back(LatencyMode::LOW_LATENCY);
  }
  active_hal_interface[index]->SetAllowedLatencyModes(latency_modes);
  active_hal_interface[index]->StartSession();
}

void end_session(uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  active_hal_interface[index]->EndSession();
  static_cast<A2dpTransport*>(active_hal_interface[index]->GetTransportInstance())->ResetPendingCmd();
  static_cast<A2dpTransport*>(active_hal_interface[index]->GetTransportInstance())
          ->ResetPresentationPosition();
}

void ack_stream_started(Status ack, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  log::info("result={}, index {}", ack, index);
  auto a2dp_sink = static_cast<A2dpTransport*>(active_hal_interface[index]->GetTransportInstance());
  auto pending_cmd = a2dp_sink->GetPendingCmd();
  if (pending_cmd == A2DP_CTRL_CMD_START) {
    active_hal_interface[index]->StreamStarted(ack);
  } else {
    log::warn("pending={} ignore result={}", pending_cmd, ack);
    return;
  }
  if (ack != Status::PENDING) {
    a2dp_sink->ResetPendingCmd();
  }
}

void ack_stream_suspended(Status ack, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  log::info("result={}", ack);
  auto a2dp_sink = static_cast<A2dpTransport*>(active_hal_interface[index]->GetTransportInstance());
  auto pending_cmd = a2dp_sink->GetPendingCmd();
  if (pending_cmd == A2DP_CTRL_CMD_SUSPEND) {
    active_hal_interface[index]->StreamSuspended(ack);
  } else if (pending_cmd == A2DP_CTRL_CMD_STOP) {
    log::info("A2DP_CTRL_CMD_STOP result={}", ack);
  } else {
    log::warn("pending={} ignore result={}", pending_cmd, ack);
    return;
  }
  if (ack != Status::PENDING) {
    a2dp_sink->ResetPendingCmd();
  }
}

// Read from the FMQ of BluetoothAudio HAL
size_t read(uint8_t* p_buf, uint32_t len, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return 0;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return 0;
  }
  if (is_hal_offloading(index)) {
    log::error("session_type={} is not A2DP_SOFTWARE_ENCODING_DATAPATH",
               toString(active_hal_interface[index]->GetTransportInstance()->GetSessionType()));
    return 0;
  }
  return active_hal_interface[index]->ReadAudioData(p_buf, len);
}

// Update A2DP delay report to BluetoothAudio HAL
void set_remote_delay(uint16_t delay_report, uint8_t index) {
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::info("not ready for DelayReport {} ms", static_cast<float>(delay_report / 10.0));
    remote_delay[index] = delay_report;
    return;
  }
  log::verbose("DELAY {} ms", static_cast<float>(delay_report / 10.0));
  static_cast<A2dpTransport*>(active_hal_interface[index]->GetTransportInstance())
          ->SetRemoteDelay(delay_report);
}

// Set low latency buffer mode allowed or disallowed
void set_low_latency_mode_allowed(bool allowed, uint8_t index) {
  is_low_latency_mode_allowed = allowed;
  if (!is_index_valid(index)) {
    log::error("A2dp source index {} is invalid", index);
    return;
  }
  if (!is_hal_enabled(index)) {
    log::error("BluetoothAudio HAL is not enabled");
    return;
  }
  std::vector<LatencyMode> latency_modes = {LatencyMode::FREE};
  if (is_low_latency_mode_allowed) {
    latency_modes.push_back(LatencyMode::LOW_LATENCY);
  }
  active_hal_interface[index]->SetAllowedLatencyModes(latency_modes);
}

/***
 * Lookup the codec info in the list of supported offloaded sink codecs.
 ***/
std::optional<btav_a2dp_codec_index_t> provider::sink_codec_index(const uint8_t* p_codec_info) {
  // Temporarily hardcoded to index 0 as offload is not used yet.
  // Remove hardcoding when dual A2DP offload is supported.
  // Same rationale applies to other hardcoded indices.
  return provider_info[0] ? provider_info[0]->SinkCodecIndex(p_codec_info) : std::nullopt;
}

/***
 * Lookup the codec info in the list of supported offloaded source codecs.
 ***/
std::optional<btav_a2dp_codec_index_t> provider::source_codec_index(const uint8_t* p_codec_info) {
  return provider_info[0] ? provider_info[0]->SourceCodecIndex(p_codec_info) : std::nullopt;
}

/***
 * Return the name of the codec which is assigned to the input index.
 * The codec index must be in the ranges
 * BTAV_A2DP_CODEC_INDEX_SINK_EXT_MIN..BTAV_A2DP_CODEC_INDEX_SINK_EXT_MAX or
 * BTAV_A2DP_CODEC_INDEX_SOURCE_EXT_MIN..BTAV_A2DP_CODEC_INDEX_SOURCE_EXT_MAX.
 * Returns nullopt if the codec_index is not assigned or codec extensibility
 * is not supported or enabled.
 ***/
std::optional<const char*> provider::codec_index_str(btav_a2dp_codec_index_t codec_index) {
  return provider_info[0] ? provider_info[0]->CodecIndexStr(codec_index) : std::nullopt;
}

/***
 * Return true if the codec is supported for the session type
 * A2DP_HARDWARE_ENCODING_DATAPATH or A2DP_HARDWARE_DECODING_DATAPATH.
 ***/
bool provider::supports_codec(btav_a2dp_codec_index_t codec_index) {
  return provider_info[0] ? provider_info[0]->SupportsCodec(codec_index) : false;
}

/***
 * Return the A2DP capabilities for the selected codec.
 ***/
bool provider::codec_info(btav_a2dp_codec_index_t codec_index, bluetooth::a2dp::CodecId* codec_id,
                          uint8_t* codec_info, btav_a2dp_codec_config_t* codec_config) {
  return provider_info[0]
                 ? provider_info[0]->CodecCapabilities(codec_index, codec_id, codec_info, codec_config)
                 : false;
}

/***
 * Query the codec selection fromt the audio HAL.
 * The HAL is expected to pick the best audio configuration based on the
 * discovered remote SEPs.
 ***/
std::optional<::bluetooth::audio::a2dp::provider::a2dp_configuration>
provider::get_a2dp_configuration(
        RawAddress peer_address,
        std::vector<::bluetooth::audio::a2dp::provider::a2dp_remote_capabilities> const&
        remote_seps, btav_a2dp_codec_config_t const& user_preferences,
        ::bluetooth::a2dp::CodecId user_preferred_codec_id,
        uint8_t index) {
  if (!is_index_valid(index)) {
    return std::nullopt;
  }
  if (provider_info[index] == nullptr) {
    return std::nullopt;
  }

  using ::aidl::android::hardware::bluetooth::audio::A2dpRemoteCapabilities;
  using ::aidl::android::hardware::bluetooth::audio::CodecId;

  // Convert the remote audio capabilities to the exchange format used
  // by the HAL.
  std::vector<A2dpRemoteCapabilities> a2dp_remote_capabilities;
  for (auto const& sep : remote_seps) {
    size_t capabilities_start = 0;
    size_t capabilities_end = 0;
    CodecId id;
    switch (sep.capabilities[2]) {
      case A2DP_MEDIA_CT_SBC:
      case A2DP_MEDIA_CT_AAC: {
        id = CodecId::make<CodecId::a2dp>(static_cast<CodecId::A2dp>(sep.capabilities[2]));
        capabilities_start = 3;
        capabilities_end = 1 + sep.capabilities[0];
        break;
      }
      case A2DP_MEDIA_CT_NON_A2DP: {
        uint32_t vendor_id = (static_cast<uint32_t>(sep.capabilities[3]) << 0) |
                             (static_cast<uint32_t>(sep.capabilities[4]) << 8) |
                             (static_cast<uint32_t>(sep.capabilities[5]) << 16) |
                             (static_cast<uint32_t>(sep.capabilities[6]) << 24);
        uint16_t codec_id = (static_cast<uint16_t>(sep.capabilities[7]) << 0) |
                            (static_cast<uint16_t>(sep.capabilities[8]) << 8);
        id = CodecId::make<CodecId::vendor>(
                CodecId::Vendor({.id = (int32_t)vendor_id, .codecId = codec_id}));
        capabilities_start = 9;
        capabilities_end = 1 + sep.capabilities[0];
        break;
      }
      default:
        continue;
    }
    A2dpRemoteCapabilities& capabilities = a2dp_remote_capabilities.emplace_back();
    capabilities.seid = sep.seid;
    capabilities.id = id;
    capabilities.capabilities.insert(capabilities.capabilities.end(),
                                     sep.capabilities + capabilities_start,
                                     sep.capabilities + capabilities_end);
  }

  // Convert the user preferences into a configuration hint.
  A2dpConfigurationHint hint;
  hint.bdAddr = peer_address.address;
  auto& codecParameters = hint.codecParameters.emplace();
  switch (user_preferences.channel_mode) {
    case BTAV_A2DP_CODEC_CHANNEL_MODE_MONO:
      codecParameters.channelMode = ChannelMode::MONO;
      break;
    case BTAV_A2DP_CODEC_CHANNEL_MODE_STEREO:
      codecParameters.channelMode = ChannelMode::STEREO;
      break;
    default:
      break;
  }
  switch (user_preferences.sample_rate) {
    case BTAV_A2DP_CODEC_SAMPLE_RATE_44100:
      codecParameters.samplingFrequencyHz = 44100;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_48000:
      codecParameters.samplingFrequencyHz = 48000;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_88200:
      codecParameters.samplingFrequencyHz = 88200;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_96000:
      codecParameters.samplingFrequencyHz = 96000;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_176400:
      codecParameters.samplingFrequencyHz = 176400;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_192000:
      codecParameters.samplingFrequencyHz = 192000;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_16000:
      codecParameters.samplingFrequencyHz = 16000;
      break;
    case BTAV_A2DP_CODEC_SAMPLE_RATE_24000:
      codecParameters.samplingFrequencyHz = 24000;
      break;
    default:
      break;
  }
  switch (user_preferences.bits_per_sample) {
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_16:
      codecParameters.bitdepth = 16;
      break;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_24:
      codecParameters.bitdepth = 24;
      break;
    case BTAV_A2DP_CODEC_BITS_PER_SAMPLE_32:
      codecParameters.bitdepth = 32;
      break;
    default:
      break;
  }

  auto aidl_codec_id = convertCodecId(user_preferred_codec_id);
  log::assert_that(aidl_codec_id.has_value(), "convertCodecId failed");
  hint.codecId = aidl_codec_id.value();

  log::info("remote capabilities:");
  for (auto const& sep : a2dp_remote_capabilities) {
    log::info("- {}", sep.toString());
  }
  log::info("hint: {}", hint.toString());

  // Streamcallbacks is hardcoded to null_stream_callbacks_ due to current lack of offload support.
  // To be updated when dual A2DP offload is enabled.
  // Same rationale applies to other hardcoded indices.
  if (offloading_hal_interface[index] == nullptr &&
      (offloading_hal_interface[index] = new_hal_interface(
               SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH, &null_stream_callbacks_, index)) == nullptr) {
    log::error("the offloading HAL interface cannot be opened");
    return std::nullopt;
  }

  // Invoke the HAL GetAdpCapabilities method with the
  // remote capabilities.
  auto result = offloading_hal_interface[index]->GetA2dpConfiguration(a2dp_remote_capabilities, hint);

  // Convert the result configuration back to the stack's format.
  if (!result.has_value()) {
    log::info("provider cannot resolve the a2dp configuration");
    return std::nullopt;
  }

  log::info("provider selected {}", result->toString());
  auto a2dp_configuration = convertA2dpConfiguration(result.value());
  a2dp_configuration.codec_parameters.codec_type =
          provider_info[index]->SourceCodecIndex(result->id).value();
  return std::make_optional(a2dp_configuration);
}

/***
 * Query the codec parameters from the audio HAL.
 * The HAL is expected to parse the codec configuration
 * received from the peer and decide whether accept
 * the it or not.
 ***/
tA2DP_STATUS provider::parse_a2dp_configuration(::bluetooth::a2dp::CodecId codec_id,
                                                const uint8_t* codec_info,
                                                btav_a2dp_codec_config_t* codec_parameters,
                                                std::vector<uint8_t>* vendor_specific_parameters) {
  std::vector<uint8_t> configuration;
  CodecParameters codec_parameters_aidl;

  auto aidl_codec_id = convertCodecId(codec_id);
  log::assert_that(aidl_codec_id.has_value(), "convertCodecId failed");

  std::copy(codec_info, codec_info + AVDT_CODEC_SIZE, std::back_inserter(configuration));

  auto a2dp_status = offloading_hal_interface[0]->ParseA2dpConfiguration(
          aidl_codec_id.value(), configuration, &codec_parameters_aidl);

  if (!a2dp_status.has_value()) {
    log::error("provider failed to parse configuration");
    return A2DP_FAIL;
  }

  convertCodecParameters(codec_parameters_aidl, codec_parameters);

  if (vendor_specific_parameters != nullptr) {
    *vendor_specific_parameters = codec_parameters_aidl.vendorSpecificParameters;
  }

  return static_cast<tA2DP_STATUS>(a2dp_status.value());
}

/***
 * Reads the provider information from the HAL.
 * May return std::nullopt if the HAL Provider Info is empty.
 ***/
std::optional<btav_a2dp_hal_provider_info_t> get_provider_info() {
  log::error("nullopt");
  return std::nullopt; // to be fixed
#if 0
  auto source_provider_info = BluetoothAudioClientInterface::GetProviderInfo(
          SessionType::A2DP_HARDWARE_OFFLOAD_ENCODING_DATAPATH, nullptr);

  auto sink_provider_info = BluetoothAudioClientInterface::GetProviderInfo(
          SessionType::A2DP_HARDWARE_OFFLOAD_DECODING_DATAPATH, nullptr);

  if (!source_provider_info.has_value() && !sink_provider_info.has_value()) {
    log::warn("the provider info is empty");
    return std::nullopt;
  }

  btav_a2dp_hal_provider_info_t codecs_info;

  for (auto& codec_info : source_provider_info->codecInfos) {
    auto source_codec = convertCodecInfo(codec_info);
    if (source_codec.has_value()) {
      log::verbose("provider source codec: {}", source_codec.value().ToString());
      codecs_info.source_codecs.push_back(source_codec.value());
    }
  }

  for (auto& codec_info : sink_provider_info->codecInfos) {
    auto sink_codec = convertCodecInfo(codec_info);
    if (sink_codec.has_value()) {
      log::verbose("provider sink codec: {}", sink_codec.value().ToString());
      codecs_info.sink_codecs.push_back(sink_codec.value());
    }
  }

  log::info("successfully loaded provider info");
  return std::make_optional<btav_a2dp_hal_provider_info_t>(codecs_info);
#endif
}

}  // namespace a2dp
}  // namespace aidl
}  // namespace audio
}  // namespace bluetooth
