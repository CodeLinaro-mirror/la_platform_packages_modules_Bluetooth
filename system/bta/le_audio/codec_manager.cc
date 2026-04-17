/*
 * Copyright 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "codec_manager.h"

#include <bluetooth/log.h>
#include <com_android_bluetooth_flags.h>

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "audio_hal_client/audio_hal_client.h"
#include "audio_hal_interface/le_audio_software.h"
#include "broadcaster/broadcast_configuration_provider.h"
#include "broadcaster/broadcaster_types.h"
#include "bta_le_audio_api.h"
#include "btm_iso_api_types.h"
#include "gmap_client.h"
#include "gmap_server.h"
#include "hardware/bt_le_audio.h"
#include "hci/controller_interface.h"
#include "hci/hci_packets.h"
#include "le_audio/le_audio_types.h"
#include "le_audio_set_configuration_provider.h"
#include "le_audio_utils.h"
#include "main/shim/entry.h"
#include "osi/include/properties.h"
#include "stack/include/btm_client_interface.h"
#include "stack/include/btm_vendor_types.h"
#include "stack/include/hcimsgs.h"

using namespace bluetooth;

namespace {

using bluetooth::hci::iso_manager::kIsoDataPathHci;
using bluetooth::hci::iso_manager::kIsoDataPathPlatformDefault;
using bluetooth::le_audio::CodecManager;
using bluetooth::le_audio::types::CodecLocation;
using bluetooth::legacy::hci::GetInterface;

using bluetooth::le_audio::btle_audio_codec_config_t;
using bluetooth::le_audio::btle_audio_codec_index_t;
using bluetooth::le_audio::types::AseConfiguration;
using bluetooth::le_audio::types::AudioSetConfiguration;
using bluetooth::le_audio::types::AudioSetConfigurations;

typedef struct offloader_stream_maps {
  std::vector<bluetooth::le_audio::stream_map_info> streams_map_target;
  std::vector<bluetooth::le_audio::stream_map_info> streams_map_current;
  bool has_changed;
  bool is_initial;
} offloader_stream_maps_t;
}  // namespace

namespace bluetooth::le_audio {
template <>
offloader_stream_maps_t& types::BidirectionalPair<offloader_stream_maps_t>::get(uint8_t direction) {
  log::assert_that(direction < types::kLeAudioDirectionBoth,
                   "Unsupported complex direction. Reference to a single "
                   "complex direction value is not supported.");
  return (direction == types::kLeAudioDirectionSink) ? sink : source;
}

// The mapping for sampling rate, frame duration, and the QoS config
static std::unordered_map<
        int, std::unordered_map<int, bluetooth::le_audio::broadcaster::BroadcastQosConfig>>
        bcast_high_reliability_qos = {{LeAudioCodecConfiguration::kSampleRate16000,
                                       {{LeAudioCodecConfiguration::kInterval7500Us,
                                         bluetooth::le_audio::broadcaster::qos_config_4_45},
                                        {LeAudioCodecConfiguration::kInterval10000Us,
                                         bluetooth::le_audio::broadcaster::qos_config_4_60}}},
                                      {LeAudioCodecConfiguration::kSampleRate24000,
                                       {{LeAudioCodecConfiguration::kInterval7500Us,
                                         bluetooth::le_audio::broadcaster::qos_config_4_45},
                                        {LeAudioCodecConfiguration::kInterval10000Us,
                                         bluetooth::le_audio::broadcaster::qos_config_4_60}}},
                                      {LeAudioCodecConfiguration::kSampleRate32000,
                                       {{LeAudioCodecConfiguration::kInterval7500Us,
                                         bluetooth::le_audio::broadcaster::qos_config_4_45},
                                        {LeAudioCodecConfiguration::kInterval10000Us,
                                         bluetooth::le_audio::broadcaster::qos_config_4_60}}},
                                      {LeAudioCodecConfiguration::kSampleRate48000,
                                       {{LeAudioCodecConfiguration::kInterval7500Us,
                                         bluetooth::le_audio::broadcaster::qos_config_4_50},
                                        {LeAudioCodecConfiguration::kInterval10000Us,
                                         bluetooth::le_audio::broadcaster::qos_config_4_65}}}};

struct codec_manager_impl {
public:
  codec_manager_impl() {
    offload_enable_ = osi_property_get_bool("ro.bluetooth.leaudio_offload.supported", false) &&
                      !osi_property_get_bool("persist.bluetooth.leaudio_offload.disabled", true);
    if (offload_enable_ == false) {
      log::info("offload disabled");
      return;
    }

    if (!LeAudioHalVerifier::SupportsLeAudioHardwareOffload()) {
      log::warn("HAL not support hardware offload");
      return;
    }

    if (!bluetooth::shim::GetController()->IsSupported(
                bluetooth::hci::OpCode::CONFIGURE_DATA_PATH)) {
      log::warn("Controller does not support config data path command");
      return;
    }
    uint8_t qll_supported_feat_len = 0;
    uint8_t soc_add_on_features_len = 0;
    bool is_apx_lossless_le_supported = false;
    bool is_qhs_enabled_locally = false;

    bt_device_qll_local_supported_features_t* qll_feature_list =
            get_btm_client_interface().vendor.BTM_GetQllLocalSupportedFeatures(
                    &qll_supported_feat_len);

    const bt_device_soc_add_on_features_t* soc_add_on_features =
            get_btm_client_interface().vendor.BTM_GetSocAddOnFeatures(&soc_add_on_features_len);

    bool is_dynamic_bn_over_qhs = BTM_QBCE_QLL_BN_VARIATION_BY_QHS_RATE(qll_feature_list->as_array);
    bool is_dynamic_ft_change_supported = BTM_QBCE_QLL_FT_CHNAGE(qll_feature_list->as_array);

    if (osi_property_get_bool("persist.vendor.qcom.bluetooth.lossless_aptx_adaptive_le.enabled",
                              false)) {
      log::debug("Aptx LE codec enabled at target level");
      is_apx_lossless_le_supported = true;
    }

    char qhs_value[PROPERTY_VALUE_MAX] = "0";
    osi_property_get("persist.vendor.qcom.bluetooth.qhs_support", qhs_value, "255");
    uint8_t qhs_support_mask = (uint8_t)atoi(qhs_value);
    log::debug("QHS support mask {} ", qhs_support_mask);
    if (qhs_support_mask != 0) {
      log::debug("QHS is enabled on this target");
      is_qhs_enabled_locally = true;
    }

    is_aptx_adaptive_le_supported_ = is_dynamic_bn_over_qhs &&
                                     is_dynamic_ft_change_supported &&
                                     is_apx_lossless_le_supported;
    is_aptx_adaptive_lex_supported_ = /*(IsAptxLeXSuppoerted(check sysprop, QLL feat)*/ true;

    is_enhanced_le_gaming_supported_ =
            BTM_QBCE_QLE_HCI_SUPPORTED(soc_add_on_features->as_array) &&
            is_dynamic_ft_change_supported && is_qhs_enabled_locally;

    is_qhs_enabled_ = is_qhs_enabled_locally;

    log::debug("FT Changes allowed {}, BN Variation allowed {}, Aptx LE Lossless enabled {}",
               is_dynamic_ft_change_supported, is_dynamic_bn_over_qhs,
               is_apx_lossless_le_supported);
    log::debug("Aptx LE supported {}, Aptx LEX Supported {}, Enhanced Gaming supported {}",
               is_aptx_adaptive_le_supported_, is_aptx_adaptive_lex_supported_,
               is_enhanced_le_gaming_supported_);

    log::debug("QHS Enabled: {}", is_qhs_enabled_);

    log::info("LeAudioCodecManagerImpl: configure_data_path for encode");
    GetInterface().ConfigureDataPath(hci_data_direction_t::HOST_TO_CONTROLLER,
                                     kIsoDataPathPlatformDefault, {});
    GetInterface().ConfigureDataPath(hci_data_direction_t::CONTROLLER_TO_HOST,
                                     kIsoDataPathPlatformDefault, {});
    SetCodecLocation(CodecLocation::ADSP);
  }
  void start(const std::vector<btle_audio_codec_config_t>& offloading_preference) {
    dual_bidirection_swb_supported_ =
            osi_property_get_bool("bluetooth.leaudio.dual_bidirection_swb.supported", false);
    bluetooth::le_audio::AudioSetConfigurationProvider::Initialize(GetCodecLocation());
    UpdateOffloadCapability(offloading_preference);

    if (IsUsingCodecExtensibility()) {
      codec_provider_info_ =
              audio::le_audio::LeAudioClientInterface::Get()->GetCodecConfigProviderInfo();
      if (codec_provider_info_.has_value() && codec_provider_info_->allowAsymmetric &&
          codec_provider_info_->lowLatency) {
        GmapClient::UpdateGmapOffloaderSupport(true);
        GmapServer::UpdateGmapOffloaderSupport(true);
        log::debug("Asymmetric configuration supported. Enabling offloader GMAP support.");
      } else {
        log::debug("Asymmetric configurations not supported. Not enabling offloader GMAP support.");
      }
    }
  }
  ~codec_manager_impl() {
    if (GetCodecLocation() != CodecLocation::HOST) {
      GetInterface().ConfigureDataPath(hci_data_direction_t::HOST_TO_CONTROLLER, kIsoDataPathHci,
                                       {});
      GetInterface().ConfigureDataPath(hci_data_direction_t::CONTROLLER_TO_HOST, kIsoDataPathHci,
                                       {});
    }
    bluetooth::le_audio::AudioSetConfigurationProvider::Cleanup();
  }
  CodecLocation GetCodecLocation(void) const { return codec_location_; }

  std::optional<ProviderInfo> GetCodecConfigProviderInfo(void) const {
    return codec_provider_info_;
  }

  bool IsDualBiDirSwbSupported(void) const {
    if (GetCodecLocation() == CodecLocation::ADSP) {
      // Whether dual bidirection swb is supported by property and for offload
      return offload_dual_bidirection_swb_supported_;
    } else if (GetCodecLocation() == CodecLocation::HOST) {
      // Whether dual bidirection swb is supported for software
      return dual_bidirection_swb_supported_;
    }

    return false;
  }

  bool IsAptxAdaptiveLeSupported(void) const { return is_aptx_adaptive_le_supported_; }

  bool IsAptxAdaptiveLeXSupported(void) const { return is_aptx_adaptive_lex_supported_; }

  bool IsEnhancedLeGamingSupported(void) const { return is_enhanced_le_gaming_supported_; }

  bool IsQhsEnabled(void) const { return is_qhs_enabled_; }

  std::vector<bluetooth::le_audio::btle_audio_codec_config_t> GetLocalAudioOutputCodecCapa() {
    return codec_output_capa;
  }

  std::vector<bluetooth::le_audio::btle_audio_codec_config_t> GetLocalAudioInputCodecCapa() {
    return codec_input_capa;
  }

  void UpdateActiveAudioConfig(
          const types::BidirectionalPair<stream_parameters>& stream_params,
          types::LeAudioCodecId id,
          std::function<void(const stream_config& config, uint8_t direction)> update_receiver,
          uint8_t remote_directions_to_update) {
    if (GetCodecLocation() != bluetooth::le_audio::types::CodecLocation::ADSP) {
      return;
    }

    log::debug("");

    for (auto direction : {bluetooth::le_audio::types::kLeAudioDirectionSink,
                           bluetooth::le_audio::types::kLeAudioDirectionSource}) {
      log::debug("direction: {}", direction);
      /* Update only the requested directions */
      if ((remote_directions_to_update & direction) != direction) {
        continue;
      }

      auto& stream_map = offloader_stream_maps.get(direction);
      if (!stream_map.has_changed && !stream_map.is_initial) {
        log::warn("unexpected call for direction {}, stream_map.has_changed {}", direction,
                  stream_map.has_changed, stream_map.is_initial);
        continue;
      }
      if (stream_params.get(direction).stream_config.stream_map.empty()) {
        log::warn("unexpected call, stream is empty for direction {}, ", direction);
        continue;
      }
      uint16_t delay = 0;
      if (stream_params.get(direction).stream_config.peer_delay_ms != 0xFFFF) {
        delay = stream_params.get(direction).stream_config.peer_delay_ms;
      } else {
        //Todo
        delay = stream_params.get(direction).stream_config.peer_delay_ms;
      }

      auto unicast_cfg = stream_params.get(direction).stream_config;
      log::debug( ": coding_format = {}, vendor_codec_id = {}",
                  unicast_cfg.codec_id.coding_format, unicast_cfg.codec_id.vendor_codec_id);
      log::debug("is_initial: {}, SupportStreamActiveApi: {}",
            stream_map.is_initial,LeAudioHalVerifier::SupportsStreamActiveApi());
      unicast_cfg.stream_map = stream_map.streams_map_target;
      unicast_cfg.codec_id = id;

      update_receiver(unicast_cfg, direction);
      stream_map.is_initial = false;
    }
  }

  bool UpdateActiveUnicastAudioHalClient(LeAudioSourceAudioHalClient* source_unicast_client,
                                         LeAudioSinkAudioHalClient* sink_unicast_client,
                                         bool is_active) {
    log::debug("local_source: {}, local_sink: {}, is_active: {}",
               std::format_ptr(source_unicast_client), std::format_ptr(sink_unicast_client),
               is_active);

    if (source_unicast_client == nullptr && sink_unicast_client == nullptr) {
      return false;
    }

    if (is_active) {
      if (source_unicast_client && unicast_local_source_hal_client != nullptr) {
        log::error("Trying to override previous source hal client {}",
                   std::format_ptr(unicast_local_source_hal_client));
        return false;
      }

      if (sink_unicast_client && unicast_local_sink_hal_client != nullptr) {
        log::error("Trying to override previous sink hal client {}",
                   std::format_ptr(unicast_local_sink_hal_client));
        return false;
      }

      if (source_unicast_client) {
        unicast_local_source_hal_client = source_unicast_client;
      }

      if (sink_unicast_client) {
        unicast_local_sink_hal_client = sink_unicast_client;
      }

      return true;
    }

    if (source_unicast_client && source_unicast_client != unicast_local_source_hal_client) {
      log::error("local source session does not match {} != {}",
                 std::format_ptr(source_unicast_client),
                 std::format_ptr(unicast_local_source_hal_client));
      return false;
    }

    if (sink_unicast_client && sink_unicast_client != unicast_local_sink_hal_client) {
      log::error("local source session does not match {} != {}",
                 std::format_ptr(sink_unicast_client),
                 std::format_ptr(unicast_local_sink_hal_client));
      return false;
    }

    if (source_unicast_client) {
      unicast_local_source_hal_client = nullptr;
    }

    if (sink_unicast_client) {
      unicast_local_sink_hal_client = nullptr;
    }

    return true;
  }

  bool UpdateActiveBroadcastAudioHalClient(LeAudioSourceAudioHalClient* source_broadcast_client,
                                           bool is_active) {
    log::debug("local_source: {},is_active: {}", std::format_ptr(source_broadcast_client),
               is_active);

    if (source_broadcast_client == nullptr) {
      return false;
    }

    if (is_active) {
      if (broadcast_local_source_hal_client != nullptr) {
        log::error("Trying to override previous source hal client {}",
                   std::format_ptr(broadcast_local_source_hal_client));
        return false;
      }
      broadcast_local_source_hal_client = source_broadcast_client;
      return true;
    }

    if (source_broadcast_client != broadcast_local_source_hal_client) {
      log::error("local source session does not match {} != {}",
                 std::format_ptr(source_broadcast_client),
                 std::format_ptr(broadcast_local_source_hal_client));
      return false;
    }

    broadcast_local_source_hal_client = nullptr;

    return true;
  }

  std::unique_ptr<AudioSetConfiguration> GetLocalCodecConfigurations(
          const CodecManager::UnicastConfigurationRequirements& requirements,
          CodecManager::UnicastConfigurationProvider provider) const {
    AudioSetConfigurations configs;
    if (GetCodecLocation() == le_audio::types::CodecLocation::ADSP) {
      log::verbose("Get offload config for the context type: {}",
                   (int)requirements.audio_context_type);
      // TODO: Need to have a mechanism to switch to software session if offload
      // doesn't support.
      configs = context_type_offload_config_map_.count(requirements.audio_context_type)
                        ? context_type_offload_config_map_.at(requirements.audio_context_type)
                        : AudioSetConfigurations();
    } else {
      log::verbose("Get software config for the context type: {}",
                   (int)requirements.audio_context_type);
      configs = *AudioSetConfigurationProvider::Get()->GetConfigurations(
              requirements.audio_context_type);
    }

    if (configs.empty()) {
      log::error("No valid configuration matching the requirements: {}", requirements);
      PrintDebugState();
      return nullptr;
    }

    // Remove the dual bidir SWB config if not supported
    if (!IsDualBiDirSwbSupported()) {
      configs.erase(std::remove_if(configs.begin(), configs.end(),
                                   [](auto const& el) {
                                     if (el->confs.source.empty()) {
                                       return false;
                                     }
                                     return AudioSetConfigurationProvider::Get()
                                             ->CheckConfigurationIsDualBiDirSwb(*el);
                                   }),
                    configs.end());
    }
    // Remove the enhanced gaming LC3Qv2 config if not supported
    if (!IsEnhancedLeGamingSupported()) {
      configs.erase(
              std::remove_if(
                      configs.begin(), configs.end(),
                      [](auto const& el) {
                        return AudioSetConfigurationProvider::Get()->CheckEnhancedGamingConfig(*el);
                      }),
              configs.end());
    }

    if (!IsQhsEnabled()) {
      configs.erase(
              std::remove_if(
                      configs.begin(), configs.end(),
                      [](auto const& el) {
                        return AudioSetConfigurationProvider::Get()->CheckQHSConfig(*el);
                      }),
              configs.end());
    }

    // Note: For the software configuration provider, we use the provider matcher
    //       logic to match the proper configuration with group capabilities.
    return provider(requirements, &configs);
  }

  void PrintDebugState() const {
    for (types::LeAudioContextType ctx_type : types::kLeAudioContextAllTypesArray) {
      std::stringstream os;
      os << ctx_type << ": ";
      if (context_type_offload_config_map_.count(ctx_type) == 0) {
        os << "{empty}";
      } else {
        os << "{";
        for (const auto& conf : context_type_offload_config_map_.at(ctx_type)) {
          os << conf->name << ", ";
        }
        os << "}";
      }
      log::info("Offload configs for {}", os.str());
    }
  }

  bool IsUsingCodecExtensibility() const {
    if (GetCodecLocation() == types::CodecLocation::HOST) {
      return false;
    }

    auto codec_ext_status =
            osi_property_get_bool("bluetooth.core.le_audio.codec_extension_aidl.enabled", false);

    log::debug("Using codec extensibility AIDL: {}", codec_ext_status);
    return codec_ext_status;
  }

  std::unique_ptr<AudioSetConfiguration> GetCodecConfig(
          const CodecManager::UnicastConfigurationRequirements& requirements,
          CodecManager::UnicastConfigurationProvider provider) {
    if (IsUsingCodecExtensibility() && unicast_local_source_hal_client) {
      auto hal_config = unicast_local_source_hal_client->GetUnicastConfig(requirements);
      if (hal_config) {
        return std::make_unique<AudioSetConfiguration>(*hal_config);
      }
      log::debug("No configuration received from AIDL, fall back to static configuration.");
    }
    return GetLocalCodecConfigurations(requirements, provider);
  }

  bool CheckCodecConfigIsBiDirSwb(const AudioSetConfiguration& config) {
    return AudioSetConfigurationProvider::Get()->CheckConfigurationIsBiDirSwb(config);
  }

  bool CheckCodecConfigIsDualBiDirSwb(const AudioSetConfiguration& config) {
    return AudioSetConfigurationProvider::Get()->CheckConfigurationIsDualBiDirSwb(config);
  }

  void UpdateSupportedBroadcastConfig(const std::vector<AudioSetConfiguration>& adsp_capabilities) {
    log::info("UpdateSupportedBroadcastConfig");

    for (const auto& adsp_audio_set_conf : adsp_capabilities) {
      if (adsp_audio_set_conf.confs.sink.empty() || !adsp_audio_set_conf.confs.source.empty()) {
        continue;
      }

      auto& adsp_config = adsp_audio_set_conf.confs.sink[0];

      const types::LeAudioCoreCodecConfig core_config =
              adsp_config.codec.params.GetAsCoreCodecConfig();
      bluetooth::le_audio::broadcast_offload_config broadcast_config;
      broadcast_config.stream_map.resize(adsp_audio_set_conf.confs.sink.size());

      // Enable the individual channels per BIS in the stream map
      auto all_channels = adsp_config.codec.channel_count_per_iso_stream;
      uint8_t channel_alloc_idx = 0;
      for (auto& [_, channels] : broadcast_config.stream_map) {
        if (all_channels) {
          channels |= (0b1 << channel_alloc_idx++);
          --all_channels;
        }
      }

      broadcast_config.bits_per_sample = LeAudioCodecConfiguration::kBitsPerSample24;
      broadcast_config.sampling_rate = core_config.GetSamplingFrequencyHz();
      broadcast_config.frame_duration = core_config.GetFrameDurationUs();
      broadcast_config.octets_per_frame = *(core_config.octets_per_codec_frame);
      broadcast_config.blocks_per_sdu = 1;

      int sample_rate = broadcast_config.sampling_rate;
      int frame_duration = broadcast_config.frame_duration;

      if (osi_property_get_bool("persist.vendor.btstack.bis_qos_config.enabled", true)) {
        uint8_t rtn = (uint8_t)osi_property_get_int32("persist.vendor.btstack.bis_rtn", 2);
        uint16_t max_transport_latency =
                (uint16_t)osi_property_get_int32("persist.vendor.btstack.transport_latency", 0);

        if (max_transport_latency == 0) {
          switch (frame_duration) {
            case LeAudioCodecConfiguration::kInterval7500Us:
              max_transport_latency = 45;  // 45msec for 7.5msec frame duration
              break;
            case LeAudioCodecConfiguration::kInterval10000Us:
              [[fallthrough]];
            default:
              max_transport_latency = 61;  // 61msec for 10msec frame duration
              break;
          }
        }
        log::info("broadcast_config rtn: {}, max_transport_latency: {}", rtn,
                  max_transport_latency);
        broadcast_config.retransmission_number = rtn;
        broadcast_config.max_transport_latency = max_transport_latency;
        supported_broadcast_config.push_back(broadcast_config);
      } else if (bcast_high_reliability_qos.find(sample_rate) != bcast_high_reliability_qos.end() &&
                 bcast_high_reliability_qos[sample_rate].find(frame_duration) !=
                         bcast_high_reliability_qos[sample_rate].end()) {
        auto qos = bcast_high_reliability_qos[sample_rate].at(frame_duration);
        broadcast_config.retransmission_number = qos.getRetransmissionNumber();
        broadcast_config.max_transport_latency = qos.getMaxTransportLatency();
        supported_broadcast_config.push_back(broadcast_config);
      } else {
        log::error(
                "Cannot find the correspoding QoS config for the sampling_rate: "
                "{}, frame_duration: {}",
                sample_rate, frame_duration);
      }

      log::info("broadcast_config sampling_rate: {}", broadcast_config.sampling_rate);
    }
  }

  int GetBroadcastTargetConfigByProperty(uint8_t preferred_quality) {
    char prop_value[PROPERTY_VALUE_MAX] = {0};
    osi_property_get("persist.vendor.btstack.bis_audio_config_setting", prop_value, "");
    uint32_t preferred_sampling_rate = -1;
    uint32_t preferred_octets_per_frame = -1;

    if (!strcmp(prop_value, "16_2")) {
      preferred_sampling_rate = 16000u;
      preferred_octets_per_frame = 40;
    } else if (!strcmp(prop_value, "24_2")) {
      preferred_sampling_rate = 24000u;
      preferred_octets_per_frame = 60;
    } else if (!strcmp(prop_value, "48_1")) {
      preferred_sampling_rate = 48000u;
      preferred_octets_per_frame = 75;
    } else if (!strcmp(prop_value, "48_2")) {
      preferred_sampling_rate = 48000u;
      preferred_octets_per_frame = 100;
    } else if (!strcmp(prop_value, "48_3")) {
      preferred_sampling_rate = 48000u;
      preferred_octets_per_frame = 90;
    } else if (!strcmp(prop_value, "48_4")) {
      preferred_sampling_rate = 48000u;
      preferred_octets_per_frame = 120;
    } else if (!strcmp(prop_value, "48_5")) {
      preferred_sampling_rate = 48000u;
      preferred_octets_per_frame = 117;
    } else if (!strcmp(prop_value, "48_6")) {
      preferred_sampling_rate = 48000u;
      preferred_octets_per_frame = 155;
    } else {
      if (preferred_quality == bluetooth::le_audio::QUALITY_STANDARD) {
        preferred_sampling_rate = 16000u;  // 16_2
        preferred_octets_per_frame = 40;
      } else {                             // perferred_quality = bluetooth::le_audio::QUALITY_HIGH
        preferred_sampling_rate = 48000u;  // 48_2
        preferred_octets_per_frame = 100;
      }
    }

    int target_config = -1;
    for (int i = 0; i < (int)supported_broadcast_config.size(); i++) {
      if (supported_broadcast_config[i].sampling_rate == preferred_sampling_rate &&
          supported_broadcast_config[i].octets_per_frame == preferred_octets_per_frame) {
        target_config = i;
        break;
      }
    }

    log::info("GetBroadcastTargetConfigByProperty: target_config: {}", target_config);
    return target_config;
  }

  const broadcast_offload_config* GetBroadcastSinkOffloadConfig(
          const BasicAudioAnnouncementData& base_data,
          const std::vector<uint8_t>& bis_indices) {
    log::info("GetBroadcastSinkOffloadConfig");

    // Validate BASE data
    if (base_data.subgroup_configs.empty()) {
      log::error("BASE data has no subgroups");
      return nullptr;
    }

    // Extract codec parameters from BASE data (focus on first subgroup)
    auto& subgroup = base_data.subgroup_configs[0];

    // Convert codec_specific_params map to LeAudioLtvMap to extract parameters
    auto ltv_map = types::LeAudioLtvMap(subgroup.codec_config.codec_specific_params);
    auto codec_config = ltv_map.GetAsCoreCodecConfig();

    uint32_t source_sampling_rate = codec_config.GetSamplingFrequencyHz();
    uint32_t source_frame_duration = codec_config.GetFrameDurationUs();
    uint16_t source_octets_per_frame = codec_config.GetOctetsPerFrame();

    log::info(
            "Source config: sampling_rate={}, frame_duration={}, "
            "octets_per_frame={}",
            source_sampling_rate, source_frame_duration, source_octets_per_frame);

    // Enhanced broadcast sink has >= 3 BISes per subgroup.  The standard
    // offload table only has 1- or 2-stream entries, so we cannot require
    // stream_map.size() == bis_indices.size() for enhanced sources.
    // For standard sink (1 or 2 BISes) we keep the strict size check so that
    // the existing source path is not affected.
    const bool is_enhanced_sink = (bis_indices.size() > 2);
    log::info("GetBroadcastSinkOffloadConfig: bis_indices.size()={}, is_enhanced_sink={}",
              bis_indices.size(), is_enhanced_sink);

    broadcast_sink_target_config = -1;
    for (size_t i = 0; i < supported_broadcast_config.size(); i++) {
      bool codec_params_match =
              (supported_broadcast_config[i].sampling_rate == source_sampling_rate &&
               supported_broadcast_config[i].frame_duration == source_frame_duration &&
               supported_broadcast_config[i].octets_per_frame == source_octets_per_frame);

      // For standard sink: also require stream_map size to match exactly.
      // For enhanced sink: relax the size check — any matching codec entry is used.
      bool size_match = is_enhanced_sink ||
                        (supported_broadcast_config[i].stream_map.size() == bis_indices.size());

      if (codec_params_match && size_match) {
        broadcast_sink_target_config = static_cast<int>(i);
        log::info("Found matching sink offload configuration at index {} "
                  "(stream_map.size={}, bis_indices.size={}, is_enhanced={})",
                  i, supported_broadcast_config[i].stream_map.size(),
                  bis_indices.size(), is_enhanced_sink);
        break;
      }
    }

    if (broadcast_sink_target_config == -1) {
      log::error(
              "No matching sink offload configuration for source BASE data "
              "(sampling_rate={}, frame_duration={}, octets_per_frame={}, bis_count={})",
              source_sampling_rate, source_frame_duration, source_octets_per_frame,
              bis_indices.size());
      return nullptr;
    }

    // For enhanced broadcast sink only: resize stream_map to match the actual
    // number of BISes.  Each slot will be filled with the ISO connection handle
    // and MONO audio location by UpdateBroadcastConnHandle() after BIG sync.
    // Standard sink (1 or 2 BISes) already has the correct stream_map size.
    if (is_enhanced_sink) {
      log::info("GetBroadcastSinkOffloadConfig: enhanced sink — resizing stream_map "
                "from {} to {} BIS slots",
                supported_broadcast_config[broadcast_sink_target_config].stream_map.size(),
                bis_indices.size());
      supported_broadcast_config[broadcast_sink_target_config].stream_map.resize(
              bis_indices.size());
    }

    log::info(
            "Matched offload config: sampling_rate={}, frame_duration={}, "
            "octets_per_frame={}, retransmission_number={}, max_transport_latency={}",
            supported_broadcast_config[broadcast_sink_target_config].sampling_rate,
            supported_broadcast_config[broadcast_sink_target_config].frame_duration,
            supported_broadcast_config[broadcast_sink_target_config].octets_per_frame,
            supported_broadcast_config[broadcast_sink_target_config].retransmission_number,
            supported_broadcast_config[broadcast_sink_target_config].max_transport_latency);

    return &supported_broadcast_config[broadcast_sink_target_config];
  }

  std::unique_ptr<broadcast_sink::BroadcastSinkConfiguration> GetBroadcastSinkConfig(
          const CodecManager::BroadcastSinkConfigurationRequirements& requirements) {
    log::info("GetBroadcastSinkConfig");

    if (requirements.base_data.subgroup_configs.empty()) {
      log::error("GetBroadcastSinkConfig: no subgroup configs in BASE data");
      return nullptr;
    }

    // Extract codec parameters from BASE data (first subgroup)
    auto& subgroup = requirements.base_data.subgroup_configs[0];
    auto ltv_map = types::LeAudioLtvMap(subgroup.codec_config.codec_specific_params);

    types::DataPathConfiguration data_path;

    // Enhanced broadcast sink (>= 3 BISes) always uses the platform offload
    // (DSP) data path for both TX and RX ISO paths, regardless of codec location.
    // Standard broadcast sink preserves the original behavior:
    //   HOST mode  → kIsoDataPathHci (0x00)  — LC3 runs on host CPU
    //   ADSP mode  → kIsoDataPathPlatformDefault (0x01) — offload DSP
    // This ensures broadcast source and unicast audio are not affected.
    const bool is_enhanced_sink = (requirements.bis_indices.size() >= 3);
    const bool use_offload_path =
            is_enhanced_sink || (GetCodecLocation() == types::CodecLocation::ADSP);

    log::info("GetBroadcastSinkConfig: is_enhanced_sink={}, codec_location={}, "
              "data_path={}",
              is_enhanced_sink, static_cast<int>(GetCodecLocation()),
              use_offload_path ? "offload(platform-default)" : "HCI");

    // When using the offload (platform-default) path, isTransparent must be
    // false so that the LC3 codec ID (0x06) is sent in LE_SETUP_ISO_DATA_PATH.
    // When using the HCI path (HOST mode, standard sink), isTransparent = true
    // sends codec_id = transparent (0x03) which is correct for host-side LC3.
    // This mirrors the broadcaster pattern:
    //   lc3_data_path        → HCI path,     isTransparent=true  → codec 0x03
    //   lc3_data_path_duplex → offload path, isTransparent=false → codec 0x06
    data_path.dataPathId = use_offload_path
            ? bluetooth::hci::iso_manager::kIsoDataPathPlatformDefault
            : bluetooth::hci::iso_manager::kIsoDataPathHci;
    data_path.dataPathConfig = {};
    data_path.isoDataPathConfig.codecId = {
            .coding_format = types::kLeAudioCodingFormatLC3,
            .vendor_company_id = types::kLeAudioVendorCompanyIdUndefined,
            .vendor_codec_id = types::kLeAudioVendorCodecIdUndefined};
    data_path.isoDataPathConfig.isTransparent = !use_offload_path;  // false→LC3, true→transparent
    data_path.isoDataPathConfig.controllerDelayUs = 0x00000000;
    data_path.isoDataPathConfig.configuration = {};

    // Construct BroadcastSubgroupCodecConfig from BASE data.
    // bits_per_sample is fixed at 16 — standard LC3 resolution in the audio framework.
    broadcaster::BroadcastSubgroupCodecConfig codec_config(
            broadcaster::kLeAudioCodecIdLc3,
            {broadcaster::BroadcastSubgroupBisCodecConfig(
                    static_cast<uint8_t>(requirements.bis_indices.size()),
                    1,  // channel_count_per_bis (MONO per BIS)
                    ltv_map)},
            16 /* bits_per_sample */);

    // Construct BroadcastSinkConfiguration
    auto sink_config = std::make_unique<broadcast_sink::BroadcastSinkConfiguration>();
    sink_config->subgroups.push_back(codec_config);
    sink_config->bis_indices = requirements.bis_indices;
    sink_config->data_path = data_path;
    sink_config->big_sync_timeout = broadcast_sink::kDefaultBigSyncTimeout;
    sink_config->mse = broadcast_sink::kDefaultMse;

    log::info(
            "Created BroadcastSinkConfiguration: num_subgroups={}, data_path={}, "
            "num_bis={}, big_sync_timeout={}, mse={}",
            sink_config->subgroups.size(), sink_config->data_path.dataPathId,
            sink_config->bis_indices.size(), sink_config->big_sync_timeout,
            sink_config->mse);

    return sink_config;
  }

  const broadcast_offload_config* GetBroadcastOffloadConfig(uint8_t preferred_quality) {
    if (supported_broadcast_config.empty()) {
      log::error("There is no valid broadcast offload config");
      return nullptr;
    }
    /* Broadcast audio config selection based on source broadcast capability
     *
     * If the preferred_quality is HIGH, the configs ranking is
     * 48_4 > 48_2 > 24_2(sink mandatory) > 16_2(source & sink mandatory)
     *
     * If the preferred_quality is STANDARD, the configs ranking is
     * 24_2(sink mandatory) > 16_2(source & sink mandatory)
     */
    broadcast_target_config = -1;
    for (int i = 0; i < (int)supported_broadcast_config.size(); i++) {
      if (preferred_quality == bluetooth::le_audio::QUALITY_STANDARD) {
        if (supported_broadcast_config[i].sampling_rate == 24000u &&
            supported_broadcast_config[i].octets_per_frame == 60) {  // 24_2
          broadcast_target_config = i;
          break;
        }

        if (supported_broadcast_config[i].sampling_rate == 16000u &&
            supported_broadcast_config[i].octets_per_frame == 40) {  // 16_2
          broadcast_target_config = i;
        }

        continue;
      }

      // perferred_quality = bluetooth::le_audio::QUALITY_HIGH
      if (supported_broadcast_config[i].sampling_rate == 48000u &&
          supported_broadcast_config[i].octets_per_frame == 120) {  // 48_4
        broadcast_target_config = i;
        break;
      }

      if ((supported_broadcast_config[i].sampling_rate == 48000u &&
           supported_broadcast_config[i].octets_per_frame == 100) ||  // 48_2
          (supported_broadcast_config[i].sampling_rate == 24000u &&
           supported_broadcast_config[i].octets_per_frame == 60) ||  // 24_2
          (supported_broadcast_config[i].sampling_rate == 16000u &&
           supported_broadcast_config[i].octets_per_frame == 40)) {  // 16_2
        if (broadcast_target_config == -1 ||
            (supported_broadcast_config[i].sampling_rate >
             supported_broadcast_config[broadcast_target_config].sampling_rate)) {
          broadcast_target_config = i;
        }
      }
    }

    if (osi_property_get_bool("persist.vendor.btstack.bis_audio_config.enabled", true)) {
      broadcast_target_config = GetBroadcastTargetConfigByProperty(preferred_quality);
    }

    if (broadcast_target_config == -1) {
      log::error("There is no valid broadcast offload config with preferred_quality");
      return nullptr;
    }

    log::info(
            "stream_map.size(): {}, sampling_rate: {}, frame_duration(us): {}, "
            "octets_per_frame: {}, blocks_per_sdu {}, retransmission_number: {}, "
            "max_transport_latency: {}",
            supported_broadcast_config[broadcast_target_config].stream_map.size(),
            supported_broadcast_config[broadcast_target_config].sampling_rate,
            supported_broadcast_config[broadcast_target_config].frame_duration,
            supported_broadcast_config[broadcast_target_config].octets_per_frame,
            (int)supported_broadcast_config[broadcast_target_config].blocks_per_sdu,
            (int)supported_broadcast_config[broadcast_target_config].retransmission_number,
            supported_broadcast_config[broadcast_target_config].max_transport_latency);

    return &supported_broadcast_config[broadcast_target_config];
  }

  void UpdateBroadcastOffloadConfig(const broadcaster::BroadcastConfiguration& config) {
    if (config.subgroups.empty()) {
      broadcast_target_config = -1;
      return;
    }

    // Use the first configuration slot
    broadcast_target_config = 0;
    auto& offload_cfg = supported_broadcast_config[broadcast_target_config];

    // Note: Currently only a single subgroup offloading is supported
    auto const& subgroup = config.subgroups.at(0);
    auto subgroup_config = subgroup.GetCommonBisCodecSpecData().GetAsCoreCodecConfig();

    offload_cfg.sampling_rate = subgroup_config.GetSamplingFrequencyHz();
    offload_cfg.frame_duration = subgroup_config.GetFrameDurationUs();
    offload_cfg.octets_per_frame = subgroup_config.GetOctetsPerFrame();
    offload_cfg.blocks_per_sdu = 1;
    offload_cfg.stream_map.resize(subgroup.GetNumBis());

    log::info(
            "stream_map.size(): {}, sampling_rate: {}, frame_duration(us): {}, "
            "octets_per_frame: {}, blocks_per_sdu {}, retransmission_number: {}, "
            "max_transport_latency: {}",
            supported_broadcast_config[broadcast_target_config].stream_map.size(),
            supported_broadcast_config[broadcast_target_config].sampling_rate,
            supported_broadcast_config[broadcast_target_config].frame_duration,
            supported_broadcast_config[broadcast_target_config].octets_per_frame,
            (int)supported_broadcast_config[broadcast_target_config].blocks_per_sdu,
            (int)supported_broadcast_config[broadcast_target_config].retransmission_number,
            supported_broadcast_config[broadcast_target_config].max_transport_latency);
  }

  std::unique_ptr<broadcaster::BroadcastConfiguration> GetBroadcastConfig(
          const CodecManager::BroadcastConfigurationRequirements& requirements) {
    if (GetCodecLocation() != types::CodecLocation::ADSP) {
      // Get the software supported broadcast configuration
      return std::make_unique<broadcaster::BroadcastConfiguration>(
              ::bluetooth::le_audio::broadcaster::GetBroadcastConfig(
                      requirements.subgroup_quality));
    }

    /* Subgroups with different audio qualities is not being supported now,
     * if any subgroup preferred to use standard audio config, choose
     * the standard audio config instead
     */
    uint8_t BIG_audio_quality = bluetooth::le_audio::QUALITY_HIGH;
    for (const auto& [_, quality] : requirements.subgroup_quality) {
      if (quality == bluetooth::le_audio::QUALITY_STANDARD) {
        BIG_audio_quality = bluetooth::le_audio::QUALITY_STANDARD;
      }
    }

    if (IsUsingCodecExtensibility()) {
      log::assert_that(broadcast_local_source_hal_client != nullptr,
                       "audio source hal client is NULL");
      auto hal_config = broadcast_local_source_hal_client->GetBroadcastConfig(
              requirements.subgroup_quality, requirements.sink_pacs);
      if (hal_config.has_value()) {
        UpdateBroadcastOffloadConfig(hal_config.value());
        return std::make_unique<broadcaster::BroadcastConfiguration>(hal_config.value());
      }

      log::debug(
              "No configuration received from AIDL, fall back to static "
              "configuration.");
    }

    auto offload_config = GetBroadcastOffloadConfig(BIG_audio_quality);
    if (offload_config == nullptr) {
      log::error("No Offload configuration supported for quality index: {}.", BIG_audio_quality);
      return nullptr;
    }

    types::LeAudioLtvMap codec_params;
    // Map sample freq. value to LE Audio codec specific config value
    if (types::LeAudioCoreCodecConfig::sample_rate_map.count(offload_config->sampling_rate)) {
      codec_params.Add(
              codec_spec_conf::kLeAudioLtvTypeSamplingFreq,
              types::LeAudioCoreCodecConfig::sample_rate_map.at(offload_config->sampling_rate));
    }
    // Map data interval value to LE Audio codec specific config value
    if (types::LeAudioCoreCodecConfig::data_interval_map.count(offload_config->frame_duration)) {
      codec_params.Add(
              codec_spec_conf::kLeAudioLtvTypeFrameDuration,
              types::LeAudioCoreCodecConfig::data_interval_map.at(offload_config->frame_duration));
    }
    codec_params.Add(codec_spec_conf::kLeAudioLtvTypeOctetsPerCodecFrame,
                     offload_config->octets_per_frame);

    // Note: We do not support a different channel count on each BIS within the
    // same subgroup.
    uint8_t allocated_channel_count =
            offload_config->stream_map.size()
                    ? std::bitset<32>{offload_config->stream_map.at(0).second}.count()
                    : 1;
    bluetooth::le_audio::broadcaster::BroadcastSubgroupCodecConfig codec_config(
            bluetooth::le_audio::broadcaster::kLeAudioCodecIdLc3,
            {bluetooth::le_audio::broadcaster::BroadcastSubgroupBisCodecConfig(
                    static_cast<uint8_t>(offload_config->stream_map.size()),
                    allocated_channel_count, codec_params)},
            offload_config->bits_per_sample);

    bluetooth::le_audio::broadcaster::BroadcastQosConfig qos_config(
            offload_config->retransmission_number, offload_config->max_transport_latency);

    // Change the default software encoder config data path ID
    auto data_path = broadcaster::lc3_data_path;
    data_path.dataPathId = bluetooth::hci::iso_manager::kIsoDataPathPlatformDefault;

    uint16_t max_sdu_octets = 0;
    for (auto [_, allocation] : offload_config->stream_map) {
      auto alloc_channels_per_bis = std::bitset<32>{allocation}.count() ?: 1;
      auto sdu_octets = offload_config->octets_per_frame * offload_config->blocks_per_sdu *
                        alloc_channels_per_bis;
      if (max_sdu_octets < sdu_octets) {
        max_sdu_octets = sdu_octets;
      }
    }

    if (requirements.subgroup_quality.size() > 1) {
      log::error("More than one subgroup is not supported!");
    }

    return std::make_unique<broadcaster::BroadcastConfiguration>(
            broadcaster::BroadcastConfiguration({
                    .subgroups = {codec_config},
                    .qos = qos_config,
                    .data_path = data_path,
                    .sduIntervalUs = offload_config->frame_duration,
                    .maxSduOctets = max_sdu_octets,
                    .phy = 0x02,   // PHY_LE_2M
                    .packing = 0,  // Sequential
                    .framing = 0   // Unframed,
            }));
  }

  void UpdateBroadcastConnHandle(
          const std::vector<uint16_t>& conn_handle,
          std::function<void(const ::bluetooth::le_audio::broadcast_offload_config& config)>
                  update_receiver,
          bool is_source = true) {
    if (GetCodecLocation() != le_audio::types::CodecLocation::ADSP) {
      return;
    }

    // Choose the appropriate config index based on is_source
    int target_config = is_source ? broadcast_target_config : broadcast_sink_target_config;

    if (target_config == -1 ||
        target_config >= (int)supported_broadcast_config.size()) {
      log::error("There is no valid broadcast offload config for {}",
                 is_source ? "source" : "sink");
      return;
    }

    auto broadcast_config = supported_broadcast_config[target_config];

    log::info("UpdateBroadcastConnHandle: is_source={}, conn_handle.size()={}, "
              "stream_map.size()={}, target_config={}",
              is_source, conn_handle.size(), broadcast_config.stream_map.size(), target_config);

    log::assert_that(conn_handle.size() == broadcast_config.stream_map.size(),
                     "assert failed: conn_handle.size() ({}) == "
                     "broadcast_config.stream_map.size() ({})",
                     conn_handle.size(), broadcast_config.stream_map.size());

    if (broadcast_config.stream_map.size() == LeAudioCodecConfiguration::kChannelNumberStereo) {
      // Standard stereo (2 BISes): L/R allocation — unchanged from original
      broadcast_config.stream_map[0] = std::pair<uint16_t, uint32_t>{
              conn_handle[0], codec_spec_conf::kLeAudioLocationFrontLeft};
      broadcast_config.stream_map[1] = std::pair<uint16_t, uint32_t>{
              conn_handle[1], codec_spec_conf::kLeAudioLocationFrontRight};
    } else if (broadcast_config.stream_map.size() ==
               LeAudioCodecConfiguration::kChannelNumberMono) {
      // Standard mono (1 BIS): center allocation — unchanged from original
      broadcast_config.stream_map[0] = std::pair<uint16_t, uint32_t>{
              conn_handle[0], codec_spec_conf::kLeAudioLocationFrontCenter};
    } else {
      // Enhanced broadcast (>= 3 BISes): each BIS carries one MONO channel.
      // This branch is only reached when enhanced broadcast source or sink is
      // active (stream_map was resized to bis_count by GetBroadcastSinkOffloadConfig).
      for (size_t i = 0; i < broadcast_config.stream_map.size(); i++) {
        broadcast_config.stream_map[i] = std::pair<uint16_t, uint32_t>{
                conn_handle[i], codec_spec_conf::kLeAudioLocationMonoAudio};
      }
      log::info("UpdateBroadcastConnHandle: enhanced broadcast — {} BISes, MONO per BIS",
                broadcast_config.stream_map.size());
    }

    update_receiver(broadcast_config);
  }

  void ClearCisConfiguration(uint8_t direction) {
    if (GetCodecLocation() != bluetooth::le_audio::types::CodecLocation::ADSP) {
      return;
    }

    auto& stream_map = offloader_stream_maps.get(direction);
    stream_map.streams_map_target.clear();
    stream_map.streams_map_current.clear();
  }

  static int AdjustAllocationForOffloader(uint32_t allocation) {
    if ((allocation & codec_spec_conf::kLeAudioLocationAnyLeft) &&
        (allocation & codec_spec_conf::kLeAudioLocationAnyRight)) {
      return codec_spec_conf::kLeAudioLocationStereo;
    }
    if (allocation & codec_spec_conf::kLeAudioLocationAnyLeft) {
      return codec_spec_conf::kLeAudioLocationFrontLeft;
    }
    if (allocation & codec_spec_conf::kLeAudioLocationAnyRight) {
      return codec_spec_conf::kLeAudioLocationFrontRight;
    }

    if (allocation == codec_spec_conf::kLeAudioLocationMonoAudio) {
      return codec_spec_conf::kLeAudioLocationMonoAudio;
    }

    return -1;
  }

  bool AppendStreamMapExtension(const std::vector<struct types::cis>& cises,
                                const stream_parameters& stream_params, uint8_t direction) {
    /* Without the codec extensibility enabled, we still need the BT stack structure to
     * have the valid extended codec configuration entries, as these are used for codec type
     * matching. The extended data fields of the AIDL API data structures are filed
     * right before the AIDL call, only if the codec extensibility is enabled
     */

    const std::string tag =
            types::BidirectionalPair<std::string>({.sink = "Sink", .source = "Source"})
                    .get(direction);

    const auto cis_type = types::BidirectionalPair<types::CisType>(
                                  {.sink = types::CisType::CIS_TYPE_UNIDIRECTIONAL_SINK,
                                   .source = types::CisType::CIS_TYPE_UNIDIRECTIONAL_SOURCE})
                                  .get(direction);

    auto stream_info_updater =
            [](const bluetooth::le_audio::stream_map_info& source_info,
               std::vector<bluetooth::le_audio::stream_map_info>& dest_info_vec) {
              for (auto& dest_entry : dest_info_vec) {
                if (source_info.stream_handle == dest_entry.stream_handle) {
                  dest_entry.codec_config = source_info.codec_config;
                  dest_entry.target_latency = source_info.target_latency;
                  dest_entry.target_phy = source_info.target_phy;
                  dest_entry.metadata = source_info.metadata;
                  dest_entry.address = source_info.address;
                  dest_entry.address_type = source_info.address_type;
                }
              }
            };

    auto& dest_stream_map = offloader_stream_maps.get(direction);
    for (auto const& cis_entry : cises) {
      if ((cis_entry.type == types::CisType::CIS_TYPE_BIDIRECTIONAL ||
           cis_entry.type == cis_type) &&
          cis_entry.conn_handle != 0) {
        auto const& source_stream_map = stream_params.stream_config.stream_map;
        auto source_info = std::find_if(source_stream_map.begin(), source_stream_map.end(),
                                        [&cis_entry](auto const& info) {
                                          return info.stream_handle == cis_entry.conn_handle;
                                        });

        if (source_info != source_stream_map.end()) {
          // Update both map entries
          stream_info_updater(*source_info, dest_stream_map.streams_map_target);
          stream_info_updater(*source_info, dest_stream_map.streams_map_current);
        }
      }
    }

    return true;
  }

  bool UpdateCisMonoConfiguration(const std::vector<struct types::cis>& cises,
                                  const stream_parameters& stream_params, uint8_t direction) {
    if (!LeAudioHalVerifier::SupportsStreamActiveApi()) {
      log::error("SupportsStreamActiveApi() not supported. Mono stream cannot be enabled");
      return false;
    }

    auto& stream_map = offloader_stream_maps.get(direction);

    stream_map.has_changed = true;
    stream_map.streams_map_target.clear();
    stream_map.streams_map_current.clear();

    const std::string tag =
            types::BidirectionalPair<std::string>({.sink = "Sink", .source = "Source"})
                    .get(direction);

    constexpr types::BidirectionalPair<types::CisType> cis_types = {
            .sink = types::CisType::CIS_TYPE_UNIDIRECTIONAL_SINK,
            .source = types::CisType::CIS_TYPE_UNIDIRECTIONAL_SOURCE};
    auto cis_type = cis_types.get(direction);

    for (auto const& cis_entry : cises) {
      if ((cis_entry.type == types::CisType::CIS_TYPE_BIDIRECTIONAL ||
           cis_entry.type == cis_type) &&
          cis_entry.conn_handle != 0) {
        bool is_active = cis_entry.addr != RawAddress::kEmpty;
        log::info("{}: {}, Cis handle {:#x}, allocation  {:#x}, active: {}", tag, cis_entry.addr,
                  cis_entry.conn_handle, codec_spec_conf::kLeAudioLocationMonoAudio, is_active);
        stream_map.streams_map_target.emplace_back(stream_map_info(
                cis_entry.conn_handle, codec_spec_conf::kLeAudioLocationMonoAudio, is_active));
        stream_map.streams_map_current.emplace_back(stream_map_info(
                cis_entry.conn_handle, codec_spec_conf::kLeAudioLocationMonoAudio, is_active));
      }
    }

    return AppendStreamMapExtension(cises, stream_params, direction);
  }

  bool UpdateCisStereoConfiguration(const std::vector<struct types::cis>& cises,
                                    const stream_parameters& stream_params, uint8_t direction) {
    auto available_allocations =
            AdjustAllocationForOffloader(stream_params.audio_channel_allocation);
    auto& stream_map = offloader_stream_maps.get(direction);

    if (stream_map.streams_map_target.empty()) {
      stream_map.is_initial = true;
    } else if (stream_map.is_initial || LeAudioHalVerifier::SupportsStreamActiveApi()) {
      /* As multiple CISes phone call case, the target_allocation already have
       * the previous data, but the is_initial flag not be cleared. We need to
       * clear here to avoid make duplicated target allocation stream map. */
      stream_map.streams_map_target.clear();
    }

    stream_map.streams_map_current.clear();
    stream_map.has_changed = true;
    bool all_cises_connected = (available_allocations == codec_spec_conf::kLeAudioLocationStereo);

    /* If all the cises are connected as stream started, reset changed_flag that
     * the bt stack wouldn't send another audio configuration for the connection
     * status. */
    if (stream_map.is_initial && all_cises_connected) {
      stream_map.has_changed = false;
    }

    const std::string tag =
            types::BidirectionalPair<std::string>({.sink = "Sink", .source = "Source"})
                    .get(direction);

    constexpr types::BidirectionalPair<types::CisType> cis_types = {
            .sink = types::CisType::CIS_TYPE_UNIDIRECTIONAL_SINK,
            .source = types::CisType::CIS_TYPE_UNIDIRECTIONAL_SOURCE};
    auto cis_type = cis_types.get(direction);

    for (auto const& cis_entry : cises) {
      if ((cis_entry.type == types::CisType::CIS_TYPE_BIDIRECTIONAL ||
           cis_entry.type == cis_type) &&
          cis_entry.conn_handle != 0) {
        uint32_t target_allocation = 0;
        uint32_t current_allocation = 0;
        bool is_active = false;
        for (const auto& s : stream_params.stream_config.stream_map) {
          if (s.stream_handle == cis_entry.conn_handle) {
            is_active = true;
            target_allocation = AdjustAllocationForOffloader(s.audio_channel_allocation);
            current_allocation = target_allocation;
            if (!all_cises_connected) {
              /* Tell offloader to mix on this CIS.*/
              current_allocation = codec_spec_conf::kLeAudioLocationStereo;
            }
            break;
          }
        }

        if (target_allocation == 0) {
          /* Take missing allocation for that one .*/
          target_allocation = codec_spec_conf::kLeAudioLocationStereo & ~available_allocations;
        }

        log::info(
                "{}: Cis handle 0x{:04x}, target allocation  0x{:08x}, current "
                "allocation 0x{:08x}, active: {}",
                tag, cis_entry.conn_handle, target_allocation, current_allocation, is_active);

        if (stream_map.is_initial || LeAudioHalVerifier::SupportsStreamActiveApi()) {
          stream_map.streams_map_target.emplace_back(
                  stream_map_info(cis_entry.conn_handle, target_allocation, is_active));
        }
        stream_map.streams_map_current.emplace_back(
                stream_map_info(cis_entry.conn_handle, current_allocation, is_active));
      }
    }

    return AppendStreamMapExtension(cises, stream_params, direction);
  }

  bool UpdateCisConfiguration(const std::vector<struct types::cis>& cises,
                              const stream_parameters& stream_params, uint8_t direction) {
    if (GetCodecLocation() != bluetooth::le_audio::types::CodecLocation::ADSP) {
      return false;
    }

    switch (AdjustAllocationForOffloader(stream_params.audio_channel_allocation)) {
      case -1:
        log::error("Unsupported allocation {:#x}", stream_params.audio_channel_allocation);
        return false;
      case codec_spec_conf::kLeAudioLocationMonoAudio:
        return UpdateCisMonoConfiguration(cises, stream_params, direction);
      default:
        return UpdateCisStereoConfiguration(cises, stream_params, direction);
    };
  }

