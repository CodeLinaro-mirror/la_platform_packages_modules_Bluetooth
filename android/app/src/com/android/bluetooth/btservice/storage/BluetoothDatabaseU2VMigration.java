/*
 * Copyright 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.btservice.storage;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.BluetoothSinkAudioPolicy;
import android.bluetooth.BluetoothUtils;
import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.database.sqlite.SQLiteDatabase;

import android.os.Build;
import android.util.Log;
import android.util.Pair;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Optional;

public final class BluetoothDatabaseU2VMigration {
    private static final String TAG = "BluetoothDatabaseU2VMigration";

    private static final String BLUETOOTH_DATABASE = "bluetooth_db";

    private static final String BLUETOOTH_CONFIG = "bluetooth_config";
    private static final String MIGRATION_DONE_PROPERTY = "migration_bt_database_u2v_done";

    private static final int MIGRATION_STATUS_TO_BE_DONE = 0;
    private static final int MIGRATION_STATUS_COMPLETED = 1;

    public static void run(Context ctx) {
        if (!isAndroidV())
            return;

        if (migrationStatus(ctx) == MIGRATION_STATUS_COMPLETED) {
            Log.d(TAG, "BT database U2V: already completed");
            return;
        }

        doU2VMigration(ctx);

        markMigrationStatus(ctx, MIGRATION_STATUS_COMPLETED);
        Log.d(TAG, "BT database U2V: completed");
    }

    private static void doU2VMigration(Context ctx) {
        File dbPath = ctx.getDatabasePath(BLUETOOTH_DATABASE);
        if (!dbPath.exists()) {
            Log.d(TAG, "doU2VMigration: database doesn't exist");
            return;
        }

        final SQLiteDatabase.OpenParams params = new SQLiteDatabase.OpenParams.Builder()
                .addOpenFlags(SQLiteDatabase.OPEN_READONLY)
                .build();
        final SQLiteDatabase db = SQLiteDatabase.openDatabase(dbPath, params);

        if (db == null) {
            Log.d(TAG, "doU2VMigration: failed to open database");
            return;
        }

        List metadataList = null;
        try {
            final Cursor cursor = db.query("metadata", null, null, null, null, null, null);
            Log.d(TAG, "doU2VMigration: count=" + cursor.getCount());

            try {
                if (!isQcBtDatabase(cursor)) {
                    return;
                }

                metadataList = generateMetaDataList(cursor);
            } finally {
                cursor.close();
            }
        } finally {
            db.close();
        }

        if (metadataList == null) {
            Log.d(TAG, "doU2VMigration: metadataList is null");
            return;
        }

        MetadataDatabase database = MetadataDatabase.createDatabase(ctx);
        if (database == null) {
            Log.d(TAG, "doU2VMigration: failed to open roomdatabase");
            return;
        }
        try {
            if (!metadataList.isEmpty()) {
                database.insert((Metadata[])metadataList.toArray(new Metadata[0]));
            }
        } finally {
            database.close();
        }
    }

    private static List<Metadata> generateMetaDataList(Cursor cursor) {
        List metadataList = new ArrayList<Metadata>();
        while (cursor.moveToNext()) {
            try {
                final String primaryKey = cursor.getString(cursor.getColumnIndexOrThrow("address"));
                final Metadata metadata = new Metadata(primaryKey);

                migrate_a2dpSupportsOptionalCodecs(cursor, metadata);
                migrate_a2dpOptionalCodecsEnabled(cursor, metadata);

                fetchInt(cursor, "last_active_time")
                        .ifPresent(val -> metadata.last_active_time = val);
                fetchInt(cursor, "is_active_a2dp_device")
                        .ifPresent(val -> metadata.is_active_a2dp_device = val > 0);
                fetchInt(cursor, "is_active_hfp_device")
                        .ifPresent(val -> metadata.isActiveHfpDevice = val > 0);
                fetchInt(cursor, "preferred_output_only_profile")
                        .ifPresent(val -> metadata.preferred_output_only_profile = val);
                fetchInt(cursor, "preferred_duplex_profile")
                        .ifPresent(val -> metadata.preferred_duplex_profile = val);
                fetchInt(cursor, "active_audio_device_policy")
                        .ifPresent(val -> metadata.active_audio_device_policy = val);

                migrate_connectionPolicy(cursor, metadata);
                migrate_customizedMeta(cursor, metadata);
                migrate_audioPolicy(cursor, metadata);

                metadataList.add(metadata);
                Log.d(TAG, "One item migrated: " + metadata);
            } catch (IllegalArgumentException e) {
                Log.e(TAG, "Failed to migrate one item: " + e);
            }
        }
        return metadataList;
    }

    private static final List<Pair<Integer, String>> CONNECTION_POLICIES =
            Arrays.asList(
            new Pair(BluetoothProfile.A2DP, "a2dp_connection_policy"),
            new Pair(BluetoothProfile.A2DP_SINK, "a2dp_sink_connection_policy"),
            new Pair(BluetoothProfile.HEADSET, "hfp_connection_policy"),
            new Pair(BluetoothProfile.HEADSET_CLIENT, "hfp_client_connection_policy"),
            new Pair(BluetoothProfile.HID_HOST, "hid_host_connection_policy"),
            new Pair(BluetoothProfile.PAN, "pan_connection_policy"),
            new Pair(BluetoothProfile.PBAP, "pbap_connection_policy"),
            new Pair(BluetoothProfile.PBAP_CLIENT, "pbap_client_connection_policy"),
            new Pair(BluetoothProfile.MAP, "map_connection_policy"),
            new Pair(BluetoothProfile.SAP, "sap_connection_policy"),
            new Pair(BluetoothProfile.HEARING_AID, "hearing_aid_connection_policy"),
            new Pair(BluetoothProfile.HAP_CLIENT, "hap_client_connection_policy"),
            new Pair(BluetoothProfile.MAP_CLIENT, "map_client_connection_policy"),
            new Pair(BluetoothProfile.LE_AUDIO, "le_audio_connection_policy"),
            new Pair(BluetoothProfile.VOLUME_CONTROL, "volume_control_connection_policy"),
            new Pair(BluetoothProfile.CSIP_SET_COORDINATOR,
                "csip_set_coordinator_connection_policy"),
            new Pair(BluetoothProfile.LE_CALL_CONTROL, "le_call_control_connection_policy"),
            new Pair(BluetoothProfile.LE_AUDIO_BROADCAST_ASSISTANT,
                "bass_client_connection_policy"),
            new Pair(BluetoothProfile.BATTERY, "battery_connection_policy")
    );

    private static final List<Pair<Integer, String>> CUSTOMIZED_META_KEYS =
            Arrays.asList(
            new Pair(BluetoothDevice.METADATA_MANUFACTURER_NAME, "manufacturer_name"),
            new Pair(BluetoothDevice.METADATA_MODEL_NAME, "model_name"),
            new Pair(BluetoothDevice.METADATA_SOFTWARE_VERSION, "software_version"),
            new Pair(BluetoothDevice.METADATA_HARDWARE_VERSION, "hardware_version"),
            new Pair(BluetoothDevice.METADATA_COMPANION_APP, "companion_app"),
            new Pair(BluetoothDevice.METADATA_MAIN_ICON, "main_icon"),
            new Pair(BluetoothDevice.METADATA_IS_UNTETHERED_HEADSET, "is_untethered_headset"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_LEFT_ICON, "untethered_left_icon"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_RIGHT_ICON, "untethered_right_icon"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_CASE_ICON, "untethered_case_icon"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_LEFT_BATTERY,
                "untethered_left_battery"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_RIGHT_BATTERY,
                "untethered_right_battery"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_CASE_BATTERY,
                "untethered_case_battery"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_LEFT_CHARGING,
                "untethered_left_charging"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_RIGHT_CHARGING,
                "untethered_right_charging"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_CASE_CHARGING,
                    "untethered_case_charging"),
            new Pair(BluetoothDevice.METADATA_ENHANCED_SETTINGS_UI_URI,
                    "enhanced_settings_ui_uri"),
            new Pair(BluetoothDevice.METADATA_DEVICE_TYPE, "device_type"),
            new Pair(BluetoothDevice.METADATA_MAIN_BATTERY, "main_battery"),
            new Pair(BluetoothDevice.METADATA_MAIN_CHARGING, "main_charging"),
            new Pair(BluetoothDevice.METADATA_MAIN_LOW_BATTERY_THRESHOLD,
                    "main_low_battery_threshold"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_LEFT_LOW_BATTERY_THRESHOLD,
                    "untethered_left_low_battery_threshold"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_RIGHT_LOW_BATTERY_THRESHOLD,
                    "untethered_right_low_battery_threshold"),
            new Pair(BluetoothDevice.METADATA_UNTETHERED_CASE_LOW_BATTERY_THRESHOLD,
                    "untethered_case_low_battery_threshold"),
            new Pair(BluetoothDevice.METADATA_SPATIAL_AUDIO, "spatial_audio"),
            new Pair(BluetoothDevice.METADATA_FAST_PAIR_CUSTOMIZED_FIELDS,
                    "fastpair_customized"),
            new Pair(BluetoothDevice.METADATA_LE_AUDIO,
                    "le_audio"),
            new Pair(BluetoothDevice.METADATA_GMCS_CCCD,
                    "gmcs_cccd"),
            new Pair(BluetoothDevice.METADATA_GTBS_CCCD,
                    "gtbs_cccd"),
            new Pair(BluetoothDevice.METADATA_EXCLUSIVE_MANAGER,
                    "exclusive_manager")
    );

    private static Optional<Integer> fetchInt(Cursor cursor, String key) {
        int index = cursor.getColumnIndex(key);
        return index >= 0 ? Optional.of(cursor.getInt(index)) : Optional.empty();
    }

    private static void migrate_a2dpSupportsOptionalCodecs(Cursor cursor,
            Metadata metadata) {
        final String key = "a2dpSupportsOptionalCodecs";
        final List<Integer> allowedValue =  new ArrayList<>(Arrays.asList(
                BluetoothA2dp.OPTIONAL_CODECS_SUPPORT_UNKNOWN,
                BluetoothA2dp.OPTIONAL_CODECS_NOT_SUPPORTED,
                BluetoothA2dp.OPTIONAL_CODECS_SUPPORTED));
        final Optional<Integer> value = fetchInt(cursor, key);
        if (!value.isPresent() || !allowedValue.contains(value.get()))
            return;
        metadata.a2dpSupportsOptionalCodecs = value.get();
    }

    private static void migrate_a2dpOptionalCodecsEnabled(Cursor cursor,
            Metadata metadata) {
        final String key = "a2dpOptionalCodecsEnabled";
        final List<Integer> allowedValue =  new ArrayList<>(Arrays.asList(
                BluetoothA2dp.OPTIONAL_CODECS_PREF_UNKNOWN,
                BluetoothA2dp.OPTIONAL_CODECS_PREF_DISABLED,
                BluetoothA2dp.OPTIONAL_CODECS_PREF_ENABLED));
        final Optional<Integer> value = fetchInt(cursor, key);
        if (!value.isPresent() || !allowedValue.contains(value.get()))
            return;
        metadata.a2dpOptionalCodecsEnabled = value.get();
    }

    @SuppressWarnings("unused")
    private static void migrate_connectionPolicy(Cursor cursor,
            Metadata metadata) {
        final List<Integer> allowedValue =  new ArrayList<>(Arrays.asList(
                BluetoothProfile.CONNECTION_POLICY_UNKNOWN,
                BluetoothProfile.CONNECTION_POLICY_FORBIDDEN,
                BluetoothProfile.CONNECTION_POLICY_ALLOWED));
        for (Pair<Integer, String> p : CONNECTION_POLICIES) {
            final Optional<Integer> policy = fetchInt(cursor, p.second);
            if (policy.isPresent() && allowedValue.contains(policy.get())) {
                // metadata.setProfileConnectionPolicy(p.first, policy.get());
            }
        }
    }

    @SuppressWarnings("unused")
    private static void migrate_customizedMeta(Cursor cursor, Metadata metadata) {
        for (Pair<Integer, String> p : CUSTOMIZED_META_KEYS) {
            final int index = cursor.getColumnIndex(p.second);
            if (index >= 0) {
                // final byte[] blob = cursor.getBlob(index);
                // There is no specific pattern to check the custom meta data
                // metadata.setCustomizedMeta(p.first, blob);
            }
        }
    }

    private static void migrate_audioPolicy(Cursor cursor,
            Metadata metadata) {
        final List<Integer> allowedValue =  new ArrayList<>(Arrays.asList(
                BluetoothSinkAudioPolicy.POLICY_UNCONFIGURED,
                BluetoothSinkAudioPolicy.POLICY_ALLOWED,
                BluetoothSinkAudioPolicy.POLICY_NOT_ALLOWED));

        Optional<Integer> policy = fetchInt(cursor, "call_establish_audio_policy");
        if (policy.isPresent() && allowedValue.contains(policy.get())) {
            metadata.audioPolicyMetadata.callEstablishAudioPolicy = policy.get();
        }

        policy = fetchInt(cursor, "connecting_time_audio_policy");
        if (policy.isPresent() && allowedValue.contains(policy.get())) {
            metadata.audioPolicyMetadata.connectingTimeAudioPolicy = policy.get();
        }

        policy = fetchInt(cursor, "in_band_ringtone_audio_policy");
        if (policy.isPresent() && allowedValue.contains(policy.get())) {
            metadata.audioPolicyMetadata.inBandRingtoneAudioPolicy = policy.get();
        }
    }

    @SuppressLint("AndroidFrameworkCompatChange")
    private static boolean isAndroidV() {
        int apiLevel = Build.VERSION.SDK_INT;
        if (!"REL".equals(Build.VERSION.CODENAME))
            apiLevel++;

        boolean is_v = apiLevel == Build.VERSION_CODES.VANILLA_ICE_CREAM;

        if (!is_v) {
            Log.d(TAG, "not V, apiLevel=" + apiLevel + ", codename=" + Build.VERSION.CODENAME);
            return false;
        }

        return true;
    }

    private static boolean isQcBtDatabase(Cursor cursor) {
        boolean is_qc_db = cursor.getColumnIndex("bc_profile_priority") >= 0 ||
                cursor.getColumnIndex("was_previously_connected_to_bc") >= 0;
        if (!is_qc_db) {
            Log.d(TAG, "is not qc bt database");
            return false;
        }

        return true;
    }

    private static int migrationStatus(Context ctx) {
        SharedPreferences pref = ctx.getSharedPreferences(BLUETOOTH_CONFIG, Context.MODE_PRIVATE);
        return pref.getInt(MIGRATION_DONE_PROPERTY, MIGRATION_STATUS_TO_BE_DONE);
    }

    private static void markMigrationStatus(Context ctx, int status) {
        ctx.getSharedPreferences(BLUETOOTH_CONFIG, Context.MODE_PRIVATE)
            .edit()
            .putInt(MIGRATION_DONE_PROPERTY, status)
            .apply();
    }
}
