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
 */

/*
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.a2dpsink;

import android.util.Log;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import com.android.bluetooth.Utils;
import android.content.Intent;
import android.content.Context;

class A2dpSinkVendorService {
    private static final String TAG = "A2dpSinkVendorService";
    @SuppressWarnings("FieldCanBeLocal")
    private final A2dpSinkService mService;
    private final BluetoothAdapter mAdapter = BluetoothAdapter.getDefaultAdapter();

    static {
        classInitNative();
    }

    public A2dpSinkVendorService(A2dpSinkService service) {
        mService = service;
    }

    public void init() {
        initNative();
    }

    public void cleanup() {
        cleanupNative();
    }

    public int StartIndRsp(byte[] address, boolean accepted) {
        int ret = StartIndRspNative(address, accepted);
        return ret;
    }

    public int SuspendIndRsp(byte[] address, boolean accepted) {
        int ret = SuspendIndRspNative(address, accepted);
        return ret;
    }

    private BluetoothDevice getDevice(byte[] address) {
        BluetoothDevice local = mAdapter.getRemoteDevice(Utils.getAddressStringFromByte(address));
        return local;
    }

    @SuppressWarnings("MethodCanBeStatic")
    private void onStartIndCallback(byte[] address) {
        A2dpSinkService service = A2dpSinkService.getA2dpSinkService();
        if (service != null) {
            service.onStartIndCallback(address);
        } else {
            Log.d(TAG,"FATAL: Stack sent event while service is not available: ");
        }
    }

    @SuppressWarnings("MethodCanBeStatic")
    private void onSuspendIndCallback(byte[] address) {
        A2dpSinkService service = A2dpSinkService.getA2dpSinkService();
        if (service != null) {
            service.onSuspendIndCallback(address);
        } else {
            Log.d(TAG,"FATAL: Stack sent event while service is not available: ");
        }
    }

    private native void initNative();
    private native static void classInitNative();
    private native void cleanupNative();
    private native int StartIndRspNative(byte[] address, boolean accepted);
    private native int SuspendIndRspNative(byte[] address, boolean accepted);
}
