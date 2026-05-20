/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.le.ScanResult;

/**
 * Callback interface for Bluetooth LE Audio Broadcast Sink service
 *
 * {@hide}
 */
interface IBluetoothLeBroadcastSinkCallback {
    // Search callbacks
    void onSearchStarted(int reason);
    void onSearchStartFailed(int reason);
    void onSearchStopped(int reason);

    // Source discovery callbacks
    void onSourceFound(int broadcastId, in ScanResult result);

    // Source add callbacks (PA sync)
    void onSourceAdded(in BluetoothLeBroadcastMetadata metadata);
    void onSourceAddFailed(int broadcastId, int reason);

    // Source join callbacks (BIG sync)
    void onSourceJoined(int broadcastId);
    void onSourceJoinFailed(in BluetoothLeBroadcastMetadata metadata, int reason);

    // Source leave callbacks (stop BIG sync)
    void onSourceLeft(int broadcastId, int reason);
    void onSourceLeaveFailed(int broadcastId, int reason);

    // Source remove callbacks (stop PA sync)
    void onSourceRemoved(int broadcastId, int reason);
    void onSourceRemoveFailed(int broadcastId, int reason);

    // Metadata update callbacks
    void onSourceMetadataChanged(int broadcastId, in BluetoothLeBroadcastMetadata metadata);
    void onSourceMetadataUpdated(int broadcastId, in BluetoothLeBroadcastMetadata metadata);
    void onSourceMetadataUpdateFailed(int broadcastId, in BluetoothLeBroadcastMetadata metadata, int reason);
}
