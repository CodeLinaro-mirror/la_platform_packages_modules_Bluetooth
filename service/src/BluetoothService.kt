/*
 * Copyright (C) 2023 The Android Open Source Project
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
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
package com.android.server.bluetooth

import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothAdapterExt
import android.bluetooth.BluetoothAdapterCommon;
import android.content.Context
import android.content.res.Resources
import android.os.HandlerThread
import android.os.SystemProperties
import android.os.UserManager
import android.provider.Settings
import com.android.server.SystemService
import com.android.server.SystemService.TargetUser

class BluetoothService(context: Context) : SystemService(context) {
    private val mHandlerThread: HandlerThread
    private val mHandlerThreadExt: HandlerThread
    private val mBluetoothManagerService: BluetoothManagerService
    private val mBluetoothManagerExtService: BluetoothManagerExtService
    private var mInitialized = false
    private var ADAPTER_1 = BluetoothAdapterCommon.ADAPTER_1;
    private val sDualBluetooth = SystemProperties.getBoolean("persist.bluetooth.dual_bt", false)

    init {
        mHandlerThread = HandlerThread("BluetoothManagerService")
        mHandlerThread.start()
        mBluetoothManagerService = BluetoothManagerService(context, mHandlerThread.getLooper())
        mHandlerThreadExt = HandlerThread("BluetoothManagerExtService")
        mHandlerThreadExt.start()
        mBluetoothManagerExtService = BluetoothManagerExtService(context, mHandlerThreadExt.getLooper())
    }

    private fun initialize(user: TargetUser) {
        if (!mInitialized) {
            mBluetoothManagerService.handleOnBootPhase(user.userHandle)
            if (sDualBluetooth) {
                mBluetoothManagerExtService.handleOnBootPhase(user.userHandle)
            }
            mInitialized = true
        }
    }

    override fun onStart() {}

    override fun onBootPhase(phase: Int) {
        if (phase == SystemService.PHASE_SYSTEM_SERVICES_READY) {
            publishBinderService(
                BluetoothAdapter.BLUETOOTH_MANAGER_SERVICE,
                mBluetoothManagerService.getBinder(),
            )
            if (sDualBluetooth) {
                publishBinderService(
                    BluetoothAdapterExt.BLUETOOTH_MANAGER_SERVICE,
                    mBluetoothManagerExtService.getBinder()
                )
            }
        }
    }

    private fun shouldInitializeBluetooth(): Boolean {
        // Not HSUM, we can initialize Bluetooth on system user
        if (!UserManager.isHeadlessSystemUserMode()) {
            return true
        }

        try {
            // In HSUM, refer to config_hsumBootStrategy to see if we can boot on system user for
            // provisioned device
            val r = Resources.getSystem()
            if (
                r.getInteger(r.getIdentifier("config_hsumBootStrategy", "integer", "android")) ==
                    1 &&
                    Settings.Global.getInt(
                        context.contentResolver,
                        Settings.Global.DEVICE_PROVISIONED,
                        0,
                    ) == 1
            ) {
                return true
            }
        } catch (_e: Resources.NotFoundException) {
            // Config not found, assuming it's 0 so no need to initialize Bluetooth
        }

        return false
    }

    override fun onUserStarting(user: TargetUser) {
        if (shouldInitializeBluetooth()) {
            initialize(user)
        }
    }

    override fun onUserSwitching(_from: TargetUser?, to: TargetUser) {
        if (!mInitialized) {
            initialize(to)
        } else {
            mBluetoothManagerService.onSwitchUser(to.userHandle)
            if (sDualBluetooth) {
                mBluetoothManagerExtService.onSwitchUser(to.userHandle)
            }
        }
    }

    override fun onUserUnlocking(user: TargetUser) {
        mBluetoothManagerService.handleOnUnlockUser(user.userHandle)
        if (sDualBluetooth) {
            mBluetoothManagerExtService.handleOnUnlockUser(user.userHandle)
        }
    }
}
