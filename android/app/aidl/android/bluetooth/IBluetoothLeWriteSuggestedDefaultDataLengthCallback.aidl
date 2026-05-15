/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

/**
 * Callback definitions for HCI LE Write Suggested Default Data Length .
 * @hide
 */
oneway interface IBluetoothLeWriteSuggestedDefaultDataLengthCallback {
    void onCommandComplete(int status);
}