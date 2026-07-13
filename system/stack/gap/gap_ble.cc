/******************************************************************************
 *
 *  Copyright 2017 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 *  Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

#include <bluetooth/log.h>
#include <bluetooth/types/address.h>
#include <com_android_bluetooth_flags.h>
#include <bluetooth/types/bt_transport.h>
#include <bluetooth/types/uuid.h>
#include <com_android_bluetooth_flags.h>
#include <string.h>

#include <array>
#include <queue>

#include "bta/include/bta_gatt_api.h"
#include "btif/include/btif_config.h"
#include "btif/include/btif_storage.h"
#include "btm_dev.h"
#include "btm_int_types.h"
#include "gap_api.h"
#include "gap_int.h"
#include "hardware/bt_gatt_types.h"
#include "main/shim/dumpsys.h"
#include "main/shim/le_advertising_manager.h"
#include "stack/gatt/gatt_int.h"
#include "stack/include/bt_types.h"
#include "stack/include/bt_uuid16.h"
#include "stack/include/btm_client_interface.h"
#include "stack/include/btm_status.h"
#include "stack_config.h"

#define GAP_ENC_KEY_CONNECTING 1     /* wait for connection */
#define GAP_ENC_KEY_CHARACTERISTIC 2 /* Read for Enc Key Material Characteristic */
#define GAP_ENC_KEY_CCCD 3           /* Discover CCCD */
#define GAP_ENC_KEY_CONFIG_CCCD 4    /* Write CCCD */
#include "stack/include/gatt_api.h"
#include "stack/include/stack_app.h"
#include "stack/include/stack_le_connection.h"

using bluetooth::Uuid;
using namespace bluetooth;
using stack::tGATT_REQ_CBACK;
extern tBTM_CB btm_cb;

void btm_ble_read_enc_key_cmpl(bool status, const RawAddress& bda, uint16_t length, char* p_data);
bool GAP_BleDiscEncKeyMaterialCCCD(const RawAddress& peer_bda);
void gap_ble_config_cccd_enc_key_cmpl(bool status, const RawAddress& bda, uint16_t length,
                                      char* p_name);
bool GAP_BleReadEncKeyMaterial(const RawAddress& peer_bda, uint16_t handle,
                               tGAP_BLE_CMPL_CBACK* p_cback);
bool GAP_BleConfigCccdForKeyMaterial(const RawAddress& peer_bda, uint16_t handle,
                                     tGAP_BLE_CMPL_CBACK* p_cback);

