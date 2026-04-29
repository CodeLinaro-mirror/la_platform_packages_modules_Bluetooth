/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.leaudio;

import android.app.Application;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothLeBroadcastChannel;
import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSink;
import android.bluetooth.BluetoothLeBroadcastSinkState;
import android.bluetooth.BluetoothLeBroadcastSubgroup;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.lifecycle.AndroidViewModel;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;
import java.util.concurrent.Executors;

public class BroadcastSinkViewModel extends AndroidViewModel {
    private static final String TAG = "BroadcastSinkViewModel";

    private final BluetoothAdapter mBluetoothAdapter;
    private BluetoothLeBroadcastSink mBroadcastSink;
    private final Executor mExecutor;

    // LiveData for UI updates
    private final MutableLiveData<Boolean> mIsSearching = new MutableLiveData<>(false);
    private final MutableLiveData<List<FoundBroadcastItem>> mFoundBroadcasts = new MutableLiveData<>(new ArrayList<>());
    private final MutableLiveData<List<BluetoothLeBroadcastMetadata>> mSyncedBroadcasts = new MutableLiveData<>(new ArrayList<>());
    private final MutableLiveData<String> mStatusMessage = new MutableLiveData<>("");
    private final MutableLiveData<Boolean> mIsBroadcastSinkSupported = new MutableLiveData<>(false);

    // Internal state
    private final Map<Integer, BluetoothLeBroadcastMetadata> mSyncedBroadcastMap = new HashMap<>();
    private final Map<Integer, FoundBroadcastItem> mFoundBroadcastsMap = new HashMap<>();

    // Helper class to hold found broadcast information
    public static class FoundBroadcastItem {
        public final int broadcastId;
        public final ScanResult scanResult;
        public BluetoothLeBroadcastMetadata metadata; // null until PA synced
        private String scanRecordBroadcastName; // Broadcast name from scan record (AD type 0x30)

        public FoundBroadcastItem(int broadcastId, ScanResult scanResult) {
            this.broadcastId = broadcastId;
            this.scanResult = scanResult;
            this.metadata = null;
            // Parse broadcast name from scan record
            this.scanRecordBroadcastName = BroadcastUtils.getBroadcastName(scanResult);
        }

        public String getBroadcastName() {
            // Priority 1: Use broadcast name from metadata if available (after PA sync)
            if (metadata != null && metadata.getBroadcastName() != null) {
                return metadata.getBroadcastName();
            }
            // Priority 2: Use broadcast name from scan record (AD type 0x30)
            if (scanRecordBroadcastName != null) {
                return scanRecordBroadcastName;
            }
            // Priority 3: Fallback to generic name with broadcast ID
            return "Broadcast " + broadcastId;
        }

        public boolean isEncrypted() {
            return metadata != null && metadata.isEncrypted();
        }

        public boolean hasPASync() {
            return metadata != null;
        }
    }

