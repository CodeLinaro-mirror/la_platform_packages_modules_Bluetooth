/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import static android.Manifest.permission.BLUETOOTH_CONNECT;
import static android.Manifest.permission.BLUETOOTH_PRIVILEGED;
import static android.Manifest.permission.BLUETOOTH_SCAN;

import static java.util.Objects.requireNonNull;

import android.annotation.CallbackExecutor;
import android.annotation.FlaggedApi;
import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.RequiresNoPermission;
import android.annotation.RequiresPermission;
import android.annotation.SystemApi;
import android.bluetooth.annotations.RequiresBluetoothConnectPermission;
import android.bluetooth.annotations.RequiresBluetoothLocationPermission;
import android.bluetooth.annotations.RequiresBluetoothScanPermission;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;

import com.android.bluetooth.flags.Flags;
import android.content.AttributionSource;
import android.content.Context;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.CloseGuard;
import android.util.Log;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executor;

/**
 * This class provides the public APIs for the LE Audio Broadcast Sink role.
 *
 * <p>A Broadcast Sink is a device that can receive and decode LE Audio broadcast streams.
 * It can search for available Broadcast Sources, synchronize to them, and receive (play)
 * the broadcast audio.
 *
 * <p>The Broadcast Sink can operate independently to discover and synchronize with Broadcast
 * Sources, or it can be assisted by a Broadcast Assistant through the Broadcast Audio Scan
 * Service (BASS).
 *
 * <p>BluetoothLeBroadcastSink is a proxy object for controlling the Broadcast Sink
 * service via IPC. Use {@link BluetoothAdapter#getProfileProxy} to get the
 * BluetoothLeBroadcastSink proxy object.
 *
 * @hide
 */
@FlaggedApi(Flags.FLAG_LEAUDIO_BROADCAST_SINK_API)
@SystemApi
public final class BluetoothLeBroadcastSink implements BluetoothProfile, AutoCloseable {
    private static final String TAG = BluetoothLeBroadcastSink.class.getSimpleName();

    private static final boolean DBG = true;
    private final Map<Callback, Executor> mCallbackExecutorMap = new HashMap<>();