namespace {

typedef struct {
  uint16_t uuid;
  uint8_t op;
  uint16_t handle;
  tGATT_DISC_TYPE disc_type;
  Uuid char_uuid;
  uint16_t s_handle;
  tGAP_BLE_CMPL_CBACK* p_cback;
} tGAP_REQUEST;

typedef struct {
  RawAddress bda;
  tGAP_BLE_CMPL_CBACK* p_cback;
  tCONN_ID conn_id;
  uint16_t cl_op_uuid;
  bool connected;
  std::queue<tGAP_REQUEST> requests;
  uint8_t enc_key_stage;
  uint16_t curr_enc_key_char_handle;
  uint16_t curr_cccd_handle;
  uint8_t enc_key_result;
  Uuid char_uuid;
  std::vector<uint16_t> enc_key_char_handles;
  bool is_enc_key_info_in_progress;
} tGAP_CLCB;

typedef struct {
  uint16_t handle;
  uint16_t uuid;
  tGAP_BLE_ATTR_VALUE attr_value;
} tGAP_ATTR;

static void client_connect_cback(tGATT_IF, const RawAddress&, tCONN_ID, bool, tGATT_DISCONN_REASON,
                                 tBT_TRANSPORT);
static void client_cmpl_cback(tCONN_ID, tGATTC_OPTYPE, tGATT_STATUS, tGATT_CL_COMPLETE*);
void client_disc_res_cback(uint16_t, tGATT_DISC_TYPE, tGATT_DISC_RES*);
void client_disc_cmpl_cback(uint16_t, tGATT_DISC_TYPE, tGATT_STATUS);
void gap_cl_get_enc_key_info(tGAP_CLCB* p_clcb);

static void gap_read_characteristic_or_descriptor_cback(tCONN_ID conn_id, uint32_t trans_id,
                                                        const RawAddress& remote_bda,
                                                        uint16_t handle, uint16_t offset,
                                                        bool is_long);
static void gap_write_characteristic_or_descriptor_cback(tCONN_ID conn_id, uint32_t trans_id,
                                                         const RawAddress& remote_bda,
                                                         uint16_t handle, uint16_t offset,
                                                         bool need_rsp, bool is_prep,
                                                         uint8_t* value, uint16_t len);
static void gap_exec_write_cback(tCONN_ID conn_id, uint32_t trans_id, const RawAddress& remote_bda,
                                 tGATT_EXEC_FLAG exec_write);
static void gap_mtu_changed_cback(tCONN_ID conn_id, const RawAddress& remote_bda, uint16_t mtu);
static void gap_conf_cback(tCONN_ID conn_id, uint32_t trans_id, const RawAddress& remote_bda);

static stack::tGATT_REQ_CBACK gap_req_cback = {
        .read_characteristic_cb = gap_read_characteristic_or_descriptor_cback,
        .read_descriptor_cb = gap_read_characteristic_or_descriptor_cback,
        .write_characteristic_cb = gap_write_characteristic_or_descriptor_cback,
        .write_descriptor_cb = gap_write_characteristic_or_descriptor_cback,
        .exec_write_cb = gap_exec_write_cback,
        .mtu_changed_cb = gap_mtu_changed_cback,
        .conf_cb = gap_conf_cback,
};

stack::tGATT_CBACK gap_cback = {
        .p_conn_cb = client_connect_cback,
        .p_cmpl_cb = client_cmpl_cback,
        .p_disc_res_cb = client_disc_res_cback,
        .p_disc_cmpl_cb = client_disc_cmpl_cback,
        .p_req_cb = &gap_req_cback,
        .p_enc_cmpl_cb = nullptr,
        .p_congestion_cb = nullptr,
        .p_phy_update_cb = nullptr,
        .p_conn_update_cb = nullptr,
        .p_subrate_chg_cb = nullptr,
};

constexpr int GAP_CHAR_DEV_NAME_SIZE = BD_NAME_LEN;
constexpr int GAP_MAX_CHAR_NUM = 6;

std::vector<tGAP_CLCB> gap_clcbs;
/* LE GAP attribute database */
std::array<tGAP_ATTR, GAP_MAX_CHAR_NUM> gatt_attr;
tGATT_IF gatt_if;

/** returns LCB with matching bd address, or nullptr */
static tGAP_CLCB* find_clcb_by_bd_addr(const RawAddress& bda) {
  for (auto& cb : gap_clcbs) {
    if (cb.bda == bda) {
      return &cb;
    }
  }

  return nullptr;
}

/** returns LCB with matching connection ID, or nullptr if not found  */
static tGAP_CLCB* ble_find_clcb_by_conn_id(tCONN_ID conn_id) {
  for (auto& cb : gap_clcbs) {
    if (cb.connected && cb.conn_id == conn_id) {
      return &cb;
    }
  }

  return nullptr;
}

/** allocates a GAP connection link control block */
static tGAP_CLCB* clcb_alloc(const RawAddress& bda) {
  gap_clcbs.emplace_back();
  tGAP_CLCB& cb = gap_clcbs.back();
  cb.bda = bda;
  return &cb;
}

/** The function clean up the pending request queue in GAP */
static void clcb_dealloc(tGAP_CLCB& clcb) {
  // put last element into place of current element, and remove last one - just
  // fast remove.
  for (auto it = gap_clcbs.begin(); it != gap_clcbs.end(); it++) {
    if (it->conn_id == clcb.conn_id) {
      auto last_one = std::prev(gap_clcbs.end());
      *it = *last_one;
      gap_clcbs.erase(last_one);
      return;
    }
  }
}

/** GAP Attributes Database Request callback */
static tGATT_STATUS read_attr_value(uint16_t handle, tGATT_VALUE* p_value, bool is_long) {
  uint8_t* p = p_value->value;
  uint16_t offset = p_value->offset;
  uint8_t* p_dev_name = NULL;

  for (const tGAP_ATTR& db_attr : gatt_attr) {
    const tGAP_BLE_ATTR_VALUE& attr_value = db_attr.attr_value;
    if (handle == db_attr.handle) {
      if (btm_cb.encrypted_advertising_data_supported) {
        if (is_long) {
          if (db_attr.uuid == GATT_UUID_GAP_DEVICE_NAME ||
              db_attr.uuid == GATT_UUID_GAP_ENC_KEY_MATERIAL) {
            log::info(
                    "read request for GATT_UUID_GAP_DEVICE_NAME or "
                    "GATT_UUID_GAP_ENC_KEY_MATERIAL");
          } else {
            return GATT_NOT_LONG;
          }
        }
      } else {
        if (db_attr.uuid != GATT_UUID_GAP_DEVICE_NAME && is_long) {
          return GATT_NOT_LONG;
        }
      }

      switch (db_attr.uuid) {
        case GATT_UUID_GAP_DEVICE_NAME:
          if (get_btm_client_interface().local.BTM_ReadLocalDeviceName((const char**)&p_dev_name) !=
              tBTM_STATUS::BTM_SUCCESS) {
            log::warn("Unable to read local device name");
          };
          if (strlen((char*)p_dev_name) > GATT_MAX_ATTR_LEN) {
            p_value->len = GATT_MAX_ATTR_LEN;
          } else {
            p_value->len = (uint16_t)strlen((char*)p_dev_name);
          }

          if (offset > p_value->len) {
            return GATT_INVALID_OFFSET;
          } else {
            p_value->len -= offset;
            p_dev_name += offset;
            ARRAY_TO_STREAM(p, p_dev_name, p_value->len);
          }
          break;

        case GATT_UUID_GAP_ICON:
          UINT16_TO_STREAM(p, attr_value.icon);
          p_value->len = 2;
          break;

        case GATT_UUID_GAP_PREF_CONN_PARAM:
          UINT16_TO_STREAM(p, attr_value.conn_param.int_min); /* int_min */
          UINT16_TO_STREAM(p, attr_value.conn_param.int_max); /* int_max */
          UINT16_TO_STREAM(p, attr_value.conn_param.latency); /* latency */
          UINT16_TO_STREAM(p, attr_value.conn_param.sp_tout); /* sp_tout */
          p_value->len = 8;
          break;

        /* address resolution */
        case GATT_UUID_GAP_CENTRAL_ADDR_RESOL:
          UINT8_TO_STREAM(p, attr_value.addr_resolution);
          p_value->len = 1;
          break;
        /* Encrypted Data Key Material*/
        case GATT_UUID_GAP_ENC_KEY_MATERIAL:
          if (btm_cb.encrypted_advertising_data_supported) {
            uint8_t* p_encr_material = p;
            uint8_t* p_temp = p_encr_material;
            // PTS override: Check if authorization should be enforced for Encrypted Data Key Material
            if (stack_config_get_interface()->get_pts_enable_authorization_encr_data_key()) {
                log::info("PTS_EnableAuthorizationEncrDataKey is set, returning GATT_INSUF_AUTHORIZATION");
                return GATT_INSUF_AUTHORIZATION;
            }
            REVERSE_ARRAY_TO_STREAM(p_encr_material, attr_value.enc_key_material.session_key,
                                    ENC_KEY_LEN);
            REVERSE_ARRAY_TO_STREAM(p_encr_material, attr_value.enc_key_material.init_vector,
                                    ENC_IV_LEN);
            if (offset > ENC_KEY_MATERIAL_LEN) {
              log::error(" GATT_INVALID_OFFSET");
              return GATT_INVALID_OFFSET;
            }
            p_value->len = ENC_KEY_MATERIAL_LEN - offset;
            p_temp += offset;
            ARRAY_TO_STREAM(p, p_temp, p_value->len);
          }
          break;
      }
      return GATT_SUCCESS;
    }
  }
  return GATT_NOT_FOUND;
}

static void gap_read_characteristic_or_descriptor_cback(tCONN_ID conn_id, uint32_t trans_id,
                                                        const RawAddress& /*remote_bda*/,
                                                        uint16_t handle, uint16_t offset,
                                                        bool is_long) {
  tGATTS_RSP rsp_msg{};
  if (is_long) {
    rsp_msg.attr_value.offset = offset;
  }
  rsp_msg.attr_value.handle = handle;
  tGATT_STATUS status = read_attr_value(handle, &rsp_msg.attr_value, is_long);
  if (GATTS_SendRsp(conn_id, trans_id, status, &rsp_msg)) {
    log::warn("Unable to send GATT server response conn_id:{}", conn_id);
  }
}

static void gap_write_characteristic_or_descriptor_cback(tCONN_ID conn_id, uint32_t trans_id,
                                                         const RawAddress& /*remote_bda*/,
                                                         uint16_t handle, uint16_t /*offset*/,
                                                         bool need_rsp, bool /*is_prep*/,
                                                         uint8_t* value, uint16_t /*len*/) {
  if (need_rsp) {
    tGATT_STATUS status = GATT_NOT_FOUND;
    for (const auto& db_addr : gatt_attr) {
      if (handle == db_addr.handle) {
        status = GATT_WRITE_NOT_PERMIT;
        break;
      }
    }
    tGATTS_RSP rsp_msg{};
    if (GATTS_SendRsp(conn_id, trans_id, status, &rsp_msg)) {
      log::warn("Unable to send GATT server response conn_id:{}", conn_id);
    }
  }
}

static void gap_exec_write_cback(tCONN_ID /*conn_id*/, uint32_t /*trans_id*/,
                                 const RawAddress& /*remote_bda*/, tGATT_EXEC_FLAG /*exec_write*/) {
  log::verbose("Ignore GATTS_REQ_TYPE_WRITE_EXEC");
}

static void gap_mtu_changed_cback(tCONN_ID /*conn_id*/, const RawAddress& /*remote_bda*/,
                                  uint16_t mtu) {
  log::verbose("Get MTU exchange new mtu size: {}", mtu);
}

static void gap_conf_cback(tCONN_ID /*conn_id*/, uint32_t /*trans_id*/,
                           const RawAddress& /*remote_bda*/) {}

/**
 * utility function to send a Discover req for CCCD descriptor.
 * Returns true if discovery started, else false if GAP is busy.
 */
bool send_cl_disc_request(tGAP_CLCB& clcb) {
  if (!clcb.requests.size()) {
    return false;
  }
  tGAP_REQUEST& req = clcb.requests.front();
  uint8_t disc_type = req.disc_type;
  Uuid char_uuid = req.char_uuid;
  clcb.requests.pop();

  uint16_t s_handle = (clcb.curr_enc_key_char_handle + 1);
  uint16_t e_handle = (clcb.curr_enc_key_char_handle + 2);

  GATTC_Discover(clcb.conn_id, static_cast<tGATT_DISC_TYPE>(disc_type), s_handle, e_handle,
                 char_uuid);
  return true;
}

/**
 * Utility function to send a read request for GAP characteristics.
 * Returns true if read started, else false if GAP is busy.
 */
static bool send_cl_read_request(tGAP_CLCB& clcb) {
  if (!clcb.requests.size() || clcb.cl_op_uuid != 0) {
    return false;
  }

  tGAP_REQUEST& req = clcb.requests.front();
  clcb.p_cback = req.p_cback;
  uint16_t uuid = req.uuid;
  clcb.requests.pop();

  tGATT_READ_PARAM param;
  memset(&param, 0, sizeof(tGATT_READ_PARAM));

  param.service.uuid = Uuid::From16Bit(uuid);
  param.service.s_handle = 1;
  param.service.e_handle = 0xFFFF;
  param.service.auth_req = 0;

  if (btm_cb.encrypted_advertising_data_supported) {
    log::verbose(": req handle: 0x{:04x} ,  UUID: 0x{:04x}", req.handle, uuid);
    if (uuid == GATT_UUID_GAP_ENC_KEY_MATERIAL) {
      param.service.s_handle = req.handle;
      param.service.e_handle = 0xFFFF;
    }
  }

  if (GATTC_Read(clcb.conn_id, GATT_READ_BY_TYPE, &param) == GATT_SUCCESS) {
    clcb.cl_op_uuid = uuid;
  }

  return true;
}

/**
 * utility function to send a write request for a CCCD descriptor.
 * Returns true if read started, else false if GAP is busy.
 */
bool send_cl_write_request(tGAP_CLCB& clcb) {
  if (!clcb.requests.size()) {
    return false;
  }
  tGAP_REQUEST& req = clcb.requests.front();
  clcb.p_cback = req.p_cback;
  clcb.cl_op_uuid = GATT_UUID_CHAR_CLIENT_CONFIG;
  uint16_t handle = req.handle;
  clcb.requests.pop();

  tGATT_VALUE ccc_value{
          .handle = handle,
          .len = 2,
          .value[0] = GATT_CLT_CONFIG_INDICATION,
  };

  GATTC_Write(clcb.conn_id, GATT_WRITE, &ccc_value);
  return true;
}

bool send_cl_request(tGAP_CLCB& clcb) {
  if (!clcb.requests.size()) {
    return false;
  }

  tGAP_REQUEST& req = clcb.requests.front();
  uint8_t op = req.op;
  if (op == GATTC_OPTYPE_READ) {
    send_cl_read_request(clcb);
  } else if (op == GATTC_OPTYPE_WRITE) {
    send_cl_write_request(clcb);
  } else if (op == GATTC_OPTYPE_DISCOVERY) {
    send_cl_disc_request(clcb);
  }
  return true;
}

/** GAP client operation complete callback */
static void cl_op_cmpl(tGAP_CLCB& clcb, bool status, uint16_t len, uint8_t* p_name) {
  tGAP_BLE_CMPL_CBACK* p_cback = clcb.p_cback;
  uint16_t op = clcb.cl_op_uuid;

  clcb.cl_op_uuid = 0;
  clcb.p_cback = NULL;

  if (p_cback && op) {
    (*p_cback)(status, clcb.bda, len, (char*)p_name);
  }

  /* if no further activity is requested in callback, drop the link */
  if (clcb.connected) {
    if (btm_cb.encrypted_advertising_data_supported) {
      if (!send_cl_request(clcb) && (clcb.enc_key_stage <= GAP_ENC_KEY_CONNECTING)) {
        log::debug(" Calling GATT Disconnect");
        GATT_Disconnect(clcb.conn_id);
        clcb_dealloc(clcb);
      }
    } else {
      if (!send_cl_read_request(clcb)) {
        if (GATT_Disconnect(clcb.conn_id) != GATT_SUCCESS) {
          log::warn("Unable to disconnect GATT conn_id:{}", clcb.conn_id);
        }
        clcb_dealloc(clcb);
      }
    }
  }
}

static void call_pending_gap_name_callback(tGAP_CLCB& clcb, bool status, uint16_t len, uint8_t* p_name) {
  while (!clcb.requests.empty()) {
    tGAP_REQUEST req = clcb.requests.front();
    clcb.requests.pop();

    if (req.uuid == GATT_UUID_GAP_DEVICE_NAME && req.p_cback && req.op) {
      log::info("bda={}", clcb.bda);
      (*(req.p_cback))(status, clcb.bda, len, (char*)p_name);
    }
  }
}

/** Client connection callback */
static void client_connect_cback(tGATT_IF, const RawAddress& bda, tCONN_ID conn_id, bool connected,
                                 tGATT_DISCONN_REASON /* reason */, tBT_TRANSPORT) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(bda);
  if (p_clcb == NULL) {
    log::info("No active GAP service found for peer:{} callback:{}", bda,
              (connected) ? "Connected" : "Disconnected");
    return;
  }

