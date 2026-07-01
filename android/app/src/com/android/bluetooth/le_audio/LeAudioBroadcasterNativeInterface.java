/*
 * Copyright 2021 HIMSA II K/S - www.himsa.com.
 * Represented by EHIMA - www.ehima.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Defines the native interface that is used by state machine/service to
 * send or receive messages from the native stack. This file is registered
 * for the native methods in the corresponding JNI C++ file.
 */
package com.android.bluetooth.le_audio;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.util.Log;

import com.android.bluetooth.Utils;
import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;

/** LeAudio Native Interface to/from JNI. */
public class LeAudioBroadcasterNativeInterface {
    private static final String TAG = LeAudioBroadcasterNativeInterface.class.getSimpleName();

    private final BluetoothAdapter mAdapter;

    @GuardedBy("INSTANCE_LOCK")
    private static LeAudioBroadcasterNativeInterface sInstance;

    private static final Object INSTANCE_LOCK = new Object();

    private LeAudioBroadcasterNativeInterface() {
        mAdapter = BluetoothAdapter.getDefaultAdapter();
        if (mAdapter == null) {
            Log.wtf(TAG, "No Bluetooth Adapter Available");
        }
    }

    /** Get singleton instance. */
    public static LeAudioBroadcasterNativeInterface getInstance() {
        synchronized (INSTANCE_LOCK) {
            if (sInstance == null) {
                sInstance = new LeAudioBroadcasterNativeInterface();
            }
            return sInstance;
        }
    }

    /** Set singleton instance. */
    @VisibleForTesting
    static void setInstance(LeAudioBroadcasterNativeInterface instance) {
        synchronized (INSTANCE_LOCK) {
            sInstance = instance;
        }
    }

    private static void sendMessageToService(LeAudioStackEvent event) {
        LeAudioService service = LeAudioService.getLeAudioService();
        if (service != null) {
            service.messageFromNative(event);
        } else {
            Log.e(TAG, "Event ignored, service not available: " + event);
        }
    }

    @VisibleForTesting
    public BluetoothDevice getDevice(byte[] address) {
        return mAdapter.getRemoteDevice(address);
    }

    // Callbacks from the native stack back into the Java framework.
    @VisibleForTesting
    public void onBroadcastCreated(int broadcastId, boolean success) {
        Log.d(TAG, "onBroadcastCreated: broadcastId=" + broadcastId);
        LeAudioStackEvent event =
                new LeAudioStackEvent(LeAudioStackEvent.EVENT_TYPE_BROADCAST_CREATED);

        event.valueInt1 = broadcastId;
        event.valueBool1 = success;
        sendMessageToService(event);
    }

    @VisibleForTesting
    public void onBroadcastDestroyed(int broadcastId) {
        Log.d(TAG, "onBroadcastDestroyed: broadcastId=" + broadcastId);
        LeAudioStackEvent event =
                new LeAudioStackEvent(LeAudioStackEvent.EVENT_TYPE_BROADCAST_DESTROYED);

        event.valueInt1 = broadcastId;
        sendMessageToService(event);
    }

    @VisibleForTesting
    public void onBroadcastStateChanged(int broadcastId, int state) {
        Log.d(TAG, "onBroadcastStateChanged: broadcastId=" + broadcastId + " state=" + state);
        LeAudioStackEvent event =
                new LeAudioStackEvent(LeAudioStackEvent.EVENT_TYPE_BROADCAST_STATE);

        /* NOTICE: This is a fake device to satisfy Audio Manager in the upper
         * layers which needs a device instance to route audio streams to the
         * proper module (here it's Bluetooth). Broadcast has no concept of a
         * destination or peer device therefore this fake device was created.
         * For now it's only important that this device is a Bluetooth device.
         */
        event.device = getDevice(Utils.getBytesFromAddress("FF:FF:FF:FF:FF:FF"));
        event.valueInt1 = broadcastId;
        event.valueInt2 = state;
        sendMessageToService(event);
    }

    @VisibleForTesting
    public void onBroadcastMetadataChanged(int broadcastId, BluetoothLeBroadcastMetadata metadata) {
        Log.d(TAG, "onBroadcastMetadataChanged: broadcastId=" + broadcastId);
        LeAudioStackEvent event =
                new LeAudioStackEvent(LeAudioStackEvent.EVENT_TYPE_BROADCAST_METADATA_CHANGED);

        event.valueInt1 = broadcastId;
        event.broadcastMetadata = metadata;
        sendMessageToService(event);
    }

