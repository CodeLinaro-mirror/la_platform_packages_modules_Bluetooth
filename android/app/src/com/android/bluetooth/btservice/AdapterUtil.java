/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package com.android.bluetooth.btservice;

import static android.Manifest.permission.BLUETOOTH_CONNECT;
import static android.Manifest.permission.LOCAL_MAC_ADDRESS;

import android.annotation.NonNull;
import android.annotation.RequiresPermission;
import android.app.Application;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothAdapterCommon;
import android.bluetooth.BluetoothAdapterExt;
import android.bluetooth.BluetoothAdapterUtil;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.content.Intent;
import android.os.SystemProperties;
import android.provider.Settings;
import android.util.Log;

import com.android.bluetooth.gatt.GattService;
import com.android.bluetooth.opp.BluetoothOppService;
import com.android.bluetooth.R;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.HashMap;

/**
 * Bluetooth adapter utility
 */
public final class AdapterUtil {
    private static final String TAG = "AdapterUtil";

    private static final int ADAPTER_DEFAULT = BluetoothAdapterCommon.ADAPTER_DEFAULT;
    private static final int ADAPTER_1 = BluetoothAdapterCommon.ADAPTER_1;
    private static final int ADAPTER_NUMBER = BluetoothAdapterCommon.ADAPTER_NUMBER;
    // @link BluetoothClass.Device.Major.BITMASK
    private static final int DEFAULT_BLUETOOTH_CLASS = 0x1F00;

    private static Context sContext = null;
    private static boolean sDualBluetooth = false;
    private static int sAdapterIndex = ADAPTER_DEFAULT;
    private static BluetoothAdapter sAdapter = null;
    private static boolean sDualAdapterMode = false;
    private static boolean sFilterDevice = false;
    private static String sCounterpartAddress = null;
    private static HashMap<Integer, ArrayList<Integer>> sProfiles;

    public static void init(@NonNull Context context) {
        sContext = context;
        sDualBluetooth = SystemProperties.getBoolean("persist.bluetooth.dual_bt", false);
        sAdapterIndex = Application.getProcessName().equals(sContext.getPackageName()) ?
               ADAPTER_DEFAULT : ADAPTER_1;
        sAdapter = getAdapter(sAdapterIndex);
        sDualAdapterMode = SystemProperties.getBoolean("persist.bluetooth.dual_adapter_mode", false);
        sFilterDevice = getFilterDeviceConfig();
        if (isDualAdapterMode() && isAdapterDefault()) {
            // In dual adapter mode, default adapter needs to monitor
            // new adapter's state.
            AdapterExt.create(sContext);
        }
        Log.d(TAG, "sDualBluetooth:" + sDualBluetooth + ", sAdapterIndex:" + sAdapterIndex +
              ", sDualAdapterMode:" + sDualAdapterMode + ", sFilterDevice:" + sFilterDevice);

        // Init profile supported in Bluetooth adapter
        sProfiles = new HashMap<Integer, ArrayList<Integer>>(ADAPTER_NUMBER);
        sProfiles.put(ADAPTER_DEFAULT, new ArrayList<Integer>(Arrays.asList(
                BluetoothProfile.GATT,
                BluetoothProfile.GATT_SERVER,
                BluetoothProfile.A2DP_SINK,
                BluetoothProfile.AVRCP_CONTROLLER,
                BluetoothProfile.LE_AUDIO_BROADCAST_ASSISTANT,
                BluetoothProfile.CSIP_SET_COORDINATOR,
                BluetoothProfile.HEADSET_CLIENT,
                BluetoothProfile.HID_HOST,
                BluetoothProfile.LE_CALL_CONTROL,
                BluetoothProfile.MAP_CLIENT,
                BluetoothProfile.MCP_SERVER,
                BluetoothProfile.OPP,
                BluetoothProfile.PAN,
                BluetoothProfile.PBAP_CLIENT,
                BluetoothProfile.VOLUME_CONTROL,
                BluetoothProfile.LE_AUDIO,
                BluetoothProfile.LE_AUDIO_BROADCAST)));
        sProfiles.put(ADAPTER_1, new ArrayList<Integer>(Arrays.asList(
                BluetoothProfile.HEADSET,
                BluetoothProfile.A2DP,
                BluetoothProfile.AVRCP,
                BluetoothProfile.GATT,
                BluetoothProfile.GATT_SERVER)));
    }

    private static boolean getFilterDeviceConfig() {
        return sDualBluetooth &&
                sContext.getResources().getBoolean(R.bool.filter_device);
    }

    public static boolean isDualBluetoothEnabled() {
        return sDualBluetooth;
    }

    public static int getAdapterIndex() {
        return sAdapterIndex;
    }

    public static boolean isAdapterDefault() {
        return isAdapterDefault(getAdapterIndex());
    }

    private static boolean isAdapterDefault(int adapterIndex) {
        return BluetoothAdapterCommon.isAdapterDefault(adapterIndex);
    }

    public static boolean isAdapterDefault(BluetoothDevice device) {
        return isAdapterDefault(device.getAdapterIndex());
    }

    public static boolean isAdapter1() {
        return isAdapter1(getAdapterIndex());
    }

    private static boolean isAdapter1(int adapterIndex) {
        return BluetoothAdapterCommon.isAdapter1(adapterIndex);
    }

    public static BluetoothAdapter getAdapter() {
        return sAdapter;
    }

    public static Intent newIntent(String action, String newAction) {
        return new Intent(isAdapter1() ? newAction : action);
    }

    public static boolean isProfileSupported(int profileId) {
        return sProfiles.get(sAdapterIndex).contains(profileId);
    }

    public static boolean isProfileSupported(long supportedProfiles, int profileId) {
        return (supportedProfiles & (1 << profileId)) != 0;
    }

    public static Class getGattServiceClass() {
        return GattService.class;
    }

    public static boolean isDualAdapterMode() {
        return sDualBluetooth && sDualAdapterMode;
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, LOCAL_MAC_ADDRESS})
    public static boolean filterDevice(BluetoothDevice device) {
        return sFilterDevice ? isCounterpartDevice(device) : false;
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, LOCAL_MAC_ADDRESS})
    public static boolean isCounterpartDevice(BluetoothDevice device) {
        if (sCounterpartAddress == null) {
            sCounterpartAddress = getCounterpartAddress();
        }
        return sCounterpartAddress != null ?
                device.getAddress().equals(sCounterpartAddress) :
                false;
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, LOCAL_MAC_ADDRESS})
    private static String getCounterpartAddress() {
        return isAdapter1() ? getAddress(ADAPTER_DEFAULT) : getAddress(ADAPTER_1);
    }

    private static int getCounterpartIndex() {
        return isAdapterDefault() ? ADAPTER_1 : ADAPTER_DEFAULT;
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, LOCAL_MAC_ADDRESS})
    private static String getAddress(int adapterIndex) {
        BluetoothAdapter adapter = getAdapter(adapterIndex);
        return adapter != null ? adapter.getAddress() : null;
    }

    private static BluetoothAdapter getAdapter(int adapterIndex) {
        return BluetoothAdapterUtil.getAdapter(adapterIndex);
    }

    public static BluetoothDevice getCounterpartDevice(BluetoothDevice device) {
        BluetoothAdapter counterpartAdapter = BluetoothAdapterUtil.getAdapter(getCounterpartIndex());
        return counterpartAdapter.getRemoteDevice(device.getAddress());
    }

    public static int getDefaultBluetoothClass() {
        return DEFAULT_BLUETOOTH_CLASS;
    }
}