  if (connected) {
    log::debug("Connected GAP to remote device");
    p_clcb->conn_id = conn_id;
    p_clcb->connected = true;
    /* start operation is pending */
    if (btm_cb.encrypted_advertising_data_supported) {
      send_cl_request(*p_clcb);
    } else {
      send_cl_read_request(*p_clcb);
    }
  } else {
    log::warn("Disconnected GAP from remote device");
    p_clcb->connected = false;
    cl_op_cmpl(*p_clcb, false, 0, NULL);
    call_pending_gap_name_callback(*p_clcb, false, 0, NULL);
    /* clean up clcb */
    clcb_dealloc(*p_clcb);
  }
}

/*******************************************************************************
 *
 * Function         client_disc_res_cback
 *
 * Description      Gatt profile discovery result callback
 *
 * Returns          void
 *
 ******************************************************************************/
void client_disc_res_cback(uint16_t conn_id, tGATT_DISC_TYPE disc_type, tGATT_DISC_RES* p_data) {
  tGAP_CLCB* p_clcb = ble_find_clcb_by_conn_id(conn_id);
  if (p_clcb == NULL) {
    return;
  }

  log::debug("disc type: {} enc_key_stage: {} ", disc_type, p_clcb->enc_key_stage);
  switch (disc_type) {
    case GATT_DISC_CHAR_DSCPT: /* stage 3 */
      if (p_data->type == Uuid::From16Bit(GATT_UUID_CHAR_CLIENT_CONFIG)) {
        p_clcb->curr_cccd_handle = p_data->handle;
        p_clcb->enc_key_stage++;
        p_clcb->enc_key_result++;
        log::debug("curr_cccd_handle: 0x{:04x}", p_clcb->curr_cccd_handle);
      }
      break;
    default:
      break;
  }
}

