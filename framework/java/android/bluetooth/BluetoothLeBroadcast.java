/*
 * Copyright (C) 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package android.bluetooth;

import static android.Manifest.permission.BLUETOOTH_CONNECT;
import static android.Manifest.permission.BLUETOOTH_PRIVILEGED;

import static java.util.Objects.requireNonNull;

import android.annotation.CallbackExecutor;
import android.annotation.IntDef;
import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.RequiresNoPermission;
import android.annotation.RequiresPermission;
import android.annotation.SuppressLint;
import android.annotation.SystemApi;
import android.bluetooth.annotations.RequiresBluetoothConnectPermission;
import android.content.AttributionSource;
import android.content.Context;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.CloseGuard;
import android.util.Log;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.Executor;

/**
 * This class provides the public APIs to control the BAP Broadcast Source profile.
 *
 * <p>BluetoothLeBroadcast is a proxy object for controlling the Bluetooth LE Broadcast Source
 * Service via IPC. Use {@link BluetoothAdapter#getProfileProxy} to get the BluetoothLeBroadcast
 * proxy object.
 *
 * @hide
 */
@SystemApi
public final class BluetoothLeBroadcast implements AutoCloseable, BluetoothProfile {
    private static final String TAG = BluetoothLeBroadcast.class.getSimpleName();

    private static final boolean DBG = true;
    private static final boolean VDBG = false;

    /**
     * TExitDbig mode: graceful exit — this PGO leaves without affecting other DBIG members.
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    public static final int DBIG_TEXIT_MODE_EXIT = 1;

    /**
     * TExitDbig mode: terminate — request the entire DBIG to be terminated for all members.
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    public static final int DBIG_TEXIT_MODE_TERMINATE = 2;

    private final CloseGuard mCloseGuard;

    private final BluetoothAdapter mAdapter;
    private final AttributionSource mAttributionSource;

    private IBluetoothLeAudio mService;

    private final Map<Callback, Executor> mCallbackExecutorMap = new HashMap<>();

    @SuppressLint("AndroidFrameworkBluetoothPermission")
    private final IBluetoothLeBroadcastCallback mCallback =
            new IBluetoothLeBroadcastCallback.Stub() {
                @Override
                public void onBroadcastStarted(int reason, int broadcastId) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onBroadcastStarted(reason, broadcastId));
                    }
                }

                @Override
                public void onBroadcastStartFailed(int reason) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onBroadcastStartFailed(reason));
                    }
                }

                @Override
                public void onBroadcastStopped(int reason, int broadcastId) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onBroadcastStopped(reason, broadcastId));
                    }
                }

                @Override
                public void onBroadcastStopFailed(int reason) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onBroadcastStopFailed(reason));
                    }
                }

                @Override
                public void onPlaybackStarted(int reason, int broadcastId) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onPlaybackStarted(reason, broadcastId));
                    }
                }

                @Override
                public void onPlaybackStopped(int reason, int broadcastId) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onPlaybackStopped(reason, broadcastId));
                    }
                }

                @Override
                public void onBroadcastUpdated(int reason, int broadcastId) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onBroadcastUpdated(reason, broadcastId));
                    }
                }

                @Override
                public void onBroadcastUpdateFailed(int reason, int broadcastId) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(
                                () -> callback.onBroadcastUpdateFailed(reason, broadcastId));
                    }
                }

                @Override
                public void onBroadcastMetadataChanged(
                        int broadcastId, BluetoothLeBroadcastMetadata metadata) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(
                                () -> callback.onBroadcastMetadataChanged(broadcastId, metadata));
                    }
                }

                @Override
                public void onRemoveDeviceDbigComplete(int status, int devId) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(
                                () -> callback.onRemoveDeviceDbigComplete(status, devId));
                    }
                }

                @Override
                public void onTexitDbigComplete(int broadcastId, int dbigHandle, int status) {
                    for (Map.Entry<BluetoothLeBroadcast.Callback, Executor> callbackExecutorEntry :
                            mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcast.Callback callback = callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(
                                () -> callback.onTexitDbigComplete(broadcastId, dbigHandle,
                                        status));
                    }
                }
            };

    /**
     * Interface for receiving events related to Broadcast Source
     *
     * @hide
     */
    @SystemApi
    public interface Callback {
        /** @hide */
        @Retention(RetentionPolicy.SOURCE)
        @IntDef(
                value = {
                    BluetoothStatusCodes.ERROR_UNKNOWN,
                    BluetoothStatusCodes.REASON_LOCAL_APP_REQUEST,
                    BluetoothStatusCodes.REASON_LOCAL_STACK_REQUEST,
                    BluetoothStatusCodes.REASON_SYSTEM_POLICY,
                    BluetoothStatusCodes.ERROR_HARDWARE_GENERIC,
                    BluetoothStatusCodes.ERROR_BAD_PARAMETERS,
                    BluetoothStatusCodes.ERROR_LOCAL_NOT_ENOUGH_RESOURCES,
                    BluetoothStatusCodes.ERROR_LE_BROADCAST_INVALID_CODE,
                    BluetoothStatusCodes.ERROR_LE_BROADCAST_INVALID_BROADCAST_ID,
                    BluetoothStatusCodes.ERROR_LE_CONTENT_METADATA_INVALID_PROGRAM_INFO,
                    BluetoothStatusCodes.ERROR_LE_CONTENT_METADATA_INVALID_LANGUAGE,
                    BluetoothStatusCodes.ERROR_LE_CONTENT_METADATA_INVALID_OTHER,
                })
        @interface Reason {}

