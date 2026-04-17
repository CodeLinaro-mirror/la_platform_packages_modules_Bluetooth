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
    // Called when a broadcast source is found during scanning, or when an enhanced
    // (enhanced broadcast) source is fully characterised after PA sync + BASE data parsing.
    // For enhanced sources the metadata will contain >= 3 BISes in at least one subgroup;
    // the app should call startEnhancedBroadcastSink(metadata) in that case.
    void onSourceFound(int broadcastId, in ScanResult result);

    // Source add callbacks (PA sync)
    // isEnhanced is true when the source has >= 3 BISes in at least one subgroup
    // (enhanced broadcast duplex source); app should call startEnhancedBroadcastSink() in that case.
    void onSourceAdded(in BluetoothLeBroadcastMetadata metadata, boolean isEnhanced);
    void onSourceAddFailed(int broadcastId, int reason);

    // Source join callbacks (BIG sync)
    void onSinkStarted(int broadcastId);
    void onSinkStartFailed(in BluetoothLeBroadcastMetadata metadata, int reason);

    // Source leave callbacks (stop BIG sync)
    void onSinkStopped(int broadcastId, int reason);
    void onSinkStopFailed(int broadcastId, int reason);

    // Source remove callbacks (stop PA sync)
    void onSourceRemoved(int broadcastId, int reason);
    void onSourceRemoveFailed(int broadcastId, int reason);

    // Metadata update callbacks

}
