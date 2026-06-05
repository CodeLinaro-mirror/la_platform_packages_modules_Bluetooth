/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.annotation.FlaggedApi;
import android.annotation.IntDef;
import android.annotation.NonNull;
import android.annotation.SystemApi;
import android.os.Parcel;
import android.os.Parcelable;

import com.android.bluetooth.flags.Flags;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/**
 * Represents the state of a Broadcast Sink for a given Broadcast Source.
 *
 * <p>This class provides detailed information about the sink's current state,
 * including synchronization status, BIS sync state, and subgroup metadata.
 *
 * @hide
 */
@FlaggedApi(Flags.FLAG_LEAUDIO_BROADCAST_SINK_API)
@SystemApi
public final class BluetoothLeBroadcastSinkState implements Parcelable {

    /**
     * The overall state of the Broadcast Sink for a given source.
     * <p>This directly maps to the internal sink state machine.
     *
     * @hide
     */
    @IntDef(prefix = "SINK_STATE_", value = {
            SINK_STATE_IDLE,
            SINK_STATE_SYNCING_PA,
            SINK_STATE_SYNCED_PA,
            SINK_STATE_JOINING_BROADCAST,
            SINK_STATE_RECEIVING_BROADCAST,
            SINK_STATE_LEAVING_BROADCAST,
            SINK_STATE_UPDATING_BROADCAST,
    })
    @Retention(RetentionPolicy.SOURCE)
    public @interface SinkState {}

    /** The sink is idle and not connected to any broadcast. */
    public static final int SINK_STATE_IDLE = 0;

    /** The sink is trying to synchronize with the Periodic Advertisements of a source. */
    public static final int SINK_STATE_SYNCING_PA = 1;

    /** The sink has successfully synchronized with the Periodic Advertisements. */
    public static final int SINK_STATE_SYNCED_PA = 2;

    /** The sink is joining the broadcast group (BIG) to receive streaming. */
    public static final int SINK_STATE_JOINING_BROADCAST = 3;

    /** The sink is actively receiving audio from the broadcast. */
    public static final int SINK_STATE_RECEIVING_BROADCAST = 4;

    /** The sink is leaving from the broadcast. */
    public static final int SINK_STATE_LEAVING_BROADCAST = 5;

    /** The sink is updating broadcast metadata. */
    public static final int SINK_STATE_UPDATING_BROADCAST = 6;

    /**
     * Reason codes for Broadcast Sink state changes.
     *
     * @hide
     */
    @Retention(RetentionPolicy.SOURCE)
    @IntDef(prefix = "REASON_", value = {
        REASON_UNKNOWN,
        REASON_LOCAL_APP_REQUEST,
        REASON_LOCAL_STACK_REQUEST,
        REASON_SYSTEM_POLICY,
        REASON_HARDWARE_GENERIC,
        REASON_BAD_PARAMETERS,
        REASON_PA_SYNC_ESTABLISHED,
        REASON_PA_SYNC_FAILED,
        REASON_PA_SYNC_LOST,
        REASON_BIG_SYNC_ESTABLISHED,
        REASON_ENCRYPTION_FAILED_BAD_CODE,
        REASON_ENCRYPTION_FAILED_NO_KEY,
        REASON_BIG_SYNC_FAILED,
        REASON_BIG_SYNC_LOST,
        REASON_BIG_SYNC_LOST_REMOTE_TERMINATED,
        REASON_BIG_SYNC_LOST_TIMEOUT,
        REASON_DUPLICATE_ADD_REQUEST,
        REASON_DUPLICATE_JOIN_REQUEST,
        REASON_MAX_PA_SYNC_REACHED,
        REASON_MAX_BIG_SYNC_REACHED,
    })
    public @interface Reason {}

    /** An unknown reason for the state change. */
    public static final int REASON_UNKNOWN = BluetoothStatusCodes.ERROR_UNKNOWN;

    /** The operation was triggered by a local application request. */
    public static final int REASON_LOCAL_APP_REQUEST = BluetoothStatusCodes.REASON_LOCAL_APP_REQUEST;

    /** Indicate that this change was initiated by the Bluetooth implementation on this device. */
    public static final int REASON_LOCAL_STACK_REQUEST = BluetoothStatusCodes.REASON_LOCAL_STACK_REQUEST;

