/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

import android.bluetooth.IAdapter;

/**
 * System private API for talking with new Bluetooth service.
 *
 * {@hide}
 */
interface IAdapterExt
{
    @JavaPassthrough(annotation="@android.annotation.RequiresNoPermission")
    IAdapter getBluetoothAdapter();
}