    private final BluetoothLeBroadcastSink.Callback mBroadcastSinkCallback = new BluetoothLeBroadcastSink.Callback() {
        @Override
        public void onSearchStarted(int reason) {
            Log.d(TAG, "onSearchStarted: reason=" + reason);
            mIsSearching.postValue(true);
            mStatusMessage.postValue("Search started (reason: " + reason + ")");
        }

        @Override
        public void onSearchStartFailed(int reason) {
            Log.d(TAG, "onSearchStartFailed: reason=" + reason + " (" + getReasonString(reason) + ")");
            mIsSearching.postValue(false);

            String message;
            switch (reason) {
                case BluetoothLeBroadcastSinkState.REASON_HARDWARE_GENERIC:
                    message = "Search start failed: Hardware error";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_SYSTEM_POLICY:
                    message = "Search start failed: System policy restriction";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS:
                    message = "Search start failed: Invalid parameters";
                    break;
                default:
                    message = "Search start failed (" + getReasonString(reason) + ")";
                    break;
            }
            mStatusMessage.postValue(message);
        }

        @Override
        public void onSearchStopped(int reason) {
            Log.d(TAG, "onSearchStopped: reason=" + reason);
            mIsSearching.postValue(false);
            mStatusMessage.postValue("Search stopped (reason: " + reason + ")");
        }

        @Override
        public void onSourceFound(int broadcastId, @NonNull ScanResult result) {
            Log.d(TAG, "onSourceFound: broadcastId=" + broadcastId);
            synchronized (mFoundBroadcastsMap) {
                if (!mFoundBroadcastsMap.containsKey(broadcastId)) {
                    FoundBroadcastItem item = new FoundBroadcastItem(broadcastId, result);
                    mFoundBroadcastsMap.put(broadcastId, item);
                    mFoundBroadcasts.postValue(new ArrayList<>(mFoundBroadcastsMap.values()));
                }
            }
            mStatusMessage.postValue("Found broadcast ID: " + broadcastId);

            // Update synced broadcast states when a new source is found
            updateSyncedBroadcasts();
        }

        @Override
        public void onSourceAdded(@NonNull BluetoothLeBroadcastMetadata metadata) {
            int broadcastId = metadata.getBroadcastId();

            // Update found broadcast with metadata
            String displayName = null;
            synchronized (mFoundBroadcastsMap) {
                FoundBroadcastItem item = mFoundBroadcastsMap.get(broadcastId);
                if (item != null) {
                    item.metadata = metadata;
                    displayName = item.getBroadcastName(); // Use the method that falls back to scan record name
                    mFoundBroadcasts.postValue(new ArrayList<>(mFoundBroadcastsMap.values()));
                }
            }

            // Use the display name from FoundBroadcastItem which includes scan record fallback
            if (displayName == null) {
                displayName = metadata.getBroadcastName() != null ? metadata.getBroadcastName() : "Broadcast " + broadcastId;
            }

            Log.d(TAG, "onSourceAdded: " + displayName);
            mStatusMessage.postValue("PA synced to: " + displayName);
        }

        @Override
        public void onSourceAddFailed(int broadcastId, int reason) {
            Log.d(TAG, "onSourceAddFailed: broadcastId=" + broadcastId + ", reason=" + reason + " (" + getReasonString(reason) + ")");

            String message;
            switch (reason) {
                case BluetoothLeBroadcastSinkState.REASON_PA_SYNC_FAILED:
                    message = "PA sync failed for broadcast ID " + broadcastId + ": Unable to sync";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_DUPLICATE_ADD_REQUEST:
                    message = "PA sync failed for broadcast ID " + broadcastId + ": Already added";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_MAX_PA_SYNC_REACHED:
                    message = "PA sync failed for broadcast ID " + broadcastId + ": Maximum sources reached";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_HARDWARE_GENERIC:
                    message = "PA sync failed for broadcast ID " + broadcastId + ": Hardware error";
                    break;
                default:
                    message = "PA sync failed for broadcast ID " + broadcastId + " (" + getReasonString(reason) + ")";
                    break;
            }
            mStatusMessage.postValue(message);
        }

        @Override
        public void onSourceJoined(int broadcastId) {
            Log.d(TAG, "onSourceJoined: broadcastId=" + broadcastId);
            mStatusMessage.postValue("Joined broadcast ID: " + broadcastId);
            updateSyncedBroadcasts();
        }

        @Override
        public void onSourceJoinFailed(@NonNull BluetoothLeBroadcastMetadata metadata, int reason) {
            Log.d(TAG, "onSourceJoinFailed: broadcastId=" +
                    (metadata != null ? metadata.getBroadcastId() : "unknown") +
                    ", reason=" + reason + " (" + getReasonString(reason) + ")");

            String name = metadata != null ? metadata.getBroadcastName() : "Unknown";
            String reasonStr = getReasonString(reason);

            // Provide user-friendly message based on reason
            String message;
            switch (reason) {
                case BluetoothLeBroadcastSinkState.REASON_ENCRYPTION_FAILED_BAD_CODE:
                    message = "Join failed for " + name + ": Incorrect broadcast code";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_ENCRYPTION_FAILED_NO_KEY:
                    message = "Join failed for " + name + ": Broadcast code required";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_BIG_SYNC_FAILED:
                    message = "Join failed for " + name + ": Unable to sync to broadcast";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_BIG_SYNC_LOST:
                    message = "Join failed for " + name + ": Broadcast sync lost";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_DUPLICATE_JOIN_REQUEST:
                    message = "Join failed for " + name + ": Already joined";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_MAX_BIG_SYNC_REACHED:
                    message = "Join failed for " + name + ": Maximum broadcasts reached";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST:
                    message = "Join failed for " + name + ": Internal error";
                    break;
                default:
                    message = "Join failed for " + name + " (" + reasonStr + ")";
                    break;
            }

            mStatusMessage.postValue(message);
        }

        @Override
        public void onSourceLeft(int broadcastId, int reason) {
            Log.d(TAG, "onSourceLeft: broadcastId=" + broadcastId + ", reason=" + reason);
            mStatusMessage.postValue("Left broadcast ID: " + broadcastId + " (reason: " + reason + ")");
            updateSyncedBroadcasts();
        }

        @Override
        public void onSourceLeaveFailed(int broadcastId, int reason) {
            Log.d(TAG, "onSourceLeaveFailed: broadcastId=" + broadcastId + ", reason=" + reason + " (" + getReasonString(reason) + ")");

            String message;
            switch (reason) {
                case BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS:
                    message = "Leave failed for broadcast ID " + broadcastId + ": Invalid broadcast ID";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_HARDWARE_GENERIC:
                    message = "Leave failed for broadcast ID " + broadcastId + ": Hardware error";
                    break;
                default:
                    message = "Leave failed for broadcast ID " + broadcastId + " (" + getReasonString(reason) + ")";
                    break;
            }
            mStatusMessage.postValue(message);
        }

        @Override
        public void onSourceRemoved(int broadcastId, int reason) {
            Log.d(TAG, "onSourceRemoved: broadcastId=" + broadcastId + ", reason=" + reason);
            synchronized (mSyncedBroadcastMap) {
                mSyncedBroadcastMap.remove(broadcastId);
            }
            synchronized (mFoundBroadcastsMap) {
                mFoundBroadcastsMap.remove(broadcastId);
                mFoundBroadcasts.postValue(new ArrayList<>(mFoundBroadcastsMap.values()));
            }
            mStatusMessage.postValue("Removed broadcast ID: " + broadcastId + " (reason: " + reason + ")");
            updateSyncedBroadcasts();
        }

        @Override
        public void onSourceRemoveFailed(int broadcastId, int reason) {
            Log.d(TAG, "onSourceRemoveFailed: broadcastId=" + broadcastId + ", reason=" + reason + " (" + getReasonString(reason) + ")");

            String message;
            switch (reason) {
                case BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS:
                    message = "Remove failed for broadcast ID " + broadcastId + ": Invalid broadcast ID";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_HARDWARE_GENERIC:
                    message = "Remove failed for broadcast ID " + broadcastId + ": Hardware error";
                    break;
                default:
                    message = "Remove failed for broadcast ID " + broadcastId + " (" + getReasonString(reason) + ")";
                    break;
            }
            mStatusMessage.postValue(message);
        }

        @Override
        public void onSourceMetadataChanged(int broadcastId, @NonNull BluetoothLeBroadcastMetadata metadata) {
            Log.d(TAG, "onSourceMetadataChanged: broadcastId=" + broadcastId);
            synchronized (mSyncedBroadcastMap) {
                mSyncedBroadcastMap.put(broadcastId, metadata);
            }
            synchronized (mFoundBroadcastsMap) {
                FoundBroadcastItem item = mFoundBroadcastsMap.get(broadcastId);
                if (item != null) {
                    item.metadata = metadata;
                    mFoundBroadcasts.postValue(new ArrayList<>(mFoundBroadcastsMap.values()));
                }
            }
            mStatusMessage.postValue("Metadata changed for broadcast ID: " + broadcastId);
            updateSyncedBroadcasts();
        }

        @Override
        public void onSourceMetadataUpdated(int broadcastId, @NonNull BluetoothLeBroadcastMetadata metadata) {
            Log.d(TAG, "onSourceMetadataUpdated: broadcastId=" + broadcastId);
            synchronized (mSyncedBroadcastMap) {
                mSyncedBroadcastMap.put(broadcastId, metadata);
            }
            mStatusMessage.postValue("Metadata updated for broadcast ID: " + broadcastId);
            updateSyncedBroadcasts();
        }

        @Override
        public void onSourceMetadataUpdateFailed(int broadcastId, @NonNull BluetoothLeBroadcastMetadata metadata, int reason) {
            Log.d(TAG, "onSourceMetadataUpdateFailed: broadcastId=" + broadcastId + ", reason=" + reason + " (" + getReasonString(reason) + ")");

            String message;
            switch (reason) {
                case BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS:
                    message = "Metadata update failed for broadcast ID " + broadcastId + ": Invalid parameters";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_ENCRYPTION_FAILED_BAD_CODE:
                    message = "Metadata update failed for broadcast ID " + broadcastId + ": Incorrect broadcast code";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_BIG_SYNC_FAILED:
                    message = "Metadata update failed for broadcast ID " + broadcastId + ": Unable to sync";
                    break;
                case BluetoothLeBroadcastSinkState.REASON_HARDWARE_GENERIC:
                    message = "Metadata update failed for broadcast ID " + broadcastId + ": Hardware error";
                    break;
                default:
                    message = "Metadata update failed for broadcast ID " + broadcastId + " (" + getReasonString(reason) + ")";
                    break;
            }
            mStatusMessage.postValue(message);
        }
    };

    private final BluetoothProfile.ServiceListener mServiceListener = new BluetoothProfile.ServiceListener() {
        @Override
        public void onServiceConnected(int profile, BluetoothProfile proxy) {
            Log.d(TAG, "BroadcastSink service connected to profile: " + profile);
            if (profile == BluetoothProfile.LE_AUDIO_BROADCAST_SINK) {
                mBroadcastSink = (BluetoothLeBroadcastSink) proxy;
                try {
                    mBroadcastSink.registerCallback(mExecutor, mBroadcastSinkCallback);
                    mIsBroadcastSinkSupported.postValue(true);
                    mStatusMessage.postValue("Broadcast Sink service connected");

                    // Load any existing synced broadcasts
                    updateSyncedBroadcasts();
                } catch (Exception e) {
                    Log.e(TAG, "Failed to register callback", e);
                    mStatusMessage.postValue("Failed to register callback: " + e.getMessage());
                }
            }
        }

        @Override
        public void onServiceDisconnected(int profile) {
            Log.d(TAG, "BroadcastSink service disconnected");
            if (profile == BluetoothProfile.LE_AUDIO_BROADCAST_SINK) {
                if (mBroadcastSink != null) {
                    try {
                        mBroadcastSink.unregisterCallback(mBroadcastSinkCallback);
                    } catch (Exception e) {
                        Log.e(TAG, "Failed to unregister callback", e);
                    }
                    mBroadcastSink = null;
                }
                mIsBroadcastSinkSupported.postValue(false);
                mStatusMessage.postValue("Broadcast Sink service disconnected");
            }
        }
    };

