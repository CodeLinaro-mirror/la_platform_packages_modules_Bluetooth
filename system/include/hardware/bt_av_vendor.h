/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
 */

/*
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef ANDROID_INCLUDE_BT_AV_VENDOR_H
#define ANDROID_INCLUDE_BT_AV_VENDOR_H

#define BT_PROFILE_A2DP_SINK_VENDOR_ID "a2dp_sink_vendor"

#include <hardware/bluetooth.h>
__BEGIN_DECLS

/** Callback for informing start indication to application.*/
typedef void (*btav_audio_split_sink_start_ind_callback)(const RawAddress *bd_addr);

/** Callback for informing suspend indication to application.*/
typedef void (*btav_audio_split_sink_suspend_ind_callback)(const RawAddress *bd_addr);


typedef struct {
    /** set to sizeof(btav_sink_vendor_callbacks_t) */
    size_t      size;
    btav_audio_split_sink_start_ind_callback start_ind_cb;
    btav_audio_split_sink_suspend_ind_callback suspend_ind_cb;
} btav_sink_vendor_callbacks_t;


/** Represents the standard BT-AV sink vendor interface.
 */
typedef struct {
   /** set to sizeof(btav_vendor_interface_t) */
   size_t  size;
   /**
    * Register the BtAvVendorcallbacks
    */
   bt_status_t (*init)(btav_sink_vendor_callbacks_t* callbacks);

   /** Closes the av vendor interface. */
   void  (*cleanup)(void);

   /** response to start indication */
   bt_status_t (*start_ind_rsp)(const RawAddress& bd_addr, bool accepted);

   /** response to suspend indication */
   bt_status_t (*suspend_ind_rsp)(const RawAddress& bd_addr, bool accepted);

} btav_sink_vendor_interface_t;

__END_DECLS

#endif /* ANDROID_INCLUDE_BT_AV_VENDOR_H */
