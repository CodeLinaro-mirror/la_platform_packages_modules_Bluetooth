/*
 * Copyright (C) 2025 The Android Open Source Project
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
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package com.android.bluetooth.btservice;

import android.bluetooth.IAdapter;
import android.bluetooth.IAdapterExt;

import com.android.bluetooth.Utils;

class AdapterExtBinder extends IAdapterExt.Stub {
    private static final String TAG = Utils.BT_PREFIX + AdapterBinder.class.getSimpleName();
    private AdapterExtService mService;

    AdapterExtBinder(AdapterExtService svc) {
        mService = svc;
    }

    public void cleanup() {
        mService = null;
    }

    // New API to get Bluetooth interface in new Bluetooth adapter
    @Override
    public synchronized IAdapter getBluetoothAdapter() {
        return mService.getBluetoothAdapter();
    }
}

