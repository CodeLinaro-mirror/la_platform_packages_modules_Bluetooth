/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package android.bluetooth;

import android.bluetooth.IBluetooth;

/**
 * System private API for talking with new Bluetooth service.
 *
 * {@hide}
 */
interface IBluetoothExt
{
    @JavaPassthrough(annotation="@android.annotation.RequiresNoPermission")
    IBluetooth getBluetooth();
}