private:
  void SetCodecLocation(CodecLocation location) {
    if (offload_enable_ == false) {
      return;
    }
    codec_location_ = location;
  }

  bool IsLc3ConfigMatched(const types::CodecConfigSetting& target_config,
                          const types::CodecConfigSetting& adsp_config) {
    if (adsp_config.id.coding_format != types::kLeAudioCodingFormatLC3 ||
        target_config.id.coding_format != types::kLeAudioCodingFormatLC3) {
      return false;
    }

    const types::LeAudioCoreCodecConfig adsp_lc3_config = adsp_config.params.GetAsCoreCodecConfig();
    const types::LeAudioCoreCodecConfig target_lc3_config =
            target_config.params.GetAsCoreCodecConfig();

    if (adsp_lc3_config.sampling_frequency != target_lc3_config.sampling_frequency ||
        adsp_lc3_config.frame_duration != target_lc3_config.frame_duration ||
        adsp_config.GetChannelCountPerIsoStream() != target_config.GetChannelCountPerIsoStream() ||
        adsp_lc3_config.octets_per_codec_frame != target_lc3_config.octets_per_codec_frame) {
      return false;
    }

    return true;
  }

  bool IsAseConfigurationMatched(const AseConfiguration& software_set_config,
                                 const AseConfiguration& adsp_set_config) {
    // Skip the check if config is APTX due to AOSP ADSP doesn't support Codec
    if (software_set_config.codec.id.vendor_codec_id == types::kLeAudioCodingFormatAptxLe ||
        software_set_config.codec.id.vendor_codec_id == types::kLeAudioCodingFormatAptxLeX) {
      return true;
    }
    // Skip the check of stategry and ase_cnt due to ADSP doesn't have the info
    return IsLc3ConfigMatched(software_set_config.codec, adsp_set_config.codec);
  }

  bool IsAudioSetConfigurationMatched(const AudioSetConfiguration* software_audio_set_conf,
                                      std::unordered_set<uint8_t>& offload_preference_set,
                                      const std::vector<AudioSetConfiguration>& adsp_capabilities) {
    if (software_audio_set_conf->confs.sink.empty() &&
        software_audio_set_conf->confs.source.empty()) {
      return false;
    }

    // No match if the codec is not on the preference list
    for (auto direction :
         {le_audio::types::kLeAudioDirectionSink, le_audio::types::kLeAudioDirectionSource}) {
      for (auto const& conf : software_audio_set_conf->confs.get(direction)) {
        if (offload_preference_set.find(conf.codec.id.coding_format) ==
            offload_preference_set.end()) {
          return false;
        }
      }
    }

    // Checks any of offload config matches the input audio set config
    for (const auto& adsp_audio_set_conf : adsp_capabilities) {
      size_t match_cnt = 0;
      size_t expected_match_cnt = 0;

      for (auto direction :
           {le_audio::types::kLeAudioDirectionSink, le_audio::types::kLeAudioDirectionSource}) {
        auto const& software_set_ase_confs = software_audio_set_conf->confs.get(direction);
        auto const& adsp_set_ase_confs = adsp_audio_set_conf.confs.get(direction);

        if (!software_set_ase_confs.size() || !adsp_set_ase_confs.size()) {
          continue;
        }

        // Check for number of ASEs mismatch
        if (adsp_set_ase_confs.size() != software_set_ase_confs.size()) {
          log::error("{}: ADSP config size mismatches the software: {} != {}",
                     direction == types::kLeAudioDirectionSink ? "Sink" : "Source",
                     adsp_set_ase_confs.size(), software_set_ase_confs.size());
          continue;
        }

        // The expected number of ASE configs, the ADSP config needs to match
        expected_match_cnt += software_set_ase_confs.size();
        if (expected_match_cnt == 0) {
          continue;
        }

        // Check for matching configs
        for (auto const& adsp_set_conf : adsp_set_ase_confs) {
          for (auto const& software_set_conf : software_set_ase_confs) {
            if (IsAseConfigurationMatched(software_set_conf, adsp_set_conf)) {
              match_cnt++;
              // Check the next adsp config if the first software config matches
              break;
            }
          }
        }
        if (match_cnt != expected_match_cnt) {
          break;
        }
      }

      // Check the match count
      if (match_cnt == expected_match_cnt) {
        return true;
      }
    }

    return false;
  }

  std::string getStrategyString(types::LeAudioConfigurationStrategy strategy) {
    switch (strategy) {
      case types::LeAudioConfigurationStrategy::MONO_ONE_CIS_PER_DEVICE:
        return "MONO_ONE_CIS_PER_DEVICE";
      case types::LeAudioConfigurationStrategy::STEREO_TWO_CISES_PER_DEVICE:
        return "STEREO_TWO_CISES_PER_DEVICE";
      case types::LeAudioConfigurationStrategy::STEREO_ONE_CIS_PER_DEVICE:
        return "STEREO_ONE_CIS_PER_DEVICE";
      default:
        return "RFU";
    }
  }

  uint8_t sampleFreqToBluetoothSigBitMask(int sample_freq) {
    switch (sample_freq) {
      case 8000:
        return bluetooth::le_audio::codec_spec_caps::kLeAudioSamplingFreq8000Hz;
      case 16000:
        return bluetooth::le_audio::codec_spec_caps::kLeAudioSamplingFreq16000Hz;
      case 24000:
        return bluetooth::le_audio::codec_spec_caps::kLeAudioSamplingFreq24000Hz;
      case 32000:
        return bluetooth::le_audio::codec_spec_caps::kLeAudioSamplingFreq32000Hz;
      case 44100:
        return bluetooth::le_audio::codec_spec_caps::kLeAudioSamplingFreq44100Hz;
      case 48000:
        return bluetooth::le_audio::codec_spec_caps::kLeAudioSamplingFreq48000Hz;
    }
    return bluetooth::le_audio::codec_spec_caps::kLeAudioSamplingFreq8000Hz;
  }

  void storeLocalCapa(
          const std::vector<::bluetooth::le_audio::types::AudioSetConfiguration>& adsp_capabilities,
          const std::vector<btle_audio_codec_config_t>& offload_preference_set) {
    log::debug("Print adsp_capabilities:");

    for (auto& adsp : adsp_capabilities) {
      log::debug("'{}':", adsp.name);
      for (auto direction :
           {le_audio::types::kLeAudioDirectionSink, le_audio::types::kLeAudioDirectionSource}) {
        log::debug("dir: {}: number of confs {}:",
                   direction == types::kLeAudioDirectionSink ? "sink" : "source",
                   (int)(adsp.confs.get(direction).size()));
        for (auto conf : adsp.confs.sink) {
          log::debug("codecId: {}, sample_freq: {}, interval {}, channel_cnt: {}",
                     conf.codec.id.coding_format, conf.codec.GetSamplingFrequencyHz(),
                     conf.codec.GetDataIntervalUs(), conf.codec.GetChannelCountPerIsoStream());

          btle_audio_codec_config_t capa_to_add = {
                  .codec_type =
                          (conf.codec.id.coding_format == types::kLeAudioCodingFormatLC3)
                                  ? btle_audio_codec_index_t::LE_AUDIO_CODEC_INDEX_SOURCE_LC3
                                  : btle_audio_codec_index_t::LE_AUDIO_CODEC_INDEX_SOURCE_INVALID,
                  .sample_rate = utils::translateToBtLeAudioCodecConfigSampleRate(
                          conf.codec.GetSamplingFrequencyHz()),
                  .bits_per_sample = utils::translateToBtLeAudioCodecConfigBitPerSample(
                          conf.codec.GetBitsPerSample()),
                  .channel_count = utils::translateToBtLeAudioCodecConfigChannelCount(
                          conf.codec.GetChannelCountPerIsoStream()),
                  .frame_duration = utils::translateToBtLeAudioCodecConfigFrameDuration(
                          conf.codec.GetDataIntervalUs()),
                  .codec_frame_blocks_per_sdu = conf.codec.GetCodecFrameBlocksPerSdu(),
          };

          auto& capa_container = (direction == types::kLeAudioDirectionSink) ? codec_output_capa
                                                                             : codec_input_capa;
          if (std::find(capa_container.begin(), capa_container.end(), capa_to_add) ==
              capa_container.end()) {
            log::debug("Adding {} capa {}",
                       (direction == types::kLeAudioDirectionSink) ? "output" : "input",
                       static_cast<int>(capa_container.size()));
            capa_container.push_back(capa_to_add);
          }
        }
      }
    }

    log::debug("Output capa: {}, Input capa: {}", static_cast<int>(codec_output_capa.size()),
               static_cast<int>(codec_input_capa.size()));

    log::debug("Print offload_preference_set: {}", (int)(offload_preference_set.size()));

    int i = 0;
    for (auto set : offload_preference_set) {
      log::debug("set {}, {}", i++, set.ToString());
    }
  }

  void UpdateOffloadCapability(
          const std::vector<btle_audio_codec_config_t>& offloading_preference) {
    log::info("");
    std::unordered_set<uint8_t> offload_preference_set;

    if (AudioSetConfigurationProvider::Get() == nullptr) {
      log::error("Audio set configuration provider is not available.");
      return;
    }

    auto adsp_capabilities = ::bluetooth::audio::le_audio::get_offload_capabilities();

    storeLocalCapa(adsp_capabilities.unicast_offload_capabilities, offloading_preference);

    for (auto codec : offloading_preference) {
      auto it = btle_audio_codec_type_map_.find(codec.codec_type);

      if (it != btle_audio_codec_type_map_.end()) {
        offload_preference_set.insert(it->second);
      }
    }

    for (types::LeAudioContextType ctx_type : types::kLeAudioContextAllTypesArray) {
      // Gets the software supported context type and the corresponding config
      // priority
      const AudioSetConfigurations* software_audio_set_confs =
              AudioSetConfigurationProvider::Get()->GetConfigurations(ctx_type);

      for (const auto& software_audio_set_conf : *software_audio_set_confs) {
        // TO-DO: Add vendor side LC3 gaming bi-directional capabilities
        if (true/*IsAudioSetConfigurationMatched(software_audio_set_conf,
                                           offload_preference_set,
                                           adsp_capabilities)*/) {
          if (!osi_property_get_bool("persist.vendor.service.bt.adv_transport", false)) {
            if (software_audio_set_conf->confs.sink.size() > 0) {
              if (software_audio_set_conf->confs.sink[0].codec.id ==
                  le_audio::types::LeAudioCodecIdAptxLeX) {
                continue;
              }
            }
          }
          if (!osi_property_get_bool("persist.bluetooth.leaudio_lex_l_r.enabled", true)){
            if (software_audio_set_conf->confs.sink.size() > 0) {
              if (software_audio_set_conf->confs.sink[0].codec.id ==
                  le_audio::types::LeAudioCodecIdAptxLeX) {
                if (software_audio_set_conf->name.ends_with("L_R"))
                  continue;
              }
            }
          }
          if ((software_audio_set_conf->confs.sink.size() > 0) &&
              (software_audio_set_conf->confs.source.size() > 0)) {
            if (software_audio_set_conf->confs.sink[0].codec.id ==
                        le_audio::types::LeAudioCodecIdAptxLeX &&
                software_audio_set_conf->confs.source[0].codec.id ==
                        le_audio::types::LeAudioCodecIdAptxLeX) {
              if (ctx_type == types::LeAudioContextType::CONVERSATIONAL &&
                  !osi_property_get_bool("persist.bluetooth.leaudio_lex_voice.enabled", true)) {
                continue;
              }
            }
          }
          log::info("Offload supported conf, context type: {}, settings -> {}", (int)ctx_type,
                    software_audio_set_conf->name);
          if (dual_bidirection_swb_supported_ &&
              AudioSetConfigurationProvider::Get()->CheckConfigurationIsDualBiDirSwb(
                      *software_audio_set_conf)) {
            offload_dual_bidirection_swb_supported_ = true;
          }
          context_type_offload_config_map_[ctx_type].push_back(software_audio_set_conf);
        }
      }
    }
    UpdateSupportedBroadcastConfig(adsp_capabilities.broadcast_offload_capabilities);
  }

  CodecLocation codec_location_ = CodecLocation::HOST;
  bool offload_enable_ = false;
  bool offload_dual_bidirection_swb_supported_ = false;
  bool is_aptx_adaptive_le_supported_ = false;  // TO-DO: Start with false
  bool is_aptx_adaptive_lex_supported_ = true;  // TO-DO: Make propery and feature based
  bool is_enhanced_le_gaming_supported_ = false;
  bool dual_bidirection_swb_supported_ = false;
  bool is_qhs_enabled_ = false;
  types::BidirectionalPair<offloader_stream_maps_t> offloader_stream_maps;
  std::vector<bluetooth::le_audio::broadcast_offload_config> supported_broadcast_config;
  std::unordered_map<types::LeAudioContextType, AudioSetConfigurations>
          context_type_offload_config_map_;
  std::unordered_map<btle_audio_codec_index_t, uint8_t> btle_audio_codec_type_map_ = {
          {::bluetooth::le_audio::LE_AUDIO_CODEC_INDEX_SOURCE_LC3, types::kLeAudioCodingFormatLC3},
          {::bluetooth::le_audio::LE_AUDIO_CODEC_INDEX_SOURCE_APTX_LE,
           types::kLeAudioCodingFormatVendorSpecific},
          {::bluetooth::le_audio::LE_AUDIO_CODEC_INDEX_SOURCE_APTX_LEX,
           types::kLeAudioCodingFormatVendorSpecific}};

  std::optional<ProviderInfo> codec_provider_info_;

  std::vector<btle_audio_codec_config_t> codec_input_capa = {};
  std::vector<btle_audio_codec_config_t> codec_output_capa = {};
  int broadcast_target_config = -1;
  int broadcast_sink_target_config = -1;

  LeAudioSourceAudioHalClient* unicast_local_source_hal_client = nullptr;
  LeAudioSinkAudioHalClient* unicast_local_sink_hal_client = nullptr;
  LeAudioSourceAudioHalClient* broadcast_local_source_hal_client = nullptr;
};

