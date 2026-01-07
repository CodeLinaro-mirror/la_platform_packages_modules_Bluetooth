/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***********************************************************************/

/*
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

/************************************************************************************
 *
 *  Filename:      btif_av_vendor.cc
 *
 *  Description:   A2DP sink Vendor Bluetooth Interface
 *
 *
 ***********************************************************************************/

#define LOG_TAG "bt_btif_av_vendor"

#include <bluetooth/log.h>
#include "base/logging.h"
#include <hardware/bt_av_vendor.h>
#include "btif_api.h"
#include "btif_av.h"
#include "bta_av_api.h"

btav_sink_vendor_callbacks_t *bt_vendor_av_sink_callbacks = NULL;

/*******************************************************************************
** VENDOR INTERFACE FUNCTIONS
*******************************************************************************/

/*******************************************************************************
**
** Function         init
**
** Description     initializes the vendor interface for A2DP sink
**
** Returns         bt_status_t
**
*******************************************************************************/

static bt_status_t init( btav_sink_vendor_callbacks_t* callbacks)
{
    bt_vendor_av_sink_callbacks = callbacks;
    LOG(INFO) << "init done";
    return BT_STATUS_SUCCESS;
}

static void cleanup(void)
{
    LOG(INFO) << "cleanup";
    if (bt_vendor_av_sink_callbacks)
        bt_vendor_av_sink_callbacks = NULL;
}

bt_status_t start_ind_rsp(const RawAddress& bd_addr, bool accepted)
{
    LOG(INFO) << "accepted: " << accepted;
    tBTA_AV_SINK_START_RSP start_rsp;
    start_rsp.peer_addr = bd_addr;
    start_rsp.accepted = accepted;
    btif_dispatch_sm_event(BTIF_AV_SINK_START_IND_RSP, &start_rsp, sizeof(start_rsp));
    return BT_STATUS_SUCCESS;
}

bt_status_t suspend_ind_rsp(const RawAddress& bd_addr, bool accepted) {
    LOG(INFO) << "accepted: " << accepted;
    return BT_STATUS_SUCCESS;
}

static const btav_sink_vendor_interface_t bt_vendor_av_sink_interface = {
    sizeof(bt_vendor_av_sink_interface),
    init,
    cleanup,
    start_ind_rsp,
    suspend_ind_rsp
};

/*******************************************************************************
**
** Function         btif_vendor_av_sink_get_interface
**
** Description      Get the vendor callback interface
**
** Returns          btav_sink_vendor_interface_t
**
*******************************************************************************/
const btav_sink_vendor_interface_t *btif_vendor_av_sink_get_interface()
{
      LOG(INFO) << "vendor interface";
    return &bt_vendor_av_sink_interface;
}