/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.le_audio;

import static android.Manifest.permission.BLUETOOTH_CONNECT;
import static android.Manifest.permission.BLUETOOTH_PRIVILEGED;
import static android.Manifest.permission.BLUETOOTH_SCAN;

import android.annotation.RequiresPermission;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothLeBroadcastChannel;
import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSinkState;
import android.bluetooth.BluetoothLeBroadcastSubgroup;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothStatusCodes;
import android.bluetooth.IBluetoothLeBroadcastSink;
import android.bluetooth.IBluetoothLeBroadcastSinkCallback;
import android.bluetooth.le.IScannerCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.AttributionSource;
import android.content.Intent;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.BluetoothProfileConnectionInfo;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.Parcel;
import android.os.ParcelUuid;
import android.os.RemoteCallbackList;
import android.os.RemoteException;
import android.sysprop.BluetoothProperties;
import android.util.Log;

import com.android.bluetooth.BluetoothMethodProxy;
import com.android.bluetooth.Utils;
import com.android.bluetooth.bass_client.BassUtils;
import com.android.bluetooth.bass_client.PublicBroadcastData;
import com.android.bluetooth.btservice.AdapterService;
import com.android.bluetooth.btservice.ProfileService;
import com.android.bluetooth.btservice.storage.DatabaseManager;
import com.android.bluetooth.le_scan.ScanController;
import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Service for LE Audio Broadcast Sink functionality
 */
public class LeAudioBroadcastSinkService extends ProfileService {
    private static final String TAG = "LeAudioBroadcastSinkService";
    private static final boolean DBG = true;

    // Maximum number of PA sync sources (sources that can be added)
    private static final int MAX_PA_SYNC_SOURCES = 5;

    // Maximum number of BIG sync sources (sources that can be joined/receiving audio)
    private static final int MAX_BIG_SYNC_SOURCES = 1;

    // Service instance
    private static LeAudioBroadcastSinkService sLeAudioBroadcastSinkService;

    // Native interface
    private LeAudioBroadcastSinkNativeInterface mNativeInterface;

    // Cache of found broadcast sources to avoid duplicates
    // Key: broadcast ID, Value: ScanResult
    private final Map<Integer, ScanResult> mFoundSources = new HashMap<>();

    // AdapterService reference
    private final AdapterService mAdapterService;

    // AudioManager reference
    private final AudioManager mAudioManager;

    // Handler for processing stack events
    private final Handler mHandler;

    // AudioManager callback for monitoring broadcast input audio device changes
    private final AudioManagerAudioDeviceCallback mAudioManagerAudioDeviceCallback =
            new AudioManagerAudioDeviceCallback();

    // Active broadcast input device
    private volatile BluetoothDevice mActiveBroadcastInDevice;

    // Callback management
    private final RemoteCallbackList<IBluetoothLeBroadcastSinkCallback> mCallbacks =
            new RemoteCallbackList<>();

    // State management
    @GuardedBy("mStateLock")
    private boolean mSearchInProgress = false;
    private final Object mStateLock = new Object();

    /**
     * Internal descriptor class for maintaining broadcast sink state and metadata
     * Similar to LeAudioBroadcastDescriptor in LeAudioService
     */
    private static class LeAudioBroadcastSinkDescriptor {
        LeAudioBroadcastSinkDescriptor() {
            mSinkState = LeAudioBroadcastSinkStackEvent.SINK_STATE_IDLE;
            mMetadata = null;
            mIsSourceAddedNotified = false;
            mPendingMetadataUpdate = null;
            mBisIndices = new ArrayList<>();
        }

        public Integer mSinkState;
        public BluetoothLeBroadcastMetadata mMetadata;
        public boolean mIsSourceAddedNotified;
        public BluetoothLeBroadcastMetadata mPendingMetadataUpdate;
        public List<Integer> mBisIndices;
    }

    // Broadcast sink descriptors - maintains internal state and metadata for each broadcast
    private final Map<Integer, LeAudioBroadcastSinkDescriptor> mBroadcastSinkDescriptors =
            new ConcurrentHashMap<>();

    // Scan callback wrapper for internal scanning
    private final BroadcastSinkScanCallbackWrapper mScanCallback = new BroadcastSinkScanCallbackWrapper();

    public LeAudioBroadcastSinkService(AdapterService adapterService) {
        super(adapterService);
        mAdapterService = adapterService;
        mNativeInterface = LeAudioBroadcastSinkNativeInterface.getInstance();
        mAudioManager = getSystemService(AudioManager.class);

        // Initialize handler for processing stack events
        mHandler = new Handler(Looper.getMainLooper());

        // Initialize native interface with max source capacity
        mNativeInterface.init(MAX_PA_SYNC_SOURCES);

        // Register audio device callback
        mAudioManager.registerAudioDeviceCallback(mAudioManagerAudioDeviceCallback, mHandler);

        // Set service instance
        setLeAudioBroadcastSinkService(this);
    }

    @Override
    protected IProfileServiceBinder initBinder() {
        return new LeAudioBroadcastSinkServiceBinder(this);
    }

    @Override
    public void cleanup() {
        if (DBG) Log.d(TAG, "cleanup()");

        // Clear service instance
        setLeAudioBroadcastSinkService(null);

        // Stop any ongoing search
        synchronized (mStateLock) {
            if (mSearchInProgress) {
                // Stop scan callback wrapper
                mScanCallback.stopScanAndUnregister();
                mSearchInProgress = false;
            }
        }

        // Clear descriptors
        mBroadcastSinkDescriptors.clear();

        // Cleanup native interface
        if (mNativeInterface != null) {
            mNativeInterface.cleanup();
            mNativeInterface = null;
        }

        // Unregister audio device callback
        mAudioManager.unregisterAudioDeviceCallback(mAudioManagerAudioDeviceCallback);

        // Clear callbacks
        mCallbacks.kill();
    }

    /**
     * Get the LeAudioBroadcastSinkService instance
     * @return LeAudioBroadcastSinkService instance
     */
    public static synchronized LeAudioBroadcastSinkService getLeAudioBroadcastSinkService() {
        if (sLeAudioBroadcastSinkService == null) {
            Log.w(TAG, "getLeAudioBroadcastSinkService(): service is null");
            return null;
        }
        if (!sLeAudioBroadcastSinkService.isAvailable()) {
            Log.w(TAG, "getLeAudioBroadcastSinkService(): service is not available");
            return null;
        }
        return sLeAudioBroadcastSinkService;
    }

    private static synchronized void setLeAudioBroadcastSinkService(LeAudioBroadcastSinkService instance) {
        if (DBG) Log.d(TAG, "setLeAudioBroadcastSinkService(): set to: " + instance);
        sLeAudioBroadcastSinkService = instance;
    }

    /**
     * Register callback for broadcast sink events
     */
    public void registerCallback(IBluetoothLeBroadcastSinkCallback callback) {
        if (DBG) Log.d(TAG, "registerCallback()");
        mCallbacks.register(callback);
    }

