/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.le_audio;

import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSinkState;
import android.util.Log;

import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;

/**
 * Native interface for LE Audio Broadcast Sink functionality.
 *
 * <h3>Enhanced (enhanced broadcast) broadcast sources</h3>
 * <p>For duplex broadcast sources call {@link #startEnhancedBroadcastSink} instead of
 * The JNI C++ layer autonomously executes the full
 * sequence:
 * <ol>
 *   <li>Sends the vendor enhanced broadcast setup HCI command.</li>
 *   <li>On success, issues BIG_CREATE_SYNC for <em>all</em> BISes in the BIG.</li>
 *   <li>Once BIG sync is established, sets up ISO data paths serially for every
 *       BIS handle — first RX (receive from source), then TX (send to source).</li>
 *   <li>After all ISO data paths are configured, fires
 *       {@link #onBroadcastSinkStateChanged} with state
 *       {@link LeAudioBroadcastSinkStackEvent#SINK_STATE_BIG_SYNCED}.</li>
 * </ol>
 * Java never sees intermediate enhanced broadcast / ISO events; it only receives the final
 * BIG_SYNCED state change.
 */
public class LeAudioBroadcastSinkNativeInterface {
    private static final String TAG = "LeAudioBroadcastSinkNativeInterface";
    private static final boolean DBG = true;

    @GuardedBy("INSTANCE_LOCK")
    private static LeAudioBroadcastSinkNativeInterface sInstance;
    private static final Object INSTANCE_LOCK = new Object();

    private LeAudioBroadcastSinkNativeInterface() {}

    /** Get singleton instance. */
    public static LeAudioBroadcastSinkNativeInterface getInstance() {
        synchronized (INSTANCE_LOCK) {
            if (sInstance == null) {
                sInstance = new LeAudioBroadcastSinkNativeInterface();
            }
            return sInstance;
        }
    }

    /** Set singleton instance (test use only). */
    @VisibleForTesting
    public static void setInstance(LeAudioBroadcastSinkNativeInterface instance) {
        synchronized (INSTANCE_LOCK) {
            sInstance = instance;
        }
    }

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    /**
     * Initialize the native interface.
     *
     * @param maxSourceCapacity Maximum number of simultaneously PA-synced sources
     */
    public void init(int maxSourceCapacity) {
        if (DBG) Log.d(TAG, "init(): maxSourceCapacity=" + maxSourceCapacity);
        initializeNative(maxSourceCapacity);
    }

    /** Cleanup the native interface. */
    public void cleanup() {
        if (DBG) Log.d(TAG, "cleanup()");
        cleanupNative();
    }

    // -------------------------------------------------------------------------
    // Source management
    // -------------------------------------------------------------------------

    /**
     * Add a broadcast source (PA sync only).
     *
     * @param address        Bluetooth address of the broadcast source
     * @param addressType    Address type (public / random)
     * @param advSid         Advertising SID
     * @param broadcastId    Broadcast ID
     * @param rssi           RSSI value
     * @param broadcastName  Broadcast name (null if not available)
     * @param isPublic       Whether the broadcast is public
     * @param publicMetadata Public broadcast metadata (raw LTV bytes, null if not available)
     * @param publicFeatures Public broadcast features (audio config quality bits)
     */
    public void addSource(String address, int addressType, int advSid, int broadcastId, int rssi,
                          String broadcastName, boolean isPublic,
                          byte[] publicMetadata, int publicFeatures) {
        if (DBG) Log.d(TAG, "addSource(): address=" + address
                + ", broadcastId=" + broadcastId + ", isPublic=" + isPublic);
        addSourceNative(address, addressType, advSid, broadcastId, rssi,
                broadcastName, isPublic, publicMetadata, publicFeatures);
    }
    /**
     * Join an enhanced (enhanced broadcast) broadcast source.
     *
     * <p>Enhanced broadcast always syncs to <em>all</em> BISes in the BIG —
     * no BIS selection is supported.  The JNI C++ layer autonomously executes
     * the full enhanced broadcast setup → BIG_CREATE_SYNC → serial ISO data path setup
     * sequence.  Java receives {@link #onBroadcastSinkStateChanged} with
     * {@link LeAudioBroadcastSinkStackEvent#SINK_STATE_BIG_SYNCED} only after
     * every ISO data path has been configured.
     *
     * <p>Should be called after receiving
     * {@link #onEnhancedSourceDetected(int, int)}.
     *
     * @param broadcastId   Broadcast ID of the enhanced source
     * @param broadcastCode Broadcast code for encrypted sources (null for unencrypted)
     */
    public void startEnhancedBroadcastSink(int broadcastId, byte[] broadcastCode) {
        if (DBG) Log.d(TAG, "startEnhancedBroadcastSink(): broadcastId=" + broadcastId
                + ", broadcastCode=" + (broadcastCode != null ? "provided" : "null"));
        startEnhancedBroadcastSinkNative(broadcastId, broadcastCode);
    }