    @VisibleForTesting
    public void onBroadcastAudioSessionCreated(boolean success) {
        Log.d(TAG, "onBroadcastAudioSessionCreated: success=" + success);
        LeAudioStackEvent event =
                new LeAudioStackEvent(LeAudioStackEvent.EVENT_TYPE_BROADCAST_AUDIO_SESSION_CREATED);

        event.device = getDevice(Utils.getBytesFromAddress("FF:FF:FF:FF:FF:FF"));
        event.valueBool1 = success;
        sendMessageToService(event);
    }

    @VisibleForTesting
    public void onDbigStatusChanged(int dbigHandle, int status, int devId, byte[] name,
                                     int numBis, char[] bisDevIds, int broadcastFeatures) {
        Log.d(TAG, "onDbigStatusChanged: dbigHandle=" + dbigHandle + " status=0x"
                + Integer.toHexString(status)
                + ", devId=0x" + Integer.toHexString(devId)
                + ", numBis=" + numBis
                + ", broadcastFeatures=0x" + Integer.toHexString(broadcastFeatures));
        LeAudioStackEvent event = new LeAudioStackEvent(
                LeAudioStackEvent.EVENT_TYPE_BROADCAST_DBIG_STATUS_CHANGED);
        event.valueInt1 = dbigHandle;
        event.valueInt2 = status;
        event.dbigDevId = devId;
        event.dbigName = name;
        event.dbigNumBis = numBis;
        event.dbigBisDevIds = bisDevIds;
        event.dbigBroadcastFeatures = broadcastFeatures;
        sendMessageToService(event);
    }

    @VisibleForTesting
    public void onRemoveDeviceDbigComplete(int dbigHandle, int devId, int status) {
        Log.d(TAG, "onRemoveDeviceDbigComplete: dbigHandle=" + dbigHandle
                + ", devId=0x" + Integer.toHexString(devId)
                + ", status=0x" + Integer.toHexString(status));
        LeAudioStackEvent event = new LeAudioStackEvent(
                LeAudioStackEvent.EVENT_TYPE_BROADCAST_REMOVE_DEVICE_DBIG_COMPLETE);
        event.valueInt1 = dbigHandle;
        event.valueInt2 = devId;
        event.valueInt3 = status;
        sendMessageToService(event);
    }

    @VisibleForTesting
    public void onSyncOnlyModeActive(int broadcastId) {
        Log.d(TAG, "onSyncOnlyModeActive: broadcastId=" + broadcastId);
        LeAudioStackEvent event = new LeAudioStackEvent(
                LeAudioStackEvent.EVENT_TYPE_BROADCAST_SYNC_ONLY_ACTIVE);
        event.valueInt1 = broadcastId;
        sendMessageToService(event);
    }

    /**
     * Initializes the native interface.
     *
     * <p>priorities to configure.
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void init() {
        initNative();
    }

    /** Stop the Broadcast Service. */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void stop() {
        stopNative();
    }

    /** Cleanup the native interface. */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void cleanup() {
        cleanupNative();
    }

