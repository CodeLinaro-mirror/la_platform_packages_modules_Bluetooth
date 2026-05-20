/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.le_audio;

import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSinkState;
import android.bluetooth.le.ScanFilter;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;

import java.util.List;

/**
 * Native interface for LE Audio Broadcast Sink functionality
 */
public class LeAudioBroadcastSinkNativeInterface {
    private static final String TAG = "LeAudioBroadcastSinkNativeInterface";
    private static final boolean DBG = true;

    @GuardedBy("INSTANCE_LOCK")
    private static LeAudioBroadcastSinkNativeInterface sInstance;
    private static final Object INSTANCE_LOCK = new Object();


    private LeAudioBroadcastSinkNativeInterface() {}

    /**
     * Get singleton instance.
     */
    public static LeAudioBroadcastSinkNativeInterface getInstance() {
        synchronized (INSTANCE_LOCK) {
            if (sInstance == null) {
                sInstance = new LeAudioBroadcastSinkNativeInterface();
            }
            return sInstance;
        }
    }

    /**
     * Set singleton instance.
     */
    @VisibleForTesting
    public static void setInstance(LeAudioBroadcastSinkNativeInterface instance) {
        synchronized (INSTANCE_LOCK) {
            sInstance = instance;
        }
    }

    /**
     * Initialize the native interface
     *
     * @param maxSourceCapacity Maximum number of active synced broadcast sources
     */
    public void init(int maxSourceCapacity) {
        if (DBG) Log.d(TAG, "init(): maxSourceCapacity=" + maxSourceCapacity);
        initializeNative(maxSourceCapacity);
    }

    /**
     * Cleanup the native interface
     */
    public void cleanup() {
        if (DBG) Log.d(TAG, "cleanup()");
        cleanupNative();
    }

    /**
     * Add a broadcast source (PA sync only)
     *
     * @param address Bluetooth address of the broadcast source
     * @param addressType Address type (public/random)
     * @param advSid Advertising SID
     * @param broadcastId Broadcast ID
     * @param rssi RSSI value
     * @param broadcastName Broadcast name (null if not available)
     * @param isPublic Whether the broadcast is public
     * @param publicMetadata Public broadcast metadata (raw LTV bytes, null if not available)
     * @param publicFeatures Public broadcast features (audio config quality bits)
     */
    public void addSource(String address, int addressType, int advSid, int broadcastId, int rssi,
                         String broadcastName, boolean isPublic, byte[] publicMetadata, int publicFeatures) {
        if (DBG) Log.d(TAG, "addSource(): address=" + address + ", addressType=" + addressType +
                ", advSid=" + advSid + ", broadcastId=" + broadcastId + ", rssi=" + rssi +
                ", broadcastName=" + broadcastName + ", isPublic=" + isPublic +
                ", publicMetadata=" + (publicMetadata != null ? publicMetadata.length + " bytes" : "null") +
                ", publicFeatures=0x" + Integer.toHexString(publicFeatures));
        addSourceNative(address, addressType, advSid, broadcastId, rssi, broadcastName, isPublic,
                       publicMetadata, publicFeatures);
    }

    /**
     * Join a broadcast source (BIG sync)
     *
     * @param broadcastId Broadcast ID to join
     * @param broadcastCode Broadcast code for encrypted broadcasts (can be null for unencrypted)
     * @param bisIndices Array of BIS indices to sync to (empty array means sync to all BISes)
     */
    public void joinSource(int broadcastId, byte[] broadcastCode, int[] bisIndices) {
        if (DBG) Log.d(TAG, "joinSource(): broadcastId=" + broadcastId +
                       ", broadcastCode=" + (broadcastCode != null ? "provided" : "null") +
                       ", bisIndices=" + java.util.Arrays.toString(bisIndices));
        joinSourceNative(broadcastId, broadcastCode, bisIndices);
    }

    /**
     * Leave a broadcast source (stop BIG sync, keep PA sync)
     *
     * @param broadcastId Broadcast ID to leave
     */
    public void leaveSource(int broadcastId) {
        if (DBG) Log.d(TAG, "leaveSource(): broadcastId=" + broadcastId);
        leaveSourceNative(broadcastId);
    }

    /**
     * Remove a broadcast source (stop PA sync)
     *
     * @param broadcastId Broadcast ID to remove
     */
    public void removeSource(int broadcastId) {
        if (DBG) Log.d(TAG, "removeSource(): broadcastId=" + broadcastId);
        removeSourceNative(broadcastId);
    }

    /**
     * Destroy a broadcast source (cleanup resources)
     *
     * @param broadcastId Broadcast ID to destroy
     */
    public void destroySource(int broadcastId) {
        if (DBG) Log.d(TAG, "destroySource(): broadcastId=" + broadcastId);
        destroySourceNative(broadcastId);
    }

    /**
     * Notify that source metadata has changed
     *
     * @param broadcastId Broadcast ID with changed metadata
     * @param broadcastName Updated broadcast name (can be null)
     * @param publicMetadata Updated public broadcast metadata (raw LTV bytes, can be null)
     */
    public void sourcePublicMetadataChanged(int broadcastId, String broadcastName, byte[] publicMetadata) {
        if (DBG) Log.d(TAG, "sourcePublicMetadataChanged(): broadcastId=" + broadcastId +
                       ", broadcastName=" + broadcastName +
                       ", publicMetadata=" + (publicMetadata != null ? publicMetadata.length + " bytes" : "null"));
        sourcePublicMetadataChangedNative(broadcastId, broadcastName, publicMetadata);
    }

    /**
     * Get all synced sink states
     *
     * @return Array of broadcast sink states for all synced broadcasts
     */
    public BluetoothLeBroadcastSinkState[] getAllSyncedSinkState() {
        if (DBG) Log.d(TAG, "getAllSyncedSinkState()");
        return getAllSyncedSinkStateNative();
    }

