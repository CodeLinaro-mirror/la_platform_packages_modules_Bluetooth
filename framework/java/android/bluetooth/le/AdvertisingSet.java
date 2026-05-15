/*
 * Copyright (C) 2017 The Android Open Source Project
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
 *
 * ​Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth.le;

import static android.Manifest.permission.BLUETOOTH_ADVERTISE;
import static android.Manifest.permission.BLUETOOTH_PRIVILEGED;

import static java.util.Objects.requireNonNull;

import android.annotation.RequiresNoPermission;
import android.annotation.RequiresPermission;
import android.annotation.SystemApi;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.IBluetoothAdvertise;
import android.bluetooth.annotations.RequiresBluetoothAdvertisePermission;
import android.bluetooth.annotations.RequiresLegacyBluetoothAdminPermission;
import android.content.AttributionSource;
import android.os.RemoteException;
import android.util.Log;

import android.bluetooth.annotations.RequiresBluetoothScanPermission;
import static android.Manifest.permission.BLUETOOTH_SCAN;
import android.bluetooth.le.PeriodicAdvertisingManager;
import android.bluetooth.le.PeriodicAdvertisingCallback;
import android.annotation.RequiresPermission;
import android.annotation.RequiresNoPermission;
import android.bluetooth.BluetoothDevice;
import android.annotation.FlaggedApi;
import android.annotation.NonNull;
import android.annotation.SystemApi;
import android.annotation.Nullable;
import com.android.bluetooth.flags.Flags;
import android.os.Handler;
import java.util.concurrent.atomic.AtomicBoolean;
import android.os.Looper;
import com.android.qcomfeatureconfig.QcomBtExtConfig;

/**
 * This class provides a way to control single Bluetooth LE advertising instance.
 *
 * <p>To get an instance of {@link AdvertisingSet}, call the {@link
 * BluetoothLeAdvertiser#startAdvertisingSet} method.
 *
 * @see AdvertiseData
 */
public final class AdvertisingSet {
    private static final String TAG = AdvertisingSet.class.getSimpleName();

    private final IBluetoothAdvertise mAdvertise;
    private int mAdvertiserId;
    private final AttributionSource mAttributionSource;

    private final BluetoothAdapter mBluetoothAdapter;
    private final AdvertisingSetCallback mCallback;
    private final Handler mHandler;


    AdvertisingSet(
            IBluetoothAdvertise advertise,
            int advertiserId,
            BluetoothAdapter bluetoothAdapter,
            AttributionSource attributionSource,
            AdvertisingSetCallback callback,
            Handler handler) {
        mAdvertiserId = advertiserId;
        mAttributionSource = attributionSource;
        mAdvertise = requireNonNull(advertise);
        mBluetoothAdapter = requireNonNull(bluetoothAdapter);
        mCallback = callback;
        mHandler = handler;
    }

    /* package */ void setAdvertiserId(int advertiserId) {
        mAdvertiserId = advertiserId;
    }

