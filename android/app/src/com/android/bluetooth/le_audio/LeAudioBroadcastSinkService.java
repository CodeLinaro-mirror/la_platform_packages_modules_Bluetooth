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
import android.bluetooth.BluetoothA2dp;
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
import android.os.UserHandle;
import android.sysprop.BluetoothProperties;
import android.util.Log;

import com.android.bluetooth.BluetoothMethodProxy;
import com.android.bluetooth.Utils;
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

    private static final int DEFAULT_VOLUME_LEVEL = 15;

    // Handler message codes
    private static final int MSG_START              = 1;
    private static final int MSG_STOP               = 2;
    /** Posted after MSG_STOP to clear the active broadcast device once teardown completes. */
    private static final int MSG_REMOVE_ACTIVE_DEVICE = 3;

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

    // Dedicated background thread that owns the handler looper.
    // Keeps all MSG_START / MSG_STOP setParameters() calls off the main service thread.
    private final android.os.HandlerThread mHandlerThread;

    // Handler for processing stack events
    private final Handler mHandler;

    // AudioManager callback for monitoring broadcast input audio device changes
    private final AudioManagerAudioDeviceCallback mAudioManagerAudioDeviceCallback =
            new AudioManagerAudioDeviceCallback();

    // AudioServer state callback for crash/recovery handling
    private final AudioManager.AudioServerStateCallback mAudioServerStateCallback =
            new AudioManager.AudioServerStateCallback() {
                @Override
                public void onAudioServerDown() {
                    Log.w(TAG, "AudioServer down — AChat parameters will be re-applied on recovery");
                }

                @Override
                public void onAudioServerUp() {
                    Log.i(TAG, "AudioServer up — re-applying AChat parameters if streaming");
                    if (mIsEnhancedStreaming) {
                        Log.i(TAG, "AudioServer recovered: re-sending MSG_START for enhanced stream");
                        mHandler.sendEmptyMessage(MSG_START);
                    }
                }
            };

    // Active broadcast input device
    private volatile BluetoothDevice mActiveBroadcastInDevice;

    // True while an enhanced (enhanced broadcast) broadcast session is actively streaming.
    // Used to re-send MSG_START after an AudioServer restart.
    private volatile boolean mIsEnhancedStreaming = false;

    /**
     * Source device pending active-device notification for enhanced broadcast sink.
     * Set in startEnhancedBroadcastSink() and consumed in EVENT_TYPE_AUDIO_SESSION_CREATED
     * so that MM audio is notified only after both HAL sessions are confirmed started
     * (mirroring the broadcast source pattern).
     */
    private volatile BluetoothDevice mPendingEnhancedSourceDevice = null;

    // Callback management
    private final RemoteCallbackList<IBluetoothLeBroadcastSinkCallback> mCallbacks =
            new RemoteCallbackList<>();

    // State management
    @GuardedBy("mStateLock")
    private boolean mSearchInProgress = false;
    private final Object mStateLock = new Object();

    // enhanced broadcast default parameters are now managed entirely in the JNI C++ layer.

    /**
     * Internal descriptor class for maintaining broadcast sink state and metadata.
     * Extended with enhanced broadcast fields for enhanced broadcast support.
     */
    private static class LeAudioBroadcastSinkDescriptor {
        LeAudioBroadcastSinkDescriptor() {
            mSinkState = LeAudioBroadcastSinkStackEvent.SINK_STATE_IDLE;
            mMetadata = null;
            mIsSourceAddedNotified = false;
            mIsEnhanced = false;
            mPendingMetadataUpdate = null;
            mBisIndices = new ArrayList<>();
        }

        public Integer mSinkState;
        public BluetoothLeBroadcastMetadata mMetadata;
        public boolean mIsSourceAddedNotified;
        /** True when BASE data parsing revealed >= 3 BISes in at least one subgroup. */
        public boolean mIsEnhanced;
        public BluetoothLeBroadcastMetadata mPendingMetadataUpdate;
        public final List<Integer> mBisIndices;
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

        // Initialize a dedicated background thread for the handler so that
        // AudioManager.setParameters() calls never block the main service thread.
        mHandlerThread = new android.os.HandlerThread("LeAudioBroadcastSinkHandler");
        mHandlerThread.start();

        // Initialize handler for processing stack events
        mHandler = new Handler(mHandlerThread.getLooper()) {
            @Override
            public void handleMessage(android.os.Message msg) {
                switch (msg.what) {
                    case MSG_START:
                        if (DBG) Log.d(TAG, "MSG_START: enabling AChat TX+RX");
                        if (mAudioManager != null) {
                            if (DBG) Log.d(TAG, "MSG_START: >>> setParameters(achat_tx_enable=true)");
                            mAudioManager.setParameters("achat_tx_enable=true");
                            if (DBG) Log.d(TAG, "MSG_START: <<< setParameters(achat_tx_enable=true) returned");
                            if (DBG) Log.d(TAG, "MSG_START: >>> setParameters(achat_rx_enable=true)");
                            mAudioManager.setParameters("achat_rx_enable=true");
                            if (DBG) Log.d(TAG, "MSG_START: <<< setParameters(achat_rx_enable=true) returned");
                        }
                        break;

                    case MSG_STOP:
                        if (DBG) Log.d(TAG, "MSG_STOP: disabling AChat RX then TX");
                        if (mAudioManager != null) {
                            if (DBG) Log.d(TAG, "MSG_STOP: >>> setParameters(achat_rx_enable=false) [BLOCKS until sink HAL acked]");
                            mAudioManager.setParameters("achat_rx_enable=false");
                            if (DBG) Log.d(TAG, "MSG_STOP: <<< setParameters(achat_rx_enable=false) returned");
                            if (DBG) Log.d(TAG, "MSG_STOP: >>> setParameters(achat_tx_enable=false) [BLOCKS until source HAL acked]");
                            mAudioManager.setParameters("achat_tx_enable=false");
                            if (DBG) Log.d(TAG, "MSG_STOP: <<< setParameters(achat_tx_enable=false) returned");
                        }
                        // BIG sync is now fully terminated (RX paths removed, TX paths removed,
                        // BIG sync terminated). The onSinkStopped notification is sent from
                        // EVENT_TYPE_BIG_SYNC_TERMINATED which is posted by the C++ layer
                        // via OnBigSyncTerminated() → JNI → onBigSyncTerminated().
                        break;
                    case MSG_REMOVE_ACTIVE_DEVICE:
                        // Runs after MSG_STOP has fully completed (handler is FIFO).
                        // At this point all HAL teardown is done; safe to clear the active device.
                        if (DBG) Log.d(TAG, "MSG_REMOVE_ACTIVE_DEVICE: clearing active broadcast device: "
                                + mActiveBroadcastInDevice);
                        if (mActiveBroadcastInDevice != null) {
                            updateBroadcastActiveInDevice(null, mActiveBroadcastInDevice, true);
                        }
                        break;
                    default:
                        super.handleMessage(msg);
                        break;
                }
            }
        };

        // Initialize native interface with max source capacity
        mNativeInterface.init(MAX_PA_SYNC_SOURCES);

        // Register audio device callback
        mAudioManager.registerAudioDeviceCallback(mAudioManagerAudioDeviceCallback, mHandler);

        // Register audio server state callback for crash/recovery
        mAudioManager.setAudioServerStateCallback(mHandler::post, mAudioServerStateCallback);

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

        // Unregister audio server state callback
        mAudioManager.clearAudioServerStateCallback();

        // Shut down the handler thread gracefully
        mHandlerThread.quitSafely();

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

            // Enhanced broadcast sources are not public sources.
            // No public broadcast metadata handling needed.
            if (DBG) Log.d(TAG, "addSource(): address=" + address + ", advSid=" + advSid);

            mNativeInterface.addSource(address, addressType, advSid, broadcastId, rssi,
                                      null /* broadcastName */, false /* isPublic */,
                                      null /* publicMetadata */, 0 /* publicFeatures */);
        }
    }

    /**
     * Join an enhanced (enhanced broadcast) broadcast source.
     *
     * <p>The entire enhanced broadcast setup → BIG_CREATE_SYNC → serial ISO data path setup
     * sequence is handled autonomously by the JNI C++ layer for <em>all</em>
     * BISes in the BIG (no BIS selection filtering is applied).  Java receives
     * {@link LeAudioBroadcastSinkStackEvent#EVENT_TYPE_STATE_CHANGED} with state
     * {@link LeAudioBroadcastSinkStackEvent#SINK_STATE_BIG_SYNCED} only after
     * every ISO data path has been configured.
     *
     * @param metadata {@link BluetoothLeBroadcastMetadata} of the enhanced broadcast source
     */
    public void startEnhancedBroadcastSink(BluetoothLeBroadcastMetadata metadata) {
        if (DBG) Log.d(TAG, "startEnhancedBroadcastSink(): " + metadata);

        if (metadata == null) {
            Log.e(TAG, "startEnhancedBroadcastSink(): metadata is null");
            notifyOnSinkStartFailed(-1, BluetoothLeBroadcastSinkState.REASON_BAD_PARAMETERS);
            return;
        }

        int broadcastId = metadata.getBroadcastId();

                        // Validate capacity and encryption
        int canJoinResult = canSourceBeJoined(broadcastId, metadata);
        if (canJoinResult != BluetoothStatusCodes.SUCCESS) {
            notifyOnSinkStartFailed(broadcastId, canJoinResult);
            return;
        }

        // Create or update descriptor
        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
        if (descriptor == null) {
            descriptor = new LeAudioBroadcastSinkDescriptor();
            mBroadcastSinkDescriptors.put(broadcastId, descriptor);
        }
        descriptor.mMetadata = metadata;

        if (DBG) Log.d(TAG, "startEnhancedBroadcastSink(): broadcastId=" + broadcastId
                + " — delegating full enhanced broadcast/ISO sequence to JNI C++ layer");

        // Hand off to C++: enhanced broadcast setup → BIG_CREATE_SYNC → ISO data paths for ALL BISes.
        // Pass null bisIndices so the native layer syncs to all BISes in the BIG.
        if (mNativeInterface != null) {
            mNativeInterface.startEnhancedBroadcastSink(broadcastId, metadata.getBroadcastCode());
        }

        // Store the source device so EVENT_TYPE_AUDIO_SESSION_CREATED can notify
        // MM audio after both HAL sessions are confirmed started.
        BluetoothDevice sourceDevice = metadata.getSourceDevice();
        mPendingEnhancedSourceDevice = sourceDevice;
        if (DBG) Log.d(TAG, "startEnhancedBroadcastSink: stored pending source device: " + sourceDevice
                + " — active device will be set after audio session created");

        // Mark streaming state.
        // MSG_START (achat_tx/rx_enable) is sent from onAudioDevicesAdded() when the
        // AudioManager fires the A2DP device-added callback after updateBroadcastActiveInDevice().
        mIsEnhancedStreaming = true;
        if (DBG) Log.d(TAG, "startEnhancedBroadcastSink: mIsEnhancedStreaming=true");
    }
    /**
     * Leave a broadcast source (stop BIG sync but keep PA synced).
     *
     * <p>{@link AudioManager#setParameters} is a <b>blocking</b> call.
     * The teardown sequence for enhanced (enhanced broadcast) sources is:
     * <ol>
     *   <li>Call native {@code stopEnhancedBroadcastSink()} <em>directly</em> (before posting
     *       {@code MSG_STOP}) so the state machine is still in {@code BIG_SYNCED}
     *       when it validates the state and clears flags.  Native
     *       {@code stopEnhancedBroadcastSink()} does <em>not</em> stop the HAL clients —
     *       {@code MSG_STOP} does that.</li>
     *   <li>Send {@code MSG_STOP} to {@code mHandler}.  The handler calls
     *       {@code setParameters("achat_rx_enable=false")} which <b>blocks</b>
     *       until the sink HAL {@code OnAudioSuspend} is fully acknowledged
     *       (all RX ISO paths removed, {@code ConfirmSuspendRequest} called on
     *       sink HAL).  Only after that returns does
     *       {@code setParameters("achat_tx_enable=false")} execute, which
     *       <b>blocks</b> until the source HAL {@code OnAudioSuspend} is fully
     *       acknowledged (all TX ISO paths removed, BIG sync terminated,
     *       {@code ConfirmSuspendRequest} called on source HAL).</li>
     * </ol>
     */
    public void stopEnhancedBroadcastSink(int broadcastId) {
        if (DBG) Log.d(TAG, "stopEnhancedBroadcastSink(): broadcastId=" + broadcastId);

        // Step 1: Call native stopEnhancedBroadcastSink DIRECTLY (not via handler) so the
        // state machine is still in BIG_SYNCED when it validates state and
        // clears flags.  Native stopEnhancedBroadcastSink does NOT stop the HAL clients.
        if (mNativeInterface != null) {
            if (DBG) Log.d(TAG, "stopEnhancedBroadcastSink: calling native stopEnhancedBroadcastSink for broadcastId=" + broadcastId);
            mNativeInterface.stopEnhancedBroadcastSink(broadcastId);
        }

        // Step 2: Post MSG_STOP to mHandler.
        // setParameters("achat_rx_enable=false") blocks until sink HAL acked.
        // setParameters("achat_tx_enable=false") blocks until source HAL acked.
        // onSinkStopped is notified from EVENT_TYPE_BIG_SYNC_TERMINATED (C++ callback),
        // so broadcastId does not need to be passed to MSG_STOP.
        mIsEnhancedStreaming = false;
        if (DBG) Log.d(TAG, "leaveSource: sending MSG_STOP for broadcastId=" + broadcastId);
        mHandler.sendEmptyMessage(MSG_STOP);

        // Step 3: Post MSG_REMOVE_ACTIVE_DEVICE AFTER MSG_STOP.
        // The handler processes messages in FIFO order, so MSG_REMOVE_ACTIVE_DEVICE
        // will only execute after MSG_STOP has fully completed (including both
        // blocking setParameters calls).  This guarantees the active device is
        // cleared only after the full HAL teardown sequence is done.
        if (DBG) Log.d(TAG, "stopEnhancedBroadcastSink: queuing MSG_REMOVE_ACTIVE_DEVICE after MSG_STOP");
        mHandler.sendEmptyMessage(MSG_REMOVE_ACTIVE_DEVICE);
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
                // -----------------------------------------------------------------
                // Broadcast sink events
                // -----------------------------------------------------------------
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_ADD_FAILED:
                    if (DBG) Log.d(TAG, "Source add failed: broadcastId=" + event.broadcastId + ", reason=" + event.reason);

                    // Notify SDK callbacks
                    notifyOnSourceAddFailed(event.broadcastId, event.reason);
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_JOIN_FAILED:
                    if (DBG) Log.d(TAG, "Source join failed: broadcastId=" + event.broadcastId + ", reason=" + event.reason);

                    // Normal join failed
                    notifyOnSinkStartFailed(event.broadcastId, BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_LEAVE_FAILED:
                    if (DBG) Log.d(TAG, "Source leave failed: broadcastId=" + event.broadcastId + ", reason=" + event.reason);

                    // Normal leave failed
                    notifyOnSinkStopFailed(event.broadcastId, BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
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

                    // Business logic: Store metadata on first receipt (onSourceAdded).
                    // Enhanced broadcast source metadata does not change after initial discovery,
                    // so onSourceMetadataChanged is never called for subsequent events.
                    if (event.metadata != null) {
                        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(event.broadcastId);
                        if (descriptor == null) {
                            descriptor = new LeAudioBroadcastSinkDescriptor();
                            mBroadcastSinkDescriptors.put(event.broadcastId, descriptor);
                        }
                        // Store metadata (only meaningful on first receipt; metadata is stable)
                        descriptor.mMetadata = event.metadata;

                        if (!descriptor.mIsSourceAddedNotified) {
                            // First time full metadata is received — notify app via onSourceAdded.
                            // mIsEnhanced was set by EVENT_TYPE_ENHANCED_SOURCE_DETECTED which
                            // fires before SOURCE_METADATA_CHANGED for the same PA sync cycle.
                            descriptor.mIsSourceAddedNotified = true;
                            if (DBG) Log.d(TAG, "Full metadata received, calling notifyOnSourceAdded"
                                    + " for broadcastId=" + event.broadcastId
                                    + ", isEnhanced=" + descriptor.mIsEnhanced);
                            notifyOnSourceAdded(event.metadata, descriptor.mIsEnhanced);
                        }
                        // No else: enhanced broadcast source metadata is stable after first receipt.
                    }
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_AUDIO_SESSION_CREATED: {
                    boolean sessionSuccess = (event.valueInt1 == 1);
                    if (DBG) Log.d(TAG, "Audio session created: success=" + sessionSuccess);
                    // Notify MM audio with active device change only after both HAL sessions
                    // are confirmed started — mirrors the broadcast source pattern.
                    if (sessionSuccess && mPendingEnhancedSourceDevice != null) {
                        BluetoothDevice pendingDevice = mPendingEnhancedSourceDevice;
                        mPendingEnhancedSourceDevice = null;
                        if (DBG) Log.d(TAG, "Audio session created: notifying MM audio "
                                + "with active device: " + pendingDevice);
                        updateBroadcastActiveInDevice(pendingDevice, mActiveBroadcastInDevice, true);
                    } else if (!sessionSuccess) {
                        // Session failed — clear pending device and streaming flag
                        mPendingEnhancedSourceDevice = null;
                        mIsEnhancedStreaming = false;
                        Log.w(TAG, "Audio session creation failed — cleared pending state");
                    }
                    break;
                }
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
                            // Active device is cleared by MSG_REMOVE_ACTIVE_DEVICE posted in
                            // stopEnhancedBroadcastSink() — no need to clear it here.

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
                            // Active device is cleared by MSG_REMOVE_ACTIVE_DEVICE posted in
                            // stopEnhancedBroadcastSink() — no need to clear it here.

                            // Transition to PA_SYNCED - check if coming from DISABLING
                            if (previousState == LeAudioBroadcastSinkStackEvent.SINK_STATE_DISABLING) {
                                // Intentional leave — notify app
                                notifyOnSinkStopped(event.broadcastId,
                                    BluetoothLeBroadcastSinkState.REASON_LOCAL_APP_REQUEST);
                            } else if (previousState == LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED) {
                                /* BIG sync lost unexpectedly (BIG_SYNCED → PA_SYNCED).
                                 * Leave notification is sent from EVENT_TYPE_BIG_SYNC_LOST
                                 * which carries bigHandle + HCI reason.
                                 * Do NOT duplicate notifyOnSinkStopped here. */
                                if (DBG) Log.d(TAG, "BIG_SYNCED→PA_SYNCED: leave notification "
                                        + "deferred to EVENT_TYPE_BIG_SYNC_LOST for broadcastId="
                                        + event.broadcastId);
                                descriptor.mPendingMetadataUpdate = null;
                            }
                            break;

                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCING:
                            if (DBG) Log.d(TAG, "Sink state: BIG_SYNCING for broadcastId=" + event.broadcastId);
                            // BIG sync in progress
                            break;

                        case LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED:
                            if (DBG) Log.d(TAG, "Sink state: BIG_SYNCED for broadcastId=" + event.broadcastId
                                    + " — join notification sent via EVENT_TYPE_BIG_SYNC_CREATED");
                            /* Active-device update and join/metadata-update notification are
                             * handled in EVENT_TYPE_BIG_SYNC_CREATED (carries bigHandle +
                             * bisHandles).  Do NOT duplicate those calls here. */
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
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_ENHANCED_SOURCE_DETECTED:
                    // BASE data parsing revealed this is an enhanced (enhanced broadcast) source.
                    // Store the flag in the descriptor; it will be passed to onSourceAdded()
                    // when the first metadata arrives via EVENT_TYPE_SOURCE_METADATA_CHANGED.
                    if (DBG) Log.d(TAG, "Enhanced source detected [internal]: broadcastId="
                            + event.broadcastId + ", numBis=" + event.valueInt1);
                    {
                        LeAudioBroadcastSinkDescriptor enhDescriptor =
                                mBroadcastSinkDescriptors.get(event.broadcastId);
                        if (enhDescriptor == null) {
                            enhDescriptor = new LeAudioBroadcastSinkDescriptor();
                            mBroadcastSinkDescriptors.put(event.broadcastId, enhDescriptor);
                        }
                        enhDescriptor.mIsEnhanced = true;
                        if (DBG) Log.d(TAG, "Marked broadcastId=" + event.broadcastId
                                + " as enhanced (numBis=" + event.valueInt1 + ")");
                    }
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_SOURCE_FOUND:
                    // Fired from NativeInterface.onSourceFound() (scan-time ScanResult).
                    if (DBG) Log.d(TAG, "Source found event: broadcastId=" + event.broadcastId);
                    if (event.scanResult != null) {
                        notifyOnSourceFound(event.broadcastId, event.scanResult);
                    }
                    break;
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_BIG_SYNC_CREATED: {
                    // BIG sync established — the only ISO-layer event forwarded to Java.
                    // ISO data path completion and enhanced broadcast setup completion are handled
                    // entirely inside the C++ layer and are NOT forwarded here.
                    if (DBG) Log.d(TAG, "BIG sync created: broadcastId=" + event.broadcastId
                            + ", bigHandle=" + event.valueInt1
                            + ", numBis=" + event.valueInt2);
                    LeAudioBroadcastSinkDescriptor bigCreatedDesc =
                            mBroadcastSinkDescriptors.get(event.broadcastId);
                    if (bigCreatedDesc == null) {
                        bigCreatedDesc = new LeAudioBroadcastSinkDescriptor();
                        mBroadcastSinkDescriptors.put(event.broadcastId, bigCreatedDesc);
                    }
                    bigCreatedDesc.mSinkState = LeAudioBroadcastSinkStackEvent.SINK_STATE_BIG_SYNCED;

                    // BIG sync established — stop scanning for sources and notify the app.
                    synchronized (mStateLock) {
                        if (mSearchInProgress) {
                            if (DBG) Log.d(TAG, "BIG sync created: stopping scan");
                            mScanCallback.stopScanAndUnregister();
                            mSearchInProgress = false;
                            notifyOnSearchStopped(
                                    BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
                        }
                    }

                    // Active-device update is done in startEnhancedBroadcastSink() for enhanced sources.
                    // Always notify onSinkStarted when BIG sync is created.
                    notifyOnSinkStarted(event.broadcastId);
                    break;
                }
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_BIG_SYNC_TERMINATED: {
                    // BIG sync intentionally terminated (user-initiated StopEnhancedBroadcastSink).
                    // All TX and RX ISO data paths have been removed and the controller
                    // has confirmed BIG termination. Notify application via onSinkStopped.
                    if (DBG) Log.d(TAG, "BIG sync terminated: broadcastId=" + event.broadcastId
                            + ", bigHandle=" + event.valueInt1
                            + ", status=0x" + Integer.toHexString(event.reason));
                    notifyOnSinkStopped(event.broadcastId,
                            BluetoothLeBroadcastSinkState.REASON_LOCAL_STACK_REQUEST);
                    break;
                }
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_DBIG_STATUS_CHANGED: {
                    notifyDbigStatusChanged(event.valueInt1, event.valueInt2);
                    break;
                }
                case LeAudioBroadcastSinkStackEvent.EVENT_TYPE_BIG_SYNC_LOST: {
                    // BIG sync lost — the only ISO-layer loss event forwarded to Java.
                    if (DBG) Log.d(TAG, "BIG sync lost: broadcastId=" + event.broadcastId
                            + ", bigHandle=" + event.valueInt1
                            + ", reason=0x" + Integer.toHexString(event.reason));
                    LeAudioBroadcastSinkDescriptor bigLostDesc =
                            mBroadcastSinkDescriptors.get(event.broadcastId);
                    if (bigLostDesc != null) {
                        bigLostDesc.mSinkState = LeAudioBroadcastSinkStackEvent.SINK_STATE_PA_SYNCED;
                        // Clear any pending metadata update since this was unexpected
                        bigLostDesc.mPendingMetadataUpdate = null;
                    }

                    // Stop source + sink audio sessions.
                    mIsEnhancedStreaming = false;
                    if (DBG) Log.d(TAG, "BIG sync lost: sending MSG_STOP");
                    mHandler.sendEmptyMessage(MSG_STOP);
                    // Post MSG_REMOVE_ACTIVE_DEVICE after MSG_STOP so the active device
                    // is cleared only after HAL teardown fully completes (FIFO handler).
                    if (DBG) Log.d(TAG, "BIG sync lost: queuing MSG_REMOVE_ACTIVE_DEVICE after MSG_STOP");
                    mHandler.sendEmptyMessage(MSG_REMOVE_ACTIVE_DEVICE);

                    notifyOnSinkStopped(event.broadcastId,
                            BluetoothLeBroadcastSinkState.REASON_BIG_SYNC_LOST);
                    break;
                }
                default:
                    Log.e(TAG, "Unknown stack event type: " + event.type);
                    break;
            }
        });
    }

    @SuppressLint("AndroidFrameworkRequiresPermission")
    private void notifyDbigStatusChanged(int dbigHandle, int status) {
        if (DBG) Log.d(TAG, "notifyDbigStatusChanged: dbig_handle=" + dbigHandle
                + ", status=0x" + Integer.toHexString(status));
        Intent intent = new Intent("android.bluetooth.action.LE_AUDIO_DBIG_STATUS_CHANGED");
        intent.putExtra("android.bluetooth.extra.DBIG_STATUS", status);
        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY_BEFORE_BOOT
                | Intent.FLAG_RECEIVER_INCLUDE_BACKGROUND);
        sendBroadcastAsUser(
                intent,
                UserHandle.ALL,
                BLUETOOTH_CONNECT,
                Utils.getTempBroadcastOptions().toBundle());
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

    private void notifyOnSourceAdded(BluetoothLeBroadcastMetadata metadata, boolean isEnhanced) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSourceAdded(metadata, isEnhanced);
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

    private void notifyOnSinkStarted(int broadcastId) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSinkStarted(broadcastId);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSinkStarted", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSinkStartFailed(int broadcastId, int reason) {
        // Get metadata from descriptor
        LeAudioBroadcastSinkDescriptor descriptor = mBroadcastSinkDescriptors.get(broadcastId);
        BluetoothLeBroadcastMetadata metadata = (descriptor != null) ? descriptor.mMetadata : null;
        if (metadata == null) {
            Log.w(TAG, "notifyOnSinkStartFailed(): No metadata found for broadcastId=" + broadcastId);
        }

        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSinkStartFailed(metadata, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSinkStartFailed", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSinkStopped(int broadcastId, int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSinkStopped(broadcastId, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSinkStopped", e);
            }
        }
        mCallbacks.finishBroadcast();
    }

    private void notifyOnSinkStopFailed(int broadcastId, int reason) {
        int callbackCount = mCallbacks.beginBroadcast();
        for (int i = 0; i < callbackCount; i++) {
            try {
                mCallbacks.getBroadcastItem(i).onSinkStopFailed(broadcastId, reason);
            } catch (RemoteException e) {
                Log.e(TAG, "Failed to call onSinkStopFailed", e);
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
                        // Enhanced broadcast source metadata does not change after initial discovery.
                        // No need to check for metadata changes.
                        return;
                    }

                    // Add to cache
                    mFoundSources.put(broadcastId, result);
                    if (DBG) Log.d(TAG, "Added broadcast to found sources: broadcastId=0x" +
                                   Integer.toHexString(broadcastId));
                }

                // Notify callbacks with the raw ScanResult.
                // Full metadata (with subgroups / BIS configs) arrives later via
                // onSourceMetadataChanged after PA sync + BASE data parsing.
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


        if (newDevice != null) {
            mAudioManager.handleBluetoothActiveDeviceChanged(newDevice, previousDevice,
                    BluetoothProfileConnectionInfo.createA2dpInfo(true, DEFAULT_VOLUME_LEVEL));
        } else {
            mAudioManager.handleBluetoothActiveDeviceChanged(newDevice, previousDevice,
                    BluetoothProfileConnectionInfo.createA2dpInfo(true, -1));
        }

        // Broadcast ACTION_ACTIVE_DEVICE_CHANGED so that other system components
        // (e.g. Settings, AudioService) are notified of the new A2DP active device.
        Intent intent = new Intent(BluetoothA2dp.ACTION_ACTIVE_DEVICE_CHANGED);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, newDevice);
        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY_BEFORE_BOOT
                | Intent.FLAG_RECEIVER_INCLUDE_BACKGROUND);
        sendBroadcast(intent, BLUETOOTH_CONNECT);
        if (DBG) Log.d(TAG, "Sent ACTION_ACTIVE_DEVICE_CHANGED intent for device: " + newDevice);
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

            if (addedDevices == null) {
                if (DBG) Log.d(TAG, "onAudioDevicesAdded: addedDevices is null, ignoring");
                return;
            }

            // Only process A2DP device-added events when an enhanced broadcast
            // source join is in progress.  For standard (non-enhanced) sources
            // MSG_START is not needed, so ignore the callback.
            if (!mIsEnhancedStreaming) {
                if (DBG) Log.d(TAG, "onAudioDevicesAdded: mIsEnhancedStreaming=false, ignoring");
                return;
            }

            for (AudioDeviceInfo deviceInfo : addedDevices) {
                Log.d(TAG, "onAudioDevicesAdded: device type=" + deviceInfo.getType()
                        + ", isSink=" + deviceInfo.isSink()
                        + ", isSource=" + deviceInfo.isSource());

                String address = deviceInfo.getAddress();
                if (address.equals("00:00:00:00:00:00")) {
                    continue;
                }

                if (deviceInfo.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) {
                    if (DBG) Log.d(TAG, "A2DP device added (enhanced streaming) — sending MSG_START");
                    mHandler.sendEmptyMessage(MSG_START);
                    break;
                }
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

                if (deviceInfo.getType() == AudioDeviceInfo.TYPE_BLUETOOTH_A2DP) {
                }
            }
        }
    }

}