/*******************************************************************************
 *
 * Function         client_disc_cmpl_cback
 *
 * Description      Gatt profile discovery complete callback
 *
 * Returns          void
 *
 ******************************************************************************/
void client_disc_cmpl_cback(uint16_t conn_id, tGATT_DISC_TYPE disc_type, tGATT_STATUS status) {
  tGAP_CLCB* p_clcb = ble_find_clcb_by_conn_id(conn_id);
  if (p_clcb == NULL) {
    return;
  }

  log::debug("status={}, enc_key_stage={}", status, p_clcb->enc_key_stage);

  if (status != GATT_SUCCESS || p_clcb->enc_key_result == 0) {
    log::warn("Unable to register for enc key material indication ");
    p_clcb->is_enc_key_info_in_progress = false;
    /* Release LE ACL link if no more pending requests and process is done */
    if (p_clcb->connected && !send_cl_request(*p_clcb)) {
      log::debug("CCCD discovery failed, releasing LE link conn_id:{}", p_clcb->conn_id);
      GATT_Disconnect(p_clcb->conn_id);
      clcb_dealloc(*p_clcb);
    }
    return;
  }

  if (p_clcb->enc_key_stage > GAP_ENC_KEY_CONNECTING) {
    p_clcb->enc_key_result = 0;
    gap_cl_get_enc_key_info(p_clcb);
  }
}

/** Client operation complete callback */
static void client_cmpl_cback(tCONN_ID conn_id, tGATTC_OPTYPE op, tGATT_STATUS status,
                              tGATT_CL_COMPLETE* p_data) {
  tGAP_CLCB* p_clcb = ble_find_clcb_by_conn_id(conn_id);
  uint16_t op_type;
  uint16_t min, max, latency, tout;
  uint16_t len;
  uint8_t* pp;

  if (p_clcb == NULL) {
    return;
  }

  op_type = p_clcb->cl_op_uuid;

  if (op != GATTC_OPTYPE_READ) {
    if (btm_cb.encrypted_advertising_data_supported && op == GATTC_OPTYPE_WRITE) {
      cl_op_cmpl(*p_clcb, status == GATT_SUCCESS, 0, NULL);
    }
    return;
  }

  if (status != GATT_SUCCESS) {
    cl_op_cmpl(*p_clcb, false, 0, NULL);
    return;
  }

  pp = p_data->att_value.value;
  switch (op_type) {
    case GATT_UUID_GAP_PREF_CONN_PARAM:
      /* Extract the peripheral preferred connection parameters and save them */
      STREAM_TO_UINT16(min, pp);
      STREAM_TO_UINT16(max, pp);
      STREAM_TO_UINT16(latency, pp);
      STREAM_TO_UINT16(tout, pp);

      get_btm_client_interface().ble.BTM_BleSetPrefConnParams(p_clcb->bda, min, max, latency, tout);
      /* release the connection here */
      cl_op_cmpl(*p_clcb, true, 0, NULL);
      break;

    case GATT_UUID_GAP_DEVICE_NAME:
      len = (uint16_t)strlen((char*)pp);
      if (len > GAP_CHAR_DEV_NAME_SIZE) {
        len = GAP_CHAR_DEV_NAME_SIZE;
      }
      cl_op_cmpl(*p_clcb, true, len, pp);
      break;

    case GATT_UUID_GAP_CENTRAL_ADDR_RESOL:
      cl_op_cmpl(*p_clcb, true, 1, pp);
      break;

    case GATT_UUID_GAP_ICON:
      cl_op_cmpl(*p_clcb, true, p_data->att_value.len, pp);
      break;

    default:
      log::error("Unexpected operation {}", op);
      break;
  }

  if (btm_cb.encrypted_advertising_data_supported) {
    if (op == GATTC_OPTYPE_READ) {
      switch (op_type) {
        case GATT_UUID_GAP_ENC_KEY_MATERIAL:
          p_clcb->curr_enc_key_char_handle = p_data->att_value.handle;
          p_clcb->enc_key_char_handles.push_back(p_data->att_value.handle);
          len = (uint16_t)strlen((char*)pp);
          cl_op_cmpl(*p_clcb, true, len, pp);
          break;
      }
    } else if (op == GATTC_OPTYPE_WRITE) {
      cl_op_cmpl(*p_clcb, true, 0, NULL);
    } else if (op == GATTC_OPTYPE_INDICATION) {
      log::info("Received Indication in GAP profile");
      uint16_t handle = p_data->att_value.handle;

      if (std::find(p_clcb->enc_key_char_handles.begin(), p_clcb->enc_key_char_handles.end(),
                    handle) != p_clcb->enc_key_char_handles.end()) {
        log::info("Received Indication for Enc key material char in GAP profile");
        GAP_BleGetEncKeyMaterialInfo(p_clcb->bda);
      }
    }
  }
}

