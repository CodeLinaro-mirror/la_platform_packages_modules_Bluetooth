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

package com.android.server.bluetooth;

import android.bluetooth.BluetoothAdapterCommon;
import android.bluetooth.IBluetoothManager;

import android.annotation.NonNull;
import android.content.Context;
import android.os.Looper;
import android.os.UserHandle;
import android.util.Log;
import android.util.Slog;

class BluetoothManagerExtService {
    private static final String TAG = "BluetoothManagerExtService";

    private static final int ADAPTER_1 = BluetoothAdapterCommon.ADAPTER_1;

    private Context mContext;
    private BluetoothManagerService mManagerService;

    BluetoothManagerExtService(@NonNull Context context, @NonNull Looper looper) {
        Slog.d(TAG, "constructor");
        mContext = context;
        mManagerService = new BluetoothManagerService(mContext, looper, ADAPTER_1);
    }

    /**
     * Send enable message and set adapter name and address. Called when the boot phase becomes
     * PHASE_SYSTEM_SERVICES_READY.
     */
    public void handleOnBootPhase(UserHandle userHandle) {
        Slog.d(TAG, "Bluetooth boot completed");
        mManagerService.handleOnBootPhase(userHandle);
    }

    /**
     * Called when switching to a different foreground user.
     */
    public void onSwitchUser(UserHandle userHandle) {
        Slog.d(TAG, "User " + userHandle + " switched");
        mManagerService.onSwitchUser(userHandle);
    }

    /**
     * Called when user is unlocked.
     */
    public void handleOnUnlockUser(UserHandle userHandle) {
        Slog.d(TAG, "User " + userHandle + " unlocked");
        mManagerService.handleOnUnlockUser(userHandle);
    }

    IBluetoothManager.Stub getBinder() {
        return mManagerService.getBinder();
    }
}