    public BroadcastSinkViewModel(@NonNull Application application) {
        super(application);
        mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        mExecutor = Executors.newSingleThreadExecutor();

        // Check if broadcast sink is supported
        if (mBluetoothAdapter != null) {
            // Try to get the broadcast sink proxy - if it fails, the feature is not supported
            try {
                mBluetoothAdapter.getProfileProxy(application, mServiceListener, BluetoothProfile.LE_AUDIO_BROADCAST_SINK);
            } catch (Exception e) {
                mIsBroadcastSinkSupported.postValue(false);
                mStatusMessage.postValue("Broadcast Sink not supported on this device: " + e.getMessage());
            }
        } else {
            mIsBroadcastSinkSupported.postValue(false);
            mStatusMessage.postValue("Bluetooth adapter not available");
        }
    }

    @Override
    protected void onCleared() {
        super.onCleared();
        cleanup();
    }

    public void cleanup() {
        if (mBroadcastSink != null) {
            try {
                // Stop any ongoing search
                if (Boolean.TRUE.equals(mIsSearching.getValue())) {
                    mBroadcastSink.stopScanningForSources();
                }
                mBroadcastSink.unregisterCallback(mBroadcastSinkCallback);
                mBluetoothAdapter.closeProfileProxy(BluetoothProfile.LE_AUDIO_BROADCAST_SINK, mBroadcastSink);
            } catch (Exception e) {
                Log.e(TAG, "Error during cleanup", e);
            }
            mBroadcastSink = null;
        }
    }

    // Public methods for UI interaction
    public void startSearchingForSources() {
        if (mBroadcastSink == null) {
            mStatusMessage.postValue("Broadcast Sink service not available");
            return;
        }

        try {
            // Clear previous results
            synchronized (mFoundBroadcastsMap) {
                mFoundBroadcastsMap.clear();
                mFoundBroadcasts.postValue(new ArrayList<>());
            }

            // Start searching with empty filter list (find all broadcasts) and default scan settings
            List<ScanFilter> filters = new ArrayList<>();
            ScanSettings settings = new ScanSettings.Builder()
                    .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                    .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
                    .setLegacy(false)
                    .build();

            mBroadcastSink.startScanningForSources(filters, settings);
        } catch (Exception e) {
            Log.e(TAG, "Failed to start searching", e);
            mStatusMessage.postValue("Failed to start search: " + e.getMessage());
        }
    }

    public void stopSearchingForSources() {
        if (mBroadcastSink == null) {
            mStatusMessage.postValue("Broadcast Sink service not available");
            return;
        }

        try {
            mBroadcastSink.stopScanningForSources();
        } catch (Exception e) {
            Log.e(TAG, "Failed to stop searching", e);
            mStatusMessage.postValue("Failed to stop search: " + e.getMessage());
        }
    }

    public void addSource(int broadcastId) {
        if (mBroadcastSink == null) {
            mStatusMessage.postValue("Broadcast Sink service not available");
            return;
        }

        try {
            mBroadcastSink.addSource(broadcastId);
            mStatusMessage.postValue("Adding source (PA sync) for broadcast ID: " + broadcastId);
        } catch (Exception e) {
            Log.e(TAG, "Failed to add source", e);
            mStatusMessage.postValue("Failed to add source: " + e.getMessage());
        }
    }

    public void joinSource(BluetoothLeBroadcastMetadata metadata, byte[] broadcastCode) {
        joinSourceWithChannelSelection(metadata, broadcastCode, null);
    }

    public void joinSourceWithChannelSelection(BluetoothLeBroadcastMetadata metadata, byte[] broadcastCode, List<Integer> selectedChannelIndices) {
        if (mBroadcastSink == null) {
            mStatusMessage.postValue("Broadcast Sink service not available");
            return;
        }

        try {
            BluetoothLeBroadcastMetadata metadataToUse = metadata;

            // Update channel selection state based on selectedChannelIndices
            if (selectedChannelIndices != null && !selectedChannelIndices.isEmpty()) {
                BluetoothLeBroadcastMetadata.Builder metadataBuilder = new BluetoothLeBroadcastMetadata.Builder(metadata);
                metadataBuilder.clearSubgroup(); // Clear existing subgroups

                // Rebuild subgroups with updated channel selection
                for (BluetoothLeBroadcastSubgroup originalSubgroup : metadata.getSubgroups()) {
                    BluetoothLeBroadcastSubgroup.Builder subgroupBuilder = new BluetoothLeBroadcastSubgroup.Builder(originalSubgroup);
                    subgroupBuilder.clearChannel(); // Clear existing channels

                    // Rebuild channels with updated selection state
                    for (BluetoothLeBroadcastChannel originalChannel : originalSubgroup.getChannels()) {
                        boolean isSelected = selectedChannelIndices.contains(originalChannel.getChannelIndex());
                        BluetoothLeBroadcastChannel updatedChannel = new BluetoothLeBroadcastChannel.Builder(originalChannel)
                                .setSelected(isSelected)
                                .build();
                        subgroupBuilder.addChannel(updatedChannel);
                    }

                    metadataBuilder.addSubgroup(subgroupBuilder.build());
                }

                metadataToUse = metadataBuilder.build();
                Log.d(TAG, "Updated metadata with channel selection: " + selectedChannelIndices);
            }

            // Set broadcast code if provided
            if (metadata.isEncrypted() && broadcastCode != null) {
                BluetoothLeBroadcastMetadata.Builder builder = new BluetoothLeBroadcastMetadata.Builder(metadataToUse);
                builder.setBroadcastCode(broadcastCode);
                metadataToUse = builder.build();
            }

            String statusMessage = "Joining broadcast: " + metadata.getBroadcastName();
            if (metadata.isEncrypted() && broadcastCode != null) {
                statusMessage = "Joining encrypted broadcast: " + metadata.getBroadcastName();
                Log.d(TAG, "Joining encrypted broadcast with code length: " + broadcastCode.length);
            }
            if (selectedChannelIndices != null && !selectedChannelIndices.isEmpty()) {
                statusMessage += " (" + selectedChannelIndices.size() + " channels selected)";
                Log.d(TAG, "Selected channel indices: " + selectedChannelIndices);
            } else {
                Log.d(TAG, "No channel selection provided, using all channels by default");
            }
            mStatusMessage.postValue(statusMessage);

            // Now the framework service layer can extract BIS indices from channels where isSelected() returns true
            mBroadcastSink.joinSource(metadataToUse);

        } catch (Exception e) {
            Log.e(TAG, "Failed to join source", e);
            mStatusMessage.postValue("Failed to join: " + e.getMessage());
        }
    }

