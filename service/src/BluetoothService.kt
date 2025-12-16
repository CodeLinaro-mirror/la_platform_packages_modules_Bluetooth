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

import android.bluetooth.BluetoothAdapterCommon
import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemProperties
import android.os.UserManager
import com.android.bluetooth.flags.Flags
import com.android.server.SystemService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.android.asCoroutineDispatcher
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking

// See BluetoothServiceManager.BLUETOOTH_MANAGER_SERVICE
private const val SERVICE_NAME = "bluetooth_manager"
private const val SERVICE_NAME_EXT = "bluetooth_manager_ext"

class BluetoothService(context: Context) : SystemService(context) {
    private val sDualBluetooth = SystemProperties.getBoolean("persist.bluetooth.dual_bt", false)

    private val looper = HandlerThread("BluetoothSystemServer").apply { start() }.looper
    private val looperExt: android.os.Looper? =
        if (sDualBluetooth) {
            HandlerThread("BluetoothSystemServerExt").apply { start() }.looper
        } else null

    private val serviceDispatcher = Handler(looper).asCoroutineDispatcher()
    private val serviceDispatcherExt = looperExt?.let { Handler(it).asCoroutineDispatcher() }

    private val scope = CoroutineScope(serviceDispatcher + SupervisorJob())
    private val scopeExt = serviceDispatcherExt?.let { CoroutineScope(it + SupervisorJob()) }

    private var supervisor: BluetoothSupervisor
    private var supervisorExt: BluetoothSupervisor? = null

    private var mInitialized = false
    private var ADAPTER_1 = BluetoothAdapterCommon.ADAPTER_1;

    init {
        Log.d("Booting now")
        val bluetoothComponent =
            if (Flags.userRestrictionRefactor()) {
                BluetoothComponent(context)
            } else {
                null
            }
        val bluetoothComponentExt =
            if (sDualBluetooth && Flags.userRestrictionRefactor()) {
                BluetoothComponent(context, ADAPTER_1)
            } else {
                null
            }

        // Run BluetoothManagerService on the correct thread even during constructor
        supervisor =
            runBlocking(serviceDispatcher) {
                BluetoothSupervisor(context, looper, bluetoothComponent)
            }
        supervisorExt =
            if (sDualBluetooth) {
                runBlocking(serviceDispatcherExt!!) {
                    BluetoothSupervisor(context, looperExt!!, bluetoothComponentExt)
                }
            } else null

        runOnBmsThread {
            if (Flags.userRestrictionRefactor()) {
                BluetoothRestriction.initialize(context, looper, supervisor::onBluetoothDisallowed)
            }
        }
        runOnBmsExtThread {
            if (Flags.userRestrictionRefactor()) {
                BluetoothRestriction.initialize(
                    context,
                    looperExt!!,
                    supervisorExt!!::onBluetoothDisallowed
                )
            }
        }
    }

    // Run any lambda on the BluetoothSystemServer thread without waiting for its completion
    private fun runOnBmsThread(block: suspend CoroutineScope.() -> Unit) = scope.launch { block() }
    private fun runOnBmsExtThread(block: suspend CoroutineScope.() -> Unit) {
        if (!sDualBluetooth || scopeExt == null) return
        scopeExt!!.launch { block() }
    }

    override fun onStart() {
        publishBinderService(
            SERVICE_NAME,
            BluetoothServiceBinder(looper, supervisor.api(), context),
        )
        if (sDualBluetooth && supervisorExt != null && looperExt != null) {
            publishBinderService(
                SERVICE_NAME_EXT,
                BluetoothServiceBinder(looperExt, supervisorExt!!.api(), context),
            )
        }
    }

    override fun onUserStarting(user: TargetUser) {
        if (mInitialized) {
            Log.i("onUserStarting($user) but already initialized")
            return
        }
        if (Flags.userVisibleOnUserStarting()) {
            val isUserVisible =
                context
                    .createContextAsUser(user.userHandle, 0)
                    .getSystemService(android.os.UserManager::class.java)!!
                    .isUserVisible
            if (!isUserVisible) {
                Log.i("onUserStarting($user) Skipping non visible user ")
                return
            }
            Log.i("onUserStarting($user) Initializing for visible user ")
        } else {
            val isForeground =
                context
                    .createContextAsUser(user.userHandle, 0)
                    .getSystemService(android.os.UserManager::class.java)!!
                    .isUserForeground
            if (!isForeground) {
                Log.i("onUserStarting($user) Skipping non foreground user ")
                return
            }
            Log.i("onUserStarting($user) Initializing for foreground user ")
        }

        runOnBmsThread { supervisor.handleOnBootPhase(user.userHandle) }
        runOnBmsExtThread { supervisorExt?.handleOnBootPhase(user.userHandle) }

        mInitialized = true
    }

    override fun onUserSwitching(_from: TargetUser?, to: TargetUser) {
        Log.d("onUserSwitching($to)")
        if (!mInitialized) {
            throw IllegalStateException("Initialize did not happen")
        }
        runOnBmsThread { supervisor.onUserSwitching(to.userHandle) }
        runOnBmsExtThread { supervisorExt?.onUserSwitching(to.userHandle) }
    }
}
