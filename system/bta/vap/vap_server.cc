/*
 * Copyright 2026 The Android Open Source Project
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

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <bluetooth/log.h>
#include <com_android_bluetooth_flags.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "bluetooth/types/address.h"
#include "bluetooth/types/ble_address_with_type.h"
#include "bluetooth/types/bt_transport.h"
#include "bluetooth/types/uuid.h"
#include "bta/include/bta_csis_api.h"
#include "bta/include/bta_gatt_api.h"
#include "bta/include/bta_vap_server_api.h"
#include "bta/le_audio/device_groups.h"
#include "bta/vap/vap_server_types.h"
#include "gd/common/utils.h"
#include "gd/os/rand.h"
#include "btif/include/btif_profile_storage.h"
#include "hardware/bt_common_types.h"
#include "internal_include/stack_config.h"
#include "main/shim/entry.h"
#include "stack/include/bt_types.h"
#include "stack/include/btm_ble_addr.h"
#include "stack/include/btm_ble_api_types.h"
#include "stack/include/gatt_api.h"
#include "stack/include/main_thread.h"

using namespace bluetooth;
using bluetooth::csis::CsisClient;
using namespace ::vap;
using namespace ::vap::uuid;
using bluetooth::stack::tGATT_REQ_CBACK;

namespace {

class VapServerImpl;
VapServerImpl* instance;

static uint8_t kVapCcid = 0;
static uint8_t kVaSupportedFeatures = 0;
static uint8_t kPtsVapCcid = 0;
static uint8_t kPtsVaSupportedFeatures = 0;
static uint8_t kVaSessionFlag = 0;
static std::string kVaSupportedLanguages = "";

class VapServerImpl : public bluetooth::vap::VapServer {
public:
  struct VapCharacteristic {
    bluetooth::Uuid uuid_;
    uint16_t attribute_handle_;
    uint16_t attribute_handle_ccc_;
  };

  struct PendingWriteResponse {
    tCONN_ID conn_id_;
    uint32_t trans_id_;
    uint16_t write_req_handle_;
  };

  struct RemoteClient {
    tCONN_ID conn_id_;
    std::unordered_map<Uuid, uint16_t> ccc_values_;
    bool handling_control_point_command_ = false;
    PendingWriteResponse pending_write_response_;
    uint16_t mtu_ = kDefaultGattMtu;
    VaSessionState va_session_state_ = VaSessionState::VA_SESSION_UNAVAILABLE;
    std::string notified_va_name_ = "";
    std::string notified_va_uuid_ = "";
    uint8_t notified_ccid_ = 0xFF;
    uint8_t notified_session_state_ = 0xFF;
    uint8_t notified_supported_features_ = 0xFF;
    bool save_pending_ = false;
  };

  void Initialize(bluetooth::vap::VapServerCallbacks* callbacks) override {
    do_in_main_thread(
            base::BindOnce(&VapServerImpl::do_initialize, base::Unretained(this), callbacks));
  }

  static void OnGattConnStatic(tGATT_IF /*server_if*/, const RawAddress& remote_bda,
                               tCONN_ID conn_id, bool connected, tGATT_DISCONN_REASON /*reason*/,
                               tBT_TRANSPORT transport) {
    if (instance) {
      if (connected) {
        instance->OnGattConnect(remote_bda, conn_id, transport);
      } else {
        instance->OnGattDisconnect(remote_bda, conn_id);
      }
    }
  }

  static void OnGattReadCharacteristicStatic(tCONN_ID conn_id, uint32_t trans_id,
                                             const RawAddress& remote_bda, uint16_t handle,
                                             uint16_t offset, bool is_long) {
    if (instance) {
      instance->OnReadCharacteristic(conn_id, trans_id, remote_bda, handle, offset, is_long);
    }
  }

  static void OnGattWriteCharacteristicStatic(tCONN_ID conn_id, uint32_t trans_id,
                                              const RawAddress& remote_bda, uint16_t handle,
                                              uint16_t offset, bool need_rsp, bool is_prep,
                                              uint8_t* value, uint16_t len) {
    if (instance) {
      instance->OnWriteCharacteristic(conn_id, trans_id, remote_bda, handle, offset, need_rsp,
                                      is_prep, value, len);
    }
  }

  static void OnGattReadDescriptorStatic(tCONN_ID conn_id, uint32_t trans_id,
                                         const RawAddress& remote_bda, uint16_t handle,
                                         uint16_t offset, bool is_long) {
    if (instance) {
      instance->OnReadDescriptor(conn_id, trans_id, remote_bda, handle, offset, is_long);
    }
  }

  static void OnGattWriteDescriptorStatic(tCONN_ID conn_id, uint32_t trans_id,
                                          const RawAddress& remote_bda, uint16_t handle,
                                          uint16_t offset, bool need_rsp, bool is_prep,
                                          uint8_t* value, uint16_t len) {
    if (instance) {
      instance->OnWriteDescriptor(conn_id, trans_id, remote_bda, handle, offset, need_rsp, is_prep,
                                  value, len);
    }
  }

  static void OnGattMtuChangedStatic(tCONN_ID conn_id, const RawAddress& remote_bda, uint16_t mtu) {
    if (instance) {
      instance->OnGattMtuChanged(conn_id, remote_bda, mtu);
    }
  }

  void do_initialize(bluetooth::vap::VapServerCallbacks* callbacks) {
    log::info("initialize vap server");
    callbacks_ = callbacks;

    Uuid uuid = Uuid::From128BitBE(bluetooth::os::GenerateRandom<Uuid::kNumBytes128>());
    app_uuid_ = uuid;
    log::info("Register server with uuid:{}", app_uuid_.ToString());

    static bluetooth::stack::tGATT_REQ_CBACK vap_req_cb = {
            .read_characteristic_cb = OnGattReadCharacteristicStatic,
            .read_descriptor_cb = OnGattReadDescriptorStatic,
            .write_characteristic_cb = OnGattWriteCharacteristicStatic,
            .write_descriptor_cb = OnGattWriteDescriptorStatic,
            .exec_write_cb = tGATT_REQ_CBACK::do_nothing,
            .mtu_changed_cb = OnGattMtuChangedStatic,
            .conf_cb = tGATT_REQ_CBACK::do_nothing,
    };
    static const stack::tGATT_CBACK vap_ops = {
            .p_conn_cb = OnGattConnStatic,
            .p_req_cb = &vap_req_cb,
    };

    server_if_ = BTA_GATTS_AppRegister(app_uuid_, &vap_ops, true);
    log::info("server_if: {}", server_if_);

    if (server_if_ == stack::GATT_IF_INVALID) {
      log::warn("Register Server fail");
      return;
    }

    std::vector<btgatt_db_element_t> service = {
            // Generic Voice Assistant Service
            btgatt_db_element_t{.uuid = kGenericVasService, .type = BTGATT_DB_PRIMARY_SERVICE},

            // VA Name characteristic
            btgatt_db_element_t{.uuid = kVaNameCharacteristic,
                                .type = BTGATT_DB_CHARACTERISTIC,
                                .properties = GATT_CHAR_PROP_BIT_READ | GATT_CHAR_PROP_BIT_NOTIFY,
                                .permissions = GATT_PERM_READ_ENCRYPTED},

            // CCC descriptor for VA Name characteristic
            btgatt_db_element_t{.uuid = kClientCharacteristicConfiguration,
                                .type = BTGATT_DB_DESCRIPTOR,
                                .permissions = GATT_PERM_WRITE | GATT_PERM_READ},

            // VA UUID characteristic
            btgatt_db_element_t{.uuid = kVaUuidCharacteristic,
                                .type = BTGATT_DB_CHARACTERISTIC,
                                .properties = GATT_CHAR_PROP_BIT_READ | GATT_CHAR_PROP_BIT_NOTIFY,
                                .permissions = GATT_PERM_READ_ENCRYPTED},
            // CCC descriptor for VA UUID characteristic
            btgatt_db_element_t{.uuid = kClientCharacteristicConfiguration,
                                .type = BTGATT_DB_DESCRIPTOR,
                                .permissions = GATT_PERM_WRITE | GATT_PERM_READ},

            // VAS Control Point (VAS-CP) characteristic
            btgatt_db_element_t{
                    .uuid = kVasControlPointCharacteristic,
                    .type = BTGATT_DB_CHARACTERISTIC,
                    .properties = GATT_CHAR_PROP_BIT_WRITE_NR | GATT_CHAR_PROP_BIT_NOTIFY,
                    .permissions = GATT_PERM_WRITE_ENCRYPTED},
            // CCC descriptor for VAS Control Point
            btgatt_db_element_t{.uuid = kClientCharacteristicConfiguration,
                                .type = BTGATT_DB_DESCRIPTOR,
                                .permissions = GATT_PERM_WRITE | GATT_PERM_READ},

            // VA CCID characteristic
            btgatt_db_element_t{.uuid = kVaCcidCharacteristic,
                                .type = BTGATT_DB_CHARACTERISTIC,
                                .properties = GATT_CHAR_PROP_BIT_READ | GATT_CHAR_PROP_BIT_NOTIFY,
                                .permissions = GATT_PERM_READ_ENCRYPTED},
            // CCC descriptor for VA CCID characteristic
            btgatt_db_element_t{.uuid = kClientCharacteristicConfiguration,
                                .type = BTGATT_DB_DESCRIPTOR,
                                .permissions = GATT_PERM_WRITE | GATT_PERM_READ},

            // VA Session State characteristic
            btgatt_db_element_t{.uuid = kVaSessionStateCharacteristic,
                                .type = BTGATT_DB_CHARACTERISTIC,
                                .properties = (GATT_CHAR_PROP_BIT_READ | GATT_CHAR_PROP_BIT_NOTIFY),
                                .permissions = GATT_PERM_READ_ENCRYPTED},
            // CCC descriptor for VA Session State characteristic
            btgatt_db_element_t{.uuid = kClientCharacteristicConfiguration,
                                .type = BTGATT_DB_DESCRIPTOR,
                                .permissions = GATT_PERM_WRITE | GATT_PERM_READ},

            // VA Supported Features characteristic
            btgatt_db_element_t{.uuid = kVaSupportedFeaturesCharacteristic,
                                .type = BTGATT_DB_CHARACTERISTIC,
                                .properties = (GATT_CHAR_PROP_BIT_READ | GATT_CHAR_PROP_BIT_NOTIFY),
                                .permissions = GATT_PERM_READ_ENCRYPTED},
            // CCC descriptor for VA Supported Features characteristic
            btgatt_db_element_t{.uuid = kClientCharacteristicConfiguration,
                                .type = BTGATT_DB_DESCRIPTOR,
                                .permissions = GATT_PERM_WRITE | GATT_PERM_READ}};

    auto status = BTA_GATTS_AddService(server_if_, &service);
    log::info("status: {}, server_if: {}", gatt_status_text(status), server_if_);
    VapCharacteristic* current_characteristic;
    for (uint16_t i = 0; i < service.size(); i++) {
      uint16_t attribute_handle = service[i].attribute_handle;
      Uuid uuid = service[i].uuid;
      if (service[i].type == BTGATT_DB_CHARACTERISTIC) {
        log::info("Characteristic uuid: 0x{:04x}, handle:0x{:04x}, {}", uuid.As16Bit(),
                  attribute_handle, getUuidName(uuid));
        characteristics_[attribute_handle].attribute_handle_ = attribute_handle;
        characteristics_[attribute_handle].uuid_ = uuid;
        current_characteristic = &characteristics_[attribute_handle];
      } else if (service[i].type == BTGATT_DB_DESCRIPTOR) {
        log::info("\tDescriptor uuid: 0x{:04x}, handle: 0x{:04x}, {}", uuid.As16Bit(),
                  attribute_handle, getUuidName(uuid));
        if (service[i].uuid == kClientCharacteristicConfiguration) {
          current_characteristic->attribute_handle_ccc_ = attribute_handle;
        }
      }
    }
    callbacks_->OnInitialized();
  }

  void Cleanup() override {
    do_in_main_thread(base::BindOnce(&VapServerImpl::do_cleanup, base::Unretained(this)));
  }

  std::vector<uint8_t> SerializeVapData(const RemoteClient& client) {
    std::vector<uint8_t> data(16 + client.notified_va_name_.length());
    uint8_t* p = data.data();

    UINT16_TO_STREAM(p, client.ccc_values_.count(kVaNameCharacteristic)
                        ? client.ccc_values_.at(kVaNameCharacteristic) : 0);
    UINT16_TO_STREAM(p, client.ccc_values_.count(kVaUuidCharacteristic)
                        ? client.ccc_values_.at(kVaUuidCharacteristic) : 0);
    UINT16_TO_STREAM(p, client.ccc_values_.count(kVasControlPointCharacteristic)
                        ? client.ccc_values_.at(kVasControlPointCharacteristic) : 0);
    UINT16_TO_STREAM(p, client.ccc_values_.count(kVaCcidCharacteristic)
                        ? client.ccc_values_.at(kVaCcidCharacteristic) : 0);
    UINT16_TO_STREAM(p, client.ccc_values_.count(kVaSessionStateCharacteristic)
                        ? client.ccc_values_.at(kVaSessionStateCharacteristic) : 0);
    UINT16_TO_STREAM(p, client.ccc_values_.count(kVaSupportedFeaturesCharacteristic)
                        ? client.ccc_values_.at(kVaSupportedFeaturesCharacteristic) : 0);

    UINT8_TO_STREAM(p, client.notified_ccid_);
    UINT8_TO_STREAM(p, client.notified_session_state_);
    UINT8_TO_STREAM(p, client.notified_supported_features_);
    UINT8_TO_STREAM(p, client.notified_va_name_.length());

    ARRAY_TO_STREAM(p, client.notified_va_name_.c_str(), client.notified_va_name_.length());
    return data;
  }

  void DeserializeVapData(RemoteClient& client, const std::vector<uint8_t>& data) {
    if (data.size() < 16) return;
    const uint8_t* p = data.data();

    STREAM_TO_UINT16(client.ccc_values_[kVaNameCharacteristic], p);
    STREAM_TO_UINT16(client.ccc_values_[kVaUuidCharacteristic], p);
    STREAM_TO_UINT16(client.ccc_values_[kVasControlPointCharacteristic], p);
    STREAM_TO_UINT16(client.ccc_values_[kVaCcidCharacteristic], p);
    STREAM_TO_UINT16(client.ccc_values_[kVaSessionStateCharacteristic], p);
    STREAM_TO_UINT16(client.ccc_values_[kVaSupportedFeaturesCharacteristic], p);

    STREAM_TO_UINT8(client.notified_ccid_, p);
    STREAM_TO_UINT8(client.notified_session_state_, p);
    STREAM_TO_UINT8(client.notified_supported_features_, p);

    uint8_t name_len;
    STREAM_TO_UINT8(name_len, p);

    size_t offset = p - data.data();
    if (data.size() >= offset + name_len) {
      client.notified_va_name_ = std::string((const char*)p, name_len);
      client.notified_va_uuid_ = client.notified_va_name_;
    }
  }

  void SaveVapData(const RawAddress& bda) {
    if (remote_clients_.find(bda) != remote_clients_.end()) {
      RemoteClient* client = &remote_clients_[bda];
      if (!client->save_pending_) {
        client->save_pending_ = true;
        do_in_main_thread_delayed(
            base::BindOnce(&VapServerImpl::ExecuteSaveVapData, base::Unretained(this), bda),
            std::chrono::milliseconds(kSaveVapDataDelayMs));
      }
    }
  }

  void ExecuteSaveVapData(const RawAddress bda) {
    if (remote_clients_.find(bda) != remote_clients_.end()) {
      RemoteClient* client = &remote_clients_[bda];
      client->save_pending_ = false;
      std::vector<uint8_t> data = SerializeVapData(*client);
      btif_storage_set_vap_server_data(bda, data);
    }
  }

  void do_cleanup() {
    if (!instance) {
      log::error("Not initialized");
      return;
    }

    if (server_if_) {
      BTA_GATTS_AppDeregister(server_if_);
    }
    characteristics_.clear();
    remote_clients_.clear();
    callbacks_ = nullptr;
    va_name_.clear();
    active_va_group_id_ = bluetooth::groups::kGroupUnknown;
    active_va_device_ = RawAddress::kEmpty;
    server_if_ = 0;

    instance = nullptr;
    log::info("cleanup done");
  }

  void NotifyVaSupportedFeaturesForPts(RawAddress bda, uint8_t features) {
     log::info("bda: {}, features: {}", bda, features);

     auto it = remote_clients_.find(bda);
     if (it != remote_clients_.end()) {
       RemoteClient* remote_client = &it->second;
       uint16_t ccc_va_supported_features =
           remote_client->ccc_values_[kVaSupportedFeaturesCharacteristic];

       SendVaSupportedFeaturesNotification(remote_client, ccc_va_supported_features, features);
     }
   }

   void NotifyVaCcidForPts(RawAddress bda, uint8_t ccid) {
     log::info("bda: {}, ccid: {}", bda, ccid);

     auto it = remote_clients_.find(bda);
     if (it != remote_clients_.end()) {
       RemoteClient* remote_client = &it->second;
       uint16_t ccc_va_ccid = remote_client->ccc_values_[kVaCcidCharacteristic];

       SendVaCcidNotification(remote_client, ccc_va_ccid, ccid);
     }
   }

   void set_ccid(int ccid) {
     log::info("ccid:{}", ccid);
     kVapCcid = ccid;
   }

   void set_va_name(std::string va_name) {
     log::info("va_name:{}", va_name);
     uint8_t va_session_state = (va_name == "None") ?
         static_cast<uint8_t>(VaSessionState::VA_SESSION_UNAVAILABLE):
         static_cast<uint8_t>(VaSessionState::VA_SESSION_RESET);
     va_name_ = va_name;

     for (auto& [bda, remote_client] : remote_clients_) {
       uint16_t ccc_va_session_state = remote_client.ccc_values_[kVaSessionStateCharacteristic];
       log::info("device:{}", bda);

       uint16_t ccc_va_name = remote_client.ccc_values_[kVaNameCharacteristic];
       uint16_t ccc_va_uuid = remote_client.ccc_values_[kVaUuidCharacteristic];
       bool changed = false;
       // Send VA Name notification
       changed |= SendVaNameNotification(&remote_client, ccc_va_name, va_name);

       // Send VA UUID notification
       // Using VA name bytes for VA UUID as we don't have an API from VA apps
       changed |= SendVaUuidNotification(&remote_client, ccc_va_uuid, va_name);

       // Send VA Session State notification
       changed |= SendVaSessionStateNotification(bda, &remote_client, ccc_va_session_state,
                                      va_session_state);

       if (changed) {
         SaveVapData(bda);
       }
     }
   }

   void SetCcid(int ccid) {
    do_in_main_thread(base::BindOnce(&VapServerImpl::set_ccid, base::Unretained(this), ccid));
  }

   void SetVaName(std::string va_name) {
     do_in_main_thread(
         base::BindOnce(&VapServerImpl::set_va_name, base::Unretained(this), va_name));
   }

   void RejectVaSession(const RawAddress& bd_addr) override {
     do_in_main_thread(
         base::BindOnce(&VapServerImpl::NotifyVaSessionStarted, base::Unretained(this),
                        std::vector<RawAddress>{bd_addr}, false));
   }

   void NotifyVaSessionInitialized(RawAddress bda) {
     bool is_success = true;
     log::info("NotifyVaSessionInitialized:, bda", bda);

     auto it = remote_clients_.find(bda);
     if (it != remote_clients_.end()) {
       RemoteClient* remote_client = &it->second;
       uint16_t ccc_vas_control_point = remote_client->ccc_values_[kVasControlPointCharacteristic];
       ResponseCodeValue rsp_code_value =
           is_success ? ResponseCodeValue::SUCCESS : ResponseCodeValue::OPERATION_FALIED;
       // Send VAS Control Point notification
       SendVasControlPointNotification(remote_client, rsp_code_value, ccc_vas_control_point);

       int group_id = GetGroupId(bda);

       log::info("group_id:{}", group_id);
       if (group_id != bluetooth::groups::kGroupUnknown) {
         auto csis_api = CsisClient::Get();
         if (csis_api != nullptr) {
           std::vector<RawAddress> devices = csis_api->GetDeviceList(group_id);

           for (const auto& device : devices) {
             log::info("NotifyVaSessionInitialized:, device:{}", device);
             if (remote_clients_.find(device) != remote_clients_.end()) {
               RemoteClient* client = &remote_clients_[device];
               uint16_t ccc_va_session_state = client->ccc_values_[kVaSessionStateCharacteristic];

               uint8_t va_session_state =
                   static_cast<uint8_t>(VaSessionState::VA_SESSION_READY);
               // Send VA Session State notification
               if (SendVaSessionStateNotification(device, client, ccc_va_session_state,
                                                  va_session_state, /*is_group_device*/ true)) {
                 SaveVapData(device);
               }
             }
           }
         }
       } else {
         uint8_t va_session_state = static_cast<uint8_t>(VaSessionState::VA_SESSION_READY);
         if (remote_clients_.find(bda) != remote_clients_.end()) {
           RemoteClient* client = &remote_clients_[bda];
           uint16_t ccc_va_session_state = client->ccc_values_[kVaSessionStateCharacteristic];
           // Send VA Session State notification
           if (SendVaSessionStateNotification(bda, client, ccc_va_session_state, va_session_state,
                                              /*is_group_device*/ false)) {
             SaveVapData(bda);
           }
         }
       }
     }
   }

   void NotifyVaSessionStartedImpl(const std::vector<RawAddress>& devices, bool is_success) {
     log::info("NotifyVaSessionStartedImpl, is_success:{}", is_success);
     if (is_success && !devices.empty()) {
       for (const auto& device : devices) {
         if (remote_clients_.find(device) != remote_clients_.end()) {
           active_va_device_ = device;
           active_va_group_id_ = GetGroupId(active_va_device_);
           break;
         }
       }
     }

     NotifySessionStateChange(devices, true, is_success);
   }

   void NotifyVaSessionStarted(std::vector<RawAddress> devices, bool is_success) {
     log::info("NotifyVaSessionStarted:, is_success:{}", is_success);

     if (devices.empty()) {
       log::error(" No devices to notify");
     }

     NotifyVaSessionStartedImpl(devices, is_success);
   }

   void NotifyVaSessionStoppedImpl(const std::vector<RawAddress>& devices, bool is_success) {
     log::info("NotifyVaSessionStoppedImpl, is_success:{}", is_success);
     bool is_active = false;
     for (const auto& device : devices) {
       if (IsActiveDeviceOrGroupMember(device)) {
         is_active = true;
         break;
       }
     }

     if (!is_active) {
       log::warn(" VA session is not active for this device or group");
       return;
     }

     if (is_success) {
       active_va_device_ = RawAddress::kEmpty;
       active_va_group_id_ = bluetooth::groups::kGroupUnknown;
     }

     NotifySessionStateChange(devices, false, is_success);
   }

   void NotifyVaSessionStopped(std::vector<RawAddress> devices, bool is_success) {
     log::info("NotifyVaSessionStopped:, is_success:{}", is_success);
     if (devices.empty()) {
       log::error(" No devices to notify");
     }

     NotifyVaSessionStoppedImpl(devices, is_success);
   }

   void NotifySessionStateChange(const std::vector<RawAddress>& devices, bool is_started,
                                 bool is_success) {
     log::debug(" Number of devices: {}", devices.size());

     for (const auto& device : devices) {
       auto it = remote_clients_.find(device);
       if (it != remote_clients_.end()) {
         RemoteClient* remote_client = &it->second;
         uint16_t ccc_vas_control_point =
                 remote_client->ccc_values_[kVasControlPointCharacteristic];
         uint16_t ccc_va_session_state =
             remote_client->ccc_values_[kVaSessionStateCharacteristic];
         ResponseCodeValue rsp_code_value =
             is_success ? ResponseCodeValue::SUCCESS : ResponseCodeValue::OPERATION_FALIED;
         if (remote_client->handling_control_point_command_) {
           // Send VAS Control Point notification
           SendVasControlPointNotification(remote_client, rsp_code_value, ccc_vas_control_point);
         }

         uint8_t session_state = ComputeSessionState(is_started, is_success);
         bool changed = false;
         // Send VA Session State notification
         changed = SendVaSessionStateNotification(device, remote_client, ccc_va_session_state,
                                                  session_state, /*is_group_device*/ true);
         if (changed) {
           SaveVapData(device);
         }
       }
     }
   }

   void SendVasControlPointNotification(RemoteClient* remote_client,
                                        ResponseCodeValue rsp_code_value,
                                        uint16_t ccc_vas_control_point) {
     log::info(" conn_id:{}, ccc_vas_cp:{}, rsp_code_value:{}, rsp_code_str:{}",
               remote_client->conn_id_, ccc_vas_control_point, (uint16_t)rsp_code_value,
               GetResponseCodeValueText(rsp_code_value));

     // Send VAS Control Point notification
     if (ccc_vas_control_point != GATT_CLT_CONFIG_NONE) {
       bool use_notification = ccc_vas_control_point & GATT_CLT_CONFIG_NOTIFICATION;
       uint16_t attr_id =
              GetCharacteristic(kVasControlPointCharacteristic)->attribute_handle_;
       std::vector<uint8_t> response(2, 0);
       response[0] = (uint8_t)CtpRespOpcode::RESPONSE_CODE;
       response[1] = (uint8_t)rsp_code_value;
       log::debug("Send VAS Control Point notification");
       BTA_GATTS_HandleValueIndication(remote_client->conn_id_, attr_id,
                                       response, !use_notification);
       remote_client->handling_control_point_command_ = false;
     }
   }

   uint8_t ComputeSessionState(bool is_started, bool is_success) {
     uint8_t session_state = 0xFF;
     log::info(" is_started:{}, is_success:{}", is_started, is_success);
     if (is_success) {
       if (is_started) {
         session_state = static_cast<uint8_t>(VaSessionState::VA_SESSION_ACTIVE);
       } else {
         session_state = static_cast<uint8_t>(VaSessionState::VA_SESSION_READY);
       }
     } else {
       session_state = static_cast<uint8_t>(VaSessionState::VA_SESSION_READY);
     }
     return session_state;
   }

   bool SendVaSessionStateNotification(const RawAddress& bda, RemoteClient* remote_client,
                                       uint16_t ccc_va_session_state,
                                       uint8_t va_session_state,
                                       bool is_group_device = false) {
     uint8_t curr_va_session_state = static_cast<uint8_t>(GetVaSessionState(bda));
     log::info(" conn_id:{}, ccc_va_session_state:{}, Curr VA session state: {},"
               " New VA session state:{}, is_group_device: {}", remote_client->conn_id_,
               ccc_va_session_state,
               GetVaSessionStateText(static_cast<VaSessionState>(curr_va_session_state)),
               GetVaSessionStateText(static_cast<VaSessionState>(va_session_state)),
               is_group_device);

     if (remote_client->notified_session_state_ == va_session_state) {
       log::info(" Not sending VA Session state notification - no change in VA session state");
       return false;
     }

     SetVaSessionState(bda, static_cast<VaSessionState>(va_session_state));
     if (ccc_va_session_state != GATT_CLT_CONFIG_NONE) {
       bool use_notification = ccc_va_session_state & GATT_CLT_CONFIG_NOTIFICATION;
       uint16_t attr_id =
               GetCharacteristic(kVaSessionStateCharacteristic)->attribute_handle_;
       std::vector<uint8_t> value(kVaSessionStateSize, 0);
       value[0] = va_session_state;
       log::debug("Send VA Session State notification");
       BTA_GATTS_HandleValueIndication(remote_client->conn_id_, attr_id, value, !use_notification);
     }
     remote_client->notified_session_state_ = va_session_state;
     return true;
   }

   bool SendVaNameNotification(RemoteClient* remote_client,
                               uint16_t ccc_va_name,
                               std::string va_name) {
     log::info(" conn_id:{}, ccc_va_name:{}, VA name: {},",
               remote_client->conn_id_, ccc_va_name,
               va_name);
     if (remote_client->notified_va_name_ == va_name) {
       log::info(" Not sending VA Name notification - no change in VA name");
       return false;
     }

     if (ccc_va_name != GATT_CLT_CONFIG_NONE) {
       bool use_notification = ccc_va_name & GATT_CLT_CONFIG_NOTIFICATION;
       uint16_t attr_id =
               GetCharacteristic(kVaNameCharacteristic)->attribute_handle_;
       std::vector<uint8_t> value(va_name.begin(), va_name.end());

       log::debug("Send VA Name notification");
       BTA_GATTS_HandleValueIndication(remote_client->conn_id_, attr_id, value, !use_notification);
     }
     remote_client->notified_va_name_ = va_name;
     return true;
   }

   bool SendVaUuidNotification(RemoteClient* remote_client, uint16_t ccc_va_uuid,
                              std::string va_uuid) {
     log::info(" conn_id:{}, ccc_va_uuid:{}, VA UUID: {},",
               remote_client->conn_id_, ccc_va_uuid, va_uuid);
     if (remote_client->notified_va_uuid_ == va_uuid) {
       log::info(" Not sending VA UUID notification - no change in VA UUID");
       return false;
     }

     if (ccc_va_uuid != GATT_CLT_CONFIG_NONE) {
       bool use_notification = ccc_va_uuid & GATT_CLT_CONFIG_NOTIFICATION;
       uint16_t attr_id =
               GetCharacteristic(kVaUuidCharacteristic)->attribute_handle_;
       std::string va_uuid_str = va_uuid.substr(0, 16);
       std::vector<uint8_t> value(va_uuid_str.begin(), va_uuid_str.end());

       log::debug("Send VA UUID notification");
       BTA_GATTS_HandleValueIndication(remote_client->conn_id_, attr_id, value, !use_notification);
     }
     remote_client->notified_va_uuid_ = va_uuid;
     return true;
   }

   void OnGattConnect(const RawAddress& remote_bda, tCONN_ID conn_id, tBT_TRANSPORT transport) {
     log::info("Address: {}, conn_id:{}", remote_bda, conn_id);
     if (transport == BT_TRANSPORT_BR_EDR) {
       log::warn("Skip BE/EDR connection");
       return;
     }

     if (remote_clients_.find(remote_bda) == remote_clients_.end()) {
       log::warn("Create new remote_client");
     }
     remote_clients_[remote_bda].conn_id_ = conn_id;

     // Default initial state is RESET, or UNAVAILABLE if VA name is not set
     VaSessionState initial_state = VaSessionState::VA_SESSION_RESET;
     if (va_name_.empty() || va_name_ == "None") {
         initial_state = VaSessionState::VA_SESSION_UNAVAILABLE;
     }

     /* If the connecting device belongs to a group (e.g., left/right earbuds),
      * and another device from the same group is already connected, inherit its
      * VA session state so that the group remains in sync.
      */
     int group_id = GetGroupId(remote_bda);
     if (group_id != bluetooth::groups::kGroupUnknown &&
         initial_state != VaSessionState::VA_SESSION_UNAVAILABLE) {
       for (auto& [other_bda, client] : remote_clients_) {
         if (other_bda != remote_bda && GetGroupId(other_bda) == group_id) {
           initial_state = client.va_session_state_;
           break;
         }
       }
     }
     remote_clients_[remote_bda].va_session_state_ = initial_state;

     std::vector<uint8_t> data;
     if (btif_storage_get_vap_server_data(remote_bda, data)) {
       DeserializeVapData(remote_clients_[remote_bda], data);
     }

     RemoteClient* client = &remote_clients_[remote_bda];

     bool changed = false;
     if (client->notified_va_name_ != va_name_) {
       changed |= SendVaNameNotification(client, client->ccc_values_[kVaNameCharacteristic],
                                         va_name_);
       changed |= SendVaUuidNotification(client, client->ccc_values_[kVaUuidCharacteristic],
                                         va_name_);
     }

     if (client->notified_ccid_ != kVapCcid) {
       changed |= SendVaCcidNotification(client, client->ccc_values_[kVaCcidCharacteristic],
                                         kVapCcid);
     }

     if (client->notified_supported_features_ != kVaSupportedFeatures) {
       changed |= SendVaSupportedFeaturesNotification(
           client, client->ccc_values_[kVaSupportedFeaturesCharacteristic], kVaSupportedFeatures);
     }

     uint8_t curr_va_session_state = static_cast<uint8_t>(GetVaSessionState(remote_bda));
     if (client->notified_session_state_ != curr_va_session_state) {
       changed |= SendVaSessionStateNotification(
           remote_bda, client, client->ccc_values_[kVaSessionStateCharacteristic],
           curr_va_session_state);
     }

     if (changed) {
       SaveVapData(remote_bda);
     }
   }

   bool SendVaCcidNotification(RemoteClient* remote_client, uint16_t ccc_va_ccid,
                               uint8_t ccid) {
     log::info(" conn_id:{}, ccc_va_ccid:{}, CCID: {},",
               remote_client->conn_id_, ccc_va_ccid, ccid);
     if (remote_client->notified_ccid_ == ccid) {
       log::info(" Not sending VA CCID notification - no change in VA CCID");
       return false;
     }

     if (ccc_va_ccid != GATT_CLT_CONFIG_NONE) {
       bool use_notification = ccc_va_ccid & GATT_CLT_CONFIG_NOTIFICATION;
       uint16_t attr_id =
               GetCharacteristic(kVaCcidCharacteristic)->attribute_handle_;
       std::vector<uint8_t> value = {ccid};

       log::debug("Send VA CCID notification");
       BTA_GATTS_HandleValueIndication(remote_client->conn_id_, attr_id, value, !use_notification);
     }
     remote_client->notified_ccid_ = ccid;
     return true;
   }

   bool SendVaSupportedFeaturesNotification(RemoteClient* remote_client,
                                            uint16_t ccc_va_supported_features,
                                            uint8_t supported_features) {
     log::info(" conn_id:{}, ccc_va_supported_features:{}, Supported Features: {},",
               remote_client->conn_id_, ccc_va_supported_features, supported_features);
     if (remote_client->notified_supported_features_ == supported_features) {
       log::info(" Not sending VA Supported Features notification "
                 "- no change in VA Supported Features");
       return false;
     }

     if (ccc_va_supported_features != GATT_CLT_CONFIG_NONE) {
       bool use_notification = ccc_va_supported_features & GATT_CLT_CONFIG_NOTIFICATION;
       uint16_t attr_id =
               GetCharacteristic(kVaSupportedFeaturesCharacteristic)->attribute_handle_;
       std::vector<uint8_t> value = {supported_features};

       log::debug("Send VA Supported Features notification");
       BTA_GATTS_HandleValueIndication(remote_client->conn_id_, attr_id, value, !use_notification);
     }
     remote_client->notified_supported_features_ = supported_features;
     return true;
   }

   void OnGattMtuChanged(tCONN_ID /*conn_id*/, const RawAddress& remote_bda, uint16_t mtu) {
     log::info("mtu is changed as {}", mtu);
     auto it = remote_clients_.find(remote_bda);
     if (it != remote_clients_.end()) {
       it->second.mtu_ = mtu;
     }
   }

   void OnGattDisconnect(const RawAddress& remote_bda, tCONN_ID conn_id) {
     log::info("Address: {}, conn_id:{}", remote_bda, conn_id);

     /* If the device that just disconnected was the one
      actively running the Voice Assistant session */
     if (active_va_device_ == remote_bda) {
       auto old_group_id = active_va_group_id_;
       RawAddress next_active_device = RawAddress::kEmpty;

       /* Search for another connected device belonging to the same group
          (i.e., the other earbud). */
       if (old_group_id != bluetooth::groups::kGroupUnknown) {
         for (auto& [bda, client] : remote_clients_) {
           if (bda != remote_bda && GetGroupId(bda) == old_group_id) {
             next_active_device = bda;
             break;
           }
         }
       }

       if (!next_active_device.IsEmpty()) {
         /* Seamless Handover: Another earbud in the group is still connected.
          * Transfer the active session state to it so the VA session continues uninterrupted.
          */
         log::info("Active VA device disconnected, but group member {} remains. "
                   "Handing over VA session.", next_active_device);
         active_va_device_ = next_active_device;
       } else {
         /* Complete Disconnect: No other earbuds in the group are connected.
          * Safely terminate the Voice Assistant session and clear the server's tracking state.
          */
         log::info("Active VA device disconnected and no group members left. "
                   "Resetting VA session");
         callbacks_->OnStopVaSession(remote_bda);
         active_va_device_ = RawAddress::kEmpty;
         active_va_group_id_ = bluetooth::groups::kGroupUnknown;
       }
     }
     remote_clients_.erase(remote_bda);
   }

   void OnReadCharacteristic(tCONN_ID conn_id, uint32_t trans_id, const RawAddress& remote_bda,
                             uint16_t handle, uint16_t offset, bool /*is_long*/) {
     log::info("read_req_handle: 0x{:04x}, offset: 0x{:04x}", handle, offset);

     std::unique_ptr<tGATTS_RSP> p_msg = std::make_unique<tGATTS_RSP>();
     p_msg->attr_value.handle = handle;
     if (characteristics_.find(handle) == characteristics_.end()) {
       log::error("Invalid handle 0x{:04x}", handle);
       BTA_GATTS_SendRsp(conn_id, trans_id, GATT_INVALID_HANDLE, std::move(p_msg));
       return;
     }

     auto uuid = characteristics_[handle].uuid_;
     log::info("Read uuid, {}", getUuidName(uuid));
     if (remote_clients_.find(remote_bda) == remote_clients_.end()) {
       log::warn("Can't find remote_client for {}", remote_bda);
       BTA_GATTS_SendRsp(conn_id, trans_id, GATT_ILLEGAL_PARAMETER, std::move(p_msg));
       return;
     }
     RemoteClient* remote_client = &remote_clients_[remote_bda];

     // Check Characteristic UUIDs of GVAS service
     switch (uuid.As16Bit()) {
       case kVaNameCharacteristic16bit: {
         std::string service_name = va_name_;
         std::vector<uint8_t> svc_name(service_name.begin(), service_name.end());
         log::info("svc_name: {}", svc_name.size());

         // Copy from the offset
         size_t copy_len = 0;
         if (offset < svc_name.size()) {
           copy_len = std::min((size_t)(svc_name.size() - offset), (size_t)remote_client->mtu_);
           memcpy(p_msg->attr_value.value, svc_name.data() + offset, copy_len);
         }
         p_msg->attr_value.len = copy_len;
       } break;
       case kVaUuidCharacteristic16bit: {
         // Use VA name as VA UUID
         std::string va_uuid_str = va_name_.substr(0, kVaUuidSize);
         std::vector<uint8_t> va_uuid(va_uuid_str.begin(), va_uuid_str.end());

         p_msg->attr_value.len = kVaUuidSize;
         memcpy(p_msg->attr_value.value, va_uuid.data(), kVaUuidSize);
       } break;
       case kVaCcidCharacteristic16bit: {
         p_msg->attr_value.len = 1;
         memcpy(p_msg->attr_value.value, &kVapCcid, sizeof(uint8_t));
       } break;
       case kVaSessionStateCharacteristic16bit: {
         p_msg->attr_value.len = 1;
         uint8_t state = static_cast<uint8_t>(GetVaSessionState(remote_bda));
         memcpy(p_msg->attr_value.value, &state, sizeof(uint8_t));
       } break;
       case kVaSessionFlagCharacteristic16bit: {
         p_msg->attr_value.len = 1;
         memcpy(p_msg->attr_value.value, &kVaSessionFlag, sizeof(uint8_t));
       } break;
       case kVaSupportedLanguagesCharacteristic16bit: {
        std::vector<uint8_t> languages(kVaSupportedLanguages.begin(), kVaSupportedLanguages.end());
        size_t copy_len = 0;
        if (offset < languages.size()) {
          copy_len = std::min((size_t)(languages.size() - offset), (size_t)remote_client->mtu_);
          memcpy(p_msg->attr_value.value, languages.data() + offset, copy_len);
        }
        p_msg->attr_value.len = copy_len;
       } break;
       case kVaSupportedFeaturesCharacteristic16bit: {
         p_msg->attr_value.len = 1;
         memcpy(p_msg->attr_value.value, &kVaSupportedFeatures, sizeof(uint8_t));
       } break;
       default:
         log::warn("Unhandled uuid {}", uuid.ToString());
         BTA_GATTS_SendRsp(conn_id, trans_id, GATT_ILLEGAL_PARAMETER, std::move(p_msg));
         return;
     }
     BTA_GATTS_SendRsp(conn_id, trans_id, GATT_SUCCESS, std::move(p_msg));
   }

   void OnReadDescriptor(tCONN_ID conn_id, uint32_t trans_id, const RawAddress& remote_bda,
                         uint16_t handle, uint16_t /*offset*/, bool /*is_long*/) {
     log::info("conn_id:{}, read_req_handle:0x{:04x}", conn_id, handle);

     std::unique_ptr<tGATTS_RSP> p_msg = std::make_unique<tGATTS_RSP>();
     p_msg->attr_value.handle = handle;

     // Only Client Characteristic Configuration (CCC) descriptor is expected
     VapCharacteristic* characteristic = GetCharacteristicByCccHandle(handle);
     if (characteristic == nullptr) {
       log::warn("Can't find Characteristic for CCC Descriptor, handle 0x{:04x}", handle);
       BTA_GATTS_SendRsp(conn_id, trans_id, GATT_INVALID_HANDLE, std::move(p_msg));
       return;
     }
     log::info("Read CCC for uuid, {}", getUuidName(characteristic->uuid_));
     uint16_t ccc_value = 0;
     if (remote_clients_.find(remote_bda) != remote_clients_.end()) {
       ccc_value = remote_clients_[remote_bda].ccc_values_[characteristic->uuid_];
     }

     p_msg->attr_value.len = kCccValueSize;
     memcpy(p_msg->attr_value.value, &ccc_value, sizeof(uint16_t));

     log::info("Send response for CCC value 0x{:04x}", ccc_value);
     BTA_GATTS_SendRsp(conn_id, trans_id, GATT_SUCCESS, std::move(p_msg));
   }

   void OnWriteCharacteristic(tCONN_ID conn_id, uint32_t trans_id, const RawAddress& remote_bda,
                              uint16_t handle, uint16_t /* offset */, bool need_rsp,
                              bool /* is_prep */, uint8_t* value, uint16_t len) {
     log::info("conn_id:{}, handle:0x{:04x}, need_rsp{}, len:{}", conn_id, handle, need_rsp, len);

     std::unique_ptr<tGATTS_RSP> p_msg = std::make_unique<tGATTS_RSP>();
     p_msg->handle = handle;
     if (characteristics_.find(handle) == characteristics_.end()) {
       log::error("Invalid handle {}", handle);
       BTA_GATTS_SendRsp(conn_id, trans_id, GATT_INVALID_HANDLE, std::move(p_msg));
       return;
     }

     auto uuid = characteristics_[handle].uuid_;
     log::info("Write uuid, {}", getUuidName(uuid));

     // Check Characteristic UUID
     switch (uuid.As16Bit()) {
       case kVasControlPointCharacteristic16bit: {
         if (remote_clients_.find(remote_bda) == remote_clients_.end()) {
           log::warn("Can't find remote_clients for {}", remote_bda);
           BTA_GATTS_SendRsp(conn_id, trans_id, GATT_ILLEGAL_PARAMETER, std::move(p_msg));
           return;
         }
         RemoteClient* remote_client = &remote_clients_[remote_bda];
         if (need_rsp) {
           BTA_GATTS_SendRsp(conn_id, trans_id, GATT_SUCCESS, std::move(p_msg));
         }
         HandleControlPoint(remote_bda, remote_client, value, len);
       } break;
       default:
         log::warn("Unhandled uuid {}", uuid.ToString());
         BTA_GATTS_SendRsp(conn_id, trans_id, GATT_ILLEGAL_PARAMETER, std::move(p_msg));
         return;
     }
   }

   void OnWriteDescriptor(tCONN_ID conn_id, uint32_t trans_id, const RawAddress& remote_bda,
                          uint16_t handle, uint16_t /* offset */, bool /*need_rsp */,
                          bool /*is_prep */, uint8_t* value, uint16_t len) {
     log::info("conn_id:{}, handle:0x{:04x}, len:{}", conn_id, handle, len);

     std::unique_ptr<tGATTS_RSP> p_msg = std::make_unique<tGATTS_RSP>();
     p_msg->handle = handle;

     // Only Client Characteristic Configuration (CCC) descriptor is expected
     VapCharacteristic* characteristic = GetCharacteristicByCccHandle(handle);
     if (characteristic == nullptr) {
       log::warn("Can't find Characteristic for CCC Descriptor, handle 0x{:04x}", handle);
       BTA_GATTS_SendRsp(conn_id, trans_id, GATT_INVALID_HANDLE, std::move(p_msg));
       return;
     }

     if (remote_clients_.find(remote_bda) == remote_clients_.end()) {
       log::warn("Can't find tracker for remote_bda {}", remote_bda);
       BTA_GATTS_SendRsp(conn_id, trans_id, GATT_ILLEGAL_PARAMETER, std::move(p_msg));
       return;
     }
     uint16_t ccc_value;
     STREAM_TO_UINT16(ccc_value, value);

     uint16_t current_ccc = 0;
     if (remote_clients_[remote_bda].ccc_values_.count(characteristic->uuid_)) {
       current_ccc = remote_clients_[remote_bda].ccc_values_[characteristic->uuid_];
     }

     if (current_ccc != ccc_value) {
       remote_clients_[remote_bda].ccc_values_[characteristic->uuid_] = ccc_value;
       log::info("Write CCC for {}, conn_id:{}, value:0x{:04x}",
                 getUuidName(characteristic->uuid_), conn_id, ccc_value);
       SaveVapData(remote_bda);
     }

     BTA_GATTS_SendRsp(conn_id, trans_id, GATT_SUCCESS, std::move(p_msg));

     if (characteristic->uuid_ == kVaSupportedFeaturesCharacteristic &&
         bluetooth::common::IsPtsTestMode() &&
         stack_config_get_interface()->get_pts_vap_notify_characteristics()) {
       kPtsVaSupportedFeatures++;
       NotifyVaSupportedFeaturesForPts(remote_bda, kPtsVaSupportedFeatures);
     } else if (characteristic->uuid_ == kVaCcidCharacteristic &&
                bluetooth::common::IsPtsTestMode() &&
                stack_config_get_interface()->get_pts_vap_notify_characteristics()) {
       kPtsVapCcid++;
       NotifyVaCcidForPts(remote_bda, kPtsVapCcid);
     }
   }

   void DebugDump(int fd) {
     std::stringstream stream;

     dprintf(fd, "VAP Server Manager:\n");
     stream << "    VA Name: " << +va_name_.c_str() << "\n"
            << "    Active VA Group ID: " << active_va_group_id_ << "\n"
            << "    Active VA Device: " << active_va_device_.ToString() << "\n"
            << "    VAP CCID: " << +kVapCcid << "\n"
            << "    VA Session Flag: " << +kVaSessionFlag << "\n"
            << "    VA Supported Languages: " << +kVaSupportedLanguages.c_str() << "\n"
            << "    VA Supported Features: " << +kVaSupportedFeatures << "\n"
            << "    VAP GATT Server IF: " << +server_if_ << "\n";
     for (auto& [address, remote_client] : remote_clients_) {
       stream << "    Remote Client: " << address.ToString() << "\n";
       stream << "    Remote Client MTU: " << remote_client.mtu_ << "\n";
       stream << "    Remote Client conn_id: " << remote_client.conn_id_ << "\n";
       stream << "    Remote Client VA Session State: "
              << GetVaSessionStateText(remote_client.va_session_state_) << "\n";
       stream << "    CCCD VAS Control Point: "
              << remote_client.ccc_values_[kVasControlPointCharacteristic] << "\n";
       stream << "    CCCD VA Session State: "
              << remote_client.ccc_values_[kVaSessionStateCharacteristic] << "\n";
       stream << "    Handling Control Point Command:  "
              << remote_client.handling_control_point_command_ << "\n";
     }

     dprintf(fd, "%s", stream.str().c_str());
     dprintf(fd, "\n");
   }

   void HandleControlPoint(RawAddress bda, RemoteClient* remote_client, uint8_t* value,
                           uint16_t len) {
     ControlPointCommand command;
     uint16_t ccc_vas_control_point = GATT_CLT_CONFIG_NONE;
     VaSessionState va_session_state = GetVaSessionState(bda);

     ccc_vas_control_point = remote_client->ccc_values_[kVasControlPointCharacteristic];
     if (ccc_vas_control_point == GATT_CLT_CONFIG_NONE) {
       log::warn(" VAS Control Point CCCD not configured by remote client, ignore the command");
       return;
     }

     ControlPointResponse cp_rsp =
             ValidateControlPointOperation(&command, value, len, va_session_state);

     if (!command.isValid_) {
       SendVasControlPointNotification(remote_client, cp_rsp.code_value_, ccc_vas_control_point);
       return;
     }

     if (IsAnySessionActive()) {
         // If the command is from a completely different device/group, we need to handle it.
         if (!IsActiveDeviceOrGroupMember(bda)) {
           if (command.ctp_opcode_ == CtpOpcode::START_VA_SESSION) {
             /* If a new device requests to START a session, we tear down the existing
              * active session (to mimic HFP parity) and reject the new request.
              */
             log::warn("Out-of-group START request. Forcing teardown of active VA session "
                       "for {} and rejecting START for {}", active_va_device_, bda);

             auto old_device = active_va_device_;
             auto old_group_id = active_va_group_id_;
             callbacks_->OnStopVaSession(active_va_device_);
             active_va_device_ = RawAddress::kEmpty;
             active_va_group_id_ = bluetooth::groups::kGroupUnknown;

             /* Notify the previously active device (and its group) that their
              * session was superseded. */
             for (auto& [other_bda, client] : remote_clients_) {
               bool is_old_device = (other_bda == old_device);
               bool is_old_group = (old_group_id != bluetooth::groups::kGroupUnknown &&
                                    GetGroupId(other_bda) == old_group_id);
               if (is_old_device || is_old_group) {
                 if (SendVaSessionStateNotification(
                     other_bda, &client,
                     client.ccc_values_[kVaSessionStateCharacteristic],
                     static_cast<uint8_t>(VaSessionState::VA_SESSION_READY), true)) {
                   SaveVapData(other_bda);
                 }
               }
             }

             SendVasControlPointNotification(remote_client, ResponseCodeValue::OPERATION_FALIED,
                                             ccc_vas_control_point);
             return;
           } else if (command.ctp_opcode_ == CtpOpcode::STOP_VA_SESSION) {
             /* A STOP_VA_SESSION command from an out-of-group device is rejected
              * because it does not own the active session.
              */
             log::warn("Rejecting STOP VA command from {} because another device/group "
                       "currently owns the session", bda);
             SendVasControlPointNotification(remote_client, ResponseCodeValue::OPERATION_FALIED,
                                             ccc_vas_control_point);
             return;
           }
         }
       }

     remote_client->handling_control_point_command_ = true;

     switch (command.ctp_opcode_) {
       case CtpOpcode::START_VA_SESSION:
         OnStartVaSession(bda);
         break;
       case CtpOpcode::STOP_VA_SESSION:
         OnStopVaSession(bda);
         break;
       case CtpOpcode::INITIALIZE_VA_SESSION:
         OnInitializeVaSession(bda);
         break;
     }
   }

   void NotifyVaSessionStateForPts(RawAddress bda, VaSessionState state) {
     log::info("bda: {}, state: {}", bda, static_cast<int>(state));

     auto it = remote_clients_.find(bda);
     if (it != remote_clients_.end()) {
       RemoteClient* remote_client = &it->second;
       uint16_t ccc_vas_control_point = remote_client->ccc_values_[kVasControlPointCharacteristic];
       uint16_t ccc_va_session_state = remote_client->ccc_values_[kVaSessionStateCharacteristic];
       ResponseCodeValue rsp_code_value = ResponseCodeValue::SUCCESS;

       // Send VAS Control Point notification
       SendVasControlPointNotification(remote_client, rsp_code_value, ccc_vas_control_point);

       uint8_t va_session_state = static_cast<uint8_t>(state);
       // Send VA Session State notification
       bool changed = false;
       changed = SendVaSessionStateNotification(bda, remote_client, ccc_va_session_state,
                                                va_session_state);
       if (changed) {
         SaveVapData(bda);
       }
     }
   }

   void OnStartVaSession(RawAddress bda) {
     log::info("bda:{}", bda);

     if (bluetooth::common::IsPtsTestMode()) {
       NotifyVaSessionStateForPts(bda, VaSessionState::VA_SESSION_ACTIVE);
     } else {
       callbacks_->OnStartVaSession(bda);
     }
   }

   void OnStopVaSession(RawAddress bda) {
     log::info("bda:{}", bda);

     if (bluetooth::common::IsPtsTestMode()) {
       NotifyVaSessionStateForPts(bda, VaSessionState::VA_SESSION_READY);
     } else {
       callbacks_->OnStopVaSession(bda);
     }
   }

   void OnInitializeVaSession(RawAddress bda) {
     log::info("bda:{}", bda);
     NotifyVaSessionInitialized(bda);
     SetVaSessionState(bda, VaSessionState::VA_SESSION_READY);

     /* If the initializing device, or another device in its group,
      * was currently holding an active session, we must clear the active state
      * to ensure the server resets its state machine properly.
      */
     if (IsActiveDeviceOrGroupMember(bda)) {
       active_va_group_id_ = bluetooth::groups::kGroupUnknown;
       active_va_device_ = RawAddress::kEmpty;
     }
   }

   VapCharacteristic* GetCharacteristic(Uuid uuid) {
     for (auto& [attribute_handle, characteristic] : characteristics_) {
       if (characteristic.uuid_ == uuid) {
         return &characteristic;
       }
     }
     return nullptr;
   }

   VapCharacteristic* GetCharacteristicByCccHandle(uint16_t descriptor_handle) {
     for (auto& [attribute_handle, characteristic] : characteristics_) {
       if (characteristic.attribute_handle_ccc_ == descriptor_handle) {
         return &characteristic;
       }
     }
     return nullptr;
   }

   void SetVaSessionState(const RawAddress& bda, VaSessionState state) {
     if (remote_clients_.find(bda) == remote_clients_.end()) return;

     auto old_state = remote_clients_[bda].va_session_state_;
     remote_clients_[bda].va_session_state_ = state;
     log::debug("{} ({:x}) -> {} ({:x}) for {}",
                 GetVaSessionStateText(old_state),
                 static_cast<int>(old_state),
                 GetVaSessionStateText(state),
                 static_cast<int>(state),
                 bda);

     int group_id = GetGroupId(bda);
     if (group_id != bluetooth::groups::kGroupUnknown) {
       for (auto& [other_bda, client] : remote_clients_) {
         if (other_bda != bda && GetGroupId(other_bda) == group_id) {
           client.va_session_state_ = state;
         }
       }
     }
   }

   VaSessionState GetVaSessionState(const RawAddress& bda) const {
     auto it = remote_clients_.find(bda);
     if (it != remote_clients_.end()) {
       return it->second.va_session_state_;
     }
     return VaSessionState::VA_SESSION_UNAVAILABLE;
   }

   bool IsAnySessionActive() const {
     return !active_va_device_.IsEmpty();
   }

   int GetGroupId(const RawAddress& bda) const {
     auto csis_api = CsisClient::Get();
     if (csis_api != nullptr) {
       return csis_api->GetGroupId(bda, bluetooth::le_audio::uuid::kCapServiceUuid);
     }
     return bluetooth::groups::kGroupUnknown;
   }

   bool IsActiveDeviceOrGroupMember(const RawAddress& bda) const {
     if (bda == active_va_device_) {
       return true;
     }
     int group_id = GetGroupId(bda);
     return group_id != bluetooth::groups::kGroupUnknown && group_id == active_va_group_id_;
   }

 private:
   bluetooth::Uuid app_uuid_;
   uint16_t server_if_;
   // A map to associate characteristics with handles
   std::unordered_map<uint16_t, VapCharacteristic> characteristics_;
   // A map to associate remote client with address
   std::unordered_map<RawAddress, RemoteClient> remote_clients_;
   bluetooth::vap::VapServerCallbacks* callbacks_;
   std::string va_name_;
   int active_va_group_id_ = bluetooth::groups::kGroupUnknown;
   RawAddress active_va_device_ = RawAddress::kEmpty;
 };

 }  // namespace

 bluetooth::vap::VapServer* bluetooth::vap::GetVapServer() {
   if (instance == nullptr) {
     instance = new VapServerImpl();
   }
   return instance;
 }
