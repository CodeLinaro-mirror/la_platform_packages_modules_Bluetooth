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
 * ​Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.bluetooth.le.AdvertiseSettings;
import android.bluetooth.le.AdvertiseData;
import android.bluetooth.le.AdvertisingSetParameters;
import android.bluetooth.le.IAdvertisingSetCallback;
import android.bluetooth.le.IPeriodicAdvertisingCallback;
import android.bluetooth.le.PeriodicAdvertisingParameters;
import android.bluetooth.le.PeriodicAdvertisingParametersV2;
import android.content.AttributionSource;

/**
 * API for interacting with BLE advertising
 * @hide
 */
interface IBluetoothAdvertise {
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_ADVERTISE,android.Manifest.permission.BLUETOOTH_PRIVILEGED}, conditional=true)")
    void startAdvertisingSet(in AdvertisingSetParameters parameters, in AdvertiseData advertiseData,
                                in AdvertiseData scanResponse, in PeriodicAdvertisingParameters periodicParameters,
                                in AdvertiseData periodicData, in int duration, in int maxExtAdvEvents, in int gattServerIf,
                                in IAdvertisingSetCallback callback, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void stopAdvertisingSet(in IAdvertisingSetCallback callback, in AttributionSource attributionSource);

    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_ADVERTISE,android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void getOwnAddress(in int advertiserId, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void enableAdvertisingSet(in int advertiserId, in boolean enable, in int duration, in int maxExtAdvEvents, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void setAdvertisingData(in int advertiserId, in AdvertiseData data, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void setScanResponseData(in int advertiserId, in AdvertiseData data, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_ADVERTISE,android.Manifest.permission.BLUETOOTH_PRIVILEGED}, conditional=true)")
    void setAdvertisingParameters(in int advertiserId, in AdvertisingSetParameters parameters, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void setPeriodicAdvertisingParameters(in int advertiserId, in PeriodicAdvertisingParameters parameters, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void setPeriodicAdvertisingParametersV2(in int advertiserId, in PeriodicAdvertisingParametersV2 parameters, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void setPeriodicAdvertisingData(in int advertiserId, in AdvertiseData data, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void setPeriodicAdvertisingSubeventData(in int advertiserId, in int numSubenvets, in byte[] data, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_ADVERTISE)")
    void setPeriodicAdvertisingEnable(in int advertiserId, in boolean enable, in AttributionSource attributionSource);
}