    /**
     * Enables Advertising. This method returns immediately, the operation status is delivered
     * through {@code callback.onAdvertisingEnabled()}.
     *
     * @param enable whether the advertising should be enabled (true), or disabled (false)
     * @param duration advertising duration, in 10ms unit. Valid range is from 1 (10ms) to 65535
     *     (655,350 ms)
     * @param maxExtendedAdvertisingEvents maximum number of extended advertising events the
     *     controller shall attempt to send prior to terminating the extended advertising, even if
     *     the duration has not expired. Valid range is from 1 to 255.
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    public void enableAdvertising(boolean enable, int duration, int maxExtendedAdvertisingEvents) {
        try {
            mAdvertise.enableAdvertisingSet(
                    mAdvertiserId,
                    enable,
                    duration,
                    maxExtendedAdvertisingEvents,
                    mAttributionSource);
        } catch (RemoteException e) {
            Log.e(TAG, "remote exception - ", e);
        }
    }

    /**
     * Set/update data being Advertised. Make sure that data doesn't exceed the size limit for
     * specified AdvertisingSetParameters. This method returns immediately, the operation status is
     * delivered through {@code callback.onAdvertisingDataSet()}.
     *
     * <p>Advertising data must be empty if non-legacy scannable advertising is used.
     *
     * @param advertiseData Advertisement data to be broadcasted. Size must not exceed {@link
     *     BluetoothAdapter#getLeMaximumAdvertisingDataLength}. If the advertisement is connectable,
     *     three bytes will be added for flags. If the update takes place when the advertising set
     *     is enabled, the data can be maximum 251 bytes long.
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    public void setAdvertisingData(AdvertiseData advertiseData) {
        try {
            mAdvertise.setAdvertisingData(mAdvertiserId, advertiseData, mAttributionSource);
        } catch (RemoteException e) {
            Log.e(TAG, "remote exception - ", e);
        }
    }

    /**
     * Set/update scan response data. Make sure that data doesn't exceed the size limit for
     * specified AdvertisingSetParameters. This method returns immediately, the operation status is
     * delivered through {@code callback.onScanResponseDataSet()}.
     *
     * @param scanResponse Scan response associated with the advertisement data. Size must not
     *     exceed {@link BluetoothAdapter#getLeMaximumAdvertisingDataLength}. If the update takes
     *     place when the advertising set is enabled, the data can be maximum 251 bytes long.
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    public void setScanResponseData(AdvertiseData scanResponse) {
        try {
            mAdvertise.setScanResponseData(mAdvertiserId, scanResponse, mAttributionSource);
        } catch (RemoteException e) {
            Log.e(TAG, "remote exception - ", e);
        }
    }

    /**
     * Update advertising parameters associated with this AdvertisingSet. Must be called when
     * advertising is not active. This method returns immediately, the operation status is delivered
     * through {@code callback.onAdvertisingParametersUpdated}.
     *
     * <p>Requires the {@link android.Manifest.permission#BLUETOOTH_PRIVILEGED} permission when
     * {@code parameters.getOwnAddressType()} is different from {@code
     * AdvertisingSetParameters.ADDRESS_TYPE_DEFAULT} or {@code parameters.isDirected()} is true.
     *
     * @param parameters advertising set parameters.
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(
            allOf = {BLUETOOTH_ADVERTISE, BLUETOOTH_PRIVILEGED},
            conditional = true)
    public void setAdvertisingParameters(AdvertisingSetParameters parameters) {
        try {
            mAdvertise.setAdvertisingParameters(mAdvertiserId, parameters, mAttributionSource);
        } catch (RemoteException e) {
            Log.e(TAG, "remote exception - ", e);
        }
    }

    /**
     * Update periodic advertising parameters associated with this set. Must be called when periodic
     * advertising is not enabled. This method returns immediately, the operation status is
     * delivered through {@code callback.onPeriodicAdvertisingParametersUpdated()}.
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    public void setPeriodicAdvertisingParameters(PeriodicAdvertisingParameters parameters) {
        try {
            mAdvertise.setPeriodicAdvertisingParameters(
                    mAdvertiserId, parameters, mAttributionSource);
        } catch (RemoteException e) {
            Log.e(TAG, "remote exception - ", e);
        }
    }

    /**
     * Update periodic advertising parameters V2 associated with this set. Must be called when periodic
     * advertising is not enabled. This method returns immediately, the operation status is
     * delivered through {@code callback.onPeriodicAdvertisingParametersV2Updated()}.
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void setPeriodicAdvertisingParametersV2(@NonNull PeriodicAdvertisingParametersV2 parameters) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            try {
                mAdvertise.setPeriodicAdvertisingParametersV2(
                        mAdvertiserId, parameters, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, "remote exception - ", e);
            }
        } else {
            Log.e(TAG, "TARGET_QCOM_IOT_BT_EXT not supported");
        }
    }

    /**
     * Used to set periodic advertising data, must be called after setPeriodicAdvertisingParameters,
     * or after advertising was started with periodic advertising data set. This method returns
     * immediately, the operation status is delivered through {@code
     * callback.onPeriodicAdvertisingDataSet()}.
     *
     * @param periodicData Periodic advertising data. Size must not exceed {@link
     *     BluetoothAdapter#getLeMaximumAdvertisingDataLength}. If the update takes place when the
     *     periodic advertising is enabled for this set, the data can be maximum 251 bytes long.
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    public void setPeriodicAdvertisingData(AdvertiseData periodicData) {
        try {
            mAdvertise.setPeriodicAdvertisingData(mAdvertiserId, periodicData, mAttributionSource);
        } catch (RemoteException e) {
            Log.e(TAG, "remote exception - ", e);
        }
    }

    /**
     * Used to set periodic advertising subevent data, must be called after periodic advertising was
     * started. This method returns immediately, the operation status is delivered through {@code
     * callback.onPeriodicAdvertisingSubeventDataSet()}.
     *
     * @param data Periodic advertising subevent data.
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void setPeriodicAdvertisingSubeventData(int numSubenvets, @NonNull byte[] data) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            try {
                mAdvertise.setPeriodicAdvertisingSubeventData(mAdvertiserId, numSubenvets, data, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, "remote exception - ", e);
            }
        } else {
            Log.e(TAG, "TARGET_QCOM_IOT_BT_EXT not supported");
        }
    }

    /**
     * Used to enable/disable periodic advertising. This method returns immediately, the operation
     * status is delivered through {@code callback.onPeriodicAdvertisingEnable()}.
     *
     * @param enable whether the periodic advertising should be enabled (true), or disabled (false).
     */
    @RequiresLegacyBluetoothAdminPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    public void setPeriodicAdvertisingEnabled(boolean enable) {
        try {
            mAdvertise.setPeriodicAdvertisingEnable(mAdvertiserId, enable, mAttributionSource);
        } catch (RemoteException e) {
            Log.e(TAG, "remote exception - ", e);
        }
    }

