/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSinkState;
import android.bluetooth.IBluetoothLeBroadcastSinkCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanSettings;
import android.content.AttributionSource;

/**
 * API for Bluetooth LE Audio Broadcast Sink service
 *
 * {@hide}
 */
interface IBluetoothLeBroadcastSink {
    // Lifecycle management
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void registerCallback(in IBluetoothLeBroadcastSinkCallback callback, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void unregisterCallback(in IBluetoothLeBroadcastSinkCallback callback, in AttributionSource attributionSource);

    // Search operations
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_SCAN, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void startScanningForSources(in List<ScanFilter> filters, in ScanSettings settings, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_SCAN, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void stopScanningForSources(in AttributionSource attributionSource);

    // Source management operations
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void addSource(int broadcastId, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void startEnhancedBroadcastSink(in BluetoothLeBroadcastMetadata metadata, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void stopEnhancedBroadcastSink(int broadcastId, int mode, in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void removeSource(int broadcastId, in AttributionSource attributionSource);

    // Query operations
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    List<BluetoothLeBroadcastSinkState> getAllSyncedSinkState(in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    int getMaximumSourceCapacity(in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    int getEnhancedBroadcastSinkCap(in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    int getEnhancedBroadcastSourceCap(in AttributionSource attributionSource);
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void setAttributes(int devId, in byte[] name, in AttributionSource attributionSource);
    /**
     * Terminate the DBIG (spec §5.3 PGP Terminates procedure).
     * Sends HCI_VS_LE_Texit_DBIG(TERMINATE) so BT FW sends PGP_REQUEST(TERMINATE) to PGO.
     */
    @JavaPassthrough(annotation="@android.annotation.RequiresPermission(allOf={android.Manifest.permission.BLUETOOTH_CONNECT, android.Manifest.permission.BLUETOOTH_PRIVILEGED})")
    void terminateDbig(in AttributionSource attributionSource);
}
