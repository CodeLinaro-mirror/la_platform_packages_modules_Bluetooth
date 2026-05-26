/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.le_audio;

import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.le.ScanResult;

/**
 * Stack event for LE Audio Broadcast Sink callbacks from native layer.
 *
 * <p>For enhanced (enhanced broadcast) broadcast sources the entire sequence of
 * enhanced broadcast setup → BIG_CREATE_SYNC → serial ISO data path setup is handled
 * autonomously inside the JNI C++ layer.  Java only receives the final
 * {@link #EVENT_TYPE_STATE_CHANGED} with state {@link #SINK_STATE_BIG_SYNCED}
 * once all ISO data paths have been configured.
 */
public class LeAudioBroadcastSinkStackEvent {
    // Event types
    public static final int EVENT_TYPE_SOURCE_ADD_FAILED        = 1;
    public static final int EVENT_TYPE_SOURCE_JOIN_FAILED       = 2;
    public static final int EVENT_TYPE_SOURCE_LEAVE_FAILED      = 3;
    public static final int EVENT_TYPE_SOURCE_REMOVE_FAILED     = 4;
    public static final int EVENT_TYPE_SOURCE_DESTROYED         = 5;
    public static final int EVENT_TYPE_SOURCE_METADATA_CHANGED  = 6;
    public static final int EVENT_TYPE_AUDIO_SESSION_CREATED    = 7;
    public static final int EVENT_TYPE_STATE_CHANGED            = 8;
    /**
     * Internal event: BASE data parsing revealed an enhanced (enhanced broadcast)
     * source (at least one subgroup carries &ge; 3 BISes).
     * {@code valueInt1} carries the total BIS count.
     * The service translates this into an {@code onSourceFound(metadata)} callback
     * so the app can call {@code startEnhancedBroadcastSink(metadata)}.
     */
    static final int EVENT_TYPE_ENHANCED_SOURCE_DETECTED = 9;
    /**
     * Fired when a broadcast source is found — either during scanning (partial
     * metadata built from the scan record) or when an enhanced source is fully
     * characterised after PA sync + BASE data parsing (full metadata).
     * {@code metadata} carries the {@link android.bluetooth.BluetoothLeBroadcastMetadata}.
     */
    public static final int EVENT_TYPE_SOURCE_FOUND = 10;
    /**
     * BIG sync successfully established.
     * {@code valueInt1} = bigHandle, {@code valueInt2} = number of BIS handles.
     * The BIS handle array is carried in {@code bisHandles}.
     *
     * ISO data path completion and enhanced broadcast setup completion are handled entirely
     * inside the C++ layer and are NOT forwarded to Java.
     */
    public static final int EVENT_TYPE_BIG_SYNC_CREATED = 11;
    /**
     * BIG sync lost (unexpected disconnection or controller termination).
     * {@code valueInt1} = bigHandle, {@code reason} = HCI disconnect reason.
     */
    public static final int EVENT_TYPE_BIG_SYNC_LOST = 12;
    /**
     * BIG sync intentionally terminated by the local device (user-initiated StopEnhancedBroadcastSink).
     * Fires after all TX and RX ISO data paths have been removed and the controller
     * has confirmed BIG termination.
     * {@code valueInt1} = bigHandle, {@code reason} = HCI status (0x00 = success).
     */
    public static final int EVENT_TYPE_BIG_SYNC_TERMINATED = 13;
    /**
     * DBIG status update received from the controller (kIsoEventDbigStatus).
     * {@code valueInt1} = dbig_handle, {@code valueInt2} = status.
     * Extended fields: dbigDevId, dbigName, dbigNumBis, dbigBisDevIds, dbigBroadcastFeatures.
     */
    public static final int EVENT_TYPE_DBIG_STATUS_CHANGED = 14;

    /**
     * TExitDbig complete event received.
     * Fired when HCI_VS_LE_Texit_DBIG_Complete arrives — covers both the
     * Terminate procedure (§5.3) result: success (0x00) or rejected by PGO (0x0E).
     * {@code broadcastId} = broadcast ID, {@code valueInt1} = dbig_handle,
     * {@code reason} = HCI status (0x00 = success, 0x0E = rejected).
     */
    public static final int EVENT_TYPE_TEXIT_DBIG_COMPLETE = 15;

    // DBIG status extended fields
    public int dbigDevId = 0;
    public byte[] dbigName;
    public int dbigNumBis = 0;
    public char[] dbigBisDevIds;
    public int dbigBroadcastFeatures = 0;

    // Broadcast sink states (from broadcast_sink_types.h SinkState enum)
    public static final int SINK_STATE_IDLE       = 0;
    public static final int SINK_STATE_PA_SYNCING = 1;
    public static final int SINK_STATE_PA_SYNCED  = 2;
    public static final int SINK_STATE_BIG_SYNCING = 3;
    public static final int SINK_STATE_BIG_SYNCED  = 4;
    public static final int SINK_STATE_DISABLING   = 5;
    public static final int SINK_STATE_STOPPING    = 6;

    // Source destroyed reason codes (from broadcast_sink_types.h)
    public static final int SOURCE_DESTROYED_REASON_NORMAL       = 0x00;
    public static final int SOURCE_DESTROYED_REASON_PA_SYNC_LOST = 0x01;

    public int type;
    public int reason;
    public int broadcastId;
    public BluetoothLeBroadcastMetadata metadata;
    public ScanResult scanResult;  // Raw scan result (EVENT_TYPE_SOURCE_FOUND only)
    public int valueInt1;   // Generic integer value (e.g. new state, success flag, bigHandle)
    public int valueInt2;   // Secondary integer value (e.g. number of BIS handles)
    public int[] bisHandles; // BIS connection handles (EVENT_TYPE_BIG_SYNC_CREATED only)

