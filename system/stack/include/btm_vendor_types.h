/******************************************************************************
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

#pragma once

#include <cstdint>

#define MAX_SUPPORTED_SCRAMBLING_FREQ_SIZE 8
#define MAX_SCRAMBLING_FREQS_SIZE 64
#define SOC_ADD_ON_FEATURES_MAX_SIZE 245
#define HOST_ADD_ON_FEATURES_MAX_SIZE 255
#define QLL_LOCAL_SUPPORTED_FEATURES_MAX_SIZE 8

#define HOST_ADD_ON_ADV_AUDIO_UNICAST_FEAT_MASK(x) ((x)[0] & 0x01)
#define HOST_ADD_ON_ADV_AUDIO_BCA_FEAT_MASK(x) ((x)[0] & 0x02)
#define HOST_ADD_ON_ADV_AUDIO_BCS_FEAT_MASK(x) ((x)[0] & 0x04)
#define HOST_ADD_ON_ADV_AUDIO_STEREO_RECORDING(x) ((x)[0] & 0x08)
#define HOST_ADD_ON_ADV_AUDIO_LC3Q_FEAT_MASK(x) ((x)[0] & 0x10)
#define HOST_ADD_ON_QHS_FEAT_MASK(x) ((x)[0] & 0x20)

/* Bluetoth Core Extension OCF */
#define QBCE_SET_QHS_HOST_MODE 0x01
#define QBCE_SET_QLL_EVENT_MASK 0x10
#define QBCE_SET_QLM_EVENT_MASK 0x0F
#define QBCE_READ_REMOTE_QLL_SUPPORTED_FEATURE 0x0C
#define QBCE_READ_LOCAL_QLL_SUPPORTED_FEATURES 0x0B
#define QBCE_QLE_SET_HOST_FEATURE 0x14
#define QBCE_QLE_ADD_CIG_MULTI_CONFIGURATIONS 0x2E
/* subcode for VOIP Network Wifi */
#define HCI_VSC_SUBCODE_VOIP_NETWORK_WIFI 0x01

#define HCI_VSE_SUBCODE_QBCE 0x51
#define HCI_VSE_SUBCODE_VS_META 0xC1
#define HCI_VS_LE_DBIG_STATUS 0x00
#define MSG_QBCE_QLL_CONNECTION_COMPLETE 0x09
#define MSG_QBCE_REMOTE_SUPPORTED_QLL_FEATURES_COMPLETE 0x06
#define MSG_QBCE_QCM_PHY_CHANGE 0x01
#define MSG_QBCE_QLE_CIG_LATENCY_CHANGED 0x03
#define MSG_QBCE_VS_CODEC_SETTINGS 0x18
#define MSG_QBCE_DBIG_UPDATE 0x19
#define MSG_QBCE_TEXIT_DBIG_COMPLETE 0x1A
#define MSG_QBCE_JOIN_CONTROL_COMPLETE 0x1B
#define HCI_VS_LINK_POWER_CTRL_EVENT 0xb4

#define BTM_QLL_FEATURES_STATE_IDLE 0             /* initial state */
#define BTM_QLL_FEATURES_STATE_CONN_COMPLETE 1    /* QLL connection complete event received */
#define BTM_QLL_FEATURES_STATE_FEATURE_COMPLETE 2 /* remote QLL features complete event received \
                                                   */
#define BTM_QLL_FEATURES_STATE_ERROR 3            /* error status */

#define BTM_QBCE_READ_REMOTE_QLL_SUPPORTED_FEATURE_LEN 3

/* Add_on features encoding - page 0 (the only page for now)*/
#define BTM_WIPOWER_FASTBOOT_ENABLE(x) ((x)[0] & 0x01)
#define BTM_SPLIT_A2DP_SCRAMBLING_DATA_REQUIRED(x) ((x)[0] & 0x02)
#define BTM_SPLIT_A2DP_44P1KHZ_SAMPLE_FREQ(x) ((x)[0] & 0x04)
#define BTM_SPLIT_A2DP_48KHZ_SAMPLE_FREQ(x) ((x)[0] & 0x08)
#define BTM_SPLIT_A2DP_SINGLE_VS_COMMAND_SUPPORTED(x) ((x)[0] & 0x10)
#define BTM_SPLIT_A2DP_SOURCE_SBC_ENCODING_SUPPORTED(x) ((x)[0] & 0x20)