static bool accept_client_operation(const RawAddress& peer_bda, uint16_t uuid,
                                    tGAP_BLE_CMPL_CBACK* p_cback) {
  if (p_cback == NULL && uuid != GATT_UUID_GAP_PREF_CONN_PARAM) {
    return false;
  }

  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(peer_bda);
  if (p_clcb == NULL) {
    p_clcb = clcb_alloc(peer_bda);
  }

  if (GATT_GetConnIdIfConnected(gatt_if, peer_bda, &p_clcb->conn_id, BT_TRANSPORT_LE)) {
    p_clcb->connected = true;
  }

  if (!stack::leConnectionConnect(gatt_if, p_clcb->bda, BLE_ADDR_PUBLIC, BTM_BLE_OPPORTUNISTIC, 0,
                                  false, com_android_bluetooth_flags_gatt_conn_settings())) {
    return false;
  }

  /* enqueue the request */
  p_clcb->requests.push({.uuid = uuid, .op = GATTC_OPTYPE_READ, .p_cback = p_cback});

  if (p_clcb->connected && p_clcb->cl_op_uuid == 0) {
    return send_cl_read_request(*p_clcb);
  } else { /* wait for connection up or pending operation to finish */
    return true;
  }
}

bool accept_client_operation(const RawAddress& peer_bda, uint16_t uuid, uint16_t handle, uint8_t op,
                             uint8_t disc_type, tGAP_BLE_CMPL_CBACK* p_cback) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(peer_bda);
  if (p_clcb == NULL) {
    p_clcb = clcb_alloc(peer_bda);
  }

  if (GATT_GetConnIdIfConnected(gatt_if, peer_bda, &p_clcb->conn_id, BT_TRANSPORT_LE)) {
    p_clcb->connected = true;
  }

  if (!stack::leConnectionConnect(gatt_if, p_clcb->bda, BLE_ADDR_PUBLIC, BTM_BLE_OPPORTUNISTIC,
                                  0, false, com_android_bluetooth_flags_gatt_conn_settings())) {
    return false;
  }

  /* enqueue the request */
  if (op == GATTC_OPTYPE_READ) {
    /* enqueue the read request */
    p_clcb->requests.push({.uuid = uuid, .op = op, .handle = handle, .p_cback = p_cback});
  } else if (op == GATTC_OPTYPE_WRITE) {
    /* enqueue the write request */
    p_clcb->requests.push({.op = op, .handle = handle, .p_cback = p_cback});
  } else if (op == GATTC_OPTYPE_DISCOVERY) {
    /* enqueue the disc request */
    p_clcb->requests.push({.op = op, .disc_type = static_cast<tGATT_DISC_TYPE>(disc_type),
                                                                     .p_cback = p_cback});
  }

  if (p_clcb->connected && p_clcb->cl_op_uuid == 0) {
    if (op == GATTC_OPTYPE_READ) {
      return send_cl_read_request(*p_clcb);
    } else if (op == GATTC_OPTYPE_WRITE) {
      return send_cl_write_request(*p_clcb);
    } else if (op == GATTC_OPTYPE_DISCOVERY) {
      return send_cl_disc_request(*p_clcb);
    } else { /* wait for connection up or pending operation to finish */
      return true;
    }
  } else { /* wait for connection up or pending operation to finish */
    return true;
  }
}

/*******************************************************************************
 *
 * Function         gap_cl_get_enc_key_info
 *
 * Description      get and configure encryption key material characteristic
 *
 * Returns          void
 *
 ******************************************************************************/
void gap_cl_get_enc_key_info(tGAP_CLCB* p_clcb) {
  log::debug("stage: {}", p_clcb->enc_key_stage);
  switch (p_clcb->enc_key_stage) {
    case GAP_ENC_KEY_CHARACTERISTIC: /* Read Enc Key Material Char */
      GAP_BleReadEncKeyMaterial(p_clcb->bda, (p_clcb->curr_enc_key_char_handle + 1),
                                btm_ble_read_enc_key_cmpl);
      break;

    case GAP_ENC_KEY_CCCD: /* Discover CCCD */
      GAP_BleDiscEncKeyMaterialCCCD(p_clcb->bda);
      break;

    case GAP_ENC_KEY_CONFIG_CCCD: /* Config cccd */
      GAP_BleConfigCccdForKeyMaterial(p_clcb->bda, p_clcb->curr_cccd_handle,
                                      gap_ble_config_cccd_enc_key_cmpl);
      break;
    default:
      log::warn("Invalid Input for Encryption Key Material to fetch ");
  }
}
}  // namespace

/*******************************************************************************
 *
 * Function         gap_attr_db_init
 *
 * Description      GAP ATT database initialization.
 *
 * Returns          void.
 *
 ******************************************************************************/
