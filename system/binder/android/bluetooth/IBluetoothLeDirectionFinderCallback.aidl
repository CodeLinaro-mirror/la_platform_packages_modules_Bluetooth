/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.bluetooth.BluetoothDevice;

/**
* Callback definitions for interacting with LE Direction Finder service
*
* @hide
*/
interface IBluetoothLeDirectionFinderCallback {
    void onEnableBleDirectionFinding(in BluetoothDevice device, in int reason);
    void onLeAoaResults(in BluetoothDevice device, in int status,
                        in double azimuth, in int azimuthUnc,
                        in double elevation, in int elevationUnc);
}