    /**
     * Unregister callback for broadcast sink events
     */
    public void unregisterCallback(IBluetoothLeBroadcastSinkCallback callback) {
        if (DBG) Log.d(TAG, "unregisterCallback()");
        mCallbacks.unregister(callback);
    }

    /**
     * Start scanning for broadcast sources with specified filters and settings
     */
    public void startScanningForSources(List<ScanFilter> filters, ScanSettings settings) {
        if (DBG) Log.d(TAG, "startScanningForSources() with settings: " + settings);

        synchronized (mStateLock) {
            if (mSearchInProgress) {
                Log.w(TAG, "Search already in progress");
                notifyOnSearchStartFailed(BluetoothLeBroadcastSinkState.REASON_UNKNOWN);
                return;
            }
            mSearchInProgress = true;
        }

        // Clear found sources when starting a new search
        mFoundSources.clear();
        if (DBG) Log.d(TAG, "Cleared found sources for new search");

        // Start scanning using the scan callback wrapper with settings
        mScanCallback.registerAndStartScan(filters, settings);

    }

    /**
     * Stop scanning for broadcast sources
     */
    public void stopScanningForSources() {
        if (DBG) Log.d(TAG, "stopScanningForSources()");

        synchronized (mStateLock) {
            if (!mSearchInProgress) {
                Log.w(TAG, "No search in progress");
                mSearchInProgress = false;
                notifyOnSearchStopped(BluetoothLeBroadcastSinkState.REASON_LOCAL_APP_REQUEST);
                return;
            }
        }

        // Stop scanning using the scan callback wrapper
        mScanCallback.stopScanAndUnregister();

        synchronized (mStateLock) {
            mSearchInProgress = false;
        }
        notifyOnSearchStopped(BluetoothLeBroadcastSinkState.REASON_LOCAL_APP_REQUEST);
    }

    /**
     * Add a broadcast source (PA sync only)
     */
    public void addSource(int broadcastId) {
        if (DBG) Log.d(TAG, "addSource(): broadcastId=" + broadcastId);

        // Check if source can be added
        int canAddResult = canSourceBeAdded(broadcastId);
        if (canAddResult != BluetoothStatusCodes.SUCCESS) {
            notifyOnSourceAddFailed(broadcastId, canAddResult);
            return;
        }

        // Get the ScanResult from found sources
        ScanResult result = mFoundSources.get(broadcastId);
        if (result == null) {
            Log.e(TAG, "addSource(): No scan result found for broadcastId=" + broadcastId);
            notifyOnSourceAddFailed(broadcastId, BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS);
            return;
        }

        if (mNativeInterface != null) {
            // Extract parameters from ScanResult
            String address = result.getDevice().getAddress();
            int addressType = result.getDevice().getAddressType();
            int advSid = result.getAdvertisingSid();
            int rssi = result.getRssi();

            // Check if broadcast is public by looking for Public Broadcast Announcement service data
            boolean isPublic = isPublicBroadcast(result.getScanRecord());

            // Parse broadcast name only for public broadcasts (AD type 0x30)
            String broadcastName = null;
            byte[] publicMetadata = null;
            int publicFeatures = 0;

            if (isPublic) {
                broadcastName = parseBroadcastName(result.getScanRecord());

                // Extract public broadcast data (metadata and features)
                PublicBroadcastData pbData = BassUtils.getPublicBroadcastData(result.getScanRecord());
                if (pbData != null) {
                    publicMetadata = pbData.getMetadata();
                    // Combine audio config quality bits and encryption bit
                    publicFeatures = pbData.getAudioConfigQuality() << 1;
                    if (pbData.isEncrypted()) {
                        publicFeatures |= 0x01; // Set encryption bit (bit 0)
                    }
                    if (DBG) Log.d(TAG, "addSource(): publicMetadata length=" +
                                   (publicMetadata != null ? publicMetadata.length : 0) +
                                   ", publicFeatures=0x" + Integer.toHexString(publicFeatures) +
                                   ", isEncrypted=" + pbData.isEncrypted());
                }
            }

            if (DBG) Log.d(TAG, "addSource(): broadcastName=" + broadcastName + ", isPublic=" + isPublic);

            mNativeInterface.addSource(address, addressType, advSid, broadcastId, rssi,
                                      broadcastName, isPublic, publicMetadata, publicFeatures);
        }
    }

    /**
     * Join a broadcast source (BIG sync, with PA sync if needed)
     */
    public void joinSource(BluetoothLeBroadcastMetadata metadata) {
        if (DBG) Log.d(TAG, "joinSource(): " + metadata);

        if (metadata == null) {
            Log.e(TAG, "joinSource(): metadata is null");
            notifyOnSourceJoinFailed(-1, BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS);
            return;
        }

        int broadcastId = metadata.getBroadcastId();

        // Check if source can be joined (including encryption validation)
        int canJoinResult = canSourceBeJoined(broadcastId, metadata);
        if (canJoinResult != BluetoothStatusCodes.SUCCESS) {
            notifyOnSourceJoinFailed(broadcastId, canJoinResult);
            return;
        }

        if (mNativeInterface != null) {
            // Extract broadcast_code from metadata
            byte[] broadcastCode = metadata.getBroadcastCode();

            // Extract BIS indices from selected channels
            List<Integer> bisIndicesList = new ArrayList<>();
            for (BluetoothLeBroadcastSubgroup subgroup : metadata.getSubgroups()) {
                for (BluetoothLeBroadcastChannel channel : subgroup.getChannels()) {
                    if (channel.isSelected()) {
                        bisIndicesList.add(channel.getChannelIndex());
                    }
                }
            }

            // Convert to int array
            int[] bisIndices = bisIndicesList.stream().mapToInt(Integer::intValue).toArray();

            // Update descriptor metadata and store BIS indices
            LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
            if (descriptor == null) {
                descriptor = new LeAudioBroadcastSinkDescriptor();
                mBroadcastSinkDescriptors.put(broadcastId, descriptor);
            }
            descriptor.mMetadata = metadata;
            descriptor.mBisIndices = bisIndicesList;
            if (DBG) Log.d(TAG, "Updated descriptor metadata and bisIndices=" + bisIndicesList + " for broadcastId=" + broadcastId);

            if (DBG) Log.d(TAG, "Calling native joinSource with broadcastId=" + broadcastId +
                           ", broadcastCode=" + (broadcastCode != null ? "provided" : "null") +
                           ", bisIndices=" + Arrays.toString(bisIndices));

            mNativeInterface.joinSource(broadcastId, broadcastCode, bisIndices);
        }
    }

    /**
     * Leave a broadcast source (stop BIG sync but keep PA synced)
     */
    public void leaveSource(int broadcastId) {
        if (DBG) Log.d(TAG, "leaveSource(): broadcastId=" + broadcastId);

        if (mNativeInterface != null) {
            mNativeInterface.leaveSource(broadcastId);
        }
    }

    /**
     * Remove a broadcast source (stop PA sync and BIG sync if active)
     */
    public void removeSource(int broadcastId) {
        if (DBG) Log.d(TAG, "removeSource(): broadcastId=" + broadcastId);

        if (mNativeInterface != null) {
            mNativeInterface.removeSource(broadcastId);
        }
    }