    /**
     * Returns address associated with this advertising set. This method is exposed only for
     * Bluetooth PTS tests, no app or system service should ever use it.
     *
     * @hide
     */
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(
            allOf = {
                BLUETOOTH_ADVERTISE,
                BLUETOOTH_PRIVILEGED,
            })
    public void getOwnAddress() {
        try {
            mAdvertise.getOwnAddress(mAdvertiserId, mAttributionSource);
        } catch (RemoteException e) {
            Log.e(TAG, "remote exception - ", e);
        }
    }

    /**
     * Returns the advertiser ID associated with this advertising set.
     *
     * <p>This corresponds to the advertising set ID used at the HCI layer, in either LE Extended
     * Advertising or Android-specific Multi-Advertising.
     *
     * @hide
     */
    @RequiresNoPermission
    @SystemApi
    public int getAdvertiserId() {
        return mAdvertiserId;
    }

    /**
     * Transfer Periodic Advertising Set Info (PAST Set Info Transfer) for this advertising set
     * to a connected peer device.
     *
     * <p>The completion result is delivered via
     * {@link AdvertisingSetCallback#onTransferSetInfo(BluetoothDevice, int)} on the same
     * handler/looper that was provided when starting the advertising set.
     *
     * @param device       The peer device to receive the set info.
     * @param serviceData  Vendor/application-defined service data to accompany the transfer.
     *
     * @hide
     */
    @SystemApi
    @RequiresBluetoothScanPermission
    @RequiresPermission(BLUETOOTH_SCAN)
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void transferSetInfo(@NonNull BluetoothDevice device, int serviceData) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            requireNonNull(device);

            final AdvertisingSetCallback cb = mCallback;
            if (cb == null) {
                Log.w(TAG, "transferSetInfo: no AdvertisingSetCallback bound; nothing to dispatch");
            }

            // Resolve PeriodicAdvertisingManager via the same adapter context used to create this set
            final PeriodicAdvertisingManager pa = mBluetoothAdapter.getPeriodicAdvertisingManager();
            if (pa == null) {
                // Manager unavailable; deliver a generic failure to the bound callback (if any)
                dispatchTransferSetInfo(cb, device, 0x01 /*generic fail*/);
                return;
            }

            // one-shot guard: guarantee exactly-once completion dispatch
            final AtomicBoolean done = new AtomicBoolean(false);

            // Bridge scanner-side callback into advertiser-side callback
            final PeriodicAdvertisingCallback bridge = new PeriodicAdvertisingCallback() {
                @Override
                @RequiresNoPermission
                public void onSyncTransferred(BluetoothDevice dev, int status) {
                    if (done.compareAndSet(false, true)) {
                        dispatchTransferSetInfo(cb, dev, status);
                    }
                }
            };

            try {
                // adv handle == mAdvertiserId; this is the HCI advertising set identifier
                final Handler h = (mHandler != null) ? mHandler : new Handler(Looper.getMainLooper());
                pa.transferSetInfo(device, serviceData, mAdvertiserId, bridge, h);

            } catch (Throwable t) {
                Log.e(TAG, "transferSetInfo failed", t);
                if (done.compareAndSet(false, true)) {
                    dispatchTransferSetInfo(cb, device, /*status*/ 0x01 /*generic fail*/);
                }
            }
        } else {
            Log.e(TAG, "TARGET_QCOM_IOT_BT_EXT not supported");
        }
    }

    /**
     * Dispatch the completion to the bound {@link AdvertisingSetCallback} using the same
     * handler/looper that was provided to {@code startAdvertisingSet(..., callback, handler)}.
     *
     * <p>If no handler was provided, dispatch synchronously on the calling thread.
     */
    private void dispatchTransferSetInfo(AdvertisingSetCallback cb,
                                         BluetoothDevice device,
                                         int status) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            if (cb == null) return;

            final Runnable r = () -> {
                try {
                    cb.onTransferSetInfo(AdvertisingSet.this, device, status);
                } catch (Throwable t) {
                    Log.e(TAG, "Callback onTransferSetInfo threw", t);
                }
            };

            if (mHandler != null) {
                mHandler.post(r);
            } else {
                // Fallback: synchronous dispatch when no handler has been bound
                r.run();
            }
        } else {
            Log.e(TAG, "TARGET_QCOM_IOT_BT_EXT not supported");
        }
    }
}
