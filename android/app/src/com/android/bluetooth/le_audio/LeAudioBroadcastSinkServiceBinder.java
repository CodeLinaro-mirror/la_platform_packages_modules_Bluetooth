/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.le_audio;

import static android.Manifest.permission.BLUETOOTH_CONNECT;
import static android.Manifest.permission.BLUETOOTH_PRIVILEGED;
import static android.Manifest.permission.BLUETOOTH_SCAN;

import android.annotation.RequiresPermission;
import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSinkState;
import android.bluetooth.IBluetoothLeBroadcastSink;
import android.bluetooth.IBluetoothLeBroadcastSinkCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanSettings;
import android.content.AttributionSource;
import android.util.Log;

import com.android.bluetooth.Utils;
import com.android.bluetooth.btservice.ProfileService.IProfileServiceBinder;

import java.util.ArrayList;
import java.util.List;

class LeAudioBroadcastSinkServiceBinder extends IBluetoothLeBroadcastSink.Stub
        implements IProfileServiceBinder {
    private static final String TAG = LeAudioBroadcastSinkServiceBinder.class.getSimpleName();

    private LeAudioBroadcastSinkService mService;

    LeAudioBroadcastSinkServiceBinder(LeAudioBroadcastSinkService svc) {
        mService = svc;
    }

    @Override
    public void cleanup() {
        mService = null;
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    private LeAudioBroadcastSinkService getServiceAndEnforceConnect(AttributionSource source) {
        LeAudioBroadcastSinkService service = mService;

        if (Utils.isInstrumentationTestMode()) {
            return service;
        }

        if (!Utils.checkServiceAvailable(service, TAG)
                || !Utils.checkCallerIsSystemOrActiveOrManagedUser(service, TAG)
                || !Utils.checkConnectPermissionForDataDelivery(service, source, TAG)) {
            return null;
        }

        service.enforceCallingOrSelfPermission(BLUETOOTH_PRIVILEGED, null);

        return service;
    }

    @RequiresPermission(allOf = {BLUETOOTH_SCAN, BLUETOOTH_PRIVILEGED})
    private LeAudioBroadcastSinkService getServiceAndEnforceScan(AttributionSource source) {
        LeAudioBroadcastSinkService service = mService;

        if (Utils.isInstrumentationTestMode()) {
            return service;
        }

        if (!Utils.checkServiceAvailable(service, TAG)
                || !Utils.checkCallerIsSystemOrActiveOrManagedUser(service, TAG)
                || !Utils.checkScanPermissionForDataDelivery(
                        service, source, TAG, "getServiceAndEnforceScan")) {
            return null;
        }

        service.enforceCallingOrSelfPermission(BLUETOOTH_PRIVILEGED, null);

        return service;
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void registerCallback(
            IBluetoothLeBroadcastSinkCallback callback, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.registerCallback(callback);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void unregisterCallback(
            IBluetoothLeBroadcastSinkCallback callback, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.unregisterCallback(callback);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_SCAN, BLUETOOTH_PRIVILEGED})
    public void startScanningForSources(
            List<ScanFilter> filters, ScanSettings settings, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceScan(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.startScanningForSources(filters, settings);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_SCAN, BLUETOOTH_PRIVILEGED})
    public void stopScanningForSources(AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceScan(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.stopScanningForSources();
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void addSource(int broadcastId, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.addSource(broadcastId);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void joinSource(BluetoothLeBroadcastMetadata metadata, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.joinSource(metadata);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void leaveSource(int broadcastId, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.leaveSource(broadcastId);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void removeSource(int broadcastId, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.removeSource(broadcastId);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public void updateSourceMetadata(
            BluetoothLeBroadcastMetadata metadata, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return;
        }
        service.updateSourceMetadata(metadata);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public List<BluetoothLeBroadcastSinkState> getAllSyncedSinkState(AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return new ArrayList<>();
        }
        return service.getAllSyncedSinkState();
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public BluetoothLeBroadcastMetadata getSourceMetadata(
            int broadcastId, AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return null;
        }
        return service.getSourceMetadata(broadcastId);
    }

    @Override
    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public int getMaximumSourceCapacity(AttributionSource source) {
        LeAudioBroadcastSinkService service = getServiceAndEnforceConnect(source);
        if (service == null) {
            Log.e(TAG, "Service is null");
            return 0;
        }
        return service.getMaximumSourceCapacity();
    }
}