    /**
     * Leave a broadcast source (stop BIG sync, keep PA sync).
     *
     * @param broadcastId Broadcast ID to leave
     */
    public void stopEnhancedBroadcastSink(int broadcastId) {
        if (DBG) Log.d(TAG, "stopEnhancedBroadcastSink(): broadcastId=" + broadcastId);
        stopEnhancedBroadcastSinkNative(broadcastId);
    }

    /**
     * Remove a broadcast source (stop PA sync).
     *
     * @param broadcastId Broadcast ID to remove
     */
    public void removeSource(int broadcastId) {
        if (DBG) Log.d(TAG, "removeSource(): broadcastId=" + broadcastId);
        removeSourceNative(broadcastId);
    }

    /**
     * Destroy a broadcast source (cleanup all native resources).
     *
     * @param broadcastId Broadcast ID to destroy
     */
    public void destroySource(int broadcastId) {
        if (DBG) Log.d(TAG, "destroySource(): broadcastId=" + broadcastId);
        destroySourceNative(broadcastId);
    }

    /**
     * Notify the native layer that the source's public metadata has changed
     * (detected via a new scan result).
     *
     * @param broadcastId    Broadcast ID with changed metadata
     * @param broadcastName  Updated broadcast name (null if unchanged / unavailable)
     * @param publicMetadata Updated public broadcast metadata (raw LTV bytes, null if unavailable)
     */
    public void sourcePublicMetadataChanged(int broadcastId,
                                            String broadcastName,
                                            byte[] publicMetadata) {
        if (DBG) Log.d(TAG, "sourcePublicMetadataChanged(): broadcastId=" + broadcastId
                + ", broadcastName=" + broadcastName
                + ", publicMetadata="
                + (publicMetadata != null ? publicMetadata.length + " bytes" : "null"));
        sourcePublicMetadataChangedNative(broadcastId, broadcastName, publicMetadata);
    }
    // -------------------------------------------------------------------------
    // Callbacks from native layer → Java service
    // -------------------------------------------------------------------------

