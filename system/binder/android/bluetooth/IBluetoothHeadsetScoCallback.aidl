/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;


/**
 * API for Bluetooth Headset SCO Callback
 *
 * @hide
 */
oneway interface IBluetoothHeadsetScoCallback {
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_PRIVILEGED)")
    void onHeadsetScoStateChanged(in int sco_state);
}
