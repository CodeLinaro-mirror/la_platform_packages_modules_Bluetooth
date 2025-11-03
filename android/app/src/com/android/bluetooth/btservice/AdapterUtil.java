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
import android.bluetooth.BluetoothAdapterUtil;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.content.Intent;
import android.os.SystemProperties;
import android.util.Log;

import com.android.bluetooth.gatt.GattService;
import com.android.bluetooth.R;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;

/**
 * Bluetooth adapter utility
 */
public final class AdapterUtil {
    private static final String TAG = "AdapterUtil";

    private static final int ADAPTER_DEFAULT = BluetoothAdapterCommon.ADAPTER_DEFAULT;
    private static final int ADAPTER_NUMBER = BluetoothAdapterCommon.ADAPTER_NUMBER;
    // BluetoothClass.Device.Major.BITMASK
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
        sAdapterIndex = resolveAdapterIndex(
                Application.getProcessName(), sContext.getPackageName());
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
        sProfiles = new HashMap<>(ADAPTER_NUMBER);
        sProfiles.put(ADAPTER_DEFAULT, new ArrayList<>(Arrays.asList(
                BluetoothProfile.GATT,
                BluetoothProfile.GATT_SERVER,
                BluetoothProfile.A2DP_SINK,
                BluetoothProfile.AVRCP_CONTROLLER,
                BluetoothProfile.LE_AUDIO_BROADCAST_ASSISTANT,
                BluetoothProfile.BATTERY,
                BluetoothProfile.CSIP_SET_COORDINATOR,
                BluetoothProfile.HAP_CLIENT,
                BluetoothProfile.HEADSET_CLIENT,
                BluetoothProfile.HEARING_AID,
                BluetoothProfile.HID_HOST,
                BluetoothProfile.LE_CALL_CONTROL,
                BluetoothProfile.MAP_CLIENT,
                BluetoothProfile.MCP_SERVER,
                BluetoothProfile.OPP,
                BluetoothProfile.PAN,
                BluetoothProfile.PBAP_CLIENT,
                BluetoothProfile.SAP,
                BluetoothProfile.VOLUME_CONTROL,
                BluetoothProfile.LE_AUDIO,
                BluetoothProfile.LE_AUDIO_BROADCAST)));
        sProfiles.put(BluetoothAdapterCommon.ADAPTER_1, new ArrayList<Integer>(Arrays.asList(
                BluetoothProfile.HEADSET,
                BluetoothProfile.A2DP,
                BluetoothProfile.AVRCP,
                BluetoothProfile.GATT,
                BluetoothProfile.GATT_SERVER,
                BluetoothProfile.HID_HOST,
                BluetoothProfile.LE_AUDIO_BROADCAST_ASSISTANT,
                BluetoothProfile.CSIP_SET_COORDINATOR,
                BluetoothProfile.HAP_CLIENT,
                BluetoothProfile.HEARING_AID,
                BluetoothProfile.LE_CALL_CONTROL,
                BluetoothProfile.MCP_SERVER,
                BluetoothProfile.VOLUME_CONTROL,
                BluetoothProfile.LE_AUDIO,
                BluetoothProfile.LE_AUDIO_BROADCAST)));
    }

    private static boolean getFilterDeviceConfig() {
        return sDualBluetooth
                && sContext.getResources().getBoolean(R.bool.filter_device);
    }

    /**
     * Resolves the adapter index for the current process.
     *
     * <p>The primary process name matches the application package name and maps to
     * {@link BluetoothAdapterCommon#ADAPTER_DEFAULT}. Secondary processes are named
     * {@code "<packageName><N>"} (e.g. {@code "com.android.bluetooth1"}) and map to adapter
     * index N.
     */
    static int resolveAdapterIndex(String processName, String packageName) {
        if (processName.equals(packageName)) {
            return ADAPTER_DEFAULT;
        }
        if (processName.startsWith(packageName)) {
            String suffix = processName.substring(packageName.length());
            try {
                int idx = Integer.parseInt(suffix);
                return BluetoothAdapterCommon.validAdapter(idx) ? idx : ADAPTER_DEFAULT;
            } catch (NumberFormatException e) {
                // Fall through to ADAPTER_DEFAULT.
            }
        }
        return ADAPTER_DEFAULT;
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

    public static BluetoothAdapter getAdapter() {
        return sAdapter;
    }

    public static Intent newIntent(String action, String newAction) {
        return new Intent(isAdapterDefault() ? action : newAction);
    }

    public static boolean isProfileSupported(int profileId) {
        if (sDualBluetooth == true && isAdapterDefault()) {
            if (profileId == BluetoothProfile.LE_AUDIO_BROADCAST ||
                profileId == BluetoothProfile.CSIP_SET_COORDINATOR ||
                profileId == BluetoothProfile.HAP_CLIENT ||
                profileId == BluetoothProfile.LE_AUDIO ||
                profileId == BluetoothProfile.LE_CALL_CONTROL ||
                profileId == BluetoothProfile.MCP_SERVER ||
                profileId == BluetoothProfile.VOLUME_CONTROL ||
                profileId == BluetoothProfile.LE_AUDIO_BROADCAST_ASSISTANT) {
                return false;
            } else {
                return sProfiles.get(sAdapterIndex).contains(profileId);
            }
        } else {
            return sProfiles.get(sAdapterIndex).contains(profileId);
        }
    }

    public static boolean isProfileSupported(long supportedProfiles, int profileId) {
        return (supportedProfiles & (1 << profileId)) != 0;
    }

    public static Class<GattService> getGattServiceClass() {
        return GattService.class;
    }

    public static boolean isDualAdapterMode() {
        return sDualBluetooth && sDualAdapterMode;
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, LOCAL_MAC_ADDRESS})
    public static boolean filterDevice(BluetoothDevice device) {
        if (!sFilterDevice) return false;
        return isCounterpartDevice(device);
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, LOCAL_MAC_ADDRESS})
    public static boolean isCounterpartDevice(BluetoothDevice device) {
        if (sCounterpartAddress == null) {
            sCounterpartAddress = getCounterpartAddress();
        }
        if (sCounterpartAddress == null) return false;
        return device.getAddress().equals(sCounterpartAddress);
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, LOCAL_MAC_ADDRESS})
    private static String getCounterpartAddress() {
        return getAddress(getCounterpartIndex());
    }

    private static int getCounterpartIndex() {
        // For the two-adapter case the counterpart is always the other adapter.
        // TODO: generalise when three or more adapters are provisioned.
        return isAdapterDefault() ? BluetoothAdapterCommon.ADAPTER_1 : ADAPTER_DEFAULT;
    }

    @RequiresPermission(allOf = {BLUETOOTH_CONNECT, LOCAL_MAC_ADDRESS})
    private static String getAddress(int adapterIndex) {
        BluetoothAdapter adapter = getAdapter(adapterIndex);
        if (adapter == null) return null;
        return adapter.getAddress();
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
