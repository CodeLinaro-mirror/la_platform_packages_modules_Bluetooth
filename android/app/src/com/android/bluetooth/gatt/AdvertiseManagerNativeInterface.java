/*
 * Copyright (C) 2023 The Android Open Source Project
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
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */


package com.android.bluetooth.gatt;

import android.bluetooth.le.AdvertisingSetParameters;
import android.bluetooth.le.PeriodicAdvertisingParameters;
import android.bluetooth.le.PeriodicAdvertisingParametersV2;

import androidx.annotation.VisibleForTesting;

import com.android.internal.annotations.GuardedBy;

import com.android.qcomfeatureconfig.QcomBtExtConfig;

/** Native interface for AdvertiseManager */
@VisibleForTesting(otherwise = VisibleForTesting.PACKAGE_PRIVATE)
public class AdvertiseManagerNativeInterface {
    private static final String TAG = AdvertiseManagerNativeInterface.class.getSimpleName();

    @GuardedBy("INSTANCE_LOCK")
    private static AdvertiseManagerNativeInterface sInstance;

    private static final Object INSTANCE_LOCK = new Object();

    private AdvertiseManager mManager;

    /** Get singleton instance. */
    static AdvertiseManagerNativeInterface getInstance() {
        synchronized (INSTANCE_LOCK) {
            if (sInstance == null) {
                sInstance = new AdvertiseManagerNativeInterface();
            }
            return sInstance;
        }
    }

    /** Set singleton instance. */
    @VisibleForTesting
    public static void setInstance(AdvertiseManagerNativeInterface instance) {
        synchronized (INSTANCE_LOCK) {
            sInstance = instance;
        }
    }

    void init(AdvertiseManager manager) {
        mManager = manager;
        initializeNative();
    }

    void cleanup() {
        cleanupNative();
        mManager = null;
    }

    void startAdvertisingSet(
            AdvertisingSetParameters parameters,
            byte[] advertiseDataBytes,
            byte[] advertiseDataEncBytes,
            byte[] scanResponseBytes,
            byte[] scanResponseEncBytes,
            PeriodicAdvertisingParameters periodicParameters,
            byte[] periodicDataBytes,
            byte[] periodicDataEncBytes,
            int duration,
            int maxExtAdvEvents,
            byte[] encryptedKeyMaterialValue,
            int cbId,
            int serverIf) {
        startAdvertisingSetNative(
                parameters,
                advertiseDataBytes,
                advertiseDataEncBytes,
                scanResponseBytes,
                scanResponseEncBytes,
                periodicParameters,
                periodicDataBytes,
                periodicDataEncBytes,
                duration,
                maxExtAdvEvents,
                encryptedKeyMaterialValue,
                cbId,
                serverIf);
    }

    void stopAdvertisingSet(int advertiserId) {
        stopAdvertisingSetNative(advertiserId);
    }

    void getOwnAddress(int advertiserId) {
        getOwnAddressNative(advertiserId);
    }

    void enableAdvertisingSet(int advertiserId, boolean enable, int duration, int maxExtAdvEvents) {
        enableAdvertisingSetNative(advertiserId, enable, duration, maxExtAdvEvents);
    }

    void setAdvertisingData(
            int advertiserId, byte[] advertiseDataBytes, byte[] advertiseDataEncBytes) {
        setAdvertisingDataNative(advertiserId, advertiseDataBytes, advertiseDataEncBytes);
    }

    void setScanResponseData(
            int advertiserId, byte[] advertiseDataBytes, byte[] advertiseDataEncBytes) {
        setScanResponseDataNative(advertiserId, advertiseDataBytes, advertiseDataEncBytes);
    }

    void setAdvertisingParameters(int advertiserId, AdvertisingSetParameters parameters) {
        setAdvertisingParametersNative(advertiserId, parameters);
    }

    void setPeriodicAdvertisingParameters(
            int advertiserId, PeriodicAdvertisingParameters parameters) {
        setPeriodicAdvertisingParametersNative(advertiserId, parameters);
    }

