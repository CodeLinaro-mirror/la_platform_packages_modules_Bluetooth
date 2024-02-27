/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;
import android.bluetooth.BluetoothHeadsetClientCall;


/**
 * API for Bluetooth Headset Client Callback
 *
 * @hide
 */
oneway interface IBluetoothHeadsetClientCallback {
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_PRIVILEGED)")
    void onHeadsetClientScoStateChanged(in int sco_state);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_PRIVILEGED)")
    void onHeadsetClientCallStateChanged(in BluetoothHeadsetClientCall call);
}