    private final IBluetoothLeBroadcastSinkCallback mCallback =
            new IBluetoothLeBroadcastSinkCallback.Stub() {
                @Override
                @RequiresNoPermission
                public void onSearchStarted(int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSearchStarted(reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSearchStartFailed(int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSearchStartFailed(reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSearchStopped(int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSearchStopped(reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceFound(int broadcastId, ScanResult result) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceFound(broadcastId, result));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceAdded(BluetoothLeBroadcastMetadata metadata) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceAdded(metadata));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceAddFailed(int broadcastId, int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceAddFailed(broadcastId, reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceJoined(int broadcastId) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceJoined(broadcastId));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceJoinFailed(BluetoothLeBroadcastMetadata metadata, int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceJoinFailed(metadata, reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceLeft(int broadcastId, int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceLeft(broadcastId, reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceLeaveFailed(int broadcastId, int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceLeaveFailed(broadcastId, reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceRemoved(int broadcastId, int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceRemoved(broadcastId, reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceRemoveFailed(int broadcastId, int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(() -> callback.onSourceRemoveFailed(broadcastId, reason));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceMetadataChanged(
                        int broadcastId, BluetoothLeBroadcastMetadata metadata) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(
                                () -> callback.onSourceMetadataChanged(broadcastId, metadata));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceMetadataUpdated(
                        int broadcastId, BluetoothLeBroadcastMetadata metadata) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(
                                () -> callback.onSourceMetadataUpdated(broadcastId, metadata));
                    }
                }

                @Override
                @RequiresNoPermission
                public void onSourceMetadataUpdateFailed(
                        int broadcastId, BluetoothLeBroadcastMetadata metadata, int reason) {
                    for (Map.Entry<BluetoothLeBroadcastSink.Callback, Executor>
                            callbackExecutorEntry : mCallbackExecutorMap.entrySet()) {
                        BluetoothLeBroadcastSink.Callback callback =
                                callbackExecutorEntry.getKey();
                        Executor executor = callbackExecutorEntry.getValue();
                        executor.execute(
                                () -> callback.onSourceMetadataUpdateFailed(
                                        broadcastId, metadata, reason));
                    }
                }
            };

    /**
     * This class provides a set of callbacks that are invoked when operating as a Broadcast Sink.
     *
     * @hide
     */
    @SystemApi
    public interface Callback {
        /**
         * Callback invoked when the Broadcast Sink started searching for nearby Broadcast Sources.
         *
         * @param reason reason code on why search has started
         * @hide
         */
        @SystemApi
        void onSearchStarted(@BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when the Broadcast Sink failed to start searching for Broadcast Sources.
         *
         * @param reason reason code on why search failed to start
         * @hide
         */
        @SystemApi
        void onSearchStartFailed(@BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when the Broadcast Sink stopped searching for nearby Broadcast Sources.
         *
         * @param reason reason code on why search has stopped
         * @hide
         */
        @SystemApi
        void onSearchStopped(@BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when a new Broadcast Source is discovered during scanning.
         *
         * <p>This provides basic information about the source. Full metadata will be available
         * after PA sync via {@link #onSourceAdded(BluetoothLeBroadcastMetadata)}.
         *
         * @param broadcastId The broadcast ID of the discovered source
         * @param result {@link ScanResult} containing the scan data
         * @hide
         */
        @SystemApi
        void onSourceFound(int broadcastId, @NonNull ScanResult result);

        /**
         * Callback invoked when a Broadcast Source has been added (PA sync succeeded).
         *
         * <p>This indicates that the device has successfully synchronized with the Periodic
         * Advertisements of the Broadcast Source and obtained its full metadata.
         *
         * @param metadata {@link BluetoothLeBroadcastMetadata} of the added source
         * @hide
         */
        @SystemApi
        void onSourceAdded(@NonNull BluetoothLeBroadcastMetadata metadata);

        /**
         * Callback invoked when adding a Broadcast Source failed (PA sync failed).
         *
         * @param broadcastId broadcast ID of the source that failed to be added
         * @param reason reason code on why the add operation failed
         * @hide
         */
        @SystemApi
        void onSourceAddFailed(int broadcastId, @BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when a Broadcast Source has been joined (BIG sync succeeded).
         *
         * <p>This indicates that the device is now receiving and decoding audio data
         * from the Broadcast Source.
         *
         * @param broadcastId broadcast ID of the joined source
         * @hide
         */
        @SystemApi
        void onSourceJoined(int broadcastId);

        /**
         * Callback invoked when joining a Broadcast Source failed (BIG sync failed).
         *
         * @param metadata {@link BluetoothLeBroadcastMetadata} that was attempted to join
         * @param reason reason code on why the join operation failed
         * @hide
         */
        @SystemApi
        void onSourceJoinFailed(
                @NonNull BluetoothLeBroadcastMetadata metadata,
                @BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when a Broadcast Source has been left (BIG sync stopped).
         *
         * <p>The PA sync is still maintained, allowing quick rejoin if needed.
         *
         * @param broadcastId broadcast ID of the left source
         * @param reason reason code on why the source was left
         * @hide
         */
        @SystemApi
        void onSourceLeft(int broadcastId, @BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when leaving a Broadcast Source failed.
         *
         * @param broadcastId broadcast ID of the source
         * @param reason reason code on why the leave operation failed
         * @hide
         */
        @SystemApi
        void onSourceLeaveFailed(
                int broadcastId, @BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when a Broadcast Source has been removed (PA sync terminated).
         *
         * @param broadcastId broadcast ID of the removed source
         * @param reason reason code on why the source was removed
         * @hide
         */
        @SystemApi
        void onSourceRemoved(int broadcastId, @BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when removing a Broadcast Source failed.
         *
         * @param broadcastId broadcast ID of the source
         * @param reason reason code on why the remove operation failed
         * @hide
         */
        @SystemApi
        void onSourceRemoveFailed(
                int broadcastId, @BluetoothLeBroadcastSinkState.Reason int reason);

        /**
         * Callback invoked when the metadata of a Broadcast Source has changed.
         *
         * <p>This is triggered by the Broadcast Source updating its metadata.
         *
         * @param broadcastId broadcast ID of the source
         * @param metadata updated {@link BluetoothLeBroadcastMetadata}
         * @hide
         */
        @SystemApi
        void onSourceMetadataChanged(
                int broadcastId, @NonNull BluetoothLeBroadcastMetadata metadata);

        /**
         * Callback invoked when the metadata of a Broadcast Source has been updated by the sink.
         *
         * <p>This is triggered by a successful call to {@link #updateSourceMetadata}.
         *
         * @param broadcastId broadcast ID of the source
         * @param metadata updated {@link BluetoothLeBroadcastMetadata}
         * @hide
         */
        @SystemApi
        void onSourceMetadataUpdated(
                int broadcastId, @NonNull BluetoothLeBroadcastMetadata metadata);

        /**
         * Callback invoked when updating the metadata of a Broadcast Source failed.
         *
         * @param broadcastId broadcast ID of the source
         * @param metadata {@link BluetoothLeBroadcastMetadata} that was attempted to update
         * @param reason reason code on why the update operation failed
         * @hide
         */
        @SystemApi
        void onSourceMetadataUpdateFailed(
                int broadcastId,
                @NonNull BluetoothLeBroadcastMetadata metadata,
                @BluetoothLeBroadcastSinkState.Reason int reason);
    }

    private final CloseGuard mCloseGuard;
    private final BluetoothAdapter mBluetoothAdapter;
    private final AttributionSource mAttributionSource;

    private IBluetoothLeBroadcastSink mService;

    /**
     * Create a new instance of a Broadcast Sink.
     *
     * @hide
     */
    /*package*/ BluetoothLeBroadcastSink(
            @NonNull Context context, @NonNull BluetoothAdapter bluetoothAdapter) {
        mBluetoothAdapter = bluetoothAdapter;
        mAttributionSource = bluetoothAdapter.getAttributionSource();
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

    /** @hide */
    @Override
    public void close() {
        mBluetoothAdapter.closeProfileProxy(this);
    }

    /** @hide */
    @Override
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void onServiceConnected(IBinder service) {
        mService = IBluetoothLeBroadcastSink.Stub.asInterface(service);
        // re-register the service-to-app callback
        log("onServiceConnected");
        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                return;
            }
            try {
                mService.registerCallback(mCallback, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(
                        TAG,
                        "onServiceConnected: Failed to register Broadcast Sink callback",
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

    private IBluetoothLeBroadcastSink getService() {
        return mService;
    }

    /** @hide */
    @Override
    @RequiresNoPermission
    public BluetoothAdapter getAdapter() {
        return mBluetoothAdapter;
    }

    /**
     * {@inheritDoc}
     *
     * @hide
     */
    @SystemApi
    @RequiresNoPermission
    @Override
    public @NonNull List<BluetoothDevice> getConnectedDevices() {
        // Broadcast Sink doesn't have traditional connections
        return new ArrayList<BluetoothDevice>();
    }

    /**
     * {@inheritDoc}
     *
     * @hide
     */
    @SystemApi
    @RequiresNoPermission
    @Override
    public @NonNull List<BluetoothDevice> getDevicesMatchingConnectionStates(@Nullable int[] states) {
        // Broadcast Sink doesn't have traditional connections
        return new ArrayList<BluetoothDevice>();
    }

    /**
     * {@inheritDoc}
     *
     * @hide
     */
    @SystemApi
    @RequiresNoPermission
    @Override
    public @BluetoothProfile.BtProfileState int getConnectionState(@Nullable BluetoothDevice device) {
        // Broadcast Sink doesn't have traditional connections
        return STATE_DISCONNECTED;
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
     * @throws NullPointerException if a null executor, or callback is given
     * @throws IllegalArgumentException if the same <var>callback<var> is already registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void registerCallback(
            @NonNull @CallbackExecutor Executor executor, @NonNull Callback callback) {
        requireNonNull(executor);
        requireNonNull(callback);
        log("registerCallback");

        synchronized (mCallbackExecutorMap) {
            // If the callback map is empty, we register the service-to-app callback
            if (mCallbackExecutorMap.isEmpty()) {
                if (!mBluetoothAdapter.isEnabled()) {
                    /* If Bluetooth is off, just store callback and it will be registered
                     * when Bluetooth is on
                     */
                    mCallbackExecutorMap.put(callback, executor);
                    return;
                }
                try {
                    final IBluetoothLeBroadcastSink service = getService();
                    if (service != null) {
                        service.registerCallback(mCallback, mAttributionSource);
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
     * Unregister the specified {@link Callback}.
     *
     * <p>The same {@link Callback} object used when calling {@link #registerCallback(Executor,
     * Callback)} must be used.
     *
     * <p>Callbacks are automatically unregistered when the application process goes away.
     *
     * @param callback user implementation of the {@link Callback}
     * @throws NullPointerException when callback is null
     * @throws IllegalArgumentException when the <var>callback</var> was not registered before
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void unregisterCallback(@NonNull Callback callback) {
        requireNonNull(callback);
        log("unregisterCallback");

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.remove(callback) == null) {
                throw new IllegalArgumentException("This callback has not been registered");
            }

            // If the callback map is empty, we unregister the service-to-app callback
            if (mCallbackExecutorMap.isEmpty()) {
                try {
                    final IBluetoothLeBroadcastSink service = getService();
                    if (service != null) {
                        service.unregisterCallback(mCallback, mAttributionSource);
                    }
                } catch (RemoteException e) {
                    Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
                }
            }
        }
    }

    /**
     * Start scanning for LE Audio Broadcast Sources.
     *
     * <p>On success, {@link Callback#onSearchStarted(int)} will be called with reason code {@link
     * BluetoothLeBroadcastSinkState#REASON_LOCAL_APP_REQUEST}.
     *
     * <p>Discovered sources will be reported via {@link Callback#onSourceFound(int, ScanResult)}.
     * Once PA sync is established, full metadata will be available via
     * {@link Callback#onSourceAdded(BluetoothLeBroadcastMetadata)}.
     *
     * <p>App must also have {@link android.Manifest.permission#ACCESS_FINE_LOCATION
     * ACCESS_FINE_LOCATION} permission in order to get results.
     *
     * @param filters {@link ScanFilter}s for finding specific Broadcast Sources, provide an empty
     *     list if no filter is needed
     * @param settings {@link ScanSettings} to control scan behavior
     * @throws NullPointerException when <var>filters</var> or <var>settings</var> is null
     * @throws IllegalStateException when no callback is registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothScanPermission
    @RequiresBluetoothLocationPermission
    @RequiresPermission(allOf = {BLUETOOTH_SCAN, BLUETOOTH_PRIVILEGED})
    public void startScanningForSources(
            @NonNull List<ScanFilter> filters, @NonNull ScanSettings settings) {
        log("startScanningForSources");
        requireNonNull(filters);
        requireNonNull(settings);
        if (mCallback == null) {
            throw new IllegalStateException("No callback was ever registered");
        }

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                throw new IllegalStateException("All callbacks are unregistered");
            }
        }

        final IBluetoothLeBroadcastSink service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                service.startScanningForSources(filters, settings, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Stop an ongoing scan for nearby Broadcast Sources.
     *
     * <p>On success, {@link Callback#onSearchStopped(int)} will be called with reason code {@link
     * BluetoothLeBroadcastSinkState#REASON_LOCAL_APP_REQUEST}.
     *
     * @throws IllegalStateException if callback was not registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothScanPermission
    @RequiresPermission(allOf = {BLUETOOTH_SCAN, BLUETOOTH_PRIVILEGED})
    public void stopScanningForSources() {
        log("stopScanningForSources");
        if (mCallback == null) {
            throw new IllegalStateException("No callback was ever registered");
        }

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                throw new IllegalStateException("All callbacks are unregistered");
            }
        }

        final IBluetoothLeBroadcastSink service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                service.stopScanningForSources(mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Add a Broadcast Source by synchronizing to its Periodic Advertisements.
     *
     * <p>This performs PA sync only, without BIG sync. Use this when you want to obtain
     * the source's metadata without starting audio streaming.
     *
     * <p>On success, {@link Callback#onSourceAdded(BluetoothLeBroadcastMetadata)} will be invoked
     * with the full metadata.
     *
     * <p>On failure, {@link Callback#onSourceAddFailed(int)} will be invoked with reason code.
     *
     * @param broadcastId broadcast ID of the source to add
     * @throws IllegalStateException if callback was not registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void addSource(int broadcastId) {
        log("addSource: " + broadcastId);
        if (mCallback == null) {
            throw new IllegalStateException("No callback was ever registered");
        }

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                throw new IllegalStateException("All callbacks are unregistered");
            }
        }

        final IBluetoothLeBroadcastSink service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                service.addSource(broadcastId, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Join a Broadcast Source to receive its audio.
     *
     * <p>This performs both PA sync and BIG sync. If the device has not yet synced to PA,
     * this will perform PA sync first, then BIG sync. This can be used for joining via QR code.
     *
     * <p>On success, {@link Callback#onSourceJoined(int)} will be invoked.
     *
     * <p>On failure, {@link Callback#onSourceJoinFailed(BluetoothLeBroadcastMetadata, int)}
     * will be invoked with reason code.
     *
     * @param metadata {@link BluetoothLeBroadcastMetadata} representing the Broadcast Source
     * @throws NullPointerException when <var>metadata</var> is null
     * @throws IllegalStateException if callback was not registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void joinSource(@NonNull BluetoothLeBroadcastMetadata metadata) {
        log("joinSource: " + metadata);
        requireNonNull(metadata);
        if (mCallback == null) {
            throw new IllegalStateException("No callback was ever registered");
        }

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                throw new IllegalStateException("All callbacks are unregistered");
            }
        }

        final IBluetoothLeBroadcastSink service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                service.joinSource(metadata, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Leave a Broadcast Source by stopping BIG sync while keeping PA sync.
     *
     * <p>This stops audio streaming but maintains the PA sync, allowing quick rejoin if needed.
     *
     * <p>On success, {@link Callback#onSourceLeft(int, int)} will be invoked with reason code
     * {@link BluetoothLeBroadcastSinkState#REASON_LOCAL_APP_REQUEST}.
     *
     * <p>On failure, {@link Callback#onSourceLeaveFailed(int, int)} will be invoked with
     * reason code.
     *
     * @param broadcastId broadcast ID of the source to leave
     * @throws IllegalStateException if callback was not registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void leaveSource(int broadcastId) {
        log("leaveSource: " + broadcastId);
        if (mCallback == null) {
            throw new IllegalStateException("No callback was ever registered");
        }

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                throw new IllegalStateException("All callbacks are unregistered");
            }
        }

        final IBluetoothLeBroadcastSink service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                service.leaveSource(broadcastId, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Remove a Broadcast Source by terminating PA sync.
     *
     * <p>This stops both BIG sync (if active) and PA sync, completely removing the source.
     *
     * <p>On success, {@link Callback#onSourceRemoved(int, int)} will be invoked with reason code
     * {@link BluetoothLeBroadcastSinkState#REASON_LOCAL_APP_REQUEST}.
     *
     * <p>On failure, {@link Callback#onSourceRemoveFailed(int, int)} will be invoked with
     * reason code.
     *
     * @param broadcastId broadcast ID of the source to remove
     * @throws IllegalStateException if callback was not registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void removeSource(int broadcastId) {
        log("removeSource: " + broadcastId);
        if (mCallback == null) {
            throw new IllegalStateException("No callback was ever registered");
        }

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                throw new IllegalStateException("All callbacks are unregistered");
            }
        }

        final IBluetoothLeBroadcastSink service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                service.removeSource(broadcastId, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Update the configuration for an ongoing broadcast session.
     *
     * <p>This method is used to modify the local sink's participation in a broadcast
     * it is already synchronized with. The primary use case is to switch between different
     * audio streams (BIS), such as different languages or content types.
     *
     * <p>The Bluetooth stack will use the broadcastId from the provided metadata to identify
     * the session, then attempt to leave and rejoin the BIG using the new metadata.
     *
     * <p>On success, {@link Callback#onSourceMetadataUpdated(int, BluetoothLeBroadcastMetadata)}
     * will be invoked.
     *
     * <p>On failure, {@link Callback#onSourceMetadataUpdateFailed(int,
     * BluetoothLeBroadcastMetadata, int)} will be invoked with reason code.
     *
     * @param metadata updated {@link BluetoothLeBroadcastMetadata}
     * @throws NullPointerException when <var>metadata</var> is null
     * @throws IllegalStateException if callback was not registered
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void updateSourceMetadata(@NonNull BluetoothLeBroadcastMetadata metadata) {
        log("updateSourceMetadata: " + metadata);
        requireNonNull(metadata);
        if (mCallback == null) {
            throw new IllegalStateException("No callback was ever registered");
        }

        synchronized (mCallbackExecutorMap) {
            if (mCallbackExecutorMap.isEmpty()) {
                throw new IllegalStateException("All callbacks are unregistered");
            }
        }

        final IBluetoothLeBroadcastSink service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                service.updateSourceMetadata(metadata, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    /**
     * Get all currently synced broadcast sink states.
     *
     * @return list of {@link BluetoothLeBroadcastSinkState} for all synced broadcasts
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    @NonNull
    public List<BluetoothLeBroadcastSinkState> getAllSyncedSinkState() {
        log("getAllSyncedSinkState");
        final IBluetoothLeBroadcastSink service = getService();
        final List<BluetoothLeBroadcastSinkState> defaultValue =
                new ArrayList<BluetoothLeBroadcastSinkState>();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                return service.getAllSyncedSinkState(mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return defaultValue;
    }

    /**
     * Get the metadata of a specific broadcast source.
     *
     * @param broadcastId broadcast ID of the source
     * @return the broadcast metadata for the given broadcastId, or null if not found
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    @Nullable
    public BluetoothLeBroadcastMetadata getSourceMetadata(int broadcastId) {
        log("getSourceMetadata: " + broadcastId);
        final IBluetoothLeBroadcastSink service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                return service.getSourceMetadata(broadcastId, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return null;
    }

    /**
     * Get the maximum number of broadcasts that can be PA synced simultaneously.
     *
     * @return maximum number of concurrent PA syncs supported
     * @hide
     */
    @SystemApi
    @RequiresBluetoothConnectPermission
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public int getMaximumSourceCapacity() {
        log("getMaximumSourceCapacity");
        final IBluetoothLeBroadcastSink service = getService();
        final int defaultValue = 0;
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                return service.getMaximumSourceCapacity(mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return defaultValue;
    }

    private static void log(@NonNull String msg) {
        if (DBG) {
            Log.d(TAG, msg);
        }
    }
}
