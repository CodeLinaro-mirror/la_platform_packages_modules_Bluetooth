/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package android.bluetooth;

import static android.Manifest.permission.BLUETOOTH_CONNECT;

import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.RequiresPermission;
import android.annotation.RequiresNoPermission;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothProfile;
import android.compat.annotation.UnsupportedAppUsage;
import android.os.RemoteException;
import android.util.Log;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;

/**
 * Bluetooth adapter utility
 */
@SuppressLint("UnflaggedApi")
public final class BluetoothAdapterUtil {
    private static final String TAG = "BluetoothAdapterUtil";

    private static final int ADAPTER_DEFAULT = BluetoothAdapterCommon.ADAPTER_DEFAULT;
    private static final int ADAPTER_1 = BluetoothAdapterCommon.ADAPTER_1;
    private static final int ADAPTER_NUMBER = BluetoothAdapterCommon.ADAPTER_NUMBER;

    private static boolean sDualBluetoothSupported = false;
    private static HashMap<Integer, ArrayList<Integer>> sProfiles;

    static {
        classInit();
    }

    private static void classInit() {
        sDualBluetoothSupported = BluetoothAdapterExt.isSupported();
        sProfiles = new HashMap<Integer, ArrayList<Integer>>(ADAPTER_NUMBER);

        sProfiles.put(ADAPTER_DEFAULT, new ArrayList<Integer>(Arrays.asList(
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
        sProfiles.put(ADAPTER_1, new ArrayList<Integer>(Arrays.asList(
                BluetoothProfile.A2DP,
                BluetoothProfile.AVRCP,
                BluetoothProfile.GATT,
                BluetoothProfile.GATT_SERVER,
                BluetoothProfile.HID_HOST)));
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
        return isDualBluetoothSupported() && (newAdapter != null) ?
                newAdapter :
                getDefaultAdapter();
    }

    /** @hide */
    @UnsupportedAppUsage
    @Nullable
    /*package*/ public static BluetoothAdapter getAdapter(int adapterIndex) {
        return validAdapter(adapterIndex) ?
                (isDefaultAdapter(adapterIndex) ?
                getDefaultAdapter() :
                getNewAdapter()) :
                null;
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
        return adapterExt != null ? adapterExt.getBluetoothAdapter() : null;
    }

    @Nullable
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SuppressLint("UnflaggedApi")
    public static BluetoothAdapter getNewAdapter(@Nullable BluetoothDevice device) {
        return isDualBluetoothSupported() &&
                isNewAdapterMatched(device) ?
                getNewAdapter() :
                null;
    }

    @SuppressLint("UnflaggedApi")
    public static int getAdapterIndex(int profileId) {
        return isProfileSupported(profileId, ADAPTER_DEFAULT) ?
                ADAPTER_DEFAULT : (isProfileSupported(profileId, ADAPTER_1) ?
                ADAPTER_1 : ADAPTER_NUMBER);
    }

    /** @hide */
    @UnsupportedAppUsage
    @SuppressLint("UnflaggedApi")
    /*package*/ public static boolean isProfileSupported(int profileId, int adapterIndex) {
        return validAdapter(adapterIndex) ?
                sProfiles.get(adapterIndex).contains(profileId) :
                false;
    }

    @SuppressLint("UnflaggedApi")
    public static boolean isProfileSupported(int profileId) {
        return isProfileSupported(profileId, ADAPTER_DEFAULT) ||
                isProfileSupported(profileId, ADAPTER_1);
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
        return (device != null) ?
                isDefaultAdapter(device.getAdapterIndex()) :
                false;
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
        return (device != null) ?
                isNewAdapter(device.getAdapterIndex()) :
                false;
    }

    private static boolean isNewAdapter(int adapterIndex) {
        return BluetoothAdapterCommon.isAdapter1(adapterIndex);
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    private static boolean isNewAdapterMatched(@Nullable BluetoothDevice device) {
        return device != null && isNewAdapter(getAdapterIndexMatched(device));
    }

    @RequiresPermission(BLUETOOTH_CONNECT)
    private static int getAdapterIndexMatched(BluetoothDevice device) {
        BluetoothClass btClass = device.getBluetoothClass();
        if (btClass == null || !isDualBluetoothSupported())
            return ADAPTER_DEFAULT;

        return btClass.doesClassMatch(BluetoothClass.PROFILE_HEADSET) ||
               btClass.doesClassMatch(BluetoothClass.PROFILE_A2DP) ||
               btClass.doesClassMatch(BluetoothClass.PROFILE_HID) ?
               ADAPTER_1 :
               ADAPTER_DEFAULT;
    }

    /** @hide */
    public static IBluetoothGatt getBluetoothGatt(int adapterIndex) {
        BluetoothAdapter adapter = getAdapter(adapterIndex);
        return adapter == null ? null : adapter.getBluetoothGatt();
    }
}
