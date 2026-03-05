/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth;

/**
 * Callback definitions for HCI Le Set Default Phy.
 * @hide
 */
oneway interface IBluetoothLeSetDefaultPhyCallback {
    void onCommandComplete(int status);
}