    /**
     * Creates LeAudio Broadcast instance.
     *
     * @param isPublicBroadcast this BIG is public broadcast
     * @param broadcastName BIG broadcast name
     * @param broadcastCode BIG broadcast code
     * @param publicMetadata BIG public broadcast meta data
     * @param qualityArray BIG sub group audio quality array
     * @param metadataArray BIG sub group metadata array
     *     <p>qualityArray and metadataArray use the same subgroup index
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void createBroadcast(
            boolean isPublicBroadcast,
            String broadcastName,
            byte[] broadcastCode,
            byte[] publicMetadata,
            int[] qualityArray,
            byte[][] metadataArray) {
        createBroadcastNative(
                isPublicBroadcast,
                broadcastName,
                broadcastCode,
                publicMetadata,
                qualityArray,
                metadataArray);
    }

        /**
     * Creates LeAudio enhanced Broadcast instance.
     *
     * @param broadcastName BIG broadcast name
     * @param broadcastCode BIG broadcast code
     * @param isoInterval Isointerval for broadcast.
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void createEnhancedBroadcast(
            String broadcastName,
            byte[] broadcastCode,
            int[] qualityArray,
            byte[][] metadataArray,
            float isoInterval) {
        createEnhancedBroadcastNative(
                broadcastName,
                broadcastCode,
                qualityArray,
                metadataArray,
                isoInterval);
    }

    /**
     * Update LeAudio Broadcast instance metadata.
     *
     * @param broadcastId broadcast instance identifier
     * @param broadcastName BIG broadcast name
     * @param publicMetadata BIG public broadcast meta data
     * @param metadataArray BIG sub group metadata array
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void updateMetadata(
            int broadcastId, String broadcastName, byte[] publicMetadata, byte[][] metadataArray) {
        updateMetadataNative(broadcastId, broadcastName, publicMetadata, metadataArray);
    }

    /**
     * Start LeAudio Broadcast instance.
     *
     * @param broadcastId broadcast instance identifier
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void startBroadcast(int broadcastId) {
        startBroadcastNative(broadcastId);
    }

    /**
     * Stop LeAudio Broadcast instance.
     *
     * @param broadcastId broadcast instance identifier
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void stopBroadcast(int broadcastId) {
        stopBroadcastNative(broadcastId);
    }

    /**
     * Stop an enhanced (DUPLEX/DBIG) broadcast with a specific TExitDbig mode.
     *
     * @param broadcastId broadcast instance identifier
     * @param mode HCI TExitDbig mode: EXIT (1) or TERMINATE (2)
     */
    public void stopEnhancedBroadcast(int broadcastId, int mode) {
        Log.d(TAG, "stopEnhancedBroadcast: broadcastId=" + broadcastId
                + ", mode=0x" + Integer.toHexString(mode));
        stopEnhancedBroadcastNative(broadcastId, mode);
    }

    /**
     * Pause LeAudio Broadcast instance.
     *
     * @param broadcastId broadcast instance identifier
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void pauseBroadcast(int broadcastId) {
        pauseBroadcastNative(broadcastId);
    }

    /**
     * Destroy LeAudio Broadcast instance.
     *
     * @param broadcastId broadcast instance identifier
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void destroyBroadcast(int broadcastId) {
        destroyBroadcastNative(broadcastId);
    }

    /** Get all LeAudio Broadcast instance states. */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void getBroadcastMetadata(int broadcastId) {
        getBroadcastMetadataNative(broadcastId);
    }

    /**
     * Set attributes (DevID and Name) for the broadcast source.
     *
     * @param devId Device ID packed into 2 octets (12-bit value with 4-bit padding)
     * @param name  Device name packed into 10 octets (UTF-8 encoded, zero-padded)
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void setAttributes(byte[] devId, byte[] name) {
        setAttributesNative(devId, name);
    }

    /**
     * Set Join Control mode for the broadcast source.
     *
     * @param enable true to enable join control, false to disable
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void setJoinControl(boolean enable) {
        setJoinControlNative(enable);
    }

    /**
     * Request the controller to remove a specific device from the DBIG.
     *
     * @param devId   12-bit device identifier (0-4095)
     * @param name    up to 10-byte name (zero-padded to 10 bytes in native layer)
     * @param reason  HCI reason code (e.g. 0x13 = Remote User Terminated)
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void removeDeviceDbig(int devId, byte[] name, int reason) {
        Log.d(TAG, "removeDeviceDbig: devId=0x" + Integer.toHexString(devId)
                + ", reason=0x" + Integer.toHexString(reason));
        removeDeviceDbigNative(devId, name, reason);
    }

    /** Accept PGP terminate request (sends TExitDbig TERMINATE as PGO). */
    public void acceptTerminateDbig(int broadcastId) {
        Log.d(TAG, "acceptTerminateDbig: broadcastId=" + broadcastId);
        acceptTerminateDbigNative(broadcastId);
    }

    /** Reject PGP terminate request (sends TExitDbig REJECT_TERMINATE as PGO). */
    public void rejectTerminateDbig(int broadcastId) {
        Log.d(TAG, "rejectTerminateDbig: broadcastId=" + broadcastId);
        rejectTerminateDbigNative(broadcastId);
    }