    /**
     * Get source metadata for a specific broadcast
     * Triggers onSourceMetadataChanged callback with the metadata
     *
     * @param broadcastId Broadcast ID
     */
    public void getSourceMetadata(int broadcastId) {
        if (DBG) Log.d(TAG, "getSourceMetadata(): broadcastId=" + broadcastId);
        getSourceMetadataNative(broadcastId);
    }

    // Callbacks from native layer - create stack events and send to service

    /**
     * Called when adding a broadcast source failed
     * @param broadcastId Broadcast ID that failed to add
     * @param reason Failure reason code (native code from broadcast_sink_types.h)
     */
    public void onSourceAddFailed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSourceAddFailed(): broadcastId=" + broadcastId + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event =
                new LeAudioBroadcastSinkStackEvent(LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_ADD_FAILED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /**
     * Called when joining a broadcast source failed (BIG sync failed)
     * @param broadcastId Broadcast ID that failed to join
     * @param reason Failure reason code (native code from broadcast_sink_types.h)
     */
    public void onSourceJoinFailed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSourceJoinFailed(): broadcastId=" + broadcastId + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event =
                new LeAudioBroadcastSinkStackEvent(LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_JOIN_FAILED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /**
     * Called when leaving a broadcast source failed
     * @param broadcastId Broadcast ID that failed to leave
     * @param reason Failure reason code (native code from broadcast_sink_types.h)
     */
    public void onSourceLeaveFailed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSourceLeaveFailed(): broadcastId=" + broadcastId + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event =
                new LeAudioBroadcastSinkStackEvent(LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_LEAVE_FAILED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /**
     * Called when a broadcast source has been destroyed (all resources freed)
     * @param broadcastId Broadcast ID that was destroyed
     * @param reason Reason code for destruction (0x00=normal/user requested, 0x01=PA sync lost)
     */
    public void onSourceDestroyed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSourceDestroyed(): broadcastId=" + broadcastId + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event =
                new LeAudioBroadcastSinkStackEvent(LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_DESTROYED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /**
     * Called when removing a broadcast source failed
     * @param broadcastId Broadcast ID that failed to remove
     * @param reason Failure reason code (native code from broadcast_sink_types.h)
     */
    public void onSourceRemoveFailed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSourceRemoveFailed(): broadcastId=" + broadcastId + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event =
                new LeAudioBroadcastSinkStackEvent(LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_REMOVE_FAILED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /**
     * Called when broadcast metadata is changed by the source
     * @param broadcastId Broadcast ID with updated metadata
     * @param metadata Updated broadcast metadata
     */
    public void onSourceMetadataChanged(int broadcastId, BluetoothLeBroadcastMetadata metadata) {
        if (DBG) Log.d(TAG, "onSourceMetadataChanged(): broadcastId=" + broadcastId + ", metadata=" + metadata);
        LeAudioBroadcastSinkStackEvent event =
                new LeAudioBroadcastSinkStackEvent(LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_METADATA_CHANGED);
        event.broadcastId = broadcastId;
        event.metadata = metadata;
        sendMessageToService(event);
    }

    /**
     * Called when audio session is created for broadcast sink
     * @param success Whether session creation succeeded
     */
    public void onBroadcastSinkAudioSessionCreated(boolean success) {
        if (DBG) Log.d(TAG, "onBroadcastSinkAudioSessionCreated(): success=" + success);
        LeAudioBroadcastSinkStackEvent event =
                new LeAudioBroadcastSinkStackEvent(LeAudioBroadcastSinkStackEvent.EVENT_TYPE_AUDIO_SESSION_CREATED);
        event.valueInt1 = success ? 1 : 0;
        sendMessageToService(event);
    }

    /**
     * Called when broadcast sink state changes
     * @param broadcastId Broadcast ID with state change
     * @param state New state (raw SinkState ordinal from native)
     */
    public void onBroadcastSinkStateChanged(int broadcastId, int state) {
        if (DBG) Log.d(TAG, "onBroadcastSinkStateChanged(): broadcastId=" + broadcastId
                + ", state=" + LeAudioBroadcastSinkStackEvent.sinkStateToString(state));
        LeAudioBroadcastSinkStackEvent event =
                new LeAudioBroadcastSinkStackEvent(LeAudioBroadcastSinkStackEvent.EVENT_TYPE_STATE_CHANGED);
        event.broadcastId = broadcastId;
        event.valueInt1 = state;
        sendMessageToService(event);
    }

    private static void sendMessageToService(LeAudioBroadcastSinkStackEvent event) {
        LeAudioBroadcastSinkService service = LeAudioBroadcastSinkService.getLeAudioBroadcastSinkService();
        if (service != null) {
            service.messageFromNative(event);
        } else {
            Log.w(TAG, "Event ignored, service not available: " + event);
        }
    }

    // Native methods
    private native void initializeNative(int maxSourceCapacity);
    private native void cleanupNative();
    private native void addSourceNative(String address, int addressType, int advSid, int broadcastId, int rssi,
                                       String broadcastName, boolean isPublic, byte[] publicMetadata, int publicFeatures);
    private native void joinSourceNative(int broadcastId, byte[] broadcastCode, int[] bisIndices);
    private native void leaveSourceNative(int broadcastId);
    private native void removeSourceNative(int broadcastId);
    private native void destroySourceNative(int broadcastId);
    private native void sourcePublicMetadataChangedNative(int broadcastId, String broadcastName, byte[] publicMetadata);
    private native BluetoothLeBroadcastSinkState[] getAllSyncedSinkStateNative();
    private native void getSourceMetadataNative(int broadcastId);
}
