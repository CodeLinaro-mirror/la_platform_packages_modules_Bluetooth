/*
 * Copyright 2020 HIMSA II K/S - www.himsa.com.
 * Represented by EHIMA - www.ehima.com
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

#pragma once

#include <cstdint>

#include "hcimsgs.h"
#include "stack/include/bt_hdr.h"
#include "stack/include/btm_ble_api_types.h"

namespace bluetooth {
namespace hci {
constexpr uint8_t kIsoCodingFormatTransparent = 0x03;
constexpr uint8_t kIsoCodingFormatLc3 = 0x06;
constexpr uint8_t kIsoCodingFormatVendorSpecific = 0xFF;

namespace qcom {
constexpr uint16_t kIsoCodingFormatAptxLe = 0x0001;
constexpr uint16_t kIsoCodingFormatAptxLeX = 0x01AD;
}  // namespace qcom

constexpr uint8_t kIsoCigPackingSequential = 0x00;
constexpr uint8_t kIsoCigPackingInterleaved = 0x01;

constexpr uint8_t kIsoCigFramingUnframed = 0x00;
constexpr uint8_t kIsoCigFramingFramed = 0x01;

constexpr uint8_t kIsoCigPhy1M = 0x01;
constexpr uint8_t kIsoCigPhy2M = 0x02;
constexpr uint8_t kIsoCigPhyC = 0x04;

namespace iso_manager {

constexpr uint8_t kIsoDataPathDirectionIn = 0x00;
constexpr uint8_t kIsoDataPathDirectionOut = 0x01;

constexpr uint8_t kRemoveIsoDataPathDirectionInput = 0x01;
constexpr uint8_t kRemoveIsoDataPathDirectionOutput = 0x02;

constexpr uint8_t kIsoDataPathHci = 0x00;
constexpr uint8_t kIsoDataPathPlatformDefault = 0x01;
constexpr uint8_t kIsoDataPathDisabled = 0xFF;

constexpr uint8_t kIsoSca251To500Ppm = 0x00;
constexpr uint8_t kIsoSca151To250Ppm = 0x01;
constexpr uint8_t kIsoSca101To150Ppm = 0x02;
constexpr uint8_t kIsoSca76To100Ppm = 0x03;
constexpr uint8_t kIsoSca51To75Ppm = 0x04;
constexpr uint8_t kIsoSca31To50Ppm = 0x05;
constexpr uint8_t kIsoSca21To30Ppm = 0x06;
constexpr uint8_t kIsoSca0To20Ppm = 0x07;

constexpr uint8_t kIsoEventCisDataAvailable = 0x00;
constexpr uint8_t kIsoEventCisEstablishCmpl = 0x01;
constexpr uint8_t kIsoEventCisDisconnected = 0x02;

constexpr uint8_t kIsoEventCigOnCreateCmpl = 0x00;
constexpr uint8_t kIsoEventCigOnReconfigureCmpl = 0x01;
constexpr uint8_t kIsoEventCigOnRemoveCmpl = 0x02;

constexpr uint8_t kIsoEventBigOnCreateCmpl = 0x00;
constexpr uint8_t kIsoEventBigOnTerminateCmpl = 0x01;
constexpr uint8_t kIsoEventBigSyncEstablished = 0x02;
constexpr uint8_t kIsoEventBigSyncLost = 0x03;

/* BIG Error/Failure events */
constexpr uint8_t kIsoEventBigOnCreateFail = 0x04;
constexpr uint8_t kIsoEventBigSyncFail = 0x05;
constexpr uint8_t kIsoEventBigTerminated = 0x06;

/* DBIG (Duplex Broadcast Information Group) event types */
constexpr uint8_t kIsoEventDbigUpdate = 0x10;
constexpr uint8_t kIsoEventDbigCreateCmpl = 0x11;
constexpr uint8_t kIsoEventDbigStatus = 0x12;
constexpr uint8_t kIsoEventDbigTexitCmpl = 0x13;
constexpr uint8_t kIsoEventDbigRemoveDeviceCmpl = 0x14;

constexpr uint8_t kIsoEventBigOnSyncEstablished = 0x00;
constexpr uint8_t kIsoEventBigOnSyncLost = 0x01;
constexpr uint8_t kIsoEventBigOnTerminateSyncCmpl = 0x02;
constexpr uint8_t kIsoEventBisDataAvailable = 0x03;