    public void leaveSource(int broadcastId) {
        if (mBroadcastSink == null) {
            mStatusMessage.postValue("Broadcast Sink service not available");
            return;
        }

        try {
            mBroadcastSink.leaveSource(broadcastId);
            mStatusMessage.postValue("Leaving broadcast ID: " + broadcastId);
        } catch (Exception e) {
            Log.e(TAG, "Failed to leave source", e);
            mStatusMessage.postValue("Failed to leave: " + e.getMessage());
        }
    }

    public void removeSource(int broadcastId) {
        if (mBroadcastSink == null) {
            mStatusMessage.postValue("Broadcast Sink service not available");
            return;
        }

        try {
            mBroadcastSink.removeSource(broadcastId);
            mStatusMessage.postValue("Removing broadcast ID: " + broadcastId);
        } catch (Exception e) {
            Log.e(TAG, "Failed to remove source", e);
            mStatusMessage.postValue("Failed to remove: " + e.getMessage());
        }
    }

    public void updateSourceMetadata(BluetoothLeBroadcastMetadata metadata, List<Integer> selectedChannelIndices) {
        if (mBroadcastSink == null) {
            mStatusMessage.postValue("Broadcast Sink service not available");
            return;
        }

        try {
            BluetoothLeBroadcastMetadata metadataToUse = metadata;

            // Update channel selection state based on selectedChannelIndices
            if (selectedChannelIndices != null && !selectedChannelIndices.isEmpty()) {
                BluetoothLeBroadcastMetadata.Builder metadataBuilder = new BluetoothLeBroadcastMetadata.Builder(metadata);
                metadataBuilder.clearSubgroup(); // Clear existing subgroups

                // Rebuild subgroups with updated channel selection
                for (BluetoothLeBroadcastSubgroup originalSubgroup : metadata.getSubgroups()) {
                    BluetoothLeBroadcastSubgroup.Builder subgroupBuilder = new BluetoothLeBroadcastSubgroup.Builder(originalSubgroup);
                    subgroupBuilder.clearChannel(); // Clear existing channels

                    // Rebuild channels with updated selection state
                    for (BluetoothLeBroadcastChannel originalChannel : originalSubgroup.getChannels()) {
                        boolean isSelected = selectedChannelIndices.contains(originalChannel.getChannelIndex());
                        BluetoothLeBroadcastChannel updatedChannel = new BluetoothLeBroadcastChannel.Builder(originalChannel)
                                .setSelected(isSelected)
                                .build();
                        subgroupBuilder.addChannel(updatedChannel);
                    }

                    metadataBuilder.addSubgroup(subgroupBuilder.build());
                }

                metadataToUse = metadataBuilder.build();
                Log.d(TAG, "Updated metadata with channel selection: " + selectedChannelIndices);
            }

            mBroadcastSink.updateSourceMetadata(metadataToUse);

            String statusMessage = "Updating metadata for broadcast: " + metadata.getBroadcastName();
            if (selectedChannelIndices != null && !selectedChannelIndices.isEmpty()) {
                statusMessage += " (" + selectedChannelIndices.size() + " channels selected)";
                Log.d(TAG, "Selected channel indices for update: " + selectedChannelIndices);
            }
            mStatusMessage.postValue(statusMessage);

        } catch (Exception e) {
            Log.e(TAG, "Failed to update source metadata", e);
            mStatusMessage.postValue("Failed to update metadata: " + e.getMessage());
        }
    }