std::ostream& operator<<(std::ostream& os,
                         const CodecManager::UnicastConfigurationRequirements& req) {
  os << "{audio context type: " << req.audio_context_type;
  if (req.sink_pacs.has_value()) {
    os << ", sink_pacs: [";
    for (auto const& pac : req.sink_pacs.value()) {
      os << "sink_pac: {";
      os << ", codec_id: " << pac.codec_id;
      os << ", caps size: " << pac.codec_spec_caps.Size();
      os << ", caps_raw size: " << pac.codec_spec_caps_raw.size();
      os << ", metadata size: " << pac.metadata.Size();
      os << "}, ";
    }
    os << "\b\b]";
  } else {
    os << ", sink_pacs: " << "None";
  }

  if (req.source_pacs.has_value()) {
    os << ", source_pacs: [";
    for (auto const& pac : req.source_pacs.value()) {
      os << "source_pac: {";
      os << ", codec_id: " << pac.codec_id;
      os << ", caps size: " << pac.codec_spec_caps.Size();
      os << ", caps_raw size: " << pac.codec_spec_caps_raw.size();
      os << ", metadata size: " << pac.metadata.Size();
      os << "}, ";
    }
    os << "\b\b]";
  } else {
    os << ", source_pacs: " << "None";
  }

  if (req.sink_requirements.has_value()) {
    for (auto const& sink_req : req.sink_requirements.value()) {
      os << ", sink_req: {";
      os << ", target_latency: " << +sink_req.target_latency;
      os << ", target_Phy: " << +sink_req.target_Phy;
      os << "}";
    }
  } else {
    os << ", sink_req: None";
  }

  if (req.source_requirements.has_value()) {
    for (auto const& source_req : req.source_requirements.value()) {
      os << ", source_req: {";
      os << ", target_latency: " << +source_req.target_latency;
      os << ", target_Phy: " << +source_req.target_Phy;
      os << "}";
    }
  } else {
    os << ", source_req: None";
  }

  os << "}";
  return os;
}