        /**
         * Callback invoked when broadcast is started, but audio may not be playing.
         *
         * <p>Caller should wait for {@link #onBroadcastMetadataChanged(int,
         * BluetoothLeBroadcastMetadata)} for the updated metadata
         *
         * @param reason for broadcast start
         * @param broadcastId as defined by the Basic Audio Profile
         * @hide
         */
        @SystemApi
        void onBroadcastStarted(@Reason int reason, int broadcastId);

        /**
         * Callback invoked when broadcast failed to start
         *
         * @param reason for broadcast start failure
         * @hide
         */
        @SystemApi
        void onBroadcastStartFailed(@Reason int reason);

        /**
         * Callback invoked when broadcast is stopped
         *
         * @param reason for broadcast stop
         * @hide
         */
        @SystemApi
        void onBroadcastStopped(@Reason int reason, int broadcastId);

        /**
         * Callback invoked when broadcast failed to stop
         *
         * @param reason for broadcast stop failure
         * @hide
         */
        @SystemApi
        void onBroadcastStopFailed(@Reason int reason);

        /**
         * Callback invoked when broadcast audio is playing
         *
         * @param reason for playback start
         * @param broadcastId as defined by the Basic Audio Profile
         * @hide
         */
        @SystemApi
        void onPlaybackStarted(@Reason int reason, int broadcastId);

        /**
         * Callback invoked when broadcast audio is not playing
         *
         * @param reason for playback stop
         * @param broadcastId as defined by the Basic Audio Profile
         * @hide
         */
        @SystemApi
        void onPlaybackStopped(@Reason int reason, int broadcastId);

        /**
         * Callback invoked when encryption is enabled
         *
         * @param reason for encryption enable
         * @param broadcastId as defined by the Basic Audio Profile
         * @hide
         */
        @SystemApi
        void onBroadcastUpdated(@Reason int reason, int broadcastId);

        /**
         * Callback invoked when Broadcast Source failed to update
         *
         * @param reason for update failure
         * @param broadcastId as defined by the Basic Audio Profile
         * @hide
         */
        @SystemApi
        void onBroadcastUpdateFailed(int reason, int broadcastId);

        /**
         * Callback invoked when Broadcast Source metadata is updated
         *
         * @param metadata updated Broadcast Source metadata
         * @param broadcastId as defined by the Basic Audio Profile
         * @hide
         */
        @SystemApi
        void onBroadcastMetadataChanged(
                int broadcastId, @NonNull BluetoothLeBroadcastMetadata metadata);

