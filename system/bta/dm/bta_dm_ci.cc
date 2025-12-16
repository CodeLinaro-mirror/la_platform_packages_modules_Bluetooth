/******************************************************************************
 *
 *  Copyright 2003-2012 Broadcom Corporation
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
 *  Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *  SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

/******************************************************************************
 *
 *  This is the API implementation file for the BTA device manager.
 *
 ******************************************************************************/
#include "bta/include/bta_dm_ci.h"

#include <base/functional/bind.h>
#include <bluetooth/types/address.h>

#include <memory>

#include "bta/dm/bta_dm_sec_int.h"
#include "stack/include/main_thread.h"

/*******************************************************************************
 *
 * Function         bta_dm_ci_rmt_oob
 *
 * Description      This function must be called in response to function
 *                  bta_dm_co_rmt_oob() to provide the OOB data associated
 *                  with the remote device.
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_dm_ci_rmt_oob(bool accept, const RawAddress& bd_addr, const Octet16& c, const Octet16& r) {
  std::unique_ptr<tBTA_DM_CI_RMT_OOB> msg = std::make_unique<tBTA_DM_CI_RMT_OOB>();

  msg->bd_addr = bd_addr;
  msg->accept = accept;
  msg->c = c;
  msg->r = r;

  do_in_main_thread(base::Bind(bta_dm_ci_rmt_oob_act, base::Passed(&msg)));
}

/*******************************************************************************
 *
 * Function         bta_dm_ci_rmt_oob_extended
 *
 * Description      This function must be called in response to function
 *                  btm_rem_oob_req() to provide the OOB data associated
 *                  with the remote device.
 *
 * Returns          void
 *
 ******************************************************************************/
void bta_dm_ci_rmt_oob_extended(bool accept, const RawAddress& bd_addr, const Octet16& c192,
                                const Octet16& r192, const Octet16& c256, const Octet16& r256) {
  std::unique_ptr<tBTA_DM_CI_RMT_OOB_EXTENDED> msg =
      std::make_unique<tBTA_DM_CI_RMT_OOB_EXTENDED>();

  msg->bd_addr = bd_addr;
  msg->accept = accept;
  msg->c192 = c192;
  msg->r192 = r192;
  msg->c256 = c256;
  msg->r256 = r256;

  do_in_main_thread(base::Bind(bta_dm_ci_rmt_oob_extended_act, base::Passed(&msg)));
}
