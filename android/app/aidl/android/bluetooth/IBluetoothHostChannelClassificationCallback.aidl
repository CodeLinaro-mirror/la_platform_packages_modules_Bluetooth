/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

/**
 * Callback definitions for HCI Le Set Host Channel Classification.
 * @hide
 */
oneway interface IBluetoothHostChannelClassificationCallback {
    void onCommandComplete(int status);
}