    /**
     * Indicates that the local system policy caused the change, such as
     * privacy policy, power management policy, permission changes, and more.
     */
    public static final int REASON_SYSTEM_POLICY = BluetoothStatusCodes.REASON_SYSTEM_POLICY;

    /**
     * Indicates that an underlying hardware incurred some error maybe try
     * again later or toggle the hardware state.
     */
    public static final int REASON_HARDWARE_GENERIC = BluetoothStatusCodes.ERROR_HARDWARE_GENERIC;

    /**
     * Indicates that the operation failed due to bad API input parameter
     * that is not covered by other more detailed error code.
     */
    public static final int REASON_BAD_PARAMETERS = BluetoothStatusCodes.ERROR_BAD_PARAMETERS;

    /** Synchronization to the Periodic Advertising train succeeded. */
    public static final int REASON_PA_SYNC_ESTABLISHED = 100;

    /** Synchronization to the Periodic Advertising train failed. */
    public static final int REASON_PA_SYNC_FAILED = 101;

    /** Synchronization to the Periodic Advertising train was lost. */
    public static final int REASON_PA_SYNC_LOST = 102;

    /** Synchronization to the Broadcast Isochronous Group succeeded. */
    public static final int REASON_BIG_SYNC_ESTABLISHED = 103;

    /** Encryption failed due to an incorrect broadcast code. */
    public static final int REASON_ENCRYPTION_FAILED_BAD_CODE = 104;

    /** Encryption failed because no broadcast code was provided. */
    public static final int REASON_ENCRYPTION_FAILED_NO_KEY = 105;

    /** Synchronization to the Broadcast Isochronous Group failed. */
    public static final int REASON_BIG_SYNC_FAILED = 106;

    /** Synchronization to the Broadcast Isochronous Group was lost. */
    public static final int REASON_BIG_SYNC_LOST = 107;

    /**
     * Reason code indicating that the operation failed because the Broadcast Source
     * has already been added.
     */
    public static final int REASON_DUPLICATE_ADD_REQUEST = 108;

    /**
     * Reason code indicating that the operation failed because the Broadcast Source
     * has already been joined.
     */
    public static final int REASON_DUPLICATE_JOIN_REQUEST = 109;

    /**
     * Reason code indicating that the addSource failed because the Broadcast sink
     * has reached the maximum broadcast source it can add.
     */
    public static final int REASON_MAX_PA_SYNC_REACHED = 110;

    /**
     * Reason code indicating that the joinSource failed because the Broadcast sink
     * has reached the maximum broadcast source it can join.
     */
    public static final int REASON_MAX_BIG_SYNC_REACHED = 111;

    /**
     * BIG sync was lost because the PGO (broadcast source) explicitly terminated
     * the BIG via HCI_LE_Terminate_BIG.
     * HCI disconnect reason: 0x13 (Remote User Terminated Connection).
     */
    public static final int REASON_BIG_SYNC_LOST_REMOTE_TERMINATED = 112;

    /**
     * BIG sync was lost because the PGO moved out of range or the link was lost.
     * HCI disconnect reason: 0x08 (Connection Timeout).
     */
    public static final int REASON_BIG_SYNC_LOST_TIMEOUT = 113;

    private final int mBroadcastId;
    private final @SinkState int mSinkState;
    private final int mNumSubgroups;
    private final List<Long> mBisSyncState;
    private final List<BluetoothLeAudioContentMetadata> mSubgroupMetadata;

    private BluetoothLeBroadcastSinkState(
            int broadcastId,
            @SinkState int sinkState,
            int numSubgroups,
            @NonNull List<Long> bisSyncState,
            @NonNull List<BluetoothLeAudioContentMetadata> subgroupMetadata) {
        mBroadcastId = broadcastId;
        mSinkState = sinkState;
        mNumSubgroups = numSubgroups;
        mBisSyncState = new ArrayList<>(bisSyncState);
        mSubgroupMetadata = new ArrayList<>(subgroupMetadata);
    }

    /**
     * Get the broadcast ID.
     *
     * @return the broadcast ID
     * @hide
     */
    @SystemApi
    public int getBroadcastId() {
        return mBroadcastId;
    }

