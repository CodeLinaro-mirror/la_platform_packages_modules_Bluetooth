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
private const val TAG = "BluetoothService"

/** Returns the binder service name for the given adapter index. */
private fun adapterServiceName(adapterIndex: Int): String =
    when (adapterIndex) {
        0 -> SERVICE_NAME
        1 -> "bluetooth_manager_ext" // keep existing name for backward compatibility
        else -> "bluetooth_manager_ext$adapterIndex"
    }

@kotlin.time.ExperimentalTime
class BluetoothService(context: Context) : SystemService(context) {
    private val sDualBluetooth = SystemProperties.getBoolean("persist.bluetooth.dual_bt", false)

    // Number of adapters to start.  Currently at most 2 (default + one extra) are provisioned.
    // To enable adapters 2 or 3, update the platform configuration and raise this value.
    private val enabledAdapterCount = if (sDualBluetooth) 2 else 1

    private val looper = HandlerThread("BluetoothSystemServer").apply { start() }.looper
    private val serviceDispatcher = Handler(looper).asCoroutineDispatcher()
    private val scope = CoroutineScope(serviceDispatcher + SupervisorJob())

    // One looper / scope per non-default adapter (indices 1 .. enabledAdapterCount-1).
    private val extraLoopers: List<android.os.Looper> =
        (1 until enabledAdapterCount).map { i ->
            HandlerThread("BluetoothSystemServer$i").apply { start() }.looper
        }
    private val extraScopes: List<CoroutineScope> =
        extraLoopers.map { l ->
            CoroutineScope(Handler(l).asCoroutineDispatcher() + SupervisorJob())
        }

    private var supervisor: BluetoothSupervisor
    private val extraSupervisors: List<BluetoothSupervisor>

    init {
        Log.d(TAG, "Booting now")
        val bluetoothComponent = BluetoothComponent(context)

        // Run BluetoothManagerService on the correct thread even during constructor
        supervisor =
            runBlocking(serviceDispatcher) {
                if (Flags.systemServerMigrateBmsToKotlin()) {
                    BluetoothSupervisorNew(context, looper, bluetoothComponent)
                } else {
                    BluetoothSupervisorLegacy(context, looper, bluetoothComponent)
                }
            }

        extraSupervisors =
            extraLoopers.mapIndexed { listIdx, xLooper ->
                val adapterIdx = listIdx + 1
                runBlocking(extraScopes[listIdx].coroutineContext) {
                    BluetoothSupervisorLegacy(
                        context, xLooper, BluetoothComponent(context, adapterIdx))
                }
            }

        launchOnServerThread {
            BluetoothRestriction.initialize(context, looper, supervisor::onRestrictionChange)
        }
        extraSupervisors.forEachIndexed { listIdx, sup ->
            launchOnAdapterThread(listIdx) {
                BluetoothRestriction.initialize(
                    context, extraLoopers[listIdx], sup::onRestrictionChange)
            }
        }
    }

    // Run lambda on the BluetoothSystemServer thread without waiting for completion
    private fun launchOnServerThread(block: suspend CoroutineScope.() -> Unit) = scope.launch {
       block()
    }

    // Run lambda on the thread owned by extra adapter at list index [listIdx].
    private fun launchOnAdapterThread(listIdx: Int, block: suspend CoroutineScope.() -> Unit) {
        if (listIdx < 0 || listIdx >= extraScopes.size) return
        extraScopes[listIdx].launch { block() }
    }

    override fun onStart() {
        publishBinderService(adapterServiceName(0), ServerBinder(looper, supervisor.api, context))
        extraSupervisors.forEachIndexed { listIdx, sup ->
            publishBinderService(
                adapterServiceName(listIdx + 1),
                ServerBinder(extraLoopers[listIdx], sup.api, context))
        }
    }

    override fun onBootPhase(phase: Int) {
        if (phase != SystemService.PHASE_BOOT_COMPLETED) return
        launchOnServerThread { supervisor.onBootCompleted() }
    }

    override fun onUserStarting(user: TargetUser) {
        val isUserVisible =
            context
                .createContextAsUser(user.userHandle, 0)
                .getSystemService(UserManager::class.java)!!
                .isUserVisible
        if (!isUserVisible) {
            Log.i(TAG, "onUserStarting($user): Skipping non visible user")
            return
        }
        Log.i(TAG, "onUserStarting($user): Initializing for visible user")
        launchOnServerThread { supervisor.onUserStarting(user.userHandle) }
        extraSupervisors.forEachIndexed { listIdx, sup ->
            launchOnAdapterThread(listIdx) { sup.onUserStarting(user.userHandle) }
        }
    }

    override fun onUserStopping(user: TargetUser) {
        if (!Flags.switchWhenCurrentUserStop()) {
            Log.i(TAG, "onUserStopping($user): Not implemented. Flag Disabled")
            return
        }
        Log.i(TAG, "onUserStopping($user)")
        launchOnServerThread { supervisor.onUserStopping(user.userHandle) }
        extraSupervisors.forEachIndexed { listIdx, sup ->
            launchOnAdapterThread(listIdx) { sup.onUserStopping(user.userHandle) }
        }
    }

    override fun onUserStopped(user: TargetUser) {
        Log.i(TAG, "onUserStopped($user): Not implemented")
    }

    override fun onUserSwitching(from: TargetUser?, to: TargetUser) {
        Log.i(TAG, "onUserSwitching($from => $to)")
        launchOnServerThread { supervisor.onUserSwitching(to.userHandle) }
        extraSupervisors.forEachIndexed { listIdx, sup ->
            launchOnAdapterThread(listIdx) { sup.onUserSwitching(to.userHandle) }
        }
    }
}