    private void updateSyncedBroadcasts() {
        if (mBroadcastSink == null) {
            return;
        }

        try {
            // Get all synced sink states
            List<BluetoothLeBroadcastSinkState> sinkStates = mBroadcastSink.getAllSyncedSinkState();

            // Extract metadata for synced broadcasts
            List<BluetoothLeBroadcastMetadata> syncedMetadataList = new ArrayList<>();
            boolean foundBroadcastsUpdated = false;

            synchronized (mSyncedBroadcastMap) {
                mSyncedBroadcastMap.clear();
                for (BluetoothLeBroadcastSinkState state : sinkStates) {
                    int broadcastId = state.getBroadcastId();
                    BluetoothLeBroadcastMetadata metadata = mBroadcastSink.getSourceMetadata(broadcastId);
                    if (metadata != null) {
                        mSyncedBroadcastMap.put(broadcastId, metadata);
                        syncedMetadataList.add(metadata);

                        // Update metadata in found broadcasts map if this broadcast exists there
                        synchronized (mFoundBroadcastsMap) {
                            FoundBroadcastItem item = mFoundBroadcastsMap.get(broadcastId);
                            if (item != null && item.metadata != metadata) {
                                item.metadata = metadata;
                                foundBroadcastsUpdated = true;
                            }
                        }
                    }
                }
            }

            mSyncedBroadcasts.postValue(syncedMetadataList);

            // Update found broadcasts list if any metadata was updated
            if (foundBroadcastsUpdated) {
                synchronized (mFoundBroadcastsMap) {
                    mFoundBroadcasts.postValue(new ArrayList<>(mFoundBroadcastsMap.values()));
                }
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to get synced broadcasts", e);
        }
    }

    // LiveData getters
    public LiveData<Boolean> getIsSearching() {
        return mIsSearching;
    }

    public LiveData<List<FoundBroadcastItem>> getFoundBroadcasts() {
        return mFoundBroadcasts;
    }

    public LiveData<List<BluetoothLeBroadcastMetadata>> getSyncedBroadcasts() {
        return mSyncedBroadcasts;
    }

    public LiveData<String> getStatusMessage() {
        return mStatusMessage;
    }

    public LiveData<Boolean> isBroadcastSinkSupported() {
        return mIsBroadcastSinkSupported;
    }

    public void getAllSyncedSinkStates() {
        if (mBroadcastSink == null) {
            mStatusMessage.postValue("Broadcast Sink service not available");
            Log.e(TAG, "BluetoothLeBroadcastSink is null");
            return;
        }

        try {
            Log.d(TAG, "Getting all synced sink states");
            List<BluetoothLeBroadcastSinkState> states =
                mBroadcastSink.getAllSyncedSinkState();

            if (states == null || states.isEmpty()) {
                Log.d(TAG, "No synced sink states found");
                mStatusMessage.postValue("No synced sink states found");
                return;
            }

            StringBuilder sb = new StringBuilder();
            sb.append("Found ").append(states.size()).append(" synced sink state(s):\n\n");

            for (BluetoothLeBroadcastSinkState state : states) {
                sb.append("Broadcast ID: ").append(state.getBroadcastId()).append("\n");
                sb.append("State: ").append(getStateString(state.getSinkState())).append("\n");
                sb.append("Num Subgroups: ").append(state.getNumSubgroups()).append("\n");

                List<Long> bisSyncStates = state.getBisSyncStates();
                sb.append("Synced BIS Indices: ");
                if (bisSyncStates != null && !bisSyncStates.isEmpty()) {
                    boolean first = true;
                    for (int subgroupIdx = 0; subgroupIdx < bisSyncStates.size(); subgroupIdx++) {
                        long bisSyncState = bisSyncStates.get(subgroupIdx);
                        if (bisSyncState != 0) {
                            // Convert bitmask to BIS indices
                            List<Integer> bisIndices = convertBitmaskToBisIndices(bisSyncState);
                            if (!bisIndices.isEmpty()) {
                                if (!first) sb.append(", ");
                                if (bisSyncStates.size() > 1) {
                                    sb.append("Subgroup ").append(subgroupIdx + 1).append(": ");
                                }
                                sb.append(bisIndices.toString());
                                first = false;
                            }
                        }
                    }
                    if (first) {
                        sb.append("None");
                    }
                } else {
                    sb.append("None");
                }
                sb.append("\n\n");
            }

            Log.d(TAG, sb.toString());
            mStatusMessage.postValue(sb.toString());
        } catch (Exception e) {
            Log.e(TAG, "Failed to get all synced sink states", e);
            mStatusMessage.postValue("Failed to get synced states: " + e.getMessage());
        }
    }

    /**
     * Convert BIS sync state bitmask to list of BIS indices
     * @param bisSyncState bitmask where bit N represents BIS index N+1
     * @return list of BIS indices
     */
    private List<Integer> convertBitmaskToBisIndices(long bisSyncState) {
        List<Integer> bisIndices = new ArrayList<>();
        for (int i = 0; i < 64; i++) {
            if ((bisSyncState & (1L << i)) != 0) {
                bisIndices.add(i + 1); // BIS index is bit position + 1
            }
        }
        return bisIndices;
    }

    private String getStateString(int state) {
        switch (state) {
            case BluetoothLeBroadcastSinkState.SINK_STATE_IDLE:
                return "IDLE";
            case BluetoothLeBroadcastSinkState.SINK_STATE_SYNCING_PA:
                return "SYNCING_PA";
            case BluetoothLeBroadcastSinkState.SINK_STATE_SYNCED_PA:
                return "SYNCED_PA";
            case BluetoothLeBroadcastSinkState.SINK_STATE_JOINING_BROADCAST:
                return "JOINING_BROADCAST";
            case BluetoothLeBroadcastSinkState.SINK_STATE_RECEIVING_BROADCAST:
                return "RECEIVING_BROADCAST";
            case BluetoothLeBroadcastSinkState.SINK_STATE_LEAVING_BROADCAST:
                return "LEAVING_BROADCAST";
            case BluetoothLeBroadcastSinkState.SINK_STATE_UPDATING_BROADCAST:
                return "UPDATING_BROADCAST";
            default:
                return "UNKNOWN(" + state + ")";
        }
    }

    private String getReasonString(int reason) {
        switch (reason) {
            case BluetoothLeBroadcastSinkState.REASON_UNKNOWN:
                return "UNKNOWN";
            case BluetoothLeBroadcastSinkState.REASON_LOCAL_APP_REQUEST:
                return "LOCAL_APP_REQUEST";
            case BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST:
                return "LOCAL_STACK_REQUEST";
            case BluetoothLeBroadcastSinkState.REASON_SYSTEM_POLICY:
                return "SYSTEM_POLICY";
            case BluetoothLeBroadcastSinkState.REASON_HARDWARE_GENERIC:
                return "HARDWARE_GENERIC";
            case BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS:
                return "BAD_PARAMETERS";
            case BluetoothLeBroadcastSinkState.REASON_PA_SYNC_ESTABLISHED:
                return "PA_SYNC_ESTABLISHED";
            case BluetoothLeBroadcastSinkState.REASON_PA_SYNC_FAILED:
                return "PA_SYNC_FAILED";
            case BluetoothLeBroadcastSinkState.REASON_PA_SYNC_LOST:
                return "PA_SYNC_LOST";
            case BluetoothLeBroadcastSinkState.REASON_BIG_SYNC_ESTABLISHED:
                return "BIG_SYNC_ESTABLISHED";
            case BluetoothLeBroadcastSinkState.REASON_ENCRYPTION_FAILED_BAD_CODE:
                return "ENCRYPTION_FAILED_BAD_CODE";
            case BluetoothLeBroadcastSinkState.REASON_ENCRYPTION_FAILED_NO_KEY:
                return "ENCRYPTION_FAILED_NO_KEY";
            case BluetoothLeBroadcastSinkState.REASON_BIG_SYNC_FAILED:
                return "BIG_SYNC_FAILED";
            case BluetoothLeBroadcastSinkState.REASON_BIG_SYNC_LOST:
                return "BIG_SYNC_LOST";
            case BluetoothLeBroadcastSinkState.REASON_DUPLICATE_ADD_REQUEST:
                return "DUPLICATE_ADD_REQUEST";
            case BluetoothLeBroadcastSinkState.REASON_DUPLICATE_JOIN_REQUEST:
                return "DUPLICATE_JOIN_REQUEST";
            case BluetoothLeBroadcastSinkState.REASON_MAX_PA_SYNC_REACHED:
                return "MAX_PA_SYNC_REACHED";
            case BluetoothLeBroadcastSinkState.REASON_MAX_BIG_SYNC_REACHED:
                return "MAX_BIG_SYNC_REACHED";
            default:
                return "UNKNOWN(" + reason + ")";
        }
    }
}
