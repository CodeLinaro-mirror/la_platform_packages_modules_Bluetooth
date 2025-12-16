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
 */

package com.android.server.bluetooth

import android.bluetooth.BluetoothAdapterCommon
import android.content.Context
import android.os.Looper
import android.os.UserHandle
import com.android.bluetooth.flags.Flags

class BluetoothSupervisor(
    context: Context,
    val looper: Looper,
    bluetoothComponent: BluetoothComponent?,
) {
    val adapterIndex: Int = bluetoothComponent?.adapterIndex ?:
                            BluetoothAdapterCommon.ADAPTER_DEFAULT

    private val bms: BluetoothManagerService

    init {
        val hciInstance = if (Flags.hciInstanceNameUseInjected()) {
            BluetoothHciInstance().getInstance(adapterIndex)
        } else {
            when (adapterIndex) {
                0 -> BluetoothHciInstance.HCI_DEFAULT_INSTANCE_NAME
                1 -> BluetoothHciInstance.HCI_NEW_INSTANCE_NAME
                else -> BluetoothHciInstance.HCI_DEFAULT_INSTANCE_NAME
            }
        }

        bms = BluetoothManagerService(context, looper, hciInstance, bluetoothComponent)
        Log.i("BluetoothSupervisor", "Created BluetoothSupervisor with HCI instance: $hciInstance")
    }

    fun api(): BluetoothManagerServiceApi {
        return bms.api
    }

    fun onBluetoothDisallowed() {
        enforceCorrectThread()
        bms.onBluetoothDisallowed()
    }

    fun handleOnBootPhase(userHandle: UserHandle) {
        enforceCorrectThread()
        bms.handleOnBootPhase(userHandle)
    }

    fun onUserSwitching(userHandle: UserHandle) {
        enforceCorrectThread()
        bms.onUserSwitching(userHandle)
    }

    private fun enforceCorrectThread() {
        if (looper == Looper.myLooper()) {
            return
        }
        throw IllegalThreadStateException("Must be called on BluetoothSystemServer looper")
    }
}
