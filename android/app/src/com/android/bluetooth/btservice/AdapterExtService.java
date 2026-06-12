/*
 * Copyright (C) 2012 The Android Open Source Project
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

import android.bluetooth.BluetoothAdapter;
import android.content.Intent;
import android.os.IBinder;
import android.util.Log;

import com.android.bluetooth.Util;

public class AdapterExtService extends AdapterService {
    private static final String TAG = Util.BT_PREFIX + AdapterExtService.class.getSimpleName();

    @Override
    public IBinder onBind(Intent intent) {
        Log.d(TAG, "onBind()");
        mLocalName = intent.getStringExtra(BluetoothAdapter.EXTRA_LOCAL_NAME);
        return mAdapterBinder;
    }
}
