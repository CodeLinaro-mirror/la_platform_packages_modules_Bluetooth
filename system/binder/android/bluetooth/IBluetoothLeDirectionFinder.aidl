/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.bluetooth.BluetoothDevice;
import android.content.AttributionSource;
import com.android.modules.utils.SynchronousResultReceiver;
import android.bluetooth.IBluetoothLeDirectionFinderCallback;
import android.bluetooth.le.ScanFilter;

/**
 * APIs for Bluetooth LE Direction Finder service
 *
 * @hide
 */
interface IBluetoothLeDirectionFinder {
    // Public API
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    boolean connect(in BluetoothDevice device, in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    boolean disconnect(in BluetoothDevice device, in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    int getConnectionState(in BluetoothDevice device, in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    List<BluetoothDevice> getDevicesMatchingConnectionStates(in int[] states, in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    List<BluetoothDevice> getConnectedDevices(in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    boolean setConnectionPolicy(in BluetoothDevice device, in int connectionPolicy, in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    int getConnectionPolicy(in BluetoothDevice device, in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    void registerAppCallback(in BluetoothDevice device, in IBluetoothLeDirectionFinderCallback cb, in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    void unregisterAppCallback(in BluetoothDevice device, in IBluetoothLeDirectionFinderCallback cb, in AttributionSource source);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)")
    void enableBleDirectionFinding(in BluetoothDevice device, in int samplingEnable, in int slotDurations, in int enable,
            in int cteReqInt, in int reqCteLen, in int dirFindingType, in AttributionSource source);
}