void gap_attr_db_init(void) {
  /* Fill our internal UUID with a fixed pattern 0x82 */
  std::array<uint8_t, Uuid::kNumBytes128> tmp;
  tmp.fill(0x82);
  Uuid app_uuid = Uuid::From128BitBE(tmp);
  gatt_attr.fill({});

  gatt_if = stack::appRegister(app_uuid, "Gap", &gap_cback, false);

  stack::appStartIf(gatt_if);

  Uuid svc_uuid = Uuid::From16Bit(UUID_SERVCLASS_GAP_SERVER);
  Uuid name_uuid = Uuid::From16Bit(GATT_UUID_GAP_DEVICE_NAME);
  Uuid icon_uuid = Uuid::From16Bit(GATT_UUID_GAP_ICON);
  Uuid addr_res_uuid = Uuid::From16Bit(GATT_UUID_GAP_CENTRAL_ADDR_RESOL);
  Uuid cccd_uuid = Uuid::From16Bit(GATT_UUID_CHAR_CLIENT_CONFIG);

  if (btm_cb.encrypted_advertising_data_supported) {
    Uuid encr_data_uuid = Uuid::From16Bit(GATT_UUID_GAP_ENC_KEY_MATERIAL);
    btgatt_db_element_t service[] = {
            {
                    .uuid = svc_uuid,
                    .type = BTGATT_DB_PRIMARY_SERVICE,
            },
            {.uuid = name_uuid,
             .type = BTGATT_DB_CHARACTERISTIC,
             .properties = GATT_CHAR_PROP_BIT_READ,
             .permissions = GATT_PERM_READ_IF_ENCRYPTED_OR_DISCOVERABLE},
            {.uuid = icon_uuid,
             .type = BTGATT_DB_CHARACTERISTIC,
             .properties = GATT_CHAR_PROP_BIT_READ,
             .permissions = GATT_PERM_READ},
            {.uuid = addr_res_uuid,
             .type = BTGATT_DB_CHARACTERISTIC,
             .properties = GATT_CHAR_PROP_BIT_READ,
             .permissions = GATT_PERM_READ},
            {.uuid = encr_data_uuid,
             .type = BTGATT_DB_CHARACTERISTIC,
             .properties = GATT_CHAR_PROP_BIT_READ | GATT_CHAR_PROP_BIT_INDICATE,
             .permissions = GATT_READ_AUTH_REQUIRED},
            {.uuid = cccd_uuid,
             .type = BTGATT_DB_DESCRIPTOR,
             .permissions = (GATT_PERM_READ | GATT_PERM_WRITE)}
#if (BTM_PERIPHERAL_ENABLED == TRUE) /* Only needed for peripheral testing */
            ,
            {.uuid = Uuid::From16Bit(GATT_UUID_GAP_PREF_CONN_PARAM),
             .type = BTGATT_DB_CHARACTERISTIC,
             .properties = GATT_CHAR_PROP_BIT_READ,
             .permissions = GATT_PERM_READ}
#endif
    };

    /* Add a GAP service */
    GATTS_AddService(gatt_if, service, sizeof(service) / sizeof(btgatt_db_element_t));

    gatt_attr[0].uuid = GATT_UUID_GAP_DEVICE_NAME;
    gatt_attr[0].handle = service[1].attribute_handle;

    gatt_attr[1].uuid = GATT_UUID_GAP_ICON;
    gatt_attr[1].handle = service[2].attribute_handle;

    gatt_attr[2].uuid = GATT_UUID_GAP_CENTRAL_ADDR_RESOL;
    gatt_attr[2].handle = service[3].attribute_handle;
    gatt_attr[2].attr_value.addr_resolution = 0;

    gatt_attr[3].uuid = GATT_UUID_GAP_ENC_KEY_MATERIAL;
    gatt_attr[3].handle = service[4].attribute_handle;
    gatt_attr[4].uuid = GATT_UUID_CHAR_CLIENT_CONFIG;
    gatt_attr[4].handle = service[5].attribute_handle;

#if (BTM_PERIPHERAL_ENABLED == TRUE) /*  Only needed for peripheral testing */

    gatt_attr[5].uuid = GATT_UUID_GAP_PREF_CONN_PARAM;
    gatt_attr[5].attr_value.conn_param.int_max = GAP_PREFER_CONN_INT_MAX; /* 6 */
    gatt_attr[5].attr_value.conn_param.int_min = GAP_PREFER_CONN_INT_MIN; /* 0 */
    gatt_attr[5].attr_value.conn_param.latency = GAP_PREFER_CONN_LATENCY; /* 0 */
    gatt_attr[5].attr_value.conn_param.sp_tout = GAP_PREFER_CONN_SP_TOUT; /* 2000 */
    gatt_attr[5].handle = service[6].attribute_handle;
#endif
    return;
  }

  btgatt_db_element_t service[] = {{
                                           .uuid = svc_uuid,
                                           .type = BTGATT_DB_PRIMARY_SERVICE,
                                   },
                                   {.uuid = name_uuid,
                                    .type = BTGATT_DB_CHARACTERISTIC,
                                    .properties = GATT_CHAR_PROP_BIT_READ,
                                    .permissions = GATT_PERM_READ_IF_ENCRYPTED_OR_DISCOVERABLE},
                                   {.uuid = icon_uuid,
                                    .type = BTGATT_DB_CHARACTERISTIC,
                                    .properties = GATT_CHAR_PROP_BIT_READ,
                                    .permissions = GATT_PERM_READ},
                                   {.uuid = addr_res_uuid,
                                    .type = BTGATT_DB_CHARACTERISTIC,
                                    .properties = GATT_CHAR_PROP_BIT_READ,
                                    .permissions = GATT_PERM_READ}
#if (BTM_PERIPHERAL_ENABLED == TRUE) /* Only needed for peripheral testing */
                                   ,
                                   {.uuid = Uuid::From16Bit(GATT_UUID_GAP_PREF_CONN_PARAM),
                                    .type = BTGATT_DB_CHARACTERISTIC,
                                    .properties = GATT_CHAR_PROP_BIT_READ,
                                    .permissions = GATT_PERM_READ}
#endif
  };

  /* Add a GAP service */
  if (GATTS_AddService(gatt_if, service, sizeof(service) / sizeof(btgatt_db_element_t)) !=
      GATT_SERVICE_STARTED) {
    log::warn("Unable to add GATT services gatt_if:{}", gatt_if);
  }

  gatt_attr[0].uuid = GATT_UUID_GAP_DEVICE_NAME;
  gatt_attr[0].handle = service[1].attribute_handle;

  gatt_attr[1].uuid = GATT_UUID_GAP_ICON;
  gatt_attr[1].handle = service[2].attribute_handle;

  gatt_attr[2].uuid = GATT_UUID_GAP_CENTRAL_ADDR_RESOL;
  gatt_attr[2].handle = service[3].attribute_handle;
  gatt_attr[2].attr_value.addr_resolution = 0;

#if (BTM_PERIPHERAL_ENABLED == TRUE) /*  Only needed for peripheral testing */

  gatt_attr[3].uuid = GATT_UUID_GAP_PREF_CONN_PARAM;
  gatt_attr[3].attr_value.conn_param.int_max = GAP_PREFER_CONN_INT_MAX; /* 6 */
  gatt_attr[3].attr_value.conn_param.int_min = GAP_PREFER_CONN_INT_MIN; /* 0 */
  gatt_attr[3].attr_value.conn_param.latency = GAP_PREFER_CONN_LATENCY; /* 0 */
  gatt_attr[3].attr_value.conn_param.sp_tout = GAP_PREFER_CONN_SP_TOUT; /* 2000 */
  gatt_attr[3].handle = service[4].attribute_handle;
#endif
}