        /**
         * Callback invoked when the Remove Device DBIG operation completes.
         *
         * @param status 0 on success, non-zero HCI error code on failure
         * @param devId  device ID of the removed PGP (12-bit value from the completion event)
         * @hide
         */
        @SystemApi
        @SuppressLint("UnflaggedApi")
        default void onRemoveDeviceDbigComplete(int status, int devId) {}

        /**
         * Callback delivered when {@code HCI_VS_LE_Texit_DBIG_Complete} is received on PGO.
         * Fired after PGO sends {@code TExitDbig(TERMINATE)} or {@code TExitDbig(REJECT_TERMINATE)}
         * in response to a PGP terminate request (spec §4.9).
         *
         * @param broadcastId broadcast ID of the enhanced broadcast
         * @param dbigHandle  DBIG handle
         * @param status      0x00=DBIG terminated (accepted); other=error/still active
         * @hide
         */
        @SystemApi
        @SuppressLint("UnflaggedApi")
        default void onTexitDbigComplete(int broadcastId, int dbigHandle, int status) {}
    }

    /**
     * Create a BluetoothLeBroadcast proxy object for interacting with the local LE Audio Broadcast
     * Source service.
     *
     * @param context for to operate this API class
     * @hide
     */
    /*package*/ BluetoothLeBroadcast(Context context, BluetoothAdapter adapter) {
        mAdapter = adapter;
        mAttributionSource = mAdapter.getAttributionSource();
        mService = null;

        mCloseGuard = new CloseGuard();
        mCloseGuard.open("close");
    }

    /** @hide */
    @SuppressWarnings("Finalize") // TODO(b/314811467)
    protected void finalize() {
        if (mCloseGuard != null) {
            mCloseGuard.warnIfOpen();
        }
        close();
    }

    /**
     * Not supported since LE Audio Broadcasts do not establish a connection.
     *
     * @hide
     */
    @Override
    @RequiresNoPermission
    public int getConnectionState(@NonNull BluetoothDevice device) {
        throw new UnsupportedOperationException("LE Audio Broadcasts are not connection-oriented.");
    }

    /**
     * Not supported since LE Audio Broadcasts do not establish a connection.
     *
     * @hide
     */
    @Override
    @RequiresNoPermission
    @NonNull
    public List<BluetoothDevice> getDevicesMatchingConnectionStates(@NonNull int[] states) {
        throw new UnsupportedOperationException("LE Audio Broadcasts are not connection-oriented.");
    }

    /**
     * Not supported since LE Audio Broadcasts do not establish a connection.
     *
     * @hide
     */
    @Override
    @RequiresNoPermission
    public @NonNull List<BluetoothDevice> getConnectedDevices() {
        throw new UnsupportedOperationException("LE Audio Broadcasts are not connection-oriented.");
    }