    void setPeriodicAdvertisingParametersV2(
            int advertiserId, PeriodicAdvertisingParametersV2 parameters) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            setPeriodicAdvertisingParametersV2Native(advertiserId, parameters);
        }
    }

    void setPeriodicAdvertisingData(
            int advertiserId, byte[] advertiseDataBytes, byte[] advertiseDataEncBytes) {
        setPeriodicAdvertisingDataNative(advertiserId, advertiseDataBytes, advertiseDataEncBytes);
    }

    void setPeriodicAdvertisingSubeventData(
            int advertiserId, int num_subevents, byte[] dataBytes) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            setPeriodicAdvertisingSubeventDataNative(advertiserId, num_subevents, dataBytes);
        }
    }

    void setPeriodicAdvertisingEnable(int advertiserId, boolean enable) {
        setPeriodicAdvertisingEnableNative(advertiserId, enable);
    }

    void onAdvertisingSetStarted(int regId, int advertiserId, int txPower, int status) {
        mManager.doOnAdvertiseThread(
                () -> mManager.onAdvertisingSetStarted(regId, advertiserId, txPower, status));
    }

    void onOwnAddressRead(int advertiserId, int addressType, String address) {
        mManager.doOnAdvertiseThread(
                () -> mManager.onOwnAddressRead(advertiserId, addressType, address));
    }

    void onAdvertisingEnabled(int advertiserId, boolean enable, int status) {
        mManager.doOnAdvertiseThread(
                () -> mManager.onAdvertisingEnabled(advertiserId, enable, status));
    }

    void onAdvertisingDataSet(int advertiserId, int status) {
        mManager.doOnAdvertiseThread(() -> mManager.onAdvertisingDataSet(advertiserId, status));
    }

    void onScanResponseDataSet(int advertiserId, int status) {
        mManager.doOnAdvertiseThread(() -> mManager.onScanResponseDataSet(advertiserId, status));
    }

    void onAdvertisingParametersUpdated(int advertiserId, int txPower, int status) {
        mManager.doOnAdvertiseThread(
                () -> mManager.onAdvertisingParametersUpdated(advertiserId, txPower, status));
    }

    void onPeriodicAdvertisingParametersUpdated(int advertiserId, int status) {
        mManager.doOnAdvertiseThread(
                () -> mManager.onPeriodicAdvertisingParametersUpdated(advertiserId, status));
    }

    void onPeriodicAdvertisingParametersV2Updated(int advertiserId, int status) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
             mManager.doOnAdvertiseThread(
                     () -> mManager.onPeriodicAdvertisingParametersV2Updated(advertiserId, status));
        }
    }

    void onPeriodicAdvertisingDataSet(int advertiserId, int status) {
        mManager.doOnAdvertiseThread(
                () -> mManager.onPeriodicAdvertisingDataSet(advertiserId, status));
    }

    void onPeriodicAdvertisingSubeventDataSet(int advertiserId, int status) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            mManager.doOnAdvertiseThread(
                    () -> mManager.onPeriodicAdvertisingSubeventDataSet(advertiserId, status));
        }
    }

    void onPeriodicAdvertisingEnabled(int advertiserId, boolean enable, int status) {
        mManager.doOnAdvertiseThread(
                () -> mManager.onPeriodicAdvertisingEnabled(advertiserId, enable, status));
    }

    void onPeriodicAdvertisingSubeventRequest(int advertiserId, int subeventStart, int subeventCount) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            mManager.doOnAdvertiseThread(
                    () -> mManager.onPeriodicAdvertisingSubeventRequest(advertiserId, subeventStart, subeventCount));
        }
    }

    void onPeriodicAdvertisingSubeventResponse(int advertiserId, int subevent, int txStatus, int numResponses, byte[] payload) {
        if (QcomBtExtConfig.TARGET_QCOM_IOT_BT_EXT) {
            mManager.doOnAdvertiseThread(
                    () -> mManager.onPeriodicAdvertisingSubeventResponse(advertiserId, subevent, txStatus, numResponses, payload));
        }
    }

    private native void initializeNative();

    private native void cleanupNative();

    private native void startAdvertisingSetNative(
            AdvertisingSetParameters parameters,
            byte[] advertiseData,
            byte[] advertiseDataEnc,
            byte[] scanResponse,
            byte[] scanResponseEnc,
            PeriodicAdvertisingParameters periodicParameters,
            byte[] periodicData,
            byte[] periodicDataEnc,
            int duration,
            int maxExtAdvEvents,
            byte[] encryptedKeyMaterialValue,
            int regId,
            int serverIf);

    private native void stopAdvertisingSetNative(int advertiserId);

    private native void getOwnAddressNative(int advertiserId);

    private native void enableAdvertisingSetNative(
            int advertiserId, boolean enable, int duration, int maxExtAdvEvents);

    private native void setAdvertisingDataNative(int advertiserId, byte[] data, byte[] dataEnc);

    private native void setScanResponseDataNative(int advertiserId, byte[] data, byte[] dataEnc);

    private native void setAdvertisingParametersNative(
            int advertiserId, AdvertisingSetParameters parameters);

    private native void setPeriodicAdvertisingParametersNative(
            int advertiserId, PeriodicAdvertisingParameters parameters);

    private native void setPeriodicAdvertisingParametersV2Native(
            int advertiserId, PeriodicAdvertisingParametersV2 parameters);

    private native void setPeriodicAdvertisingDataNative(
            int advertiserId, byte[] data, byte[] dataEnc);

    private native void setPeriodicAdvertisingSubeventDataNative(
            int advertiserId, int num_subevents, byte[] data);

    private native void setPeriodicAdvertisingEnableNative(int advertiserId, boolean enable);
}