    /**
     * Get the current sink state.
     *
     * @return the sink state
     * @hide
     */
    @SystemApi
    public @SinkState int getSinkState() {
        return mSinkState;
    }

    /**
     * Get the BIS sync state for each subgroup.
     *
     * <p>Each Long value represents a bitmask of synchronized BIS indices for that subgroup.
     *
     * @return list of BIS sync states
     * @hide
     */
    @SystemApi
    @NonNull
    public List<Long> getBisSyncStates() {
        return new ArrayList<>(mBisSyncState);
    }

    /**
     * Get the number of subgroups.
     *
     * @return the number of subgroups
     * @hide
     */
    @SystemApi
    public int getNumSubgroups() {
        return mNumSubgroups;
    }

    /**
     * Get the metadata for each subgroup.
     *
     * @return list of subgroup metadata
     * @hide
     */
    @SystemApi
    @NonNull
    public List<BluetoothLeAudioContentMetadata> getSubgroupMetadatas() {
        return new ArrayList<>(mSubgroupMetadata);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof BluetoothLeBroadcastSinkState)) return false;
        BluetoothLeBroadcastSinkState that = (BluetoothLeBroadcastSinkState) o;
        return mBroadcastId == that.mBroadcastId
                && mSinkState == that.mSinkState
                && mNumSubgroups == that.mNumSubgroups
                && Objects.equals(mBisSyncState, that.mBisSyncState)
                && Objects.equals(mSubgroupMetadata, that.mSubgroupMetadata);
    }

    @Override
    public int hashCode() {
        return Objects.hash(mBroadcastId, mSinkState, mNumSubgroups, mBisSyncState, mSubgroupMetadata);
    }

    @Override
    public String toString() {
        return "BluetoothLeBroadcastSinkState{"
                + "broadcastId=" + mBroadcastId
                + ", sinkState=" + sinkStateToString(mSinkState)
                + ", numSubgroups=" + mNumSubgroups
                + ", bisSyncState=" + mBisSyncState
                + ", subgroupMetadata=" + mSubgroupMetadata
                + '}';
    }

    /**
     * Convert sink state to string for debugging.
     *
     * @param state the sink state
     * @return string representation
     */
    private static String sinkStateToString(@SinkState int state) {
        switch (state) {
            case SINK_STATE_IDLE: return "IDLE";
            case SINK_STATE_SYNCING_PA: return "SYNCING_PA";
            case SINK_STATE_SYNCED_PA: return "SYNCED_PA";
            case SINK_STATE_JOINING_BROADCAST: return "JOINING_BROADCAST";
            case SINK_STATE_RECEIVING_BROADCAST: return "RECEIVING_BROADCAST";
            case SINK_STATE_LEAVING_BROADCAST: return "LEAVING_BROADCAST";
            case SINK_STATE_UPDATING_BROADCAST: return "UPDATING_BROADCAST";
            default: return "UNKNOWN(" + state + ")";
        }
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(@NonNull Parcel dest, int flags) {
        dest.writeInt(mBroadcastId);
        dest.writeInt(mSinkState);
        dest.writeInt(mNumSubgroups);
        dest.writeInt(mBisSyncState.size());
        for (Long bisSyncState : mBisSyncState) {
            dest.writeLong(bisSyncState);
        }
        dest.writeInt(mSubgroupMetadata.size());
        for (BluetoothLeAudioContentMetadata metadata : mSubgroupMetadata) {
            dest.writeTypedObject(metadata, flags);
        }
    }

    public static final @NonNull Creator<BluetoothLeBroadcastSinkState> CREATOR =
            new Creator<BluetoothLeBroadcastSinkState>() {
                @Override
                public BluetoothLeBroadcastSinkState createFromParcel(@NonNull Parcel in) {
                    Builder builder = new Builder();
                    builder.setBroadcastId(in.readInt());
                    builder.setSinkState(in.readInt());
                    builder.setNumSubgroups(in.readInt());

                    int bisSyncStateSize = in.readInt();
                    for (int i = 0; i < bisSyncStateSize; i++) {
                        builder.addBisSyncState(in.readLong());
                    }

                    int metadataSize = in.readInt();
                    for (int i = 0; i < metadataSize; i++) {
                        builder.addSubgroupMetadata(
                                in.readTypedObject(BluetoothLeAudioContentMetadata.CREATOR));
                    }

                    return builder.build();
                }

                @Override
                public BluetoothLeBroadcastSinkState[] newArray(int size) {
                    return new BluetoothLeBroadcastSinkState[size];
                }
            };

    /**
     * Builder for {@link BluetoothLeBroadcastSinkState}.
     *
     * @hide
     */
    @SystemApi
    public static final class Builder {
        private int mBroadcastId = 0;
        private @SinkState int mSinkState = SINK_STATE_IDLE;
        private int mNumSubgroups = 0;
        private final List<Long> mBisSyncState = new ArrayList<>();
        private final List<BluetoothLeAudioContentMetadata> mSubgroupMetadata = new ArrayList<>();

        /**
         * Create an empty builder.
         *
         * @hide
         */
        @SystemApi
        public Builder() {}

        /**
         * Create a builder with copies of information from original object.
         *
         * @param original original object
         * @hide
         */
        @SystemApi
        public Builder(@NonNull BluetoothLeBroadcastSinkState original) {
            mBroadcastId = original.getBroadcastId();
            mSinkState = original.getSinkState();
            mNumSubgroups = original.getNumSubgroups();
            mBisSyncState.addAll(original.getBisSyncStates());
            for (BluetoothLeAudioContentMetadata metadata : original.getSubgroupMetadatas()) {
                mSubgroupMetadata.add(metadata);
            }
        }

        /**
         * Set the broadcast ID.
         *
         * @param broadcastId the broadcast ID
         * @return this builder
         * @hide
         */
        @SystemApi
        @NonNull
        public Builder setBroadcastId(int broadcastId) {
            mBroadcastId = broadcastId;
            return this;
        }

        /**
         * Set the sink state.
         *
         * @param sinkState the sink state
         * @return this builder
         * @hide
         */
        @SystemApi
        @NonNull
        public Builder setSinkState(@SinkState int sinkState) {
            mSinkState = sinkState;
            return this;
        }

        /**
         * Set the number of subgroups.
         *
         * @param numSubgroups the number of subgroups
         * @return this builder
         * @hide
         */
        @SystemApi
        @NonNull
        public Builder setNumSubgroups(int numSubgroups) {
            mNumSubgroups = numSubgroups;
            return this;
        }

        /**
         * Add a BIS sync state for a subgroup.
         *
         * @param bisSyncState the BIS sync state bitmask for a subgroup
         * @return this builder
         * @hide
         */
        @SystemApi
        @NonNull
        public Builder addBisSyncState(long bisSyncState) {
            mBisSyncState.add(bisSyncState);
            return this;
        }

        /**
         * Add metadata for a subgroup.
         *
         * @param metadata the subgroup metadata
         * @return this builder
         * @hide
         */
        @SystemApi
        @NonNull
        public Builder addSubgroupMetadata(@NonNull BluetoothLeAudioContentMetadata metadata) {
            Objects.requireNonNull(metadata);
            mSubgroupMetadata.add(metadata);
            return this;
        }

        /**
         * Build {@link BluetoothLeBroadcastSinkState}.
         *
         * @return {@link BluetoothLeBroadcastSinkState}
         * @throws IllegalArgumentException if the object cannot be built
         * @hide
         */
        @SystemApi
        @NonNull
        public BluetoothLeBroadcastSinkState build() {
            if (mBisSyncState.size() != mNumSubgroups) {
                throw new IllegalArgumentException(
                        "BIS sync state list size (" + mBisSyncState.size()
                        + ") must match number of subgroups (" + mNumSubgroups + ")");
            }
            if (mSubgroupMetadata.size() != mNumSubgroups) {
                throw new IllegalArgumentException(
                        "Subgroup metadata list size (" + mSubgroupMetadata.size()
                        + ") must match number of subgroups (" + mNumSubgroups + ")");
            }
            return new BluetoothLeBroadcastSinkState(
                    mBroadcastId,
                    mSinkState,
                    mNumSubgroups,
                    mBisSyncState,
                    mSubgroupMetadata);
        }
    }
}