struct cig_create_params {
  uint32_t sdu_itv_mtos;
  uint32_t sdu_itv_stom;
  uint8_t sca;
  uint8_t packing;
  uint8_t framing;
  uint16_t max_trans_lat_stom;
  uint16_t max_trans_lat_mtos;
  std::vector<EXT_CIS_CFG> cis_cfgs;
};

struct cig_remove_cmpl_evt {
  uint8_t status;
  uint8_t cig_id;
};

struct cig_create_cmpl_evt {
  uint8_t status;
  uint8_t cig_id;
  std::vector<uint16_t> conn_handles;
};

struct cis_data_evt {
  uint8_t cig_id;
  uint16_t cis_conn_hdl;
  uint32_t ts;
  uint16_t evt_lost;
  uint16_t seq_nb;
  BT_HDR* p_msg;
};

struct cis_establish_params {
  std::vector<EXT_CIS_CREATE_CFG> conn_pairs;
};

struct cis_establish_cmpl_evt {
  uint8_t status;
  uint8_t cig_id;
  uint16_t cis_conn_hdl;
  uint32_t cig_sync_delay;
  uint32_t cis_sync_delay;
  uint32_t trans_lat_mtos;
  uint32_t trans_lat_stom;
  uint8_t phy_mtos;
  uint8_t phy_stom;
  uint8_t nse;
  uint8_t bn_mtos;
  uint8_t bn_stom;
  uint8_t ft_mtos;
  uint8_t ft_stom;
  uint16_t max_pdu_mtos;
  uint16_t max_pdu_stom;
  uint16_t iso_itv;
};

struct cis_disconnected_evt {
  uint8_t reason;
  uint8_t cig_id;
  uint16_t cis_conn_hdl;
};

struct big_create_params {
  uint8_t adv_handle;
  uint8_t num_bis;
  uint32_t sdu_itv;
  uint16_t max_sdu_size;
  uint16_t max_transport_latency;
  uint8_t rtn;
  uint8_t phy;
  uint8_t packing;
  uint8_t framing;
  uint8_t enc;
  std::array<uint8_t, 16> enc_code;
};

struct dbig_create_params {
  uint8_t dbig_handle;
  uint8_t dbig_feature_set;
  uint8_t bis_detection_attempts;
  uint8_t max_payload_dbig_control;
  uint8_t bis_control_event_interval;
  uint8_t send_exit;
  uint8_t pgp_timeout;
  uint8_t pgo_timeout;
  uint8_t sgo_timeout;
  uint8_t join_timeout;
  uint8_t exit_timeout;
  uint8_t remove_timeout;
  uint8_t terminate_timeout;
  uint8_t tx_power;
};

/* Callback for HCI_VS_LE_JOIN_CONTROL complete event */
typedef void (dbig_join_control_complete_cb)(uint8_t status, uint8_t dbig_handle);

/* Callback for HCI_VS_LE_Texit_DBIG command complete */
typedef void (dbig_texit_cmpl_cb)(uint8_t status, uint8_t sub_opcode);

/* Callback for HCI_VS_LE_SET_DevID command complete */
typedef void (dbig_set_devid_cmpl_cb)(uint8_t status, uint8_t sub_opcode, uint16_t dev_id);

/* Callback for HCI_VS_LE_Remove_Device_DBIG command complete */
typedef void (dbig_remove_device_cmpl_cb)(uint8_t status, uint8_t dbig_handle, uint16_t dev_id);

/* Parameters for HCI_VS_LE_JOIN_CONTROL command */
struct dbig_join_control_params {
  uint8_t dbig_handle;
  uint8_t mode;
  dbig_join_control_complete_cb* p_cb;
};

/* Parameters for HCI_VS_LE_Texit_DBIG command */
struct dbig_texit_params {
  uint8_t dbig_handle;
  uint8_t texit_mode;
  uint8_t reason;
  dbig_texit_cmpl_cb* p_cb;
};

/* Parameters for HCI_VS_LE_SET_DevID command */
struct dbig_set_devid_params {
  uint16_t dev_id;
  uint8_t name[10];
  dbig_set_devid_cmpl_cb* p_cb;
};

typedef void dbig_sync_only_cmpl_cb(uint8_t status, uint8_t sub_opcode, uint8_t dbig_handle);