    /**
     * Destroy a broadcast source (cleanup all resources)
     * This is typically called automatically when a source is removed,
     * but can also be called explicitly to force cleanup.
     */
    public void destroySource(int broadcastId) {
        if (DBG) Log.d(TAG, "destroySource(): broadcastId=" + broadcastId);

        // Call native interface to cleanup native resources
        if (mNativeInterface != null) {
            mNativeInterface.destroySource(broadcastId);
        }
    }

    /**
     * Update source metadata (change BIS selection).
     * Leaves current source then rejoins with new metadata.
     */
    public void updateSourceMetadata(BluetoothLeBroadcastMetadata metadata) {
        if (DBG) Log.d(TAG, "updateSourceMetadata(): " + metadata);

        if (metadata == null) {
            Log.e(TAG, "updateSourceMetadata(): metadata is null");
            return;
        }

        int broadcastId = metadata.getBroadcastId();

        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
        if (descriptor == null) {
            Log.e(TAG, "updateSourceMetadata(): No descriptor found for broadcastId=" + broadcastId);
            notifyOnSourceMetadataUpdateFailed(broadcastId, metadata,
                    BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS);
            return;
        }

        if (descriptor.mSinkState != LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED) {
            Log.e(TAG, "updateSourceMetadata(): Source not in BIG_SYNCED state, current state=" +
                  LeAudioBroadcastSinkStackEvent.sinkStateToString(descriptor.mSinkState));
            notifyOnSourceMetadataUpdateFailed(broadcastId, metadata,
                    BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS);
            return;
        }

        descriptor.mPendingMetadataUpdate = metadata;
        if (DBG) Log.d(TAG, "Saved pending metadata update for broadcastId=" + broadcastId);

        leaveSource(broadcastId);
    }

    /**
     * Get all synced broadcast sink states
     */
    public List<BluetoothLeBroadcastSinkState> getAllSyncedSinkState() {
        if (DBG) Log.d(TAG, "getAllSyncedSinkState()");

        List<BluetoothLeBroadcastSinkState> states = new ArrayList<>();
        for (Integer broadcastId : mBroadcastSinkDescriptors.keySet()) {
            BluetoothLeBroadcastSinkState state = generateBroadcastSinkState(broadcastId);
            if (state != null) {
                states.add(state);
            }
        }
        return states;
    }

    /**
     * Get source metadata for a specific broadcast
     */
    public BluetoothLeBroadcastMetadata getSourceMetadata(int broadcastId) {
        if (DBG) Log.d(TAG, "getSourceMetadata(): broadcastId=" + broadcastId);

        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
        return (descriptor != null) ? descriptor.mMetadata : null;
    }

    /**
     * Helper function to generate BluetoothLeBroadcastSinkState from internal descriptor
     *
     * @param broadcastId the broadcast ID
     * @return BluetoothLeBroadcastSinkState or null if descriptor not found
     */
    private BluetoothLeBroadcastSinkState generateBroadcastSinkState(int broadcastId) {
        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
        if (descriptor == null) {
            Log.w(TAG, "generateBroadcastSinkState: No descriptor found for broadcastId=" + broadcastId);
            return null;
        }

        BluetoothLeBroadcastMetadata metadata = descriptor.mMetadata;
        if (metadata == null) {
            Log.w(TAG, "generateBroadcastSinkState: No metadata found for broadcastId=" + broadcastId);
            return null;
        }

        // Extract subgroup information from metadata
        List<BluetoothLeBroadcastSubgroup> subgroups = metadata.getSubgroups();
        int numSubgroups = subgroups.size();

        // Build BIS sync state and subgroup metadata lists
        BluetoothLeBroadcastSinkState.Builder builder = new BluetoothLeBroadcastSinkState.Builder();
        builder.setBroadcastId(broadcastId);
        builder.setSinkState(descriptor.mSinkState);
        builder.setNumSubgroups(numSubgroups);

        boolean isBigSynced = (descriptor.mSinkState == LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED);

        // For each subgroup, extract BIS sync state and metadata
        for (BluetoothLeBroadcastSubgroup subgroup : subgroups) {
            // Calculate BIS sync state bitmask from stored bisIndices if BIG is synced
            long bisSyncState = 0;
            if (isBigSynced && descriptor.mBisIndices != null && !descriptor.mBisIndices.isEmpty()) {
                for (int bisIndex : descriptor.mBisIndices) {
                    bisSyncState |= (1L << (bisIndex - 1));
                }
            }
            builder.addBisSyncState(bisSyncState);

            // Add subgroup metadata
            builder.addSubgroupMetadata(subgroup.getContentMetadata());
        }

        return builder.build();
    }

    /**
     * Get maximum source capacity (max PA syncs)
     */
    public int getMaximumSourceCapacity() {
        if (DBG) Log.d(TAG, "getMaximumSourceCapacity: " + MAX_PA_SYNC_SOURCES);
        return MAX_PA_SYNC_SOURCES;
    }

    /**
     * Helper function to check if a source is currently in BIG sync state
     * (either syncing or already synced to BIG)
     *
     * @param broadcastId the broadcast ID to check
     * @return true if source is in BIG_SYNCING or BIG_SYNCED state, false otherwise
     */
    private boolean isSourceInBigSyncState(int broadcastId) {
        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
        if (descriptor == null) {
            return false;
        }
        int state = descriptor.mSinkState;
        return (state == LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCING ||
                state == LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED);
    }

    /**
     * Helper function to count how many sources are currently in BIG sync state
     * (either syncing or already synced to BIG)
     *
     * @return the number of sources currently in BIG_SYNCING or BIG_SYNCED state
     */
    private int getActiveBigSyncSourcesCount() {
        int count = 0;
        for (LeAudioBroadcastSinkDescriptor descriptor : mBroadcastSinkDescriptors.values()) {
            int state = descriptor.mSinkState;
            if (state == LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCING ||
                state == LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED) {
                count++;
            }
        }
        return count;
    }

    /**
     * Helper function to check if a source can be added (PA sync)
     * Similar to canBroadcastBeCreated pattern in LeAudioService
     *
     * @param broadcastId the broadcast ID to check
     * @return BluetoothStatusCodes.SUCCESS if source can be added, or appropriate error reason code
     */
    private int canSourceBeAdded(int broadcastId) {
        // Check for duplicate add request
        LeAudioBroadcastSinkDescriptor existingDescriptor = mBroadcastSinkDescriptors.get(broadcastId);
        if (existingDescriptor != null) {
            Log.w(TAG, "canSourceBeAdded(): Duplicate add request for broadcastId=" + broadcastId);
            return BluetoothLeBroadcastSinkState.REASON_DUPLICATE_ADD_REQUEST;
        }

        // Check if maximum PA sync capacity has been reached
        int currentPaSyncCount = mBroadcastSinkDescriptors.size();
        if (currentPaSyncCount >= MAX_PA_SYNC_SOURCES) {
            Log.w(TAG, "canSourceBeAdded(): Maximum PA sync capacity reached, current=" + currentPaSyncCount +
                  ", max=" + MAX_PA_SYNC_SOURCES);
            return BluetoothLeBroadcastSinkState.REASON_MAX_PA_SYNC_REACHED;
        }

        return BluetoothStatusCodes.SUCCESS;
    }