struct CodecManager::impl {
  impl(const CodecManager& codec_manager) : codec_manager_(codec_manager) {}

  void Start(const std::vector<btle_audio_codec_config_t>& offloading_preference) {
    log::assert_that(!codec_manager_impl_, "assert failed: !codec_manager_impl_");
    codec_manager_impl_ = std::make_unique<codec_manager_impl>();
    codec_manager_impl_->start(offloading_preference);
  }

  void Stop() {
    log::assert_that(codec_manager_impl_ != nullptr,
                     "assert failed: codec_manager_impl_ != nullptr");
    codec_manager_impl_.reset();
  }

  bool IsRunning() { return codec_manager_impl_ ? true : false; }

  const CodecManager& codec_manager_;
  std::unique_ptr<codec_manager_impl> codec_manager_impl_;
};

CodecManager::CodecManager() : pimpl_(std::make_unique<impl>(*this)) {}

void CodecManager::Start(const std::vector<btle_audio_codec_config_t>& offloading_preference) {
  if (!pimpl_->IsRunning()) {
    pimpl_->Start(offloading_preference);
  }
}

void CodecManager::Stop() {
  if (pimpl_->IsRunning()) {
    pimpl_->Stop();
  }
}

types::CodecLocation CodecManager::GetCodecLocation(void) const {
  if (!pimpl_->IsRunning()) {
    return CodecLocation::HOST;
  }

  return pimpl_->codec_manager_impl_->GetCodecLocation();
}

