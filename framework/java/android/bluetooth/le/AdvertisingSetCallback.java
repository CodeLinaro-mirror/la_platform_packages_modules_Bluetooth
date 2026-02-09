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

import android.annotation.FlaggedApi;
import android.annotation.NonNull;
import com.android.bluetooth.flags.Flags;
import android.bluetooth.BluetoothDevice;
import android.annotation.SystemApi;

/** Bluetooth LE advertising set callbacks, used to deliver advertising operation status. */
public abstract class AdvertisingSetCallback {

    /** The requested operation was successful. */
    public static final int ADVERTISE_SUCCESS = 0;

    /** Failed to start advertising as the advertise data to be broadcasted is too large. */
    public static final int ADVERTISE_FAILED_DATA_TOO_LARGE = 1;

    /** Failed to start advertising because no advertising instance is available. */
    public static final int ADVERTISE_FAILED_TOO_MANY_ADVERTISERS = 2;

    /** Failed to start advertising as the advertising is already started. */
    public static final int ADVERTISE_FAILED_ALREADY_STARTED = 3;

    /** Operation failed due to an internal error. */
    public static final int ADVERTISE_FAILED_INTERNAL_ERROR = 4;

    /** This feature is not supported on this platform. */
    public static final int ADVERTISE_FAILED_FEATURE_UNSUPPORTED = 5;

    /**
     * Callback triggered in response to {@link BluetoothLeAdvertiser#startAdvertisingSet}
     * indicating result of the operation. If status is ADVERTISE_SUCCESS, then advertisingSet
     * contains the started set and it is advertising. If error occurred, advertisingSet is null,
     * and status will be set to proper error code.
     *
     * @param advertisingSet The advertising set that was started or null if error.
     * @param txPower tx power that will be used for this set.
     * @param status Status of the operation.
     */
    public void onAdvertisingSetStarted(AdvertisingSet advertisingSet, int txPower, int status) {}

    /**
     * Callback triggered in response to {@link BluetoothLeAdvertiser#stopAdvertisingSet} indicating
     * advertising set is stopped.
     *
     * @param advertisingSet The advertising set.
     */
    public void onAdvertisingSetStopped(AdvertisingSet advertisingSet) {}

    /**
     * Callback triggered in response to {@link BluetoothLeAdvertiser#startAdvertisingSet}
     * indicating result of the operation. If status is ADVERTISE_SUCCESS, then advertising set is
     * advertising.
     *
     * @param advertisingSet The advertising set.
     * @param status Status of the operation.
     */
    public void onAdvertisingEnabled(AdvertisingSet advertisingSet, boolean enable, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#setAdvertisingData} indicating result
     * of the operation. If status is ADVERTISE_SUCCESS, then data was changed.
     *
     * @param advertisingSet The advertising set.
     * @param status Status of the operation.
     */
    public void onAdvertisingDataSet(AdvertisingSet advertisingSet, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#setAdvertisingData} indicating result
     * of the operation.
     *
     * @param advertisingSet The advertising set.
     * @param status Status of the operation.
     */
    public void onScanResponseDataSet(AdvertisingSet advertisingSet, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#setAdvertisingParameters} indicating
     * result of the operation.
     *
     * @param advertisingSet The advertising set.
     * @param txPower tx power that will be used for this set.
     * @param status Status of the operation.
     */
    public void onAdvertisingParametersUpdated(
            AdvertisingSet advertisingSet, int txPower, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#setPeriodicAdvertisingParameters}
     * indicating result of the operation.
     *
     * @param advertisingSet The advertising set.
     * @param status Status of the operation.
     */
    public void onPeriodicAdvertisingParametersUpdated(AdvertisingSet advertisingSet, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#setPeriodicAdvertisingParametersV2}
     * indicating result of the operation.
     *
     * @param advertisingSet The advertising set.
     * @param status Status of the operation.
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void onPeriodicAdvertisingParametersV2Updated(@NonNull AdvertisingSet advertisingSet, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#setPeriodicAdvertisingData}
     * indicating result of the operation.
     *
     * @param advertisingSet The advertising set.
     * @param status Status of the operation.
     */
    public void onPeriodicAdvertisingDataSet(AdvertisingSet advertisingSet, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#setPeriodicAdvertisingSubeventData}
     * indicating result of the operation.
     *
     * @param advertisingSet The advertising set.
     * @param status Status of the operation.
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void onPeriodicAdvertisingSubeventDataSet(@NonNull AdvertisingSet advertisingSet, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#setPeriodicAdvertisingEnabled}
     * indicating result of the operation.
     *
     * @param advertisingSet The advertising set.
     * @param status Status of the operation.
     */
    public void onPeriodicAdvertisingEnabled(
            AdvertisingSet advertisingSet, boolean enable, int status) {}

    /**
     * Callback triggered in response to {@link AdvertisingSet#getOwnAddress()} indicating result of
     * the operation.
     *
     * @param advertisingSet The advertising set.
     * @param addressType type of address.
     * @param address advertising set bluetooth address.
     * @hide
     */
    public void onOwnAddressRead(AdvertisingSet advertisingSet, int addressType, String address) {}

    /**
     * Callback triggered when Periodic Advertising Subevent Request event is
     * reported
     *
     * @param advertisingSet The advertising set.
     * @param subeventStart  The subevent start number.
     * @param subeventStart  The subevent count number.
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void onPeriodicAdvertisingSubeventRequest(
                    @NonNull AdvertisingSet advertisingSet, int subeventStart, int subeventCount) {
    }

    /**
     * Callback triggered when Periodic Advertising Subevent Response is reported.
     *
     * @param advertisingSet The advertising set.
     * @param subevent       The subevent number.
     * @param txStatus       TX status of the command in controller.
     * @param numResponses   The number of the responses.
     * @param payload        The payload data.
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void onPeriodicAdvertisingSubeventResponse(@NonNull AdvertisingSet advertisingSet, int subevent,
                    int txStatus, int numResponses, @NonNull byte[] payload) {
    }

    /**
     * Called when {@link AdvertisingSet#transferSetInfo(BluetoothDevice, int)}
     * finishes.
     *
     * This is a system-only API callback to deliver the controller command status
     * of the PAST Set Info Transfer initiated on this {@link AdvertisingSet}.
     *
     * @param advertisingSet The advertising set.
     * @param device The peer device that received the periodic advertising set
     *               info.
     * @param status The controller command status. A value of {@code 0} indicates
     *               success;
     *               non-zero indicates a controller-reported failure.
     *
     * @hide
     */
    @SystemApi
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void onTransferSetInfo(@NonNull AdvertisingSet advertisingSet, @NonNull BluetoothDevice device,
                        int status) {
    }

}
