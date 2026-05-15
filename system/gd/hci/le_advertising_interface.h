/*
 * Copyright (C) 2019 The Android Open Source Project
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
 *  Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#pragma once

#include "hci/command_interface.h"
#include "hci/hci_packets.h"

namespace bluetooth {
namespace hci {

constexpr hci::SubeventCode LeAdvertisingEvents[] = {
        hci::SubeventCode::SCAN_REQUEST_RECEIVED,
        hci::SubeventCode::ADVERTISING_SET_TERMINATED,
#ifdef TARGET_QCOM_IOT_BT_EXT
        hci::SubeventCode::BLE_META_SUBEVENT_PAWR_SUBEVENT_REQUEST,
        hci::SubeventCode::BLE_META_SUBEVENT_PAWR_SUBEVENT_RESPONSE,
#endif
};

typedef CommandInterface<LeAdvertisingCommandBuilder> LeAdvertisingInterface;

}  // namespace hci
}  // namespace bluetooth
