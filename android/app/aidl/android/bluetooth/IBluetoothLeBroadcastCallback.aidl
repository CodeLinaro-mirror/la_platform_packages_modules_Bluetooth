/*
 *  Copyright (C) 2022 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at:
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

package android.bluetooth;

import android.bluetooth.BluetoothLeBroadcastMetadata;

/**
 * Callback definitions for interacting with Le Audio Broadcaster
 * @hide
 */
oneway interface IBluetoothLeBroadcastCallback {
    void onBroadcastStarted(in int reason, in int broadcastId);
    void onBroadcastStartFailed(in int reason);
    void onBroadcastStopped(in int reason, in int broadcastId);
    void onBroadcastStopFailed(in int reason);
    void onPlaybackStarted(in int reason, in int broadcastId);
    void onPlaybackStopped(in int reason, in int broadcastId);
    void onBroadcastUpdated(in int reason, in int broadcastId);
    void onBroadcastUpdateFailed(in int reason, in int broadcastId);
    void onBroadcastMetadataChanged(in int broadcastId, in BluetoothLeBroadcastMetadata metadata);
    void onRemoveDeviceDbigComplete(in int status, in int devId);
    /**
     * HCI_VS_LE_Texit_DBIG_Complete on PGO side.
     * status=0x00 DBIG terminated; other = error or rejected by PGO itself.
     */
    void onTexitDbigComplete(in int broadcastId, in int dbigHandle, in int status);
}