#define BTM_SPLIT_A2DP_SOURCE_SBC_SUPPORTED(x) ((x)[1] & 0x01)
#define BTM_SPLIT_A2DP_SOURCE_MP3_SUPPORTED(x) ((x)[1] & 0x02)
#define BTM_SPLIT_A2DP_SOURCE_AAC_SUPPORTED(x) ((x)[1] & 0x04)
#define BTM_SPLIT_A2DP_SOURCE_LDAC_SUPPORTED(x) ((x)[1] & 0x08)
#define BTM_SPLIT_A2DP_SOURCE_APTX_SUPPORTED(x) ((x)[1] & 0x10)
#define BTM_SPLIT_A2DP_SOURCE_APTX_HD_SUPPORTED(x) ((x)[1] & 0x20)
#define BTM_SPLIT_A2DP_SOURCE_APTX__ADAPTIVE_SUPPORTED(x) ((x)[1] & 0x40)
#define BTM_SPLIT_A2DP_SOURCE_APTX__TWS_PLUS_SUPPORTED(x) ((x)[1] & 0x80)

#define BTM_SPLIT_A2DP_SINK_SBC_SUPPORTED(x) ((x)[2] & 0x01)
#define BTM_SPLIT_A2DP_SINK_MP3_SUPPORTED(x) ((x)[2] & 0x02)
#define BTM_SPLIT_A2DP_SINK_AAC_SUPPORTED(x) ((x)[2] & 0x04)
#define BTM_SPLIT_A2DP_SINK_LDAC_SUPPORTED(x) ((x)[2] & 0x08)
#define BTM_SPLIT_A2DP_SINK_APTX_SUPPORTED(x) ((x)[2] & 0x10)
#define BTM_SPLIT_A2DP_SINK_APTX_HD_SUPPORTED(x) ((x)[2] & 0x20)
#define BTM_SPLIT_A2DP_SINK_APTX__ADAPTIVE_SUPPORTED(x) ((x)[2] & 0x40)
#define BTM_SPLIT_A2DP_SINK_APTX__TWS_PLUS_SUPPORTED(x) ((x)[2] & 0x80)

#define BTM_VOICE_DUAL_SCO_SUPPORTED(x) ((x)[3] & 0x01)
#define BTM_VOICE_TWS_PLUS_DUAL_ESCO_AG_SUPPORTED(x) ((x)[3] & 0x02)
#define BTM_SWB_VOICE_WITH_APTX_ADAPTIVE_SUPPORTED(x) ((x)[3] & 0x04)
#define BTM_QBCE_QLE_HCI_SUPPORTED(x) ((x)[3] & 0x10)
#define BTM_QBCE_QCM_HCI_SUPPORTED(x) ((x)[3] & 0x20)
#define BTM_QBCE_QLL_FT_CHNAGE(x) ((x)[3] & 0x04)
#define BTM_QBCE_QLL_BN_VARIATION_BY_QHS_RATE(x) ((x)[3] & 0x08)
#define BTM_QBCE_QLL_CIS_PARAMETER_UPDATE_CONTROLLER(x) ((x)[4] & 0x08)
#define BTM_QBCE_QLL_MULTI_CONFIG_CIS_PARAMETER_UPDATE_CONTROLLER(x) ((x)[4] & 0x80)
#define BTM_QBCE_QLL_QSS_HOST_SUPPORT_BIT_0(x) ((x)[7] & 0x40]
#define BTM_QBCE_QLL_QSS_HOST_SUPPORT_BIT_1(x) ((x)[7] & 0x80]
#define BTM_SPLIT_A2DP_SOURCE_AAC_ABR_SUPPORTED(x) ((x)[3] & 0x40)
#define BTM_SPLIT_A2DP_SOURCE_Tx_Split_APTX_ADAPTIVE_SUPPORTED(x) ((x)[3] & 0x80)

#define BTM_BROADCAST_AUDIO_TX_WITH_EC_2_5(x) ((x)[4] & 0x01)
#define BTM_BROADCAST_AUDIO_TX_WITH_EC_3_9(x) ((x)[4] & 0x02)
#define BTM_BROADCAST_AUDIO_RX_WITH_EC_2_5(x) ((x)[4] & 0x04)
#define BTM_BROADCAST_AUDIO_RX_WITH_EC_3_9(x) ((x)[4] & 0x08)
#define BTM_ISO_CIG_PARAMETER_CALCULATOR(x) ((x)[4] & 0x10)

/* QCM PHY state */
#define QCM_PHY_STATE_BR_EDR 0
#define QCM_PHY_STATE_QHS 1

typedef struct {
  uint8_t as_array[SOC_ADD_ON_FEATURES_MAX_SIZE];
} bt_device_soc_add_on_features_t;

typedef struct {
  uint8_t as_array[HOST_ADD_ON_FEATURES_MAX_SIZE];
} bt_device_host_add_on_features_t;

typedef struct {
  uint8_t as_array[QLL_LOCAL_SUPPORTED_FEATURES_MAX_SIZE];
} bt_device_qll_local_supported_features_t;