/* Function to check if Indications are required*/
void gap_chk_encr_data(RawAddress bda, uint16_t conn_id, tGAP_ATTR db_attr) {
  uint8_t encr_data_cccd = btif_storage_get_encr_data_cccd(bda);
  if (encr_data_cccd != GATT_CHAR_CLIENT_CONFIG_INDICTION) {
    log::debug("Discard Encr DataKey - CCCD disabled: {}", encr_data_cccd);
    return;
  }

  if (conn_id == GATT_INVALID_CONN_ID) {
    log::error("Unable to find conn_id for {}", bda.ToRedactedStringForLogging());
    return;
  }

  uint8_t encr_material[ENC_KEY_MATERIAL_LEN];
  memcpy(encr_material, &db_attr.attr_value.enc_key_material, ENC_KEY_MATERIAL_LEN);
  GATTS_HandleValueIndication(conn_id, db_attr.handle, sizeof(encr_material), encr_material);
}

/*******************************************************************************
 *
 * Function         GAP_BleAttrDBUpdate
 *
 * Description      GAP ATT database update.
 *
 ******************************************************************************/
void GAP_BleAttrDBUpdate(uint16_t attr_uuid, tGAP_BLE_ATTR_VALUE* p_value) {
  for (tGAP_ATTR& db_attr : gatt_attr) {
    if (db_attr.uuid == attr_uuid) {
      switch (attr_uuid) {
        case GATT_UUID_GAP_ICON:
          db_attr.attr_value.icon = p_value->icon;
          break;

        case GATT_UUID_GAP_PREF_CONN_PARAM:
          memcpy((void*)&db_attr.attr_value.conn_param, (const void*)&p_value->conn_param,
                 sizeof(tGAP_BLE_PREF_PARAM));
          break;

        case GATT_UUID_GAP_DEVICE_NAME:
          if (get_btm_client_interface().local.BTM_SetLocalDeviceName(
                      (const char*)p_value->p_dev_name) != tBTM_STATUS::BTM_SUCCESS) {
            log::warn("Unable to set local name");
          }
          break;

        case GATT_UUID_GAP_CENTRAL_ADDR_RESOL:
          db_attr.attr_value.addr_resolution = p_value->addr_resolution;
          break;
        case GATT_UUID_GAP_ENC_KEY_MATERIAL:
          if (btm_cb.encrypted_advertising_data_supported) {
            if (std::memcmp((const void*)&db_attr.attr_value.enc_key_material,
                            (const void*)&p_value->enc_key_material,
                            sizeof(tGAP_BLE_ENC_KEY_MATERIAL)) != 0) {
              db_attr.attr_value.enc_key_material = p_value->enc_key_material;
              for (auto& cb : gap_clcbs) {
                if (cb.connected) {
                  log::debug(" BDA: {} conn_id: {}", cb.bda.ToRedactedStringForLogging(), cb.conn_id);
                  gap_chk_encr_data(cb.bda, cb.conn_id, db_attr);
                }
              }
            }
          }
          break;
      }
      break;
    }
  }

  return;
}

/*******************************************************************************
 *
 * Function         GAP_BleReadPeerPrefConnParams
 *
 * Description      Start a process to read a connected peripheral's preferred
 *                  connection parameters
 *
 * Returns          true if read started, else false if GAP is busy
 *
 ******************************************************************************/
bool GAP_BleReadPeerPrefConnParams(const RawAddress& peer_bda) {
  return accept_client_operation(peer_bda, GATT_UUID_GAP_PREF_CONN_PARAM, NULL);
}

/*******************************************************************************
 *
 * Function         GAP_BleReadPeerDevName
 *
 * Description      Start a process to read a connected peripheral's device
 *                  name.
 *
 * Returns          true if request accepted
 *
 ******************************************************************************/
bool GAP_BleReadPeerDevName(const RawAddress& peer_bda, tGAP_BLE_CMPL_CBACK* p_cback) {
  return accept_client_operation(peer_bda, GATT_UUID_GAP_DEVICE_NAME, p_cback);
}

/*******************************************************************************
 *
 * Function         GAP_BleReadPeerAppearance
 *
 * Description      Start a process to read a connected peripheral's appearance.
 *
 * Returns          true if request accepted
 *
 ******************************************************************************/
bool GAP_BleReadPeerAppearance(const RawAddress& peer_bda, tGAP_BLE_CMPL_CBACK* p_cback) {
  return accept_client_operation(peer_bda, GATT_UUID_GAP_ICON, p_cback);
}

/*******************************************************************************
 *
 * Function         GAP_BleCancelReadPeerDevName
 *
 * Description      Cancel reading a peripheral's device name.
 *
 * Returns          true if request accepted
 *
 ******************************************************************************/
bool GAP_BleCancelReadPeerDevName(const RawAddress& peer_bda) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(peer_bda);

  if (p_clcb == NULL) {
    log::error("Cannot cancel current op is not get dev name");
    return false;
  }

  if (!p_clcb->connected) {
    if (!stack::leConnectionCancelConnect(gatt_if, peer_bda, true)) {
      log::error("Cannot cancel where No connection id");
      return false;
    }
  }

  cl_op_cmpl(*p_clcb, false, 0, NULL);

  return true;
}

void gap_ble_config_cccd_enc_key_cmpl(bool status, const RawAddress& bda, uint16_t length,
                                      char* p_name) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(bda);

  if (p_clcb == NULL) {
    log::debug("p_clcb is NULL for {}", bda.ToRedactedStringForLogging());
    return;
  }

  p_clcb->enc_key_result = 0;

  log::debug("curr enc key char handle: {}", p_clcb->curr_enc_key_char_handle);

  std::vector<uint16_t>::iterator it =
          std::find(p_clcb->enc_key_char_handles.begin(), p_clcb->enc_key_char_handles.end(),
                    p_clcb->curr_enc_key_char_handle);

  // Check if next enc key char handle available, then discover and configure
  // CCCD for it
  if (it == p_clcb->enc_key_char_handles.end()) {
    return;
  }

  std::vector<uint16_t>::iterator it_next = std::next(it, 1);
  if ((it_next) != p_clcb->enc_key_char_handles.end()) {
    p_clcb->enc_key_stage = GAP_ENC_KEY_CCCD;
    p_clcb->curr_enc_key_char_handle = *(it_next);
    gap_cl_get_enc_key_info(p_clcb);
  } else {
    log::debug(" next enc key char handle is NOT available");
    p_clcb->is_enc_key_info_in_progress = false;
    p_clcb->enc_key_stage = GAP_ENC_KEY_CONNECTING;
  }
}

