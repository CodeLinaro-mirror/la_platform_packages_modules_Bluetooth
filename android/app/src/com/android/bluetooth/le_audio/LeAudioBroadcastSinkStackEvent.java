/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.le_audio;

import android.bluetooth.BluetoothLeBroadcastMetadata;

/**
 * Stack event for LE Audio Broadcast Sink callbacks from native layer
 */
public class LeAudioBroadcastSinkStackEvent {
    // Event types - aligned with new API proposal
    public static final int EVENT_TYPE_SOURCE_ADD_FAILED = 1;
    public static final int EVENT_TYPE_SOURCE_JOIN_FAILED = 2;
    public static final int EVENT_TYPE_SOURCE_LEAVE_FAILED = 3;
    public static final int EVENT_TYPE_SOURCE_REMOVE_FAILED = 4;
    public static final int EVENT_TYPE_SOURCE_DESTROYED = 5;
    public static final int EVENT_TYPE_SOURCE_METADATA_CHANGED = 6;
    public static final int EVENT_TYPE_AUDIO_SESSION_CREATED = 7;
    public static final int EVENT_TYPE_STATE_CHANGED = 8;

    // Broadcast sink states (from broadcast_sink_types.h SinkState enum)
    public static final int SINK_STATE_IDLE = 0;
    public static final int SINK_STATE_PA_SYNCING = 1;
    public static final int SINK_STATE_PA_SYNCED = 2;
    public static final int SINK_STATE_BIG_SYNCING = 3;
    public static final int SINK_STATE_BIG_SYNCED = 4;
    public static final int SINK_STATE_DISABLING = 5;
    public static final int SINK_STATE_STOPPING = 6;

    // Source destroyed reason codes (from broadcast_sink_types.h)
    public static final int SOURCE_DESTROYED_REASON_NORMAL = 0x00;      // User requested
    public static final int SOURCE_DESTROYED_REASON_PA_SYNC_LOST = 0x01; // PA sync lost

    public int type;
    public int reason;
    public int broadcastId;
    public BluetoothLeBroadcastMetadata metadata;
    public int valueInt1;  // Generic integer value for various purposes

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
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", reason: ").append(reason);
                break;
            case EVENT_TYPE_SOURCE_JOIN_FAILED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", reason: ").append(reason);
                break;
            case EVENT_TYPE_SOURCE_LEAVE_FAILED:
                builder.append(", broadcastId: ").append(broadcastId)
                        .append(", reason: ").append(reason);
                break;
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
                        .append(", state: ").append(valueInt1);
                break;
        }

        builder.append("}");
        return builder.toString();
    }

    private static String eventTypeToString(int type) {
        switch (type) {
            case EVENT_TYPE_SOURCE_ADD_FAILED:
                return "EVENT_TYPE_SOURCE_ADD_FAILED";
            case EVENT_TYPE_SOURCE_JOIN_FAILED:
                return "EVENT_TYPE_SOURCE_JOIN_FAILED";
            case EVENT_TYPE_SOURCE_LEAVE_FAILED:
                return "EVENT_TYPE_SOURCE_LEAVE_FAILED";
            case EVENT_TYPE_SOURCE_REMOVE_FAILED:
                return "EVENT_TYPE_SOURCE_REMOVE_FAILED";
            case EVENT_TYPE_SOURCE_DESTROYED:
                return "EVENT_TYPE_SOURCE_DESTROYED";
            case EVENT_TYPE_SOURCE_METADATA_CHANGED:
                return "EVENT_TYPE_SOURCE_METADATA_CHANGED";
            case EVENT_TYPE_AUDIO_SESSION_CREATED:
                return "EVENT_TYPE_AUDIO_SESSION_CREATED";
            case EVENT_TYPE_STATE_CHANGED:
                return "EVENT_TYPE_STATE_CHANGED";
            default:
                return "UNKNOWN(" + type + ")";
        }
    }

    /**
     * Convert broadcast sink state to string for logging
     * @param state Broadcast sink state constant
     * @return String representation of the state
     */
    public static String sinkStateToString(int state) {
        switch (state) {
            case SINK_STATE_IDLE:
                return "IDLE";
            case SINK_STATE_PA_SYNCING:
                return "PA_SYNCING";
            case SINK_STATE_PA_SYNCED:
                return "PA_SYNCED";
            case SINK_STATE_BIG_SYNCING:
                return "BIG_SYNCING";
            case SINK_STATE_BIG_SYNCED:
                return "BIG_SYNCED";
            case SINK_STATE_DISABLING:
                return "DISABLING";
            case SINK_STATE_STOPPING:
                return "STOPPING";
            default:
                return "UNKNOWN(" + state + ")";
        }
    }

    /**
     * Convert source destroyed reason code to string for logging
     * @param reason Source destroyed reason code
     * @return String representation of the reason
     */
    private static String sourceDestroyedReasonToString(int reason) {
        switch (reason) {
            case SOURCE_DESTROYED_REASON_NORMAL:
                return "NORMAL(0x00)";
            case SOURCE_DESTROYED_REASON_PA_SYNC_LOST:
                return "PA_SYNC_LOST(0x01)";
            default:
                return "UNKNOWN(0x" + Integer.toHexString(reason) + ")";
        }
    }
}
