/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package com.android.bluetooth.btservice;

import static android.Manifest.permission.BLUETOOTH_CONNECT;
import static android.Manifest.permission.BLUETOOTH_PRIVILEGED;
import static android.Manifest.permission.BLUETOOTH_SCAN;

import android.annotation.RequiresPermission;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothAdapterCommon;
import android.bluetooth.BluetoothAdapterExt;
import android.bluetooth.BluetoothAdapterUtil;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.util.Log;

public final class AdapterExt {
    private static final String TAG = "AdapterExt";

    public static final int ENABLE_TIMEOUT = 2000;
    public static final int DISABLE_TIMEOUT = 2000;

    private static Context sContext;

    private static int sNewAdapterState = BluetoothAdapter.STATE_OFF;

    private AdapterExt() {}

    private static final BroadcastReceiver sReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (BluetoothAdapterExt.ACTION_STATE_CHANGED.equals(action)) {
                int state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE,
                        BluetoothAdapter.ERROR);
                int prevState = intent.getIntExtra(BluetoothAdapter.EXTRA_PREVIOUS_STATE,
                        BluetoothAdapter.ERROR);

                // Bluetooth adapter is in BluetoothAdapter.STATE_BLE_TURNING_OFF actually
                // when state retrieved from intent is BluetoothAdapter.STATE_OFF. The
                // authoritative OFF signal is ACTION_BLE_STATE_CHANGED handled below, so
                // skip cache/notify here for STATE_OFF to avoid delivering the OFF
                // notification before the default adapter has transitioned into
                // NewAdapterState (which would leave it waiting on the disable timeout).
                if (state != BluetoothAdapter.STATE_OFF) {
                    sNewAdapterState = state;
                    handleActionStateChanged(state, prevState);
                }
            } else if (BluetoothAdapterExt.ACTION_BLE_STATE_CHANGED.equals(action)) {
                int state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE,
                        BluetoothAdapter.ERROR);

                sNewAdapterState = state;
                if (isOff(state)) {
                    AdapterService adapterService =
                            AdapterService.deprecatedGetAdapterService();
                    if (adapterService != null) {
                        adapterService.notifyNewAdapterState(false);
                    }
                }
            }
        }
    };

    public static void create(Context context) {
        sContext = context;
        init();
    }

    private static void init() {
        IntentFilter filter = new IntentFilter();
        filter.addAction(BluetoothAdapterExt.ACTION_STATE_CHANGED);
        filter.addAction(BluetoothAdapterExt.ACTION_BLE_STATE_CHANGED);
        sContext.registerReceiver(sReceiver, filter);
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    private static void handleActionStateChanged(int state, int prevState) {
        Log.d(TAG, "handleActionStateChanged state: " + state + "(" +
                BluetoothAdapter.nameForState(state) + ")" + ", prevState: " +
                prevState + "(" + BluetoothAdapter.nameForState(prevState) + ")");
        AdapterService adapterService = AdapterService.deprecatedGetAdapterService();
        if (adapterService != null) {
            if (isOn(state)) {
                if (AdapterUtil.isDualAdapterMode()) {
                    handleDualAdapterMode();
                }
                adapterService.notifyNewAdapterState(true);
            } else if (isOff(state)) {
                adapterService.notifyNewAdapterState(false);
            }
        }
    }

    private static BluetoothAdapter getAdapter() {
        return BluetoothAdapterUtil.getAdapter(BluetoothAdapterCommon.ADAPTER_1);
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    public static boolean enable() {
        BluetoothAdapter adapter = getAdapter();
        if (adapter == null) return false;
        return adapter.enable();
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, BLUETOOTH_PRIVILEGED})
    public static boolean disable() {
        BluetoothAdapter adapter = getAdapter();
        if (adapter == null) return false;
        return adapter.disable(false);
    }

    @RequiresPermission(BLUETOOTH_SCAN)
    public static boolean startDiscovery() {
        BluetoothAdapter adapter = getAdapter();
        if (adapter == null) return false;
        return adapter.startDiscovery();
    }

    @RequiresPermission(BLUETOOTH_SCAN)
    public static boolean cancelDiscovery() {
        BluetoothAdapter adapter = getAdapter();
        if (adapter == null) return false;
        return adapter.cancelDiscovery();
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    private static String getName() {
        BluetoothAdapter adapter = getAdapter();
        if (adapter == null) return "";
        return adapter.getName();
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    private static boolean setName(String name) {
        BluetoothAdapter adapter = getAdapter();
        if (adapter == null) return false;
        return adapter.setName(name);
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    private static boolean handleDualAdapterMode() {
        String name = getName();
        String newName = name.endsWith("_NEW") ? name : name + "_NEW";
        Log.d(TAG, "handleDualAdapterMode: setName " + newName);
        return setName(newName);
    }

    public static int getState() {
        return sNewAdapterState;
    }

    public static boolean isOn(int state) {
        return state == BluetoothAdapter.STATE_ON;
    }

    public static boolean isOff(int state) {
        return state == BluetoothAdapter.STATE_OFF;
    }

    public static boolean isTurningOn(int state) {
        return (state == BluetoothAdapter.STATE_ON
                || state == BluetoothAdapter.STATE_TURNING_ON
                || state == BluetoothAdapter.STATE_BLE_TURNING_ON
                || state == BluetoothAdapter.STATE_BLE_ON);
    }
}
