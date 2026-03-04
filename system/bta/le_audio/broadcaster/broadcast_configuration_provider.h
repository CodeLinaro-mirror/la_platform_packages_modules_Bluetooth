/*
 * Copyright 2024 The Android Open Source Project
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
#pragma once

#include "bta/le_audio/broadcaster/broadcaster_types.h"
#include "bta/le_audio/le_audio_types.h"

namespace bluetooth::le_audio {
namespace broadcaster {

constexpr types::LeAudioCodecId kLeAudioCodecIdLc3 = {
        .coding_format = types::kLeAudioCodingFormatLC3,
        .vendor_company_id = types::kLeAudioVendorCompanyIdUndefined,
        .vendor_codec_id = types::kLeAudioVendorCodecIdUndefined};

// Quality subgroup configurations
static const BroadcastSubgroupCodecConfig lc3_mono_16_2 = BroadcastSubgroupCodecConfig(
        kLeAudioCodecIdLc3,
        {BroadcastSubgroupBisCodecConfig{
                // num_bis
                1,
                // bis_channel_cnt_
                1,
                // codec_specific
                types::LeAudioLtvMap({
                        LTV_ENTRY_SAMPLING_FREQUENCY(codec_spec_conf::kLeAudioSamplingFreq16000Hz),
                        LTV_ENTRY_FRAME_DURATION(codec_spec_conf::kLeAudioCodecFrameDur10000us),
                        LTV_ENTRY_OCTETS_PER_CODEC_FRAME(40),
                }),
        }},
        // bits_per_sample
        16);

static const BroadcastSubgroupCodecConfig lc3_stereo_16_2 = BroadcastSubgroupCodecConfig(
        kLeAudioCodecIdLc3,
        {BroadcastSubgroupBisCodecConfig{
                // num_bis
                2,
                // bis_channel_cnt_
                1,
                // codec_specific
                types::LeAudioLtvMap({
                        LTV_ENTRY_SAMPLING_FREQUENCY(codec_spec_conf::kLeAudioSamplingFreq16000Hz),
                        LTV_ENTRY_FRAME_DURATION(codec_spec_conf::kLeAudioCodecFrameDur10000us),
                        LTV_ENTRY_OCTETS_PER_CODEC_FRAME(40),
                }),
        }},
        // bits_per_sample
        16);

static const BroadcastSubgroupCodecConfig lc3_stereo_24_2 = BroadcastSubgroupCodecConfig(
        kLeAudioCodecIdLc3,
        {BroadcastSubgroupBisCodecConfig{
                // num_bis
                2,
                // bis_channel_cnt_
                1,
                // codec_specific
                types::LeAudioLtvMap({
                        LTV_ENTRY_SAMPLING_FREQUENCY(codec_spec_conf::kLeAudioSamplingFreq24000Hz),
                        LTV_ENTRY_FRAME_DURATION(codec_spec_conf::kLeAudioCodecFrameDur10000us),
                        LTV_ENTRY_OCTETS_PER_CODEC_FRAME(60),
                }),
        }},
        // bits_per_sample
        16);

static const BroadcastSubgroupCodecConfig lc3_stereo_48_1 = BroadcastSubgroupCodecConfig(
        kLeAudioCodecIdLc3,
        {BroadcastSubgroupBisCodecConfig{
                // num_bis
                2,
                // bis_channel_cnt_
                1,
                // codec_specific
                types::LeAudioLtvMap({
                        LTV_ENTRY_SAMPLING_FREQUENCY(codec_spec_conf::kLeAudioSamplingFreq48000Hz),
                        LTV_ENTRY_FRAME_DURATION(codec_spec_conf::kLeAudioCodecFrameDur7500us),
                        LTV_ENTRY_OCTETS_PER_CODEC_FRAME(75),
                }),
        }},
        // bits_per_sample
        16);

static const BroadcastSubgroupCodecConfig lc3_stereo_48_2 = BroadcastSubgroupCodecConfig(
        kLeAudioCodecIdLc3,
        {BroadcastSubgroupBisCodecConfig{
                // num_bis
                2,
                // bis_channel_cnt_
                1,
                // codec_specific
                types::LeAudioLtvMap({
                        LTV_ENTRY_SAMPLING_FREQUENCY(codec_spec_conf::kLeAudioSamplingFreq48000Hz),
                        LTV_ENTRY_FRAME_DURATION(codec_spec_conf::kLeAudioCodecFrameDur10000us),
                        LTV_ENTRY_OCTETS_PER_CODEC_FRAME(100),
                }),
        }},
        // bits_per_sample
        16);

static const BroadcastSubgroupCodecConfig lc3_stereo_48_3 = BroadcastSubgroupCodecConfig(
        kLeAudioCodecIdLc3,
        {BroadcastSubgroupBisCodecConfig{
                // num_bis
                2,
                // bis_channel_cnt_
                1,
                // codec_specific
                types::LeAudioLtvMap({
                        LTV_ENTRY_SAMPLING_FREQUENCY(codec_spec_conf::kLeAudioSamplingFreq48000Hz),
                        LTV_ENTRY_FRAME_DURATION(codec_spec_conf::kLeAudioCodecFrameDur7500us),
                        LTV_ENTRY_OCTETS_PER_CODEC_FRAME(90),
                }),
        }},
        // bits_per_sample
        16);

static const BroadcastSubgroupCodecConfig lc3_stereo_48_4 = BroadcastSubgroupCodecConfig(
        kLeAudioCodecIdLc3,
        {BroadcastSubgroupBisCodecConfig{
                // num_bis
                2,
                // bis_channel_cnt_
                1,
                // codec_specific
                types::LeAudioLtvMap({
                        LTV_ENTRY_SAMPLING_FREQUENCY(codec_spec_conf::kLeAudioSamplingFreq48000Hz),
                        LTV_ENTRY_FRAME_DURATION(codec_spec_conf::kLeAudioCodecFrameDur10000us),
                        LTV_ENTRY_OCTETS_PER_CODEC_FRAME(120),
                }),
        }},
        // bits_per_sample
        16);

static const types::DataPathConfiguration lc3_data_path = {
        .dataPathId = bluetooth::hci::iso_manager::kIsoDataPathHci,
        .dataPathConfig = {},
        .isoDataPathConfig =
                {
                        .codecId = kLeAudioCodecIdLc3,
                        .isTransparent = true,
                        .controllerDelayUs = 0x00000000,  // irrlevant for transparent mode
                        .configuration = {},
                },
};

// Data path configuration for duplex broadcast (AuraChat DBIG)
static const types::DataPathConfiguration lc3_data_path_duplex = {
        .dataPathId = bluetooth::hci::iso_manager::kIsoDataPathPlatformDefault,
        .dataPathConfig = {},
        .isoDataPathConfig =
                {
                        .codecId = kLeAudioCodecIdLc3,
                        .isTransparent = false,
                        .controllerDelayUs = 0x00000000,
                        .configuration = {},
                },
};

static const BroadcastQosConfig qos_config_2_10 = BroadcastQosConfig(2, 10);
static const BroadcastQosConfig qos_config_4_45 = BroadcastQosConfig(4, 45);
static const BroadcastQosConfig qos_config_4_50 = BroadcastQosConfig(4, 50);
static const BroadcastQosConfig qos_config_4_60 = BroadcastQosConfig(4, 60);
static const BroadcastQosConfig qos_config_4_65 = BroadcastQosConfig(4, 65);
// AuraChat DBIG: rtn=1, max_transport_latency=10ms
static const BroadcastQosConfig qos_config_1_10 = BroadcastQosConfig(1, 10);

// Standard single subgroup configurations
static const BroadcastConfiguration lc3_mono_16_2_1 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_mono_16_2},
        .qos = qos_config_2_10,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 40,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0,  // Unframed
};

static const BroadcastConfiguration lc3_mono_16_2_2 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_mono_16_2},
        .qos = qos_config_4_60,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 40,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0,  // Unframed
};

static const BroadcastConfiguration lc3_stereo_16_2_2 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_stereo_16_2},
        .qos = qos_config_4_60,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 80,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0,  // Unframed
};

static const BroadcastConfiguration lc3_stereo_24_2_1 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_stereo_24_2},
        .qos = qos_config_2_10,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 120,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0,  // Unframed
};

static const BroadcastConfiguration lc3_stereo_24_2_2 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_stereo_24_2},
        .qos = qos_config_4_60,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 120,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0,  // Unframed
};

static const BroadcastConfiguration lc3_stereo_48_1_2 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_stereo_48_1},
        .qos = qos_config_4_50,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 150,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0   // Unframed,
};

static const BroadcastConfiguration lc3_stereo_48_2_2 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_stereo_48_2},
        .qos = qos_config_4_65,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 200,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0   // Unframed,
};

static const BroadcastConfiguration lc3_stereo_48_3_2 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_stereo_48_3},
        .qos = qos_config_4_50,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 180,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0   // Unframed,
};

static const BroadcastConfiguration lc3_stereo_48_4_2 = {
        // subgroup list, qos configuration, data path configuration
        .subgroups = {lc3_stereo_48_4},
        .qos = qos_config_4_65,
        .data_path = lc3_data_path,
        .sduIntervalUs = 10000,
        .maxSduOctets = 240,
        .phy = 0x02,   // PHY_LE_2M
        .packing = 0,  // Sequential
        .framing = 0   // Unframed,
};

// AuraChat DBIG duplex subgroup: 4 BISes, 16kHz, 10ms frame duration, 25 bytes/BIS
// (num_bises=4 for 2M PHY, max_sdu=100 bytes total, codec_specific_2=1 for 10ms frame duration)
static const BroadcastSubgroupCodecConfig aurachat_duplex_4bis_16_2 =
        BroadcastSubgroupCodecConfig(
                kLeAudioCodecIdLc3,
                {BroadcastSubgroupBisCodecConfig{
                        // num_bis (4 for duplex 2M PHY: 2 TX + 2 RX)
                        4,
                        // bis_channel_cnt_
                        1,
                        // codec_specific
                        types::LeAudioLtvMap({
                                LTV_ENTRY_SAMPLING_FREQUENCY(
                                        codec_spec_conf::kLeAudioSamplingFreq16000Hz),
                                LTV_ENTRY_FRAME_DURATION(
                                        codec_spec_conf::kLeAudioCodecFrameDur10000us),
                                LTV_ENTRY_OCTETS_PER_CODEC_FRAME(40),
                        }),
                }},
                // bits_per_sample
                16);

// AuraChat DBIG duplex BIG configuration (2M PHY)
// BIG params:
//   sdu_int=10000us, max_sdu=100 bytes, max_transport_latency=10ms,
//   rtn=1, phy=2 (LE 2M), packing=0 (Sequential), framing=0 (Unframed)
// DBIG params:
//   dbig_feature_set=3, bis_detection_attempts=10, max_payload_dbig_control=16,
//   bis_control_event_interval=9 (for 10ms MTL), send_exit=2,
//   pgp_timeout=10, pgo_timeout=10, sgo_timeout=6, tx_power=8dBm
// Duplex specifics:
//   num_bises=4 (2M PHY), encryption=enabled, data_path=bidirectional (TX=0, RX=1),
//   frame_duration=10ms (codec_specific_2=1)
static const BroadcastConfiguration aurachat_duplex_2m = {
        .subgroups = {aurachat_duplex_4bis_16_2},
        .qos = qos_config_1_10,
        .data_path = lc3_data_path_duplex,
        .sduIntervalUs = 10000,  // 10ms ISO interval
        .maxSduOctets = 40,     // 40 bytes (varies with bitrate: 26-155 bytes)
        .phy = 0x02,             // PHY_LE_2M
        .packing = 0,            // Sequential (duplex mode)
        .framing = 0,            // Unframed
};

// Takes a list of subgroup requirements (audio context, quality index)
BroadcastConfiguration GetBroadcastConfig(
        const std::vector<std::pair<types::LeAudioContextType, uint8_t>>& subgroup_quality);

}  // namespace broadcaster
}  // namespace bluetooth::le_audio