std::optional<ProviderInfo> CodecManager::GetCodecConfigProviderInfo(void) const {
  if (!pimpl_->IsRunning()) {
    return std::nullopt;
  }

  return pimpl_->codec_manager_impl_->GetCodecConfigProviderInfo();
}

bool CodecManager::IsDualBiDirSwbSupported(void) const {
  if (!pimpl_->IsRunning()) {
    return false;
  }

  return pimpl_->codec_manager_impl_->IsDualBiDirSwbSupported();
}

bool CodecManager::IsEnhancedLeGamingSupported(void) const {
  if (!pimpl_->IsRunning()) {
    return false;
  }

  return pimpl_->codec_manager_impl_->IsEnhancedLeGamingSupported();
}

bool CodecManager::IsQhsEnabled(void) const {
  if (!pimpl_->IsRunning()) {
    return false;
  }

  return pimpl_->codec_manager_impl_->IsQhsEnabled();
}

bool CodecManager::IsAptxAdaptiveLeSupported(void) const {
  if (!pimpl_->IsRunning()) {
    return false;
  }

  return pimpl_->codec_manager_impl_->IsAptxAdaptiveLeSupported();
}

bool CodecManager::IsAptxAdaptiveLeXSupported(void) const {
  if (!pimpl_->IsRunning()) {
    return false;
  }

  return pimpl_->codec_manager_impl_->IsAptxAdaptiveLeXSupported();
}