    /**
     * Register a {@link Callback} that will be invoked during the operation of this profile.
     *
     * <p>Repeated registration of the same <var>callback</var> object after the first call to this
     * method will result with IllegalArgumentException being thrown, even when the
     * <var>executor</var> is different. API caller must call {@link #unregisterCallback(Callback)}
     * with the same callback object before registering it again.
     *
     * @param executor an {@link Executor} to execute given callback
     * @param callback user implementation of the {@link Callback}
     * @throws NullPointerException if a null executor, or callback is given, or
     *     IllegalArgumentException if the same <var>callback<var> is already registered.
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void registerCallback(
            @NonNull @CallbackExecutor Executor executor, @NonNull Callback callback) {
        requireNonNull(executor);
        requireNonNull(callback);

        if (DBG) log("registerCallback");

        synchronized (mCallbackExecutorMap) {
            // If the callback map is empty, we register the service-to-app callback
            if (mCallbackExecutorMap.isEmpty()) {
                if (!mAdapter.isEnabled()) {
                    /* If Bluetooth is off, just store callback and it will be registered
                     * when Bluetooth is on
                     */
                    mCallbackExecutorMap.put(callback, executor);
                    return;
                }
                try {
                    final IBluetoothLeAudio service = getService();
                    if (service != null) {
                        service.registerLeBroadcastCallback(mCallback, mAttributionSource);
                    }
                } catch (RemoteException e) {
                    Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
                }
            }

            // Adds the passed in callback to our map of callbacks to executors
            if (mCallbackExecutorMap.containsKey(callback)) {
                throw new IllegalArgumentException("This callback has already been registered");
            }
            mCallbackExecutorMap.put(callback, executor);
        }
    }

    /**
     * Unregister the specified {@link Callback}
     *
     * <p>The same {@link Callback} object used when calling {@link #registerCallback(Executor,
     * Callback)} must be used.
     *
     * <p>Callbacks are automatically unregistered when the application process goes away
     *
     * @param callback user implementation of the {@link Callback}
     * @throws NullPointerException when callback is null or IllegalArgumentException when no
     *     callback is registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void unregisterCallback(@NonNull Callback callback) {
        requireNonNull(callback);

        if (DBG) log("unregisterCallback");

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.remove(callback) == null) {
                throw new IllegalArgumentException("This callback has not been registered");
            }
        }

        // If the callback map is empty, we unregister the service-to-app callback
        if (mCallbackExecutorMap.isEmpty()) {
            try {
                final IBluetoothLeAudio service = getService();
                if (service != null) {
                    service.unregisterLeBroadcastCallback(mCallback, mAttributionSource);
                }
            } catch (RemoteException | IllegalStateException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Start broadcasting to nearby devices using <var>broadcastCode</var> and
     * <var>contentMetadata</var>
     *
     * <p>Encryption will be enabled when <var>broadcastCode</var> is not null.
     *
     * <p>As defined in Volume 3, Part C, Section 3.2.6 of Bluetooth Core Specification, Version
     * 5.3, Broadcast Code is used to encrypt a broadcast audio stream.
     *
     * <p>It must be a UTF-8 string that has at least 4 octets and should not exceed 16 octets.
     *
     * <p>If the provided <var>broadcastCode</var> is non-null and does not meet the above
     * requirements, encryption will fail to enable with reason code {@link
     * BluetoothStatusCodes#ERROR_LE_BROADCAST_INVALID_CODE}
     *
     * <p>Caller can set content metadata such as program information string in
     * <var>contentMetadata</var>
     *
     * <p>On success, {@link Callback#onBroadcastStarted(int, int)} will be invoked with {@link
     * BluetoothStatusCodes#REASON_LOCAL_APP_REQUEST} reason code. On failure, {@link
     * Callback#onBroadcastStartFailed(int)} will be invoked with reason code.
     *
     * <p>In particular, when the number of Broadcast Sources reaches {@link
     * #getMaximumNumberOfBroadcasts()}, this method will fail with {@link
     * BluetoothStatusCodes#ERROR_LOCAL_NOT_ENOUGH_RESOURCES}
     *
     * <p>After broadcast is started, {@link Callback#onBroadcastMetadataChanged(int,
     * BluetoothLeBroadcastMetadata)} will be invoked to expose the latest Broadcast Group metadata
     * that can be shared out of band to set up Broadcast Sink without scanning.
     *
     * <p>Alternatively, one can also get the latest Broadcast Source meta via {@link
     * #getAllBroadcastMetadata()}
     *
     * @param contentMetadata metadata for the default Broadcast subgroup
     * @param broadcastCode Encryption will be enabled when <var>broadcastCode</var> is not null
     * @throws IllegalStateException if callback was not registered
     * @throws NullPointerException if <var>contentMetadata</var> is null
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void startBroadcast(
            @NonNull BluetoothLeAudioContentMetadata contentMetadata,
            @Nullable byte[] broadcastCode) {
        requireNonNull(contentMetadata);
        if (mCallbackExecutorMap.isEmpty()) {
            throw new IllegalStateException("No callback was ever registered");
        }

        if (DBG) log("startBroadcasting");
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.startBroadcast(
                        buildBroadcastSettingsFromMetadata(contentMetadata, broadcastCode),
                        mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Start broadcasting to nearby devices using {@link BluetoothLeBroadcastSettings}.
     *
     * @param broadcastSettings broadcast settings for this broadcast group
     * @throws IllegalStateException if callback was not registered
     * @throws NullPointerException if <var>broadcastSettings</var> is null
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void startBroadcast(@NonNull BluetoothLeBroadcastSettings broadcastSettings) {
        requireNonNull(broadcastSettings);
        if (mCallbackExecutorMap.isEmpty()) {
            throw new IllegalStateException("No callback was ever registered");
        }

        if (DBG) log("startBroadcasting");
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.startBroadcast(broadcastSettings, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Start broadcasting to nearby devices using {@link BluetoothLeBroadcastSettings} with
     * specified ISO interval.
     *
     * @param broadcastSettings broadcast settings for this broadcast group
     * @param isoInterval ISO interval in milliseconds (valid values: 7.5, 10, 20, 30)
     * @throws IllegalStateException if callback was not registered
     * @throws NullPointerException if <var>broadcastSettings</var> is null
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void startEnhancedBroadcast(@NonNull BluetoothLeBroadcastSettings broadcastSettings,
            float isoInterval) {
        requireNonNull(broadcastSettings);
        if (mCallbackExecutorMap.isEmpty()) {
            throw new IllegalStateException("No callback was ever registered");
        }

        if (DBG) log("startEnhancedBroadcast with ISO interval: " + isoInterval);
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.startEnhancedBroadcast(broadcastSettings, isoInterval, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Update the broadcast with <var>broadcastId</var> with new <var>contentMetadata</var>
     *
     * <p>On success, {@link Callback#onBroadcastUpdated(int, int)} will be invoked with reason code
     * {@link BluetoothStatusCodes#REASON_LOCAL_APP_REQUEST}. On failure, {@link
     * Callback#onBroadcastUpdateFailed(int, int)} will be invoked with reason code
     *
     * @param broadcastId broadcastId as defined by the Basic Audio Profile
     * @param contentMetadata updated metadata for the default Broadcast subgroup
     * @throws IllegalStateException if callback was not registered
     * @throws NullPointerException if <var>contentMetadata</var> is null
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void updateBroadcast(
            int broadcastId, @NonNull BluetoothLeAudioContentMetadata contentMetadata) {
        requireNonNull(contentMetadata);
        if (mCallbackExecutorMap.isEmpty()) {
            throw new IllegalStateException("No callback was ever registered");
        }

        if (DBG) log("updateBroadcast");
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.updateBroadcast(
                        broadcastId,
                        buildBroadcastSettingsFromMetadata(contentMetadata, null),
                        mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Update the broadcast with <var>broadcastId</var> with <var>BluetoothLeBroadcastSettings</var>
     *
     * <p>On success, {@link Callback#onBroadcastUpdated(int, int)} will be invoked with reason code
     * {@link BluetoothStatusCodes#REASON_LOCAL_APP_REQUEST}. On failure, {@link
     * Callback#onBroadcastUpdateFailed(int, int)} will be invoked with reason code
     *
     * @param broadcastId broadcastId as defined by the Basic Audio Profile
     * @param broadcastSettings broadcast settings for this broadcast group
     * @throws IllegalStateException if callback was not registered
     * @throws NullPointerException if <var>broadcastSettings</var> is null
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void updateBroadcast(
            int broadcastId, @NonNull BluetoothLeBroadcastSettings broadcastSettings) {
        requireNonNull(broadcastSettings);
        if (mCallbackExecutorMap.isEmpty()) {
            throw new IllegalStateException("No callback was ever registered");
        }

        if (DBG) log("updateBroadcast");
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.updateBroadcast(broadcastId, broadcastSettings, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Stop broadcasting.
     *
     * <p>On success, {@link Callback#onBroadcastStopped(int, int)} will be invoked with reason code
     * {@link BluetoothStatusCodes#REASON_LOCAL_APP_REQUEST} and the <var>broadcastId</var> On
     * failure, {@link Callback#onBroadcastStopFailed(int)} will be invoked with reason code
     *
     * @param broadcastId as defined by the Basic Audio Profile
     * @throws IllegalStateException if callback was not registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void stopBroadcast(int broadcastId) {
        if (mCallbackExecutorMap.isEmpty()) {
            throw new IllegalStateException("No callback was ever registered");
        }

        if (DBG) log("disableBroadcastMode");
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.stopBroadcast(broadcastId, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Stop enhanced broadcasting.
     *
     * <p>This method provides the same functionality as {@link #stopBroadcast(int)} but is named
     * for consistency with {@link #startEnhancedBroadcast(BluetoothLeBroadcastSettings, float)}.
     * The stop operation is identical regardless of whether the broadcast was started as regular
     * or enhanced.
     *
     * <p>On success, {@link Callback#onBroadcastStopped(int, int)} will be invoked with reason code
     * {@link BluetoothStatusCodes#REASON_LOCAL_APP_REQUEST} and the <var>broadcastId</var> On
     * failure, {@link Callback#onBroadcastStopFailed(int)} will be invoked with reason code
     *
     * @param broadcastId as defined by the Basic Audio Profile
     * @throws IllegalStateException if callback was not registered
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void stopEnhancedBroadcast(int broadcastId, int mode) {
        if (mCallbackExecutorMap.isEmpty()) {
            throw new IllegalStateException("No callback was ever registered");
        }
        if (DBG) log("stopEnhancedBroadcast broadcastId=" + broadcastId + " mode=" + mode);
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.stopEnhancedBroadcast(broadcastId, mode, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Return true if audio is being broadcasted on the Broadcast Source as identified by the
     * <var>broadcastId</var>
     *
     * @param broadcastId as defined in the Basic Audio Profile
     * @return true if audio is being broadcasted
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public boolean isPlaying(int broadcastId) {
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                return service.isPlaying(broadcastId, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return false;
    }

    /**
     * Get {@link BluetoothLeBroadcastMetadata} for all Broadcast Groups currently running on this
     * device
     *
     * @return list of {@link BluetoothLeBroadcastMetadata}
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public @NonNull List<BluetoothLeBroadcastMetadata> getAllBroadcastMetadata() {
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                return service.getAllBroadcastMetadata(mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return Collections.emptyList();
    }

    /**
     * Get the maximum number of Broadcast Isochronous Group supported on this device
     *
     * @return maximum number of Broadcast Isochronous Group supported on this device
     * @hide
     */
    @SystemApi
    @RequiresPermission(BLUETOOTH_PRIVILEGED)
    public int getMaximumNumberOfBroadcasts() {
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                return service.getMaximumNumberOfBroadcasts();
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return 1;
    }

    /**
     * Get the maximum number of streams per broadcast Single stream means single Audio PCM stream
     *
     * @return maximum number of broadcast streams per broadcast group
     * @hide
     */
    @SystemApi
    @RequiresPermission(BLUETOOTH_PRIVILEGED)
    public int getMaximumStreamsPerBroadcast() {
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                return service.getMaximumStreamsPerBroadcast();
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return 1;
    }

    /**
     * Get the maximum number of subgroups per broadcast Single stream means single Audio PCM
     * stream, one stream could support single or multiple subgroups based on language and audio
     * configuration. e.g. Stream 1 -> 2 subgroups with English and Spanish, Stream 2 -> 1 subgroups
     * with English, Stream 3 -> 2 subgroups with hearing Aids Standard and High Quality
     *
     * @return maximum number of broadcast subgroups per broadcast group
     * @hide
     */
    @SystemApi
    @RequiresPermission(BLUETOOTH_PRIVILEGED)
    public int getMaximumSubgroupsPerBroadcast() {
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                return service.getMaximumSubgroupsPerBroadcast();
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return 1;
    }

    /**
     * Returns the Broadcast_States field from HCI_VS_LE_Read_Supported_States (0xFD90/0x0B).
     * Bit 1: Terminate supported, Bit 2: Remove supported.
     *
     * @return capability bitmask, or -1 if service unavailable
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public int getEnhancedBroadcastCap() {
        if (DBG) Log.d(TAG, "getEnhancedBroadcastCap");
        final IBluetoothLeAudio service = getService();
        final int defaultValue = -1;
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
        } else if (isEnabled()) {
            try {
                return service.getEnhancedBroadcastCap(mAttributionSource);
            } catch (RemoteException e) {
                throw e.rethrowFromSystemServer();
            }
        }
        return defaultValue;
    }

    /**
     * {@inheritDoc}
     *
     * @hide
     */
    @Override
    public void close() {
        if (VDBG) log("close()");

        mAdapter.closeProfileProxy(this);
    }

    private static BluetoothLeBroadcastSettings buildBroadcastSettingsFromMetadata(
            BluetoothLeAudioContentMetadata contentMetadata, @Nullable byte[] broadcastCode) {
        BluetoothLeBroadcastSubgroupSettings.Builder subgroupBuilder =
                new BluetoothLeBroadcastSubgroupSettings.Builder()
                        .setContentMetadata(contentMetadata);

        BluetoothLeBroadcastSettings.Builder builder =
                new BluetoothLeBroadcastSettings.Builder()
                        .setPublicBroadcast(false)
                        .setBroadcastCode(broadcastCode);
        // builder expect at least one subgroup setting
        builder.addSubgroupSettings(subgroupBuilder.build());
        return builder.build();
    }

    private boolean isEnabled() {
        if (mAdapter.getState() == BluetoothAdapter.STATE_ON) return true;
        return false;
    }

    /** @hide */
    @Override
    @SuppressLint("AndroidFrameworkRequiresPermission") // Unexposed re-entrant callback
    @RequiresNoPermission
    public void onServiceConnected(IBinder service) {
        mService = IBluetoothLeAudio.Stub.asInterface(service);
        // re-register the service-to-app callback
        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                return;
            }
            try {
                if (service != null) {
                    mService.registerLeBroadcastCallback(mCallback, mAttributionSource);
                }
            } catch (RemoteException e) {
                Log.e(
                        TAG,
                        "onServiceConnected: Failed to register " + "Le Broadcaster callback",
                        e);
            }
        }
    }

    /** @hide */
    @Override
    @RequiresNoPermission
    public void onServiceDisconnected() {
        mService = null;
    }

    private IBluetoothLeAudio getService() {
        return mService;
    }

    /** @hide */
    @Override
    @RequiresNoPermission
    public BluetoothAdapter getAdapter() {
        return mAdapter;
    }

    /**
     * Set Achat-specific attributes for the broadcast source.
     *
     * @param devId Device ID (12-bit value, 0-4095)
     * @param name Device name (up to 10 octets, UTF-8 encoded)
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void setAttributes(int devId, @NonNull byte[] name) {
        if (devId < 0 || devId > 4095) {
            Log.e(TAG, "setAttributes: invalid devId=" + devId + " (must be 0-4095)");
            throw new IllegalArgumentException(
                    "Invalid devId: " + devId + ". Must be 0-4095 (12-bit)");
        }
        Objects.requireNonNull(name, "name cannot be null");
        if (name.length == 0) {
            Log.e(TAG, "setAttributes: name is empty, ignoring request");
            return;
        }
        if (name.length > 10) {
            Log.e(TAG, "setAttributes: name length=" + name.length
                    + " exceeds 10 octets, ignoring request");
            return;
        }
        // Find the actual length (stop at first null byte)
        int actualLength = name.length;
        for (int i = 0; i < name.length; i++) {
            if (name[i] == 0) {
                actualLength = i;
                break;
            }
        }
        String nameStr = new String(name, 0, actualLength, java.nio.charset.StandardCharsets.UTF_8);
        if (nameStr.trim().isEmpty()) {
            Log.e(TAG, "setAttributes: name consists entirely of spaces, ignoring request");
            return;
        }
        if (nameStr.contains(" ")) {
            Log.e(TAG, "setAttributes: name contains space character(s): \""
                    + nameStr + "\", ignoring request");
            return;
        }
        if (DBG) log("setAttributes: devId=" + devId
                + ", name=\"" + nameStr + "\", nameLen=" + name.length);
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.setAttributes(devId, name, mAttributionSource);
            } catch (RemoteException e) {
                throw e.rethrowFromSystemServer();
            }
        }
    }

    /**
     * Set DBIG Join Control mode for the broadcast source.
     *
     * @param mode true to enable DBIG join control, false to disable
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void setJoinControl(boolean mode) {
        if (DBG) log("setJoinControl: mode=" + mode);
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.setJoinControl(mode, mAttributionSource);
            } catch (RemoteException e) {
                throw e.rethrowFromSystemServer();
            }
        }
    }

    /**
     * Request the controller to remove a specific device from the DBIG.
     * <p>The result is delivered asynchronously via
     * {@link Callback#onRemoveDeviceDbigComplete(int, int)}.
     *
     * @param devId  12-bit device identifier (0–4095), as reported in
     *               {@link android.bluetooth.action#ACTION_DBIG_STATUS_CHANGED} extras
     * @param name   shortened local name of the device (1–10 UTF-8 bytes, no spaces)
     * @param reason HCI reason code for the removal (e.g. 0x13 = Remote User Terminated)
     * @throws IllegalArgumentException if devId is out of range or name is invalid
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void removeDeviceFromDbig(int devId, @NonNull byte[] name, int reason) {
        if (devId < 0 || devId > 4095) {
            throw new IllegalArgumentException("Invalid devId: " + devId + ". Must be 0-4095");
        }
        Objects.requireNonNull(name, "name cannot be null");
        if (name.length == 0 || name.length > 10) {
            throw new IllegalArgumentException(
                    "name length must be 1-10 bytes, got " + name.length);
        }
        if (DBG) log("removeDeviceFromDbig: devId=0x" + Integer.toHexString(devId)
                + ", reason=0x" + Integer.toHexString(reason));
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (isEnabled()) {
            try {
                service.removeDeviceFromDbig(devId, name, reason, mAttributionSource);
            } catch (RemoteException e) {
                throw e.rethrowFromSystemServer();
            }
        }
    }

    private static void log(String msg) {
        Log.d(TAG, msg);
    }

    /**
     * Accept a PGP terminate request (spec §4.9 PGO Remote Host Terminate procedure).
     * Sends {@code HCI_VS_LE_Texit_DBIG(TERMINATE)} to terminate the DBIG.
     * Called after PGO user accepts the terminate dialog (shown by DBIG status bit 10 = 0x0400).
     * Result delivered via {@link Callback#onTexitDbigComplete}.
     *
     * @param broadcastId broadcast ID of the active enhanced broadcast
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void acceptTerminateDbig(int broadcastId) {
        if (DBG) log("acceptTerminateDbig: broadcastId=" + broadcastId);
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
        } else if (isEnabled()) {
            try {
                service.acceptTerminateDbig(broadcastId, mAttributionSource);
            } catch (RemoteException e) {
                throw e.rethrowFromSystemServer();
            }
        }
    }

    /**
     * Reject a PGP terminate request.
     * Sends {@code HCI_VS_LE_Texit_DBIG(REJECT_TERMINATE)} to keep the DBIG active.
     * Result delivered via {@link Callback#onTexitDbigComplete} (status will be non-zero on PGP).
     *
     * @param broadcastId broadcast ID of the active enhanced broadcast
     * @hide
     */
    @SystemApi
    @SuppressLint("UnflaggedApi")
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void rejectTerminateDbig(int broadcastId) {
        if (DBG) log("rejectTerminateDbig: broadcastId=" + broadcastId);
        final IBluetoothLeAudio service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
        } else if (isEnabled()) {
            try {
                service.rejectTerminateDbig(broadcastId, mAttributionSource);
            } catch (RemoteException e) {
                throw e.rethrowFromSystemServer();
            }
        }
    }
}