    /** Called when adding a broadcast source (PA sync) failed. */
    public void onSourceAddFailed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSourceAddFailed(): broadcastId=" + broadcastId
                + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_ADD_FAILED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /** Called when joining a broadcast source (BIG sync) failed. */
    public void onSinkStartFailed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSinkStartFailed(): broadcastId=" + broadcastId
                + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_JOIN_FAILED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /** Called when leaving a broadcast source failed. */
    public void onSinkStopFailed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSinkStopFailed(): broadcastId=" + broadcastId
                + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_LEAVE_FAILED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /** Called when removing a broadcast source failed. */
    public void onSourceRemoveFailed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSourceRemoveFailed(): broadcastId=" + broadcastId
                + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_REMOVE_FAILED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /**
     * Called when a broadcast source has been fully destroyed (all resources freed).
     *
     * @param broadcastId Broadcast ID that was destroyed
     * @param reason      0x00 = normal / user requested, 0x01 = PA sync lost
     */
    public void onSourceDestroyed(int broadcastId, int reason) {
        if (DBG) Log.d(TAG, "onSourceDestroyed(): broadcastId=" + broadcastId
                + ", reason=" + reason);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_DESTROYED);
        event.broadcastId = broadcastId;
        event.reason = reason;
        sendMessageToService(event);
    }

    /** Called when broadcast source metadata changes (PA announcement updated). */
    public void onSourceMetadataChanged(int broadcastId,
                                        BluetoothLeBroadcastMetadata metadata) {
        if (DBG) Log.d(TAG, "onSourceMetadataChanged(): broadcastId=" + broadcastId);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_METADATA_CHANGED);
        event.broadcastId = broadcastId;
        event.metadata = metadata;
        sendMessageToService(event);
    }

    /** Called when the broadcast sink audio session is created. */
    public void onBroadcastSinkAudioSessionCreated(boolean success) {
        if (DBG) Log.d(TAG, "onBroadcastSinkAudioSessionCreated(): success=" + success);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_AUDIO_SESSION_CREATED);
        event.valueInt1 = success ? 1 : 0;
        sendMessageToService(event);
    }

    /**
     * Called when a broadcast source is found during scanning.
     * Fires {@link LeAudioBroadcastSinkStackEvent#EVENT_TYPE_SOURCE_FOUND} with
     * the raw {@link android.bluetooth.le.ScanResult} so the service can notify
     * registered callbacks.
     *
     * @param broadcastId Broadcast ID of the found source
     * @param result      Raw {@link android.bluetooth.le.ScanResult} from the BLE scanner
     */
    public void onSourceFound(int broadcastId, android.bluetooth.le.ScanResult result) {
        if (DBG) Log.d(TAG, "onSourceFound(): broadcastId=" + broadcastId);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_FOUND);
        event.broadcastId = broadcastId;
        event.scanResult = result;
        sendMessageToService(event);
    }

    /**
     * Internal callback: BASE data parsing revealed an enhanced (enhanced broadcast)
     * source.  The service translates this into an {@code onSourceFound(metadata)}
     * app callback so the app can call {@code startEnhancedBroadcastSink(metadata)}.
     *
     * @param broadcastId Broadcast ID of the enhanced source
     * @param numBis      Total number of BISes detected in the subgroup
     */
    void onEnhancedSourceDetected(int broadcastId, int numBis) {
        if (DBG) Log.d(TAG, "onEnhancedSourceDetected() [internal]: broadcastId=" + broadcastId
                + ", numBis=" + numBis);
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_ENHANCED_SOURCE_DETECTED);
        event.broadcastId = broadcastId;
        event.valueInt1 = numBis;
        sendMessageToService(event);
    }

    /**
     * Called when BIG sync is successfully established.
     *
     * <p>ISO data path completion and enhanced broadcast setup completion are handled
     * entirely inside the C++ layer and are <em>not</em> forwarded to Java.
     * This callback is the first (and only) ISO-layer event Java receives
     * for a given BIG sync establishment.
     *
     * @param broadcastId Broadcast ID
     * @param bigHandle   BIG handle assigned by the controller
     * @param bisHandles  Connection handles for each BIS in the BIG
     */
    public void onBigSyncCreated(int broadcastId, int bigHandle, int[] bisHandles) {
        if (DBG) Log.d(TAG, "onBigSyncCreated(): broadcastId=" + broadcastId
                + ", bigHandle=" + bigHandle
                + ", numBis=" + (bisHandles != null ? bisHandles.length : 0));
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_BIG_SYNC_CREATED);
        event.broadcastId = broadcastId;
        event.valueInt1 = bigHandle;
        event.valueInt2 = (bisHandles != null) ? bisHandles.length : 0;
        event.bisHandles = bisHandles;
        sendMessageToService(event);
    }

    /**
     * Called when BIG sync is lost (unexpected disconnection or controller
     * termination).
     *
     * @param broadcastId Broadcast ID
     * @param bigHandle   BIG handle that was lost
     * @param reason      HCI disconnect reason code
     */
    public void onBigSyncLost(int broadcastId, int bigHandle, int reason) {
        if (DBG) Log.d(TAG, "onBigSyncLost(): broadcastId=" + broadcastId
                + ", bigHandle=" + bigHandle
                + ", reason=0x" + Integer.toHexString(reason));
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_BIG_SYNC_LOST);
        event.broadcastId = broadcastId;
        event.valueInt1 = bigHandle;
        event.reason = reason;
        sendMessageToService(event);
    }

    /**
     * Called when BIG sync is intentionally terminated by the local device
     * (user-initiated StopEnhancedBroadcastSink).  Fires after all TX and RX ISO data paths
     * have been removed and the controller has confirmed BIG termination.
     *
     * @param broadcastId Broadcast ID
     * @param bigHandle   BIG handle that was terminated
     * @param status      HCI status code (0x00 = success)
     */
    public void onBigSyncTerminated(int broadcastId, int bigHandle, int status) {
        if (DBG) Log.d(TAG, "onBigSyncTerminated(): broadcastId=" + broadcastId
                + ", bigHandle=" + bigHandle
                + ", status=0x" + Integer.toHexString(status));
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_BIG_SYNC_TERMINATED);
        event.broadcastId = broadcastId;
        event.valueInt1 = bigHandle;
        event.reason = status;
        sendMessageToService(event);
    }

    /**
     * Called when a DBIG status update event is received from the controller.
     * Carries only the BIG handle and status — no other parameters.
     *
     * @param dbigHandle BIG handle from the controller event
     * @param status     DBIG status value from the controller event
     */
    public void onDbigStatusChanged(int dbigHandle, int status) {
        if (DBG) Log.d(TAG, "onDbigStatusChanged(): dbigHandle=" + dbigHandle
                + ", status=0x" + Integer.toHexString(status));
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_DBIG_STATUS_CHANGED);
        event.valueInt1 = dbigHandle;
        event.valueInt2 = status;
        sendMessageToService(event);
    }

    /**
     * Called when the broadcast sink state changes.
     *
     * <p>For enhanced (enhanced broadcast) sources this is fired by the JNI C++ layer only
     * after all ISO data paths have been configured, so Java sees
     * {@link LeAudioBroadcastSinkStackEvent#SINK_STATE_BIG_SYNCED} as the
     * completion signal for the entire enhanced-join sequence.
     *
     * @param broadcastId Broadcast ID whose state changed
     * @param state       New state (raw SinkState ordinal from native)
     */
    public void onBroadcastSinkStateChanged(int broadcastId, int state) {
        if (DBG) Log.d(TAG, "onBroadcastSinkStateChanged(): broadcastId=" + broadcastId
                + ", state=" + LeAudioBroadcastSinkStackEvent.sinkStateToString(state));
        LeAudioBroadcastSinkStackEvent event = new LeAudioBroadcastSinkStackEvent(
                LeAudioBroadcastSinkStackEvent.EVENT_TYPE_STATE_CHANGED);
        event.broadcastId = broadcastId;
        event.valueInt1 = state;
        sendMessageToService(event);
    }

    // -------------------------------------------------------------------------
    // Internal helpers
    // -------------------------------------------------------------------------

    private static void sendMessageToService(LeAudioBroadcastSinkStackEvent event) {
        LeAudioBroadcastSinkService service =
                LeAudioBroadcastSinkService.getLeAudioBroadcastSinkService();
        if (service != null) {
            service.messageFromNative(event);
        } else {
            Log.w(TAG, "Event ignored, service not available: " + event);
        }
    }

    // -------------------------------------------------------------------------
    // Native method declarations
    // -------------------------------------------------------------------------

    private native void initializeNative(int maxSourceCapacity);
    private native void cleanupNative();
    private native void addSourceNative(String address, int addressType, int advSid,
                                        int broadcastId, int rssi, String broadcastName,
                                        boolean isPublic, byte[] publicMetadata,
                                        int publicFeatures);
    /**
     * Triggers the full enhanced broadcast → BIG_CREATE_SYNC → serial ISO data path setup
     * sequence inside the JNI C++ layer for <em>all</em> BISes.
     * Java receives only the final BIG_SYNCED state-change callback.
     * Enhanced broadcast always syncs to all BISes — no BIS selection needed.
     */
    private native void startEnhancedBroadcastSinkNative(int broadcastId, byte[] broadcastCode);
    private native void stopEnhancedBroadcastSinkNative(int broadcastId);
    private native void removeSourceNative(int broadcastId);
    private native void destroySourceNative(int broadcastId);
    private native void sourcePublicMetadataChangedNative(int broadcastId,
                                                          String broadcastName,
                                                          byte[] publicMetadata);
}