    /**
     * Helper function to check if a source can be joined (BIG sync)
     * Similar to canBroadcastBeCreated pattern in LeAudioService
     *
     * @param broadcastId the broadcast ID to check
     * @param metadata the broadcast metadata containing encryption info and broadcast code
     * @return BluetoothStatusCodes.SUCCESS if source can be joined, or appropriate error reason code
     */
    private int canSourceBeJoined(int broadcastId, BluetoothLeBroadcastMetadata metadata) {
        // Check encryption requirements first
        if (metadata != null && metadata.isEncrypted()) {
            byte[] broadcastCode = metadata.getBroadcastCode();

            // Check if broadcast code is missing for encrypted source
            if (broadcastCode == null || broadcastCode.length == 0) {
                Log.w(TAG, "canSourceBeJoined(): Encrypted source requires broadcast code, broadcastId=" + broadcastId);
                return BluetoothLeBroadcastSinkState.REASON_ENCRYPTION_FAILED_NO_KEY;
            }

            // Validate broadcast code length (must be 4-16 octets per Bluetooth spec)
            if (broadcastCode.length < 4 || broadcastCode.length > 16) {
                Log.w(TAG, "canSourceBeJoined(): Invalid broadcast code length=" + broadcastCode.length +
                      " (must be 4-16 octets), broadcastId=" + broadcastId);
                return BluetoothLeBroadcastSinkState.REASON_ENCRYPTION_FAILED_BAD_CODE;
            }
        }

        // Check for duplicate join request
        if (isSourceInBigSyncState(broadcastId)) {
            Log.w(TAG, "canSourceBeJoined(): Duplicate join request for broadcastId=" + broadcastId);
            return BluetoothLeBroadcastSinkState.REASON_DUPLICATE_JOIN_REQUEST;
        }

        // Check if maximum BIG sync capacity has been reached
        int activeBigSyncCount = getActiveBigSyncSourcesCount();
        if (activeBigSyncCount >= MAX_BIG_SYNC_SOURCES) {
            Log.w(TAG, "canSourceBeJoined(): Maximum BIG sync capacity reached, current=" + activeBigSyncCount +
                  ", max=" + MAX_BIG_SYNC_SOURCES);
            return BluetoothLeBroadcastSinkState.REASON_MAX_BIG_SYNC_REACHED;
        }

        return BluetoothStatusCodes.SUCCESS;
    }

