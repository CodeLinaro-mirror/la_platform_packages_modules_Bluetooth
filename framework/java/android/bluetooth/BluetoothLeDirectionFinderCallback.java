/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import java.lang.annotation.RetentionPolicy;
import java.lang.annotation.Retention;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.le.ScanResult;
import android.annotation.IntDef;
import java.util.Map;
import java.lang.String;
import java.lang.Integer;
import java.util.List;

/**
 * Bluetooth LE Direction Finder related callbacks, used to deliver result of
 * operations performed using {@link BluetoothLeDirectionFinder}
 *
 * @hide
 * @see BluetoothLeDirectionFinderCallback
 */
public abstract class BluetoothLeDirectionFinderCallback {

    /** @hide */
    @IntDef(prefix = "LE_DIRECTION_FINDING_STATUS_", value = {
              LE_DIR_FINDING_STATUS_SUCCESS,
              LE_DIR_FINDING_STATUS_FAILURE,
     })

    @Retention(RetentionPolicy.SOURCE)
    public @interface Le_AoA_Status {}

    public static final int LE_DIR_FINDING_STATUS_SUCCESS = 0x00;
    public static final int LE_DIR_FINDING_STATUS_FAILURE = 0x01;

    /**
     * Callback to indicate Ble Direction Finding enable status.
     *
     * @param device {@link BluetoothDevice} remote device for
     *        which LE Direction Finding is enabled
     * @param status status of Enable Ble Direction Finding
     */
    public void onEnableBleDirectionFinding(BluetoothDevice device, int status) {
    }

    /**
     * Callback to indicate LE AoA Results.
     *
     * @param device {@link BluetoothDevice} remote device for which LE AoA Results is received
     * @param status status of LE AoA Results
     * @param azimuth  azimuth value
     * @param azimuthUnc  The uncertainty of azimuth result
     * @param elevation  elevation value
     * @param elevationUnc  The uncertainty of elevation result
     */
    public void onLeAoaResults(BluetoothDevice device, int status, double azimuth,
                               int azimuthUnc, double elevation, int elevationUnc) {
    }
}