/*******************************************************************************
 *
 * Function         GAP_BleConfigCccdForKeyMaterial
 *
 * Description      Start configuration of CCC Descriptor of Encryption Key
 *                  Material characteristic for indications
 *
 * Returns          true if read started, else false if GAP is busy
 *
 ******************************************************************************/
bool GAP_BleConfigCccdForKeyMaterial(const RawAddress& peer_bda, uint16_t handle,
                                     tGAP_BLE_CMPL_CBACK* p_cback) {
  return accept_client_operation(peer_bda, GATT_UUID_CHAR_CLIENT_CONFIG, handle, GATTC_OPTYPE_WRITE,
                                 0, p_cback);
}

/*******************************************************************************
 *
 * Function         GAP_BleDiscEncKeyMaterialCCCD
 *
 * Description      Start discvoery of CCC Descriptor of Encryption Key
 *                  Material characteristic
 *
 * Returns          true if read started, else false if GAP is busy
 *
 ******************************************************************************/
bool GAP_BleDiscEncKeyMaterialCCCD(const RawAddress& peer_bda) {
  log::debug("peer_bda: {}", peer_bda.ToRedactedStringForLogging());
  return accept_client_operation(peer_bda, 0, 0, GATTC_OPTYPE_DISCOVERY, GATT_DISC_CHAR_DSCPT,
                                 NULL);
}

void btm_ble_read_enc_key_cmpl(bool status, const RawAddress& bda, uint16_t length, char* p_data) {
  tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(bda);
  if (p_clcb == NULL) {
    log::debug("p_clcb is NULL: ");
    return;
  }

  if (!status) {
    log::debug("status is false");
    /* Status false(error) implies that read of enc key chars are completed,
     * Go to next stage(desc) now.*/
    if (p_clcb->enc_key_char_handles.empty()) {
      log::debug("enc_key_char_handles empty ");
      p_clcb->is_enc_key_info_in_progress = false;
      return;
    }
    p_clcb->enc_key_stage++;

    // Pick first enc key char handle, and find CCCD desc for it to configure
    if (!p_clcb->enc_key_char_handles.empty()) {
      p_clcb->curr_enc_key_char_handle = p_clcb->enc_key_char_handles[0];
    }

    gap_cl_get_enc_key_info(p_clcb);
    return;
  }

  log::info("curr_enc_key_char_handle: 0x{:04x}, len: {}", p_clcb->curr_enc_key_char_handle,
            length);

  char reversekeyiv[ENC_KEY_MATERIAL_LEN];
  char* reversekeyiv_ptr = reversekeyiv;
  REVERSE_ARRAY_TO_STREAM(reversekeyiv_ptr, p_data, ENC_KEY_LEN);
  REVERSE_ARRAY_TO_STREAM(reversekeyiv_ptr, p_data + 16, ENC_IV_LEN);
  memcpy(p_data, reversekeyiv, ENC_KEY_MATERIAL_LEN);
  size_t len = btif_storage_get_enc_key_material_length(&bda);
  if (len <= 0) {
    log::debug("prev enc key char value NOT available ");
    btif_storage_set_enc_key_material(bda, (uint8_t*)p_data, length);
  } else {
    uint8_t concat_enc_key_material[length + len];
    if (btif_storage_get_enc_key_material(&bda, concat_enc_key_material, &len) ==
        BT_STATUS_SUCCESS) {
      if (length > 0) {
        memcpy(concat_enc_key_material + len, p_data, length);
        btif_storage_remove_enc_key_material(&bda);
        btif_storage_set_enc_key_material(bda, concat_enc_key_material, (length + len));
      }
    }
  }

  /* Don't increment p_clcb->enc_key_stage as we want to be in Read enc key
   * material char state to read multiple other enc key material char values and
   * concatenate them */
  gap_cl_get_enc_key_info(p_clcb);
}

/*******************************************************************************
 *
 * Function         GAP_BleReadEncKeyMaterial
 *
 * Description      Start a process to read a connected peripheral's Encryption
 *                  Key material characteristic
 *
 * Returns          true if read started, else false if GAP is busy
 *
 ******************************************************************************/
bool GAP_BleReadEncKeyMaterial(const RawAddress& peer_bda, uint16_t handle,
                               tGAP_BLE_CMPL_CBACK* p_cback) {
  return accept_client_operation(peer_bda, GATT_UUID_GAP_ENC_KEY_MATERIAL, handle,
                                 GATTC_OPTYPE_READ, 0, p_cback);
}

/*******************************************************************************
 *
 * Function         GAP_BleGetEncKeyMaterialInfo
 *
 * Description      Get Encryption Key Material information characteristic value
 *                  from remote device
 *
 * Returns          none
 *
 ******************************************************************************/
void GAP_BleGetEncKeyMaterialInfo(const RawAddress& remote_bda) {
  const BtmDevice* p_dev_rec = btm_find_dev(remote_bda);

  /*
   * If remote device is encrypted, then only get the encrypt key material
   * information
   */
  if (p_dev_rec && (p_dev_rec->sec_rec.sec_flags & BTM_SEC_LE_ENCRYPTED)) {
    log::debug("Encryption is done, read enc key values");
    tGAP_CLCB* p_clcb = find_clcb_by_bd_addr(remote_bda);
    if (p_clcb == NULL) {
      p_clcb = clcb_alloc(remote_bda);
      if (p_clcb == NULL) {
        return;
      }
    }

    if (p_clcb->is_enc_key_info_in_progress) {
      log::debug("Enc key info already in progress");
      return;
    }

    // TODO need to check
    // btif_storage_remove_enc_key_material(&remote_bda);

    p_clcb->is_enc_key_info_in_progress = true;
    p_clcb->enc_key_stage = GAP_ENC_KEY_CONNECTING;
    p_clcb->curr_enc_key_char_handle = 0;
    p_clcb->curr_cccd_handle = 0;
    p_clcb->enc_key_char_handles.clear();

    p_clcb->enc_key_stage++;
    gap_cl_get_enc_key_info(p_clcb);
  } else {
    tGATT_TCB* p_tcb = gatt_find_tcb_by_addr(remote_bda, BT_TRANSPORT_LE);
    if (p_tcb) {
      p_tcb->is_read_enc_key_pending = true;
      log::debug("Encryption is not done, don't read enc key values");
    }
  }
}