std::vector<bluetooth::le_audio::btle_audio_codec_config_t>
CodecManager::GetLocalAudioOutputCodecCapa() {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->GetLocalAudioOutputCodecCapa();
  }

  std::vector<bluetooth::le_audio::btle_audio_codec_config_t> empty{};
  return empty;
}

std::vector<bluetooth::le_audio::btle_audio_codec_config_t>
CodecManager::GetLocalAudioInputCodecCapa() {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->GetLocalAudioInputCodecCapa();
  }
  std::vector<bluetooth::le_audio::btle_audio_codec_config_t> empty{};
  return empty;
}

void CodecManager::UpdateActiveAudioConfig(
        const types::BidirectionalPair<stream_parameters>& stream_params,
        types::LeAudioCodecId id,
        std::function<void(const stream_config& config, uint8_t direction)> update_receiver,
        uint8_t remote_directions_to_update) {
  if (pimpl_->IsRunning()) {
    pimpl_->codec_manager_impl_->UpdateActiveAudioConfig(stream_params, id, update_receiver,
                                                         remote_directions_to_update);
  }
}

bool CodecManager::UpdateActiveUnicastAudioHalClient(
        LeAudioSourceAudioHalClient* source_unicast_client,
        LeAudioSinkAudioHalClient* sink_unicast_client, bool is_active) {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->UpdateActiveUnicastAudioHalClient(
            source_unicast_client, sink_unicast_client, is_active);
  }
  return false;
}