/* Parameters for HCI_VS_LE_DBIG_SYNC_ONLY command */
struct dbig_sync_only_params {
  uint8_t dbig_handle;
  uint8_t enable;
  dbig_sync_only_cmpl_cb* p_cb;
};

/* Parameters for HCI_VS_LE_Remove_Device_DBIG command */
struct dbig_remove_device_params {
  uint8_t dbig_handle;
  uint16_t dev_id;
  uint8_t name[10];
  uint8_t reason;
  dbig_remove_device_cmpl_cb* p_cb;
};

struct big_create_cmpl_evt {
  uint8_t status;
  uint8_t big_id;
  uint32_t big_sync_delay;
  uint32_t transport_latency_big;
  uint8_t phy;
  uint8_t nse;
  uint8_t bn;
  uint8_t pto;
  uint8_t irc;
  uint16_t max_pdu;
  uint16_t iso_interval;
  std::vector<uint16_t> conn_handles;
};

struct big_terminate_cmpl_evt {
  uint8_t big_id;
  uint8_t reason;
};

struct big_sync_params {
  uint16_t sync_handle;
  uint8_t encryption;
  std::array<uint8_t, 16> broadcast_code;
  uint8_t mse;
  uint16_t big_sync_timeout;
  std::vector<uint8_t> bis;
};

struct big_sync_established_evt {
  uint8_t status;
  uint8_t big_handle;
  uint32_t transport_latency_big;
  uint8_t nse;
  uint8_t bn;
  uint8_t pto;
  uint8_t irc;
  uint16_t max_pdu;
  uint16_t iso_interval;
  std::vector<uint16_t> conn_handles;
};

struct big_sync_lost_evt {
  uint8_t big_handle;
  uint8_t reason;
};

struct big_terminate_sync_cmpl_evt {
  uint8_t status;
  uint8_t big_handle;
};

struct bis_data_evt {
  uint8_t big_handle;
  uint16_t bis_conn_hdl;
  uint32_t ts;
  uint16_t evt_lost;
  uint16_t seq_nb;
  BT_HDR* p_msg;
};

struct iso_data_path_params {
  uint8_t data_path_dir;
  uint8_t data_path_id;
  uint8_t codec_id_format;
  uint16_t codec_id_company;
  uint16_t codec_id_vendor;
  uint32_t controller_delay;
  std::vector<uint8_t> codec_conf;
};

/* DBIG (Duplex Broadcast Information Group) create complete event */
struct dbig_create_cmpl_evt {
  uint8_t status;
  uint8_t sub_opcode;
  uint8_t dbig_handle;
};

/* Maximum number of BIS channels in a DBIG */
constexpr uint8_t kDbigMaxBisCount = 4;

/* DBIG (Duplex Broadcast Information Group) update event */
struct dbig_update_evt {
  uint8_t status;
  uint8_t big_handle;
  uint8_t bis_state;
  uint8_t timing_source;
  uint8_t local_bis_id;
  /* Extended fields: device ID, name, BIS info, broadcast features */
  uint16_t dev_id;
  std::vector<uint8_t> name;
  uint8_t num_bis;
  std::vector<uint16_t> bis_dev_ids;
  uint16_t broadcast_features;
};

/* DBIG (Duplex Broadcast Information Group) status event */
struct dbig_status_evt {
  uint8_t dbig_handle;
  uint16_t dbig_status;
  /* Extended fields: device ID, name, BIS info, broadcast features */
  uint16_t dev_id;
  std::vector<uint8_t> name;
  uint8_t num_bis;
  std::vector<uint16_t> bis_dev_ids;
  uint16_t broadcast_features;
};

/* DBIG TExitDbig completion event */
struct dbig_texit_cmpl_evt {
  uint8_t status;
  uint8_t dbig_handle;
  uint8_t reason;
};

/* DBIG Remove Device completion event */
struct dbig_remove_device_cmpl_evt {
  uint8_t  status;
  uint8_t  dbig_handle;
  uint16_t dev_id;
};

/* DBIG callbacks interface */
struct DbigCallbacks {
  virtual ~DbigCallbacks() = default;
  virtual void OnDbigEvent(uint8_t event, void* data) = 0;
};

}  // namespace iso_manager
}  // namespace hci
}  // namespace bluetooth