    LeAudioBroadcastSinkStackEvent(int type) {
        this.type = type;
    }

    @Override
    public String toString() {
        StringBuilder builder = new StringBuilder();
        builder.append("LeAudioBroadcastSinkStackEvent {type: ")
                .append(eventTypeToString(type));

        switch (type) {
            case EVENT_TYPE_SOURCE_ADD_FAILED:
            case EVENT_TYPE_SOURCE_JOIN_FAILED:
            case EVENT_TYPE_SOURCE_LEAVE_FAILED:
            case EVENT_TYPE_SOURCE_REMOVE_FAILED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", reason: ").append(reason);
                break;
            case EVENT_TYPE_SOURCE_DESTROYED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", reason: ").append(sourceDestroyedReasonToString(reason));
                break;
            case EVENT_TYPE_SOURCE_METADATA_CHANGED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", metadata: ").append(metadata);
                break;
            case EVENT_TYPE_AUDIO_SESSION_CREATED:
                builder.append(", success: ").append(valueInt1 == 1);
                break;
            case EVENT_TYPE_STATE_CHANGED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", state: ").append(sinkStateToString(valueInt1));
                break;
            case EVENT_TYPE_BIG_SYNC_CREATED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", bigHandle: ").append(valueInt1)
                        .append(", numBis: ").append(valueInt2);
                break;
            case EVENT_TYPE_BIG_SYNC_LOST:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", bigHandle: ").append(valueInt1)
                        .append(", reason: 0x")
                        .append(Integer.toHexString(reason));
                break;
            case EVENT_TYPE_BIG_SYNC_TERMINATED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", bigHandle: ").append(valueInt1)
                        .append(", status: 0x")
                        .append(Integer.toHexString(reason));
                break;
            case EVENT_TYPE_ENHANCED_SOURCE_DETECTED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", numBis: ").append(valueInt1)
                        .append(" [internal → onSourceFound]");
                break;
            case EVENT_TYPE_SOURCE_FOUND:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", scanResult: ").append(scanResult);
                break;
        }

        builder.append("}");
        return builder.toString();
    }

    private static String eventTypeToString(int type) {
        switch (type) {
            case EVENT_TYPE_SOURCE_ADD_FAILED:       return "EVENT_TYPE_SOURCE_ADD_FAILED";
            case EVENT_TYPE_SOURCE_JOIN_FAILED:      return "EVENT_TYPE_SOURCE_JOIN_FAILED";
            case EVENT_TYPE_SOURCE_LEAVE_FAILED:     return "EVENT_TYPE_SOURCE_LEAVE_FAILED";
            case EVENT_TYPE_SOURCE_REMOVE_FAILED:    return "EVENT_TYPE_SOURCE_REMOVE_FAILED";
            case EVENT_TYPE_SOURCE_DESTROYED:        return "EVENT_TYPE_SOURCE_DESTROYED";
            case EVENT_TYPE_SOURCE_METADATA_CHANGED: return "EVENT_TYPE_SOURCE_METADATA_CHANGED";
            case EVENT_TYPE_AUDIO_SESSION_CREATED:   return "EVENT_TYPE_AUDIO_SESSION_CREATED";
            case EVENT_TYPE_STATE_CHANGED:             return "EVENT_TYPE_STATE_CHANGED";
            case EVENT_TYPE_ENHANCED_SOURCE_DETECTED:  return "EVENT_TYPE_ENHANCED_SOURCE_DETECTED(internal)";
            case EVENT_TYPE_SOURCE_FOUND:              return "EVENT_TYPE_SOURCE_FOUND";
            case EVENT_TYPE_BIG_SYNC_CREATED:          return "EVENT_TYPE_BIG_SYNC_CREATED";
            case EVENT_TYPE_BIG_SYNC_LOST:             return "EVENT_TYPE_BIG_SYNC_LOST";
            case EVENT_TYPE_BIG_SYNC_TERMINATED:       return "EVENT_TYPE_BIG_SYNC_TERMINATED";
            case EVENT_TYPE_DBIG_STATUS_CHANGED:           return "EVENT_TYPE_DBIG_STATUS_CHANGED";
            case EVENT_TYPE_TEXIT_DBIG_COMPLETE:           return "EVENT_TYPE_TEXIT_DBIG_COMPLETE";
            default:                                       return "UNKNOWN(" + type + ")";
        }
    }

    /**
     * Convert broadcast sink state to string for logging.
     */
    public static String sinkStateToString(int state) {
        switch (state) {
            case SINK_STATE_IDLE:       return "IDLE";
            case SINK_STATE_PA_SYNCING: return "PA_SYNCING";
            case SINK_STATE_PA_SYNCED:  return "PA_SYNCED";
            case SINK_STATE_BIG_SYNCING: return "BIG_SYNCING";
            case SINK_STATE_BIG_SYNCED:  return "BIG_SYNCED";
            case SINK_STATE_DISABLING:   return "DISABLING";
            case SINK_STATE_STOPPING:    return "STOPPING";
            default:                     return "UNKNOWN(" + state + ")";
        }
    }

    private static String sourceDestroyedReasonToString(int reason) {
        switch (reason) {
            case SOURCE_DESTROYED_REASON_NORMAL:       return "NORMAL(0x00)";
            case SOURCE_DESTROYED_REASON_PA_SYNC_LOST: return "PA_SYNC_LOST(0x01)";
            default: return "UNKNOWN(0x" + Integer.toHexString(reason) + ")";
        }
    }
}