    /**
     * Callback: HCI_VS_LE_Texit_DBIG_Complete on PGO side.
     * status=0x00 success (DBIG terminated); other = error.
     */
    public void onTexitDbigComplete(int broadcastId, int dbigHandle, int status) {
        Log.d(TAG, "onTexitDbigComplete (PGO): broadcastId=" + broadcastId
                + ", dbigHandle=" + dbigHandle
                + ", status=0x" + Integer.toHexString(status));
        LeAudioStackEvent event = new LeAudioStackEvent(
                LeAudioStackEvent.EVENT_TYPE_BROADCAST_TEXIT_DBIG_COMPLETE);
        event.valueInt1 = broadcastId;
        event.valueInt2 = dbigHandle;
        event.valueInt3 = status;
        sendMessageToService(event);
    }

    // Native methods that call into the JNI interface
    private native void initNative();

    private native void stopNative();

    private native void cleanupNative();

    private native void createBroadcastNative(
            boolean isPublicBroadcast,
            String broadcastName,
            byte[] broadcastCode,
            byte[] publicMetadata,
            int[] qualityArray,
            byte[][] metadataArray);

    private native void createEnhancedBroadcastNative(
            String broadcastName,
            byte[] broadcastCode,
            int[] qualityArray,
            byte[][] metadataArray,
            float  isoInterval);

    private native void updateMetadataNative(
            int broadcastId, String broadcastName, byte[] publicMetadata, byte[][] metadataArray);

    private native void startBroadcastNative(int broadcastId);

    private native void stopBroadcastNative(int broadcastId);

    private native void stopEnhancedBroadcastNative(int broadcastId, int mode);

    private native void pauseBroadcastNative(int broadcastId);

    private native void destroyBroadcastNative(int broadcastId);

    // -------------------------------------------------------------------------
    // Enhanced DBIG / Supported-States APIs (duplex broadcast source)
    // -------------------------------------------------------------------------

    /**
     * Read LE Supported States for duplex broadcast source flow.
     * Blocks until the HCI command completes (up to 1 s) and returns the
     * PGO FW capability bitmask: bit0=Terminate, bit1=Remove Device.
     * @return broadcast_states bitmask, or 0 if not available / timed out
     */
    public int readSupportedStates() {
        Log.d(TAG, "readSupportedStates");
        return readSupportedStatesNative();
    }

    /**
     * Get DBIG parameters from the native stack for use in the PA vendor-specific LTV.
     * Returns 12 bytes in order:
     *   [0]=dbig_feature_set  [1]=bis_detection_attempts  [2]=max_payload_dbig_control
     *   [3]=bis_control_event_interval  [4]=send_exit  [5]=pgp_timeout
     *   [6]=pgo_timeout  [7]=sgo_timeout  [8]=join_timeout
     *   [9]=exit_timeout  [10]=remove_timeout  [11]=terminate_timeout
     *
     * @return 12-byte DBIG parameter array, or null if not available
     */
    public byte[] getDbigParams() {
        return getDbigParamsNative();
    }

    /**
     * Returns Broadcast_States from HCI_VS_LE_Read_Supported_States.
     * Populated after {@link #readSupportedStates()} completes.
     *
     * @return broadcast_states bitmask, or -1 if not yet available
     */
    public int getEnhancedBroadcastCap() {
        return getEnhancedBroadcastCapNative();
    }

    private native int  readSupportedStatesNative();
    private native byte[] getDbigParamsNative();
    private native int getEnhancedBroadcastCapNative();

    private native void getBroadcastMetadataNative(int broadcastId);

    private native void setAttributesNative(byte[] devId, byte[] name);

    private native void setJoinControlNative(boolean enable);
    private native void removeDeviceDbigNative(int devId, byte[] name, int reason);
    private native void acceptTerminateDbigNative(int broadcastId);
    private native void rejectTerminateDbigNative(int broadcastId);
    private native void setDbigSyncOnlyNative(int dbigHandle, boolean enable);
    private native void notifyCallStateNative(int broadcastId, boolean isCallActive);

    /**
     * Send HCI VS LE DBIG Sync-Only command.
     * Enable=true puts the DBIG in sync-only mode (BIG stays alive, no audio data).
     * Enable=false resumes normal audio data transfer.
     * Called from services when an HFP call or VR session preempts the duplex broadcast.
     *
     * @param dbigHandle DBIG handle (0 for the currently active DBIG)
     * @param enable     true to enter sync-only, false to exit
     */
    public void setDbigSyncOnly(int dbigHandle, boolean enable) {
        setDbigSyncOnlyNative(dbigHandle, enable);
    }

    public void notifyCallState(int broadcastId, boolean isCallActive) {
        notifyCallStateNative(broadcastId, isCallActive);
    }
}
