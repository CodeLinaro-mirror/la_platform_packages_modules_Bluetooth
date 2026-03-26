/*
 * Copyright 2019 The Android Open Source Project
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
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#define LOG_TAG "bt_shim_btm"

#include "main/shim/btm_api.h"

#include <base/functional/callback.h>

#include "hci/acl_manager.h"
#include "hci/controller.h"
#include "hci/controller_interface.h"
#include "hci/le_advertising_manager.h"
#include "main/shim/acl.h"
#include "main/shim/entry.h"
#include "main/shim/helpers.h"
#include "main/shim/stack.h"
#include "stack/btm/btm_ble_sec.h"
#include "stack/btm/btm_dev.h"
#include "stack/include/btm_status.h"
#include "types/raw_address.h"

tBTM_STATUS bluetooth::shim::BTM_ClearEventFilter() {
  GetController()->SetEventFilterClearAll();
  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_ClearEventMask() {
  GetController()->SetEventMask(0);
  GetController()->LeSetEventMask(0);
  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_ClearFilterAcceptList() {
  Stack::GetInstance()->GetAcl()->ClearFilterAcceptList();
  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_DisconnectAllAcls() {
  Stack::GetInstance()->GetAcl()->DisconnectAllForSuspend();
  //  Stack::GetInstance()->GetAcl()->Shutdown();
  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_SetEventFilterConnectionSetupAllDevices() {
  // Autoplumbed
  GetController()->SetEventFilterConnectionSetupAllDevices(
          bluetooth::hci::AutoAcceptFlag::AUTO_ACCEPT_ON_ROLE_SWITCH_ENABLED);
  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_AllowWakeByHid(
        std::vector<RawAddress> classic_hid_devices,
        std::vector<std::pair<RawAddress, uint8_t>> le_hid_devices) {
  // First set ACL to suspended state.
  Stack::GetInstance()->GetAcl()->SetSystemSuspendState(/*suspended=*/true);

  // Allow classic HID wake.
  auto controller = GetController();
  for (auto device : classic_hid_devices) {
    controller->SetEventFilterConnectionSetupAddress(bluetooth::ToGdAddress(device),
                                                     hci::AutoAcceptFlag::AUTO_ACCEPT_OFF);
  }

  // Allow BLE HID
  for (auto hid_address : le_hid_devices) {
    tBLE_BD_ADDR bdadr = BTM_Sec_GetAddressWithType(hid_address.first);
    bluetooth::shim::GetAclManager()->CreateLeConnection(ToAddressWithTypeFromLegacy(bdadr),
                                                         /*is_direct=*/false);
  }

  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_RestoreFilterAcceptList(
        std::vector<std::pair<RawAddress, uint8_t>> le_devices) {
  // First, mark ACL as no longer suspended.
  Stack::GetInstance()->GetAcl()->SetSystemSuspendState(/*suspended=*/false);

  // Next, Allow BLE connection from all devices that need to be restored.
  // This will also re-arm the LE connection.
  for (auto address_pair : le_devices) {
    tBLE_BD_ADDR bdadr = BTM_Sec_GetAddressWithType(address_pair.first);
    bluetooth::shim::GetAclManager()->CreateLeConnection(ToAddressWithTypeFromLegacy(bdadr),
                                                         /*is_direct=*/false);
  }

  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_SetDefaultEventMaskExcept(uint64_t mask, uint64_t le_mask) {
  uint64_t applied_mask = bluetooth::hci::Controller::kDefaultEventMask & ~(mask);
  uint64_t applied_le_mask = bluetooth::hci::Controller::kDefaultLeEventMask & ~(le_mask);
  GetController()->SetEventMask(applied_mask);
  GetController()->LeSetEventMask(applied_le_mask);
  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_SetEventFilterInquiryResultAllDevices() {
  // Autoplumbed
  GetController()->SetEventFilterInquiryResultAllDevices();
  return tBTM_STATUS::BTM_SUCCESS;
}

#ifdef TARGET_QCOM_IOT_BT_EXT
tBTM_STATUS bluetooth::shim::BTM_SetHostChannelClassification(
    std::vector<uint8_t> channel_map,
    common::OnceCallback<void(uint8_t)> bta_cb) {
  auto gd_cb = std::move(bta_cb);

  uint8_t map_array[5] = {0};
  for (size_t i = 0; i < 5 && i < channel_map.size(); ++i) {
    map_array[i] = channel_map[i];
  }
  GetController()->LeSetHostChannelClassification(map_array, std::move(gd_cb));
  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_WriteSuggestedDefaultDataLength(uint16_t suggested_max_tx_octets,
                                                uint16_t suggested_max_tx_time,
                                                common::OnceCallback<void(uint8_t)> bta_cb){
  auto gd_cb = std::move(bta_cb);

  GetController()->LeWriteSuggestedDefaultDataLength(suggested_max_tx_octets,
                                                     suggested_max_tx_time,
                                                     std::move(gd_cb));
  return tBTM_STATUS::BTM_SUCCESS;
}

tBTM_STATUS bluetooth::shim::BTM_SetDefaultPhy(
    uint8_t all_phys,
    uint8_t tx_phys,
    uint8_t rx_phys,
    common::OnceCallback<void(uint8_t)> bta_cb) {
  auto gd_cb = std::move(bta_cb);

  GetController()->LeSetDefaultPhy(all_phys, tx_phys, rx_phys, std::move(gd_cb));

  return tBTM_STATUS::BTM_SUCCESS;
}
#endif

tBTM_STATUS bluetooth::shim::BTM_BleResetId() {
  btm_ble_reset_id();
  return tBTM_STATUS::BTM_SUCCESS;
}

size_t bluetooth::shim::BTM_BleGetNumberOfAdvertisingInstancesInUse(void) {
  return GetAdvertising()->GetNumberOfAdvertisingInstancesInUse();
}
