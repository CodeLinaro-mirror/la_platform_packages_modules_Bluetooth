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
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */
package com.android.server.bluetooth

import android.bluetooth.BluetoothAdapterCommon;
import android.bluetooth.IBluetooth
import android.bluetooth.IBluetoothExt
import android.bluetooth.IBluetoothCallback
import android.content.AttributionSource
import android.os.IBinder
import android.os.RemoteException

class AdapterBinder(rawBinder: IBinder, index: Int? =
BluetoothAdapterCommon.ADAPTER_DEFAULT) {
    private val TAG = "AdapterBinder"
    val adapterBinder: IBluetooth
    private val createdAt: Long = System.currentTimeMillis()
    private var index: Int? = index

    init {
        adapterBinder = if (BluetoothAdapterCommon.isAdapterDefault(index ?:
            BluetoothAdapterCommon.ADAPTER_DEFAULT)) {
            IBluetooth.Stub.asInterface(rawBinder)
        } else {
            IBluetoothExt.Stub.asInterface(rawBinder).getBluetooth()
        }
    }

    override fun toString(): String =
        "[Binder=" +
            adapterBinder.hashCode() +
            ", createdAt=" +
            Log.timeToStringWithZone(createdAt) +
            (index?.let { ", index=$it" } ?: "") +
            "]"

    @Throws(RemoteException::class)
    fun onToBleOn(source: AttributionSource) {
        adapterBinder.onToBleOn(source)
    }

    @Throws(RemoteException::class)
    fun offToBleOn(quietMode: Boolean, source: AttributionSource) {
        adapterBinder.offToBleOn(quietMode, source)
    }

    @Throws(RemoteException::class)
    fun getAddress(source: AttributionSource): String? {
        return adapterBinder.getAddress(source)
    }

    @Throws(RemoteException::class)
    fun getName(source: AttributionSource): String? {
        return adapterBinder.getName(source)
    }

    @Throws(RemoteException::class)
    fun bleOnToOff(source: AttributionSource) {
        adapterBinder.bleOnToOff(source)
    }

    @Throws(RemoteException::class)
    fun bleOnToOn(source: AttributionSource) {
        adapterBinder.bleOnToOn(source)
    }

    @Throws(RemoteException::class)
    fun registerCallback(callback: IBluetoothCallback, source: AttributionSource) {
        adapterBinder.registerCallback(callback, source)
    }

    @Throws(RemoteException::class)
    fun unregisterCallback(callback: IBluetoothCallback, source: AttributionSource) {
        adapterBinder.unregisterCallback(callback, source)
    }

    @Throws(RemoteException::class)
    fun setForegroundUserId(userId: Int, source: AttributionSource) {
        adapterBinder.setForegroundUserId(userId, source)
    }

    @Throws(RemoteException::class)
    fun unregAllGattClient(source: AttributionSource) {
        adapterBinder.unregAllGattClient(source)
    }

    fun isMediaProfileConnected(source: AttributionSource): Boolean {
        try {
            return adapterBinder.isMediaProfileConnected(source)
        } catch (ex: RemoteException) {
            Log.e(TAG, "Error when calling isMediaProfileConnected", ex)
        }
        return false
    }

    @Throws(RemoteException::class)
    fun killBluetoothProcess() {
        adapterBinder.killBluetoothProcess()
    }
}
