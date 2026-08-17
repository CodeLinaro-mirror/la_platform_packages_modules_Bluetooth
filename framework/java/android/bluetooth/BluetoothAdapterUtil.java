/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package android.bluetooth;

import static android.Manifest.permission.BLUETOOTH_CONNECT;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.RequiresNoPermission;
import android.annotation.RequiresPermission;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothProfile;
import android.compat.annotation.UnsupportedAppUsage;
import android.util.Log;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;

/**
 * Bluetooth adapter utility
 */
@SuppressLint("UnflaggedApi")
public final class BluetoothAdapterUtil {
    private static final String TAG = "BluetoothAdapterUtil";

    private static final int ADAPTER_DEFAULT = BluetoothAdapterCommon.ADAPTER_DEFAULT;
    private static final int ADAPTER_NUMBER = BluetoothAdapterCommon.ADAPTER_NUMBER;

    private static boolean sDualBluetoothSupported = false;
    private static HashMap<Integer, ArrayList<Integer>> sProfiles;

    static {
        classInit();
    }

    private static void classInit() {
        sDualBluetoothSupported = BluetoothAdapterExt.isSupported();
        sProfiles = new HashMap<>(ADAPTER_NUMBER);

        sProfiles.put(ADAPTER_DEFAULT, new ArrayList<>(Arrays.asList(
                BluetoothProfile.GATT,
                BluetoothProfile.GATT_SERVER,
                BluetoothProfile.A2DP,
                BluetoothProfile.A2DP_SINK,
                BluetoothProfile.AVRCP,
                BluetoothProfile.AVRCP_CONTROLLER,
                BluetoothProfile.LE_AUDIO_BROADCAST_ASSISTANT,
                BluetoothProfile.BATTERY,
                BluetoothProfile.CSIP_SET_COORDINATOR,
                BluetoothProfile.HAP_CLIENT,
                BluetoothProfile.HEADSET,
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

    // Fully-static utility classes must not have constructor
    private BluetoothAdapterUtil() {
    }

    @SuppressLint("UnflaggedApi")
    public static boolean isDualBluetoothSupported() {
        return sDualBluetoothSupported;
    }

    @NonNull
    @SuppressLint("UnflaggedApi")
    public static BluetoothAdapter getAdapter() {
        BluetoothAdapter newAdapter = getNewAdapter();
        // Return new Bluetooth adapter if available.
        // Otherwise, return default Bluetooth adapter.
        if (isDualBluetoothSupported() && newAdapter != null) return newAdapter;
        return getDefaultAdapter();
    }

    /** @hide */
    @UnsupportedAppUsage
    @Nullable
    /*package*/ public static BluetoothAdapter getAdapter(int adapterIndex) {
        if (!validAdapter(adapterIndex)) return null;
        if (isDefaultAdapter(adapterIndex)) return getDefaultAdapter();
        return getNewAdapter();
    }

    @SuppressLint("UnflaggedApi")
    @NonNull
    public static BluetoothAdapter getDefaultAdapter() {
        return BluetoothAdapter.getDefaultAdapter();
    }

    @Nullable
    @RequiresNoPermission
    @SuppressLint("UnflaggedApi")
    public static BluetoothAdapter getNewAdapter() {
        BluetoothAdapterExt adapterExt = BluetoothAdapterExt.getDefaultAdapter();
        if (adapterExt == null) return null;
        return adapterExt.getBluetoothAdapter();
    }

    @Nullable
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SuppressLint("UnflaggedApi")
    public static BluetoothAdapter getNewAdapter(@Nullable BluetoothDevice device) {
        if (isDualBluetoothSupported() && isNewAdapterMatched(device)) return getNewAdapter();
        return null;
    }

    @SuppressLint("UnflaggedApi")
    public static int getAdapterIndex(int profileId) {
        for (int i = ADAPTER_DEFAULT; i < ADAPTER_NUMBER; i++) {
            if (isProfileSupported(profileId, i)) {
                return i;
            }
        }
        return ADAPTER_NUMBER; // no registered adapter supports this profile
    }

    /** @hide */
    @UnsupportedAppUsage
    @SuppressLint("UnflaggedApi")
    /*package*/ public static boolean isProfileSupported(int profileId, int adapterIndex) {
        if (!validAdapter(adapterIndex)) return false;
        return sProfiles.get(adapterIndex).contains(profileId);
    }

    @SuppressLint("UnflaggedApi")
    public static boolean isProfileSupported(int profileId) {
        for (int i = ADAPTER_DEFAULT; i < ADAPTER_NUMBER; i++) {
            if (isProfileSupported(profileId, i)) {
                return true;
            }
        }
        return false;
    }

    private static boolean validAdapter(int adapterIndex) {
        return BluetoothAdapterCommon.validAdapter(adapterIndex);
    }

    @SuppressLint("UnflaggedApi")
    public static boolean isDefaultAdapter(@Nullable BluetoothAdapter adapter) {
        return adapter != null && isDefaultAdapter(adapter.getAdapterIndex());
    }

    @SuppressLint("UnflaggedApi")
    public static boolean isDefaultAdapter(@Nullable BluetoothDevice device) {
        if (device == null) return false;
        return isDefaultAdapter(device.getAdapterIndex());
    }

    private static boolean isDefaultAdapter(int adapterIndex) {
        return BluetoothAdapterCommon.isAdapterDefault(adapterIndex);
    }

    @SuppressLint("UnflaggedApi")
    public static boolean isNewAdapter(@Nullable BluetoothAdapter adapter) {
        return adapter != null && isNewAdapter(adapter.getAdapterIndex());
    }

    @SuppressLint("UnflaggedApi")
    public static boolean isNewAdapter(@Nullable BluetoothDevice device) {
        if (device == null) return false;
        return isNewAdapter(device.getAdapterIndex());
    }

    private static boolean isNewAdapter(int adapterIndex) {
        return BluetoothAdapterCommon.isNonDefaultAdapter(adapterIndex);
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    private static boolean isNewAdapterMatched(@Nullable BluetoothDevice device) {
        return device != null && isNewAdapter(getAdapterIndexMatched(device));
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    private static int getAdapterIndexMatched(BluetoothDevice device) {
        BluetoothClass btClass = device.getBluetoothClass();
        if (btClass == null || !isDualBluetoothSupported()) {
            return ADAPTER_DEFAULT;
        }
        return btClass.doesClassMatch(BluetoothClass.PROFILE_HEADSET) ||
               btClass.doesClassMatch(BluetoothClass.PROFILE_A2DP) ||
               btClass.doesClassMatch(BluetoothClass.PROFILE_HID) ?
               BluetoothAdapterCommon.ADAPTER_1 :
               ADAPTER_DEFAULT;
    }

    /** @hide */
    public static IBluetoothGatt getBluetoothGatt(int adapterIndex) {
        BluetoothAdapter adapter = getAdapter(adapterIndex);
        if (adapter == null) return null;
        return adapter.getBluetoothGatt();
    }
}