bool CodecManager::UpdateActiveBroadcastAudioHalClient(
        LeAudioSourceAudioHalClient* source_broadcast_client, bool is_active) {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->UpdateActiveBroadcastAudioHalClient(source_broadcast_client,
                                                                            is_active);
  }
  return false;
}

std::unique_ptr<AudioSetConfiguration> CodecManager::GetCodecConfig(
        const CodecManager::UnicastConfigurationRequirements& requirements,
        CodecManager::UnicastConfigurationProvider provider) {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->GetCodecConfig(requirements, provider);
  }

  return nullptr;
}

bool CodecManager::CheckCodecConfigIsBiDirSwb(const types::AudioSetConfiguration& config) const {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->CheckCodecConfigIsBiDirSwb(config);
  }
  return false;
}

bool CodecManager::CheckCodecConfigIsDualBiDirSwb(
        const types::AudioSetConfiguration& config) const {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->CheckCodecConfigIsDualBiDirSwb(config);
  }
  return false;
}

std::unique_ptr<broadcaster::BroadcastConfiguration> CodecManager::GetBroadcastConfig(
        const CodecManager::BroadcastConfigurationRequirements& requirements) const {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->GetBroadcastConfig(requirements);
  }

  return nullptr;
}

void CodecManager::UpdateBroadcastConnHandle(
        const std::vector<uint16_t>& conn_handle,
        std::function<void(const ::bluetooth::le_audio::broadcast_offload_config& config)>
                update_receiver,
        bool is_source) {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->UpdateBroadcastConnHandle(conn_handle, update_receiver,
                                                                  is_source);
  }
}

bool CodecManager::UpdateCisConfiguration(const std::vector<struct types::cis>& cises,
                                          const stream_parameters& stream_params,
                                          uint8_t direction) {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->UpdateCisConfiguration(cises, stream_params, direction);
  }
  return false;
}

void CodecManager::ClearCisConfiguration(uint8_t direction) {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->ClearCisConfiguration(direction);
  }
}

bool CodecManager::IsUsingCodecExtensibility() const {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->IsUsingCodecExtensibility();
  }
  return false;
}

std::unique_ptr<broadcast_sink::BroadcastSinkConfiguration> CodecManager::GetBroadcastSinkConfig(
        const CodecManager::BroadcastSinkConfigurationRequirements& requirements) const {
  if (pimpl_->IsRunning()) {
    return pimpl_->codec_manager_impl_->GetBroadcastSinkConfig(requirements);
  }

  return nullptr;
}

}  // namespace bluetooth::le_audio