    /**
     * Process stack event from native layer
     */
    void messageFromNative(LeAudioBroadcastSinkStackEvent event) {
        if (DBG) Log.d(TAG, "messageFromNative(): " + event);

        // Post event to handler for processing on main thread
        mHandler.post(() -> {
            switch (event.type) {
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_ADD_FAILED:
                    if (DBG) Log.d(TAG, "Source add failed: broadcastId=" + event.broadcastId + ", reason=" + event.reason);

                    // Notify SDK callbacks
                    notifyOnSourceAddFailed(event.broadcastId, event.reason);
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_JOIN_FAILED:
                    if (DBG) Log.d(TAG, "Source join failed: broadcastId=" + event.broadcastId + ", reason=" + event.reason);

                    // Check if this was part of metadata update flow
                    LeAudioBroadcastSinkDescriptor joinFailDescriptor = mBroadcastSinkDescriptors.get(event.broadcastId);
                    if (joinFailDescriptor != null && joinFailDescriptor.mPendingMetadataUpdate != null) {
                        // Metadata update failed during rejoin
                        notifyOnSourceMetadataUpdateFailed(event.broadcastId, joinFailDescriptor.mPendingMetadataUpdate,
                            BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
                        joinFailDescriptor.mPendingMetadataUpdate = null;
                    } else {
                        // Normal join failed
                        notifyOnSourceJoinFailed(event.broadcastId, BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
                    }
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_LEAVE_FAILED:
                    if (DBG) Log.d(TAG, "Source leave failed: broadcastId=" + event.broadcastId + ", reason=" + event.reason);

                    // Check if this was part of metadata update flow
                    LeAudioBroadcastSinkDescriptor leaveFailDescriptor = mBroadcastSinkDescriptors.get(event.broadcastId);
                    if (leaveFailDescriptor != null && leaveFailDescriptor.mPendingMetadataUpdate != null) {
                        // Metadata update failed during leave
                        notifyOnSourceMetadataUpdateFailed(event.broadcastId, leaveFailDescriptor.mPendingMetadataUpdate,
                            BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
                        leaveFailDescriptor.mPendingMetadataUpdate = null;
                    } else {
                        // Normal leave failed
                        notifyOnSourceLeaveFailed(event.broadcastId, BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
                    }
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_REMOVE_FAILED:
                    if (DBG) Log.d(TAG, "Source remove failed: broadcastId=" + event.broadcastId + ", reason=" + event.reason);

                    // Notify SDK callbacks
                    notifyOnSourceRemoveFailed(event.broadcastId, BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_DESTROYED:
                    if (DBG) Log.d(TAG, "Source destroyed: broadcastId=" + event.broadcastId + ", reason=" + event.reason);

                    // Notify based on reason code using defined constants
                    if (event.reason == LeAudioBroadcastSinkStackEvent.SOURCE_DESTROYED_REASON_PA_SYNC_LOST) {
                        // PA sync lost - notify as unexpected removal
                        notifyOnSourceRemoved(event.broadcastId,
                            BluetoothLeBroadcastSinkState.REASON_PA_SYNC_LOST);
                    } else {
                        // Normal destruction - notify as user requested removal
                        notifyOnSourceRemoved(event.broadcastId,
                            BluetoothLeBroadcastSinkState.REASON_LOCAL_APP_REQUEST);
                    }

                    // Resource cleanup completed in native layer, now clean up local cache
                    mBroadcastSinkDescriptors.remove(event.broadcastId);
                    if (DBG) Log.d(TAG, "Cleaned up local cache for broadcastId=" + event.broadcastId);

                    // Remove from found sources cache
                    synchronized (mFoundSources) {
                        if (mFoundSources.remove(event.broadcastId) != null) {
                            if (DBG) Log.d(TAG, "Removed destroyed source from found sources cache: broadcastId=0x" +
                                           Integer.toHexString(event.broadcastId));
                        }
                    }
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_METADATA_CHANGED:
                    if (DBG) Log.d(TAG, "Source metadata changed: broadcastId=" + event.broadcastId);

                    // Business logic: Update descriptor metadata
                    if (event.metadata != null) {
                        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(event.broadcastId);
                        if (descriptor == null) {
                            descriptor = new LeAudioBroadcastSinkDescriptor();
                            mBroadcastSinkDescriptors.put(event.broadcastId, descriptor);
                        }
                        descriptor.mMetadata = event.metadata;

                        // Call notifyOnSourceAdded if this is the first time metadata is received
                        if (!descriptor.mIsSourceAddedNotified) {
                            descriptor.mIsSourceAddedNotified = true;
                            if (DBG) Log.d(TAG, "First metadata received, calling notifyOnSourceAdded for broadcastId=" + event.broadcastId);
                            notifyOnSourceAdded(event.metadata);
                        } else {
                            notifyOnSourceMetadataChanged(event.broadcastId, event.metadata);
                        }
                    }
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_AUDIO_SESSION_CREATED:
                    if (DBG) Log.d(TAG, "Audio session created: success=" + (event.valueInt1 == 1));
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_STATE_CHANGED:
                    // Get or create descriptor for this broadcast
                    LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(event.broadcastId);
                    if (descriptor == null) {
                        descriptor = new LeAudioBroadcastSinkDescriptor();
                        mBroadcastSinkDescriptors.put(event.broadcastId, descriptor);
                    }

                    // Update descriptor state
                    int previousState = descriptor.mSinkState;
                    descriptor.mSinkState = event.valueInt1;
                    if (DBG) Log.d(TAG, "State changed: broadcastId=" + event.broadcastId
                            + ", previousState=" + LeAudioBroadcastSinkStackEvent.sinkStateToString(previousState)
                            + ", newState=" + LeAudioBroadcastSinkStackEvent.sinkStateToString(event.valueInt1));

                    // Handle state transitions and notify appropriate callbacks
                    switch (descriptor.mSinkState) {
                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_IDLE:
                            if (DBG) Log.d(TAG, "Sink state: IDLE for broadcastId=" + event.broadcastId);
                            // Only clear active device if it matches the source device from metadata
                            if (mActiveBroadcastInDevice != null && descriptor.mMetadata != null) {
                                BluetoothDevice sourceDevice = descriptor.mMetadata.getSourceDevice();
                                if (sourceDevice != null && sourceDevice.equals(mActiveBroadcastInDevice)) {
                                    if (DBG) Log.d(TAG, "Clearing active device as it matches source device: " + sourceDevice);
                                    updateBroadcastActiveInDevice(null, mActiveBroadcastInDevice, true);
                                } else {
                                    if (DBG) Log.d(TAG, "Active device does not match source device, not clearing");
                                }
                            }

                            // Transition to IDLE - notification will be sent in SOURCE_DESTROYED event
                            // Destroy source resources when transitioning to IDLE
                            destroySource(event.broadcastId);
                            break;

                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_PA_SYNCING:
                            if (DBG) Log.d(TAG, "Sink state: PA_SYNCING for broadcastId=" + event.broadcastId);
                            // PA sync in progress
                            break;

                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_PA_SYNCED:
                            if (DBG) Log.d(TAG, "Sink state: PA_SYNCED for broadcastId=" + event.broadcastId);

                            // Only clear active device if it matches the source device from metadata
                            if (mActiveBroadcastInDevice != null && descriptor.mMetadata != null) {
                                BluetoothDevice sourceDevice = descriptor.mMetadata.getSourceDevice();
                                if (sourceDevice != null && sourceDevice.equals(mActiveBroadcastInDevice)) {
                                    if (DBG) Log.d(TAG, "Clearing active device as it matches source device: " + sourceDevice);
                                    updateBroadcastActiveInDevice(null, mActiveBroadcastInDevice, true);
                                }
                            }
                            // Transition to PA_SYNCED - check if coming from DISABLING
                            if (previousState == LeAudioBroadcastSinkStackEvent.SINK_STATE_DISABLING) {
                                // Check if this is part of updateSourceMetadata flow
                                if (descriptor.mPendingMetadataUpdate != null) {
                                    if (DBG) Log.d(TAG, "Rejoining source with updated metadata for broadcastId=" + event.broadcastId);
                                    // Rejoin with the new metadata (don't notify onSourceLeft for metadata update)
                                    joinSource(descriptor.mPendingMetadataUpdate);
                                } else {
                                    // Intentional leave (not metadata update)
                                    notifyOnSourceLeft(event.broadcastId,
                                        BluetoothLeBroadcastSinkState.REASON_LOCAL_APP_REQUEST);
                                }
                            } else if (previousState == LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED) {
                                // BIG sync lost unexpectedly (BIG_SYNCED -> PA_SYNCED)
                                notifyOnSourceLeft(event.broadcastId,
                                    BluetoothLeBroadcastSinkState.REASON_BIG_SYNC_LOST);

                                // Clear any pending metadata update since this was unexpected
                                descriptor.mPendingMetadataUpdate = null;
                            }
                            break;

                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCING:
                            if (DBG) Log.d(TAG, "Sink state: BIG_SYNCING for broadcastId=" + event.broadcastId);
                            // BIG sync in progress
                            break;

                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED:
                            if (DBG) Log.d(TAG, "Sink state: BIG_SYNCED for broadcastId=" + event.broadcastId);
                            // BIG sync established, receiving broadcast audio
                            // Update active broadcast input device using mSourceDevice from metadata
                            if (descriptor.mMetadata != null && descriptor.mMetadata.getSourceDevice() != null) {
                                BluetoothDevice sourceDevice = descriptor.mMetadata.getSourceDevice();
                                if (DBG) Log.d(TAG, "Setting broadcast input active device from metadata: " + sourceDevice);
                                updateBroadcastActiveInDevice(sourceDevice, mActiveBroadcastInDevice, true);
                            }

                            // Check if this was a rejoin after metadata update
                            if (descriptor.mPendingMetadataUpdate != null) {
                                // Metadata update completed successfully
                                notifyOnSourceMetadataUpdated(event.broadcastId, descriptor.mMetadata);
                                descriptor.mPendingMetadataUpdate = null;
                            } else {
                                // Normal join
                                notifyOnSourceJoined(event.broadcastId);
                            }
                            break;

                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_DISABLING:
                            if (DBG) Log.d(TAG, "Sink state: DISABLING for broadcastId=" + event.broadcastId);
                            // Disabling BIG sync (transitioning from BIG_SYNCED to PA_SYNCED)
                            break;

                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_STOPPING:
                            if (DBG) Log.d(TAG, "Sink state: STOPPING for broadcastId=" + event.broadcastId);
                            // Stopping PA sync (transitioning to IDLE)
                            break;

                        default:
                            Log.w(TAG, "Unknown sink state: " + descriptor.mSinkState + " for broadcastId=" + event.broadcastId);
                            break;
                    }
                    break;
                default:
                    Log.e(TAG, "Unknown stack event type: " + event.type);
                    break;
            }
        });
    }

    // Private notify methods for SDK callbacks - ONLY invoke SDK callbacks, no business logic
    private void notifyOnSearchStarted(int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSearchStarted(reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSearchStarted", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSearchStartFailed(int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSearchStartFailed(reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSearchStartFailed", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSearchStopped(int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSearchStopped(reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSearchStopped", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceFound(int broadcastId, ScanResult result) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceFound(broadcastId, result);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceFound", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceAdded(BluetoothLeBroadcastMetadata metadata) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceAdded(metadata);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceAdded", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceAddFailed(int broadcastId, int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceAddFailed(broadcastId, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceAddFailed", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceJoined(int broadcastId) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceJoined(broadcastId);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceJoined", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceJoinFailed(int broadcastId, int reason) {
        // Get metadata from descriptor
        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
        BluetoothLeBroadcastMetadata metadata = (descriptor != null) ? descriptor.mMetadata : null;
        if (metadata == null) {
            Log.w(TAG, "notifyOnSourceJoinFailed(): No metadata found for broadcastId=" + broadcastId);
        }

        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceJoinFailed(metadata, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceJoinFailed", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceLeft(int broadcastId, int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceLeft(broadcastId, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceLeft", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceLeaveFailed(int broadcastId, int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceLeaveFailed(broadcastId, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceLeaveFailed", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceRemoved(int broadcastId, int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceRemoved(broadcastId, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceRemoved", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceRemoveFailed(int broadcastId, int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceRemoveFailed(broadcastId, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceRemoveFailed", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceMetadataChanged(int broadcastId, BluetoothLeBroadcastMetadata metadata) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceMetadataChanged(broadcastId, metadata);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceMetadataChanged", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceMetadataUpdated(int broadcastId, BluetoothLeBroadcastMetadata metadata) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceMetadataUpdated(broadcastId, metadata);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceMetadataUpdated", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSourceMetadataUpdateFailed(int broadcastId, BluetoothLeBroadcastMetadata metadata, int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceMetadataUpdateFailed(broadcastId, metadata, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSourceMetadataUpdateFailed", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    /**
     * Helper method to check if a UUID is contained in the scan filters
     * Following the same pattern as BassClientService
     */
    private static boolean containsUuid(List<ScanFilter> filters, ParcelUuid uuid) {
        if (filters == null || uuid == null) {
            return false;
        }

        for (ScanFilter filter : filters) {
            if (filter.getServiceDataUuid() != null &&
                (filter.getServiceDataUuid().equals(uuid))) {
                return true;
            }
            if (filter.getServiceUuid() != null &&
                uuid.equals(filter.getServiceUuid())) {
                return true;
            }
        }
        return false;
    }

    /**
     * Scan callback wrapper for broadcast sink scanning
     */
    private class BroadcastSinkScanCallbackWrapper extends IScannerCallback.Stub {
        private static final int SCANNER_ID_NOT_INITIALIZED = -2;
        private static final int SCANNER_ID_INITIALIZING = -1;

        // Basic Audio Announcement Service UUID (0x1851)
        private static final ParcelUuid BAAS_UUID =
            ParcelUuid.fromString("00001852-0000-1000-8000-00805F9B34FB");

        private final List<ScanFilter> mBaasUuidFilters = new ArrayList<ScanFilter>();
        private int mScannerId = SCANNER_ID_NOT_INITIALIZED;
        private ScanSettings mScanSettings;

        void registerAndStartScan(List<ScanFilter> filters, ScanSettings settings) {
            mScanSettings = settings;
            synchronized (this) {
                if (mScannerId == SCANNER_ID_INITIALIZING) {
                    Log.d(TAG, "registerAndStartScan: Scanner is already initializing");
                    notifyOnSearchStarted(BluetoothStatusCodes.ERROR_ALREADY_IN_TARGET_STATE);
                    return;
                }

                if (mAdapterService == null) {
                    Log.e(TAG, "registerAndStartScan: AdapterService is null");
                    notifyOnSearchStarted(BluetoothStatusCodes.ERROR_UNKNOWN);
                    return;
                }

                ScanController controller = mAdapterService.getBluetoothScanController();
                if (controller == null) {
                    Log.d(TAG, "registerAndStartScan: ScanController is null");
                    notifyOnSearchStarted(BluetoothStatusCodes.ERROR_UNKNOWN);
                    return;
                }

                // Add provided filters to our filter list
                if (filters != null) {
                    mBaasUuidFilters.addAll(filters);
                }

                // Check if BAAS UUID filter is already present, following BassClientService pattern
                if (!containsUuid(mBaasUuidFilters, BAAS_UUID)) {
                    byte[] serviceData = {0x00, 0x00, 0x00}; // Broadcast_ID placeholder
                    byte[] serviceDataMask = {0x00, 0x00, 0x00}; // Match any broadcast ID

                    mBaasUuidFilters.add(
                            new ScanFilter.Builder()
                                    .setServiceData(BAAS_UUID, serviceData, serviceDataMask)
                                    .build());
                }

                mScannerId = SCANNER_ID_INITIALIZING;
                controller.registerScannerInternal(this, getAttributionSource(), null);
            }
        }

        void stopScanAndUnregister() {
            synchronized (this) {
                if (mAdapterService == null) {
                    Log.e(TAG, "stopScanAndUnregister: AdapterService is null");
                    notifyOnSearchStopped(BluetoothStatusCodes.ERROR_UNKNOWN);
                    return;
                }

                ScanController controller = mAdapterService.getBluetoothScanController();
                if (controller == null) {
                    Log.d(TAG, "stopScanAndUnregister: ScanController is null");
                    notifyOnSearchStopped(BluetoothStatusCodes.ERROR_UNKNOWN);
                    return;
                }

                if (mScannerId >= 0) {
                    controller.stopScanInternal(mScannerId);
                    controller.unregisterScannerInternal(mScannerId);
                }
                mBaasUuidFilters.clear();
                mScannerId = SCANNER_ID_NOT_INITIALIZED;
                mScanSettings = null;
            }
        }
/*
        boolean isScanActive() {
            synchronized (this) {
                return mScannerId >= 0;
            }
        }
*/
        @Override
        public void onScannerRegistered(int status, int scannerId) {
            Log.d(TAG, "onScannerRegistered: Status: " + status + ", id:" + scannerId);
            synchronized (this) {
                if (status != BluetoothStatusCodes.SUCCESS) {
                    Log.e(TAG, "onScannerRegistered: Scanner registration failed: " + status);
                    notifyOnSearchStarted(BluetoothStatusCodes.ERROR_UNKNOWN);
                    mScannerId = SCANNER_ID_NOT_INITIALIZED;
                    return;
                }
                mScannerId = scannerId;

                if (mAdapterService == null) {
                    Log.e(TAG, "onScannerRegistered: AdapterService is null");
                    notifyOnSearchStarted(BluetoothStatusCodes.ERROR_UNKNOWN);
                    return;
                }

                ScanController controller = mAdapterService.getBluetoothScanController();
                if (controller == null) {
                    Log.d(TAG, "onScannerRegistered: ScanController is null");
                    notifyOnSearchStarted(BluetoothStatusCodes.ERROR_UNKNOWN);
                    return;
                }

                // Use provided settings or create default if null
                ScanSettings scanSettings = mScanSettings;
                if (scanSettings == null) {
                    scanSettings = new ScanSettings.Builder()
                            .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
                            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                            .setLegacy(false)
                            .build();
                }

                // Use the filters that were configured in registerAndStartScan
                controller.startScanInternal(scannerId, scanSettings, mBaasUuidFilters);

                synchronized (mStateLock) {
                    mSearchInProgress = true;
                }
                notifyOnSearchStarted(BluetoothStatusCodes.REASON_LOCAL_APP_REQUEST);
            }
        }

        @Override
        public void onScanResult(ScanResult result) {
            if (DBG) Log.d(TAG, "onScanResult:" + result);
            synchronized (this) {
                if (mScannerId < 0) {
                    Log.d(TAG, "onScanResult: Ignoring result as scan stopped.");
                    return;
                }
            }

            // Process scan result and notify about found broadcast source
            if (result != null && result.getDevice() != null) {
                Log.d(TAG, "Broadcast Source Found:" + result.getDevice());

                // Parse broadcast metadata from scan result
                ScanRecord scanRecord = result.getScanRecord();
                if (scanRecord == null) {
                    Log.e(TAG, "onScanResult: Null scan record");
                    return;
                }

                // Parse broadcast ID from BAAS service data (0x1852)
                Integer broadcastId = parseBroadcastId(scanRecord);
                if (broadcastId == null || broadcastId == -1) {
                    Log.d(TAG, "onScanResult: Invalid broadcast ID");
                    return;
                }

                Log.d(TAG, "Broadcast ID parsed: " + broadcastId);

                // Check if this broadcast has already been found
                synchronized (mFoundSources) {
                    if (mFoundSources.containsKey(broadcastId)) {
                        if (DBG) Log.d(TAG, "Broadcast already found, checking for metadata changes: broadcastId=0x" +
                                       Integer.toHexString(broadcastId));

                        // Check for public metadata changes in existing source
                        checkAndNotifyPublicMetadataChanges(broadcastId, scanRecord);
                        return;
                    }

                    // Add to cache
                    mFoundSources.put(broadcastId, result);
                    if (DBG) Log.d(TAG, "Added broadcast to found sources: broadcastId=0x" +
                                   Integer.toHexString(broadcastId));
                }

                // Notify framework callbacks about the broadcast source found
                notifyOnSourceFound(broadcastId, result);
            }
        }

        @Override
        public void onBatchScanResults(List<ScanResult> batchResults) {
            // Handle batch scan results if needed
        }

        @Override
        public void onFoundOrLost(boolean onFound, ScanResult scanResult) {
            // Handle found/lost events if needed
        }

        @Override
        public void onScanManagerErrorCallback(int errorCode) {
            Log.d(TAG, "onScanManagerErrorCallback: errorCode = " + errorCode);
            synchronized (this) {
                if (mScannerId < 0) {
                    return;
                }
            }
            mScannerId = SCANNER_ID_NOT_INITIALIZED;

            synchronized (mStateLock) {
                mSearchInProgress = false;
            }
            notifyOnSearchStopped(BluetoothStatusCodes.ERROR_UNKNOWN);
        }
    }

    /**
     * Parse broadcast ID from scan record
     * Following BassClientService pattern
     */
    private static Integer parseBroadcastId(android.bluetooth.le.ScanRecord scanRecord) {
        if (scanRecord == null) {
            Log.e(TAG, "parseBroadcastId: Null scan record");
            return -1;
        }

        Map<ParcelUuid, byte[]> serviceData = scanRecord.getServiceData();
        if (serviceData == null) {
            Log.e(TAG, "parseBroadcastId: Null service data");
            return -1;
        }

        // BAAS UUID (0x1852) contains the broadcast ID
        ParcelUuid BAAS_UUID = ParcelUuid.fromString("00001852-0000-1000-8000-00805F9B34FB");
        if (serviceData.containsKey(BAAS_UUID)) {
            byte[] bId = serviceData.get(BAAS_UUID);
            if (bId != null && bId.length >= 3) {
                // Parse 3-byte broadcast ID (little-endian)
                int broadcastId = (0x00FF0000 & (bId[2] << 16));
                broadcastId |= (0x0000FF00 & (bId[1] << 8));
                broadcastId |= (0x000000FF & bId[0]);
                return broadcastId;
            }
        } else {
            Log.e(TAG, "parseBroadcastId: No broadcast ID in service data");
        }

        return -1;
    }

    /**
     * Update the active broadcast input device and report to audio framework
     * Similar to updateBroadcastActiveDevice() in LeAudioService
     *
     * @param newDevice new active broadcast input device
     * @param previousDevice previous active broadcast input device
     * @param suppressNoisyIntent whether to suppress noisy intent
     */
    private void updateBroadcastActiveInDevice(
            BluetoothDevice newDevice,
            BluetoothDevice previousDevice,
            boolean suppressNoisyIntent) {
        mActiveBroadcastInDevice = newDevice;

        if (DBG) Log.d(TAG, "updateBroadcastActiveInDevice: newDevice: " + newDevice +
                       ", previousDevice: " + previousDevice +
                       ", suppressNoisyIntent: " + suppressNoisyIntent);

        if (mAudioManager == null) {
            Log.e(TAG, "updateBroadcastActiveInDevice: AudioManager is null");
            return;
        }

        /* LE_AUDIO_BROADCAST_SINK is still not supported in Audio framework,
         * Skip update Bluetooth Active Device changed
        mAudioManager.handleBluetoothActiveDeviceChanged(
                newDevice, previousDevice, getBroadcastSinkProfile(suppressNoisyIntent));
        */
    }

    /**
     * Get broadcast sink profile connection info
     *
     * @param suppressNoisyIntent whether to suppress noisy intent
     * @return BluetoothProfileConnectionInfo for broadcast sink
     */
    BluetoothProfileConnectionInfo getBroadcastSinkProfile(boolean suppressNoisyIntent) {
        Parcel parcel = Parcel.obtain();
        parcel.writeInt(BluetoothProfile.LE_AUDIO_BROADCAST_SINK);
        parcel.writeBoolean(suppressNoisyIntent);
        parcel.writeInt(-1); // Volume not applicable for broadcast sink
        parcel.writeBoolean(false); // isLeOutput - false for sink (input)
        parcel.setDataPosition(0);

        BluetoothProfileConnectionInfo profileInfo =
                BluetoothProfileConnectionInfo.CREATOR.createFromParcel(parcel);
        parcel.recycle();
        return profileInfo;
    }

    /**
     * Helper method to handle broadcast input audio device added event
     *
     * @param device the broadcast input device that was added
     * @param type the audio device type
     * @param isSink whether the device is a sink
     * @param isSource whether the device is a source
     */
    void handleBroadcastInDeviceAdded(
            BluetoothDevice device, int type, boolean isSink, boolean isSource) {
        if (DBG) {
            Log.d(TAG, "handleBroadcastInDeviceAdded: device=" + device
                    + ", type=" + type
                    + ", isSink=" + isSink
                    + ", isSource=" + isSource);
        }

        // Broadcast sink is an input device (not a sink from audio framework perspective)
        if (isSink) {
            Log.w(TAG, "handleBroadcastInDeviceAdded: ignoring sink device, expected source");
            return;
        }

        // Check if this is the broadcast input device we're expecting
        if (!device.equals(mActiveBroadcastInDevice)) {
            Log.w(TAG, "handleBroadcastInDeviceAdded: device mismatch, expected="
                    + mActiveBroadcastInDevice + ", got=" + device);
            return;
        }

        if (DBG) {
            Log.d(TAG, "Broadcast sink audio device added successfully: " + device);
        }
    }

    /**
     * Helper method to handle broadcast input audio device removed event
     *
     * @param device the broadcast input device that was removed
     * @param type the audio device type
     * @param isSink whether the device is a sink
     * @param isSource whether the device is a source
     */
    void handleBroadcastInDeviceRemoved(
            BluetoothDevice device, int type, boolean isSink, boolean isSource) {
        if (DBG) {
            Log.d(TAG, "handleBroadcastInDeviceRemoved: device=" + device
                    + ", type=" + type
                    + ", isSink=" + isSink
                    + ", isSource=" + isSource);
        }

        // Broadcast sink is an input device (not a sink from audio framework perspective)
        if (isSink) {
            Log.w(TAG, "handleBroadcastInDeviceRemoved: ignoring sink device, expected source");
            return;
        }

        if (DBG) {
            Log.d(TAG, "Broadcast sink audio device removed: " + device);
        }
    }

    /**
     * AudioManager callback for monitoring broadcast input audio device changes
     */
    private class AudioManagerAudioDeviceCallback extends AudioDeviceCallback {
        @Override
        public void onAudioDevicesAdded(AudioDeviceInfo[] addedDevices) {
            if (!isAvailable()) {
                Log.e(TAG, "onAudioDevicesAdded: Service not available");
                return;
            }

            for (AudioDeviceInfo deviceInfo : addedDevices) {
                Log.d(TAG, "onAudioDevicesAdded: device type=" + deviceInfo.getType()
                        + ", isSink=" + deviceInfo.isSink()
                        + ", isSource=" + deviceInfo.isSource());

                // Only handle TYPE_BLE_BROADCAST devices
                if (deviceInfo.getType() != AudioDeviceInfo.TYPE_BLE_BROADCAST) {
                    continue;
                }

                String address = deviceInfo.getAddress();
                if (address.equals("00:00:00:00:00:00")) {
                    continue;
                }

                byte[] addressBytes = Utils.getBytesFromAddress(address);
                BluetoothDevice device = mAdapterService.getDeviceFromByte(addressBytes);

                handleBroadcastInDeviceAdded(
                        device, deviceInfo.getType(), deviceInfo.isSink(), deviceInfo.isSource());
            }
        }

        @Override
        public void onAudioDevicesRemoved(AudioDeviceInfo[] removedDevices) {
            if (!isAvailable()) {
                Log.e(TAG, "onAudioDevicesRemoved: Service not available");
                return;
            }

            for (AudioDeviceInfo deviceInfo : removedDevices) {
                Log.d(TAG, "onAudioDevicesRemoved: device type=" + deviceInfo.getType()
                        + ", isSink=" + deviceInfo.isSink()
                        + ", isSource=" + deviceInfo.isSource());

                // Only handle TYPE_BLE_BROADCAST devices
                if (deviceInfo.getType() != AudioDeviceInfo.TYPE_BLE_BROADCAST) {
                    continue;
                }

                String address = deviceInfo.getAddress();
                if (address.equals("00:00:00:00:00:00")) {
                    continue;
                }

                byte[] addressBytes = Utils.getBytesFromAddress(address);
                BluetoothDevice device = mAdapterService.getDeviceFromByte(addressBytes);

                handleBroadcastInDeviceRemoved(
                        device, deviceInfo.getType(), deviceInfo.isSink(), deviceInfo.isSource());
            }
        }
    }

    /**
     * Parse broadcast name from scan record using BassUtils
     *
     * @param scanRecord The scan record to parse
     * @return The broadcast name, or null if not found or invalid
     */
    private static String parseBroadcastName(ScanRecord scanRecord) {
        return BassUtils.getBroadcastName(scanRecord);
    }

    /**
     * Check if broadcast is public using BassUtils
     *
     * @param scanRecord The scan record to check
     * @return true if broadcast is public, false otherwise
     */
    private static boolean isPublicBroadcast(ScanRecord scanRecord) {
        return BassUtils.getPublicBroadcastData(scanRecord) != null;
    }

    /**
     * Check for public metadata changes and notify native layer if changed
     *
     * @param broadcastId The broadcast ID to check
     * @param scanRecord The new scan record to compare
     */
    private void checkAndNotifyPublicMetadataChanges(int broadcastId, ScanRecord scanRecord) {
        if (DBG) Log.d(TAG, "checkAndNotifyPublicMetadataChanges: broadcastId=0x" +
                       Integer.toHexString(broadcastId));

        // Get the existing descriptor
        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
        if (descriptor == null || descriptor.mMetadata == null) {
            if (DBG) Log.d(TAG, "checkAndNotifyPublicMetadataChanges: No existing metadata for broadcastId=0x" +
                           Integer.toHexString(broadcastId));
            return;
        }

        // Only check for public broadcasts
        if (!isPublicBroadcast(scanRecord)) {
            if (DBG) Log.d(TAG, "checkAndNotifyPublicMetadataChanges: Not a public broadcast, skipping");
            return;
        }

        // Parse current scan record data
        String broadcastName = parseBroadcastName(scanRecord);
        String newBroadcastName =  (broadcastName != null) ? broadcastName : "";
        byte[] newPublicMetadata = null;

        PublicBroadcastData pbData = BassUtils.getPublicBroadcastData(scanRecord);
        if (pbData != null) {
            newPublicMetadata = pbData.getMetadata();
        }

        // Get existing data from metadata
        String previousBroadcastName = descriptor.mMetadata.getBroadcastName();
        byte[] previousPublicMetadata = descriptor.mMetadata.getPublicBroadcastMetadata().getRawMetadata();

        // Compare broadcast name
        boolean nameChanged = !Objects.equals(previousBroadcastName, newBroadcastName);

        // Compare public metadata
        boolean metadataChanged = !Arrays.equals(previousPublicMetadata, newPublicMetadata);

        if (nameChanged || metadataChanged) {
            if (DBG) Log.d(TAG, "checkAndNotifyPublicMetadataChanges: Changes detected for broadcastId=0x" +
                           Integer.toHexString(broadcastId) +
                           ", nameChanged=" + nameChanged +
                           ", metadataChanged=" + metadataChanged +
                           ", newBroadcastName=" + newBroadcastName +
                           ", newPublicMetadata=" + (newPublicMetadata != null ? newPublicMetadata.length + " bytes" : "null"));

            // Call the native interface to update public metadata
            if (mNativeInterface != null) {
                mNativeInterface.sourcePublicMetadataChanged(broadcastId,
                                                           newBroadcastName,
                                                           newPublicMetadata);
            }
        }
    }

}
