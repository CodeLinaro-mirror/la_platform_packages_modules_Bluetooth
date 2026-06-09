/*
 * Copyright 2021 HIMSA II K/S - www.himsa.com.
 * Represented by EHIMA - www.ehima.com
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
 */

package com.android.bluetooth.leaudio;

import android.bluetooth.BluetoothLeAudioContentMetadata;
import android.bluetooth.BluetoothLeBroadcast;
import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSettings;
import android.bluetooth.BluetoothLeBroadcastSubgroupSettings;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.media.AudioManager;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.NumberPicker;
import android.widget.ScrollView;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.ViewModelProviders;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.floatingactionbutton.FloatingActionButton;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

public class BroadcasterActivity extends AppCompatActivity {
    private static final String TAG = "BroadcasterActivity";

    private BroadcasterViewModel mViewModel;

    private final String BROADCAST_PREFS_KEY = "BROADCAST_PREFS_KEY";
    private final String PREF_SEP = ":";
    private final String VALUE_NOT_SET = "undefined";

    private static final String ACTION_DBIG_STATUS_CHANGED =
            "android.bluetooth.action.LE_AUDIO_DBIG_STATUS_CHANGED";
    private static final String EXTRA_DBIG_STATUS =
            "android.bluetooth.extra.DBIG_STATUS";
    /* Join Control state - persisted across app restarts */
    private static final String PREFS_NAME = "leaudio_prefs";
    private static final String KEY_JOIN_CONTROL_ENABLED = "join_control_enabled";

    /**
     * Tracks whether DBIG Join Control is currently enabled.
     * Default is true (join control on by default).
     * Persisted in SharedPreferences so the state survives crashes.
     */
    private boolean mJoinControlEnabled = true;

    /* ------------------------------------------------------------------
     *  BIS connectivity state (updated via ACTION_DBIG_STATUS_CHANGED)
     * ------------------------------------------------------------------ */
    private enum BisAvailability {
        AVAILABLE,
        UNAVAILABLE
    }

    private BisAvailability mBisAvailability = BisAvailability.UNAVAILABLE;
    private boolean mLocalOccupyingBis = false;

    private int mLastBroadcastFeatures = -1;
    private int[] mLastBisDevIds = null;

    /* ------------------------------------------------------------------
     *  PGP tracking — up to 10 devices, populated from DBIG status events
     * ------------------------------------------------------------------ */
    private static final int MAX_PGP_TRACKED = 10;
    /** devId → display name; insertion-ordered so oldest entry can be evicted. */
    private final java.util.LinkedHashMap<Integer, String> mTrackedPgps =
            new java.util.LinkedHashMap<>();
    /** True while a Remove Device procedure is outstanding (between request and completion). */
    private boolean mRemovePending = false;

    // Tracks broadcast IDs for which join control was already auto-enabled in this
    // session. Guards against duplicate STREAMING callbacks (e.g., duplex TX then TX+RX).
    private final java.util.Set<Integer> mJoinControlAutoEnabledIds = new java.util.HashSet<>();
    /** Retained reference so we can enable/disable the button across Remove completions. */
    private Button mBtnRemoveDevice = null;
    /** Persistent status label shown below the Remove Device button. */
    private TextView mRemoveStatusText = null;

    private AudioManager mAudioManager;
    /** Reference to the currently visible broadcast-info dialog (for in-place refresh). */
    private AlertDialog mCurrentInfoDialog = null;

    private final BroadcastReceiver mDbigStatusReceiver =
            new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    String action = intent.getAction();
                    Log.d(TAG, "Received broadcast action: " + action);
                    if (!ACTION_DBIG_STATUS_CHANGED.equals(action)) {
                        return;
                    }
                    int status = intent.getIntExtra(EXTRA_DBIG_STATUS, -1);
                    if (status < 0) {
                        Log.w(TAG, "DBIG status broadcast missing status extra");
                        return;
                    }
                int[] bisDevIds = intent.getIntArrayExtra("android.bluetooth.extra.DBIG_BIS_DEV_IDS");
                int broadcastFeatures = intent.getIntExtra("android.bluetooth.extra.DBIG_BROADCAST_FEATURES", 0);
                int totalBis = (bisDevIds != null) ? bisDevIds.length : 0;
                int occupiedCount = 0, availableCount = 0;
                if (bisDevIds != null) {
                    for (int d : bisDevIds) { if (d != 0) occupiedCount++; else availableCount++; }
                }
                Log.i(TAG, "DBIG: totalBis=" + totalBis + ", occupied=" + occupiedCount
                        + ", available=" + availableCount
                        + ", features=0x" + Integer.toHexString(broadcastFeatures));
                boolean featuresChanged = (broadcastFeatures != mLastBroadcastFeatures);
                boolean bisIdsChanged = !java.util.Arrays.equals(bisDevIds, mLastBisDevIds);
                if (featuresChanged || bisIdsChanged) {
                    mLastBroadcastFeatures = broadcastFeatures;
                    mLastBisDevIds = (bisDevIds != null) ? java.util.Arrays.copyOf(bisDevIds, bisDevIds.length) : null;
                    Toast.makeText(context, "DBIG: totalBis=" + totalBis
                            + ", occupiedBis=" + occupiedCount + ", availableBis=" + availableCount
                            + ", features=0x" + Integer.toHexString(broadcastFeatures),
                            Toast.LENGTH_SHORT).show();
                }
                    // bit8 (0x0100) – new device added to DBIG
                    boolean newDeviceAdded = (status & 0x0100) != 0;
                    Log.d(TAG, "Device added bit" + newDeviceAdded);
                    if (newDeviceAdded) {
                        int devId = intent.getIntExtra(
                                "android.bluetooth.extra.DBIG_DEV_ID", -1);
                        byte[] nameBytes = intent.getByteArrayExtra(
                                "android.bluetooth.extra.DBIG_NAME");
                        String nameStr = (nameBytes != null)
                                ? new String(nameBytes,
                                        java.nio.charset.StandardCharsets.UTF_8).trim()
                                : "";
                        Log.i(TAG, "New device added to DBIG: devId=0x"
                                + String.format("%04X", devId) + ", name=" + nameStr);
                        Toast.makeText(context,
                                "New device joined DBIG: DevID=" + devId
                                        + ", Name=" + nameStr,
                                Toast.LENGTH_LONG).show();
                    }
                    // bit9 (0x0200) – device is exiting / removed from DBIG
                    boolean deviceRemoved = (status & 0x0200) != 0;
                    Log.d(TAG, "Device removed bit" + deviceRemoved);
                    if (deviceRemoved) {
                        int devId = intent.getIntExtra(
                                "android.bluetooth.extra.DBIG_DEV_ID", -1);
                        byte[] nameBytes = intent.getByteArrayExtra(
                                "android.bluetooth.extra.DBIG_NAME");
                        String nameStr = (nameBytes != null)
                                ? new String(nameBytes,
                                        java.nio.charset.StandardCharsets.UTF_8).trim()
                                : "";
                        Log.i(TAG, "Device removed from DBIG: devId=0x"
                                + String.format("%04X", devId) + ", name=" + nameStr);
                        Toast.makeText(context,
                                "Device exited DBIG: DevID"
                                        + devId
                                        + ", Name=" + nameStr,
                                Toast.LENGTH_LONG).show();
                    }

                    // bit0 (0x0001) - at least one BIS is AVAILABLE
                    // bit1 (0x0002) - a BIS is OCCUPIED by the LOCAL device
                    boolean bisAvailable   = (status & 0x0001) != 0;
                    boolean localOccupying = (status & 0x0002) != 0;

                    mBisAvailability = (bisAvailable && !localOccupying)
                            ? BisAvailability.AVAILABLE
                            : BisAvailability.UNAVAILABLE;
                    mLocalOccupyingBis = localOccupying;

                    if ((mBisAvailability == BisAvailability.AVAILABLE) || localOccupying) {
                        Toast.makeText(context, "BIS is available, user can speak now",
                                Toast.LENGTH_SHORT).show();
                    } else if (!bisAvailable && !localOccupying) {
                        Toast.makeText(context,
                                "BIS is not available, please wait until BIS is available",
                                Toast.LENGTH_SHORT).show();
                    }

                    Log.d(TAG, "DBIG status - availability: " + mBisAvailability
                            + ", local occupying: " + mLocalOccupyingBis);

                    refreshDialogIfVisible();
                }
            };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.broadcaster_activity);

        mAudioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        // Restore persisted Join Control state (default: enabled = true)
        mJoinControlEnabled = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                .getBoolean(KEY_JOIN_CONTROL_ENABLED, true);
        Log.d(TAG, "Restored Join Control state: enabled=" + mJoinControlEnabled);
        FloatingActionButton fab = findViewById(R.id.broadcast_fab);
        fab.setOnClickListener(
                view -> {
                    if (mViewModel.getBroadcastCount() < mViewModel.getMaximumNumberOfBroadcast()) {
                        // Start Dialog with the broadcast input details
                        AlertDialog.Builder alert = new AlertDialog.Builder(this);
                        LayoutInflater inflater = getLayoutInflater();
                        alert.setTitle("Add the Broadcast:");

                        View alertView =
                                inflater.inflate(R.layout.broadcaster_add_broadcast_dialog, null);
                        final EditText code_input_text =
                                alertView.findViewById(R.id.broadcast_code_input);
                        final EditText program_info =
                                alertView.findViewById(R.id.broadcast_program_info_input);
                        final NumberPicker contextPicker =
                                alertView.findViewById(R.id.context_picker);
                        final EditText broadcast_name =
                                alertView.findViewById(R.id.broadcast_name_input);
                        final CheckBox publicCheckbox =
                                alertView.findViewById(R.id.is_public_checkbox);
                        final EditText public_content =
                                alertView.findViewById(R.id.broadcast_public_content_input);
                        final Switch high_quality =
                                alertView.findViewById(R.id.broadcast_high_quality);
                        final EditText iso_interval_input =
                                alertView.findViewById(R.id.iso_interval_input);
                        // Add context type selector
                        contextPicker.setMinValue(1);
                        contextPicker.setMaxValue(
                                alertView
                                                .getResources()
                                                .getStringArray(R.array.content_types)
                                                .length
                                        - 1);
                        contextPicker.setDisplayedValues(
                                alertView.getResources().getStringArray(R.array.content_types));
                        final Button loadButton = alertView.findViewById(R.id.load_button);
                        loadButton.setOnClickListener(
                                new View.OnClickListener() {
                                    @Override
                                    public void onClick(View v) {
                                        showSelectSavedBroadcastAlert(
                                                code_input_text,
                                                program_info,
                                                contextPicker,
                                                broadcast_name,
                                                publicCheckbox,
                                                public_content);
                                    }
                                });
                        final Button clearButton = alertView.findViewById(R.id.clear_button);
                        clearButton.setOnClickListener(
                                new View.OnClickListener() {
                                    @Override
                                    public void onClick(View v) {
                                        SharedPreferences broadcastsPrefs =
                                                getSharedPreferences(BROADCAST_PREFS_KEY, 0);
                                        SharedPreferences.Editor editor = broadcastsPrefs.edit();
                                        editor.clear();
                                        editor.commit();
                                        Toast.makeText(
                                                        BroadcasterActivity.this,
                                                        "Saved broadcasts cleared",
                                                        Toast.LENGTH_SHORT)
                                                .show();
                                    }
                                });
                        alert.setView(alertView)
                                .setNegativeButton(
                                        "Cancel",
                                        (dialog, which) -> {
                                            // Do nothing
                                        })
                                .setNeutralButton(
                                        "Start",
                                        (dialog, which) -> {
                                            // Parse and validate ISO interval
                                            float isoInterval = 0.0f;
                                            String isoIntervalStr = iso_interval_input.getText().toString();
                                            if (!isoIntervalStr.isEmpty()) {
                                                try {
                                                    isoInterval = Float.parseFloat(isoIntervalStr);
                                                    // Validate against allowed values
                                                    if (isoInterval != 7.5f && isoInterval != 10.0f &&
                                                        isoInterval != 20.0f && isoInterval != 30.0f) {
                                                        Toast.makeText(
                                                            BroadcasterActivity.this,
                                                            "Invalid ISO interval. Must be one of: 7.5, 10, 20, 30",
                                                            Toast.LENGTH_LONG).show();
                                                        return;
                                                    }
                                                } catch (NumberFormatException e) {
                                                    Log.w("BroadcasterActivity", "Invalid ISO interval format: " + e.getMessage());
                                                    Toast.makeText(
                                                        BroadcasterActivity.this,
                                                        "Invalid ISO interval format. Must be one of: 7.5, 10, 20, 30",
                                                        Toast.LENGTH_LONG).show();
                                                    return;
                                                }
                                            }

                                            // Enhanced broadcast: validate code before building settings
                                            if (isoInterval > 0) {
                                                String codeStr = code_input_text.getText().toString();
                                                if (!codeStr.isEmpty()) {
                                                    byte[] codeBytes = codeStr.getBytes(java.nio.charset.StandardCharsets.UTF_8);
                                                    if (codeBytes.length < 4 || codeBytes.length > 16) {
                                                        Toast.makeText(BroadcasterActivity.this,
                                                                "Broadcast code must be 4-16 bytes (leave empty for unencrypted)",
                                                                Toast.LENGTH_SHORT).show();
                                                        return;
                                                    }
                                                }
                                            }

                                            BluetoothLeBroadcastSettings broadcastSettings =
                                                    createBroadcastSettingsFromUI(
                                                            program_info.getText().toString(),
                                                            public_content.getText().toString(),
                                                            contextPicker.getValue(),
                                                            publicCheckbox.isChecked(),
                                                            broadcast_name.getText().toString(),
                                                            code_input_text.getText().toString(),
                                                            high_quality.isChecked()
                                                                    ? BluetoothLeBroadcastSubgroupSettings.QUALITY_HIGH
                                                                    : BluetoothLeBroadcastSubgroupSettings.QUALITY_STANDARD);

                                            boolean broadcastStarted;
                                            if (isoInterval > 0) {
                                                // Use enhanced broadcast with ISO interval
                                                broadcastStarted = mViewModel.startEnhancedBroadcast(broadcastSettings, isoInterval);
                                            } else {
                                                // Use standard broadcast without ISO interval
                                                broadcastStarted = mViewModel.startBroadcast(broadcastSettings);
                                            }

                                            if (broadcastStarted) {
                                                String message = "Broadcast was created";
                                                if (isoInterval > 0) {
                                                    message += " with ISO interval: " + isoInterval;
                                                }
                                                Toast.makeText(
                                                                BroadcasterActivity.this,
                                                                message,
                                                                Toast.LENGTH_SHORT)
                                                        .show();
                                            }
                                        })
                                .setPositiveButton(
                                        "Start & save",
                                        (dialog, which) -> {
                                            // Parse and validate ISO interval
                                            float isoInterval = 0.0f;
                                            String isoIntervalStr = iso_interval_input.getText().toString();
                                            if (!isoIntervalStr.isEmpty()) {
                                                try {
                                                    isoInterval = Float.parseFloat(isoIntervalStr);
                                                    // Validate against allowed values
                                                    if (isoInterval != 7.5f && isoInterval != 10.0f &&
                                                        isoInterval != 20.0f && isoInterval != 30.0f) {
                                                        Toast.makeText(
                                                            BroadcasterActivity.this,
                                                            "Invalid ISO interval. Must be one of: 7.5, 10, 20, 30",
                                                            Toast.LENGTH_LONG).show();
                                                        return;
                                                    }
                                                } catch (NumberFormatException e) {
                                                    Log.w("BroadcasterActivity", "Invalid ISO interval format: " + e.getMessage());
                                                    Toast.makeText(
                                                        BroadcasterActivity.this,
                                                        "Invalid ISO interval format. Must be one of: 7.5, 10, 20, 30",
                                                        Toast.LENGTH_LONG).show();
                                                    return;
                                                }
                                            }

                                            // Enhanced broadcast: validate code before building settings
                                            if (isoInterval > 0) {
                                                String codeStr = code_input_text.getText().toString();
                                                if (!codeStr.isEmpty()) {
                                                    byte[] codeBytes = codeStr.getBytes(java.nio.charset.StandardCharsets.UTF_8);
                                                    if (codeBytes.length < 4 || codeBytes.length > 16) {
                                                        Toast.makeText(BroadcasterActivity.this,
                                                                "Broadcast code must be 4-16 bytes (leave empty for unencrypted)",
                                                                Toast.LENGTH_SHORT).show();
                                                        return;
                                                    }
                                                }
                                            }

                                            BluetoothLeBroadcastSettings broadcastSettings =
                                                    createBroadcastSettingsFromUI(
                                                            program_info.getText().toString(),
                                                            public_content.getText().toString(),
                                                            contextPicker.getValue(),
                                                            publicCheckbox.isChecked(),
                                                            broadcast_name.getText().toString(),
                                                            code_input_text.getText().toString(),
                                                            high_quality.isChecked()
                                                            ? BluetoothLeBroadcastSubgroupSettings.QUALITY_HIGH
                                                            : BluetoothLeBroadcastSubgroupSettings.QUALITY_STANDARD);

                                            boolean broadcastStarted;
                                            if (isoInterval > 0) {
                                                // Use enhanced broadcast with ISO interval
                                                broadcastStarted = mViewModel.startEnhancedBroadcast(broadcastSettings, isoInterval);
                                            } else {
                                                // Use standard broadcast without ISO interval
                                                broadcastStarted = mViewModel.startBroadcast(broadcastSettings);
                                            }

                                            if (broadcastStarted) {
                                                // Save only if started successfully
                                                if (saveBroadcastToSharedPref(
                                                        program_info.getText().toString(),
                                                        public_content.getText().toString(),
                                                        contextPicker.getValue(),
                                                        publicCheckbox.isChecked(),
                                                        broadcast_name.getText().toString(),
                                                        code_input_text.getText().toString())) {
                                                    String message = "Broadcast was created and saved";
                                                    if (isoInterval > 0) {
                                                        message += " with ISO interval: " + isoInterval;
                                                    }
                                                    Toast.makeText(
                                                                    BroadcasterActivity.this,
                                                                    message,
                                                                    Toast.LENGTH_SHORT)
                                                            .show();
                                                } else {
                                                    String message = "Broadcast was created, but not saved (already exists).";
                                                    if (isoInterval > 0) {
                                                        message = "Broadcast was created with ISO interval: " + isoInterval + ", but not saved (already exists).";
                                                    }
                                                    Toast.makeText(
                                                                    BroadcasterActivity.this,
                                                                    message,
                                                                    Toast.LENGTH_SHORT)
                                                            .show();
                                                }
                                            }
                                        });

                        alert.show();
                    } else {
                        Toast.makeText(
                                        BroadcasterActivity.this,
                                        "Maximum number of broadcasts reached: "
                                                + Integer.valueOf(
                                                                mViewModel
                                                                        .getMaximumNumberOfBroadcast())
                                                        .toString(),
                                        Toast.LENGTH_SHORT)
                                .show();
                    }
                });

        RecyclerView recyclerView = findViewById(R.id.broadcaster_recycle_view);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        recyclerView.setHasFixedSize(true);

        final BroadcastItemsAdapter itemsAdapter = new BroadcastItemsAdapter();
        itemsAdapter.setOnItemClickListener(
                broadcastId -> {
                    AlertDialog.Builder alert = new AlertDialog.Builder(this);
                    alert.setTitle("Broadcast Info:");

                    // Load and fill in the metadata layout
                    final View metaLayout =
                            getLayoutInflater().inflate(R.layout.broadcast_metadata, null);
                    alert.setView(metaLayout);

                    BluetoothLeBroadcastMetadata metadata = null;
                    for (BluetoothLeBroadcastMetadata b : mViewModel.getAllBroadcastMetadata()) {
                        if (b.getBroadcastId() == broadcastId) {
                            metadata = b;
                            break;
                        }
                    }

                    if (metadata != null) {
                        TextView addr_text = metaLayout.findViewById(R.id.device_addr_text);
                        addr_text.setText(
                                "Device Address: " + metadata.getSourceDevice().toString());
                        // Store broadcast ID so refreshDialogIfVisible() can re-open the dialog
                        addr_text.setTag(broadcastId);

                        addr_text = metaLayout.findViewById(R.id.adv_sid_text);
                        addr_text.setText("Advertising SID: " + metadata.getSourceAdvertisingSid());

                        addr_text = metaLayout.findViewById(R.id.pasync_interval_text);
                        addr_text.setText("Pa Sync Interval: " + metadata.getPaSyncInterval());

                        addr_text = metaLayout.findViewById(R.id.is_encrypted_text);
                        addr_text.setText(
                                "Is Encrypted: " + (metadata.isEncrypted() ? "Yes" : "No"));

                        boolean isPublic = metadata.isPublicBroadcast();
                        addr_text = metaLayout.findViewById(R.id.is_public_text);
                        addr_text.setText("Is Public Broadcast: " + (isPublic ? "Yes" : "No"));

                        String name = metadata.getBroadcastName();
                        addr_text = metaLayout.findViewById(R.id.broadcast_name_text);
                        if (isPublic && name != null) {
                            addr_text.setText("Public Name: " + name);
                        } else {
                            addr_text.setVisibility(View.INVISIBLE);
                        }

                        BluetoothLeAudioContentMetadata publicMetadata =
                                metadata.getPublicBroadcastMetadata();
                        addr_text = metaLayout.findViewById(R.id.public_program_info_text);
                        if (isPublic && publicMetadata != null) {
                            addr_text.setText("Public Info: " + publicMetadata.getProgramInfo());
                        } else {
                            addr_text.setVisibility(View.INVISIBLE);
                        }

                        byte[] code = metadata.getBroadcastCode();
                        addr_text = metaLayout.findViewById(R.id.broadcast_code_text);
                        if (code != null) {
                            addr_text.setText(
                                    "Broadcast Code: " + new String(code, StandardCharsets.UTF_8));
                        } else {
                            addr_text.setVisibility(View.INVISIBLE);
                        }

                        addr_text = metaLayout.findViewById(R.id.presentation_delay_text);
                        addr_text.setText(
                                "Presentation Delay: "
                                        + metadata.getPresentationDelayMicros()
                                        + " [us]");
                    }

                    alert.setNeutralButton(
                            "Stop",
                            (dialog, which) -> {
                                mViewModel.stopBroadcast(broadcastId);
                            });
                    alert.setPositiveButton(
                            "Modify",
                            (dialog, which) -> {
                                // Open activity for progam info
                                AlertDialog.Builder modifyAlert = new AlertDialog.Builder(this);
                                modifyAlert.setTitle("Modify the Broadcast:");

                                LayoutInflater inflater = getLayoutInflater();
                                View alertView =
                                        inflater.inflate(
                                                R.layout.broadcaster_add_broadcast_dialog, null);
                                EditText program_info_input_text =
                                        alertView.findViewById(R.id.broadcast_program_info_input);
                                EditText broadcast_name_input_text =
                                        alertView.findViewById(R.id.broadcast_name_input);
                                EditText public_content_input_text =
                                        alertView.findViewById(R.id.broadcast_public_content_input);

                                // The Code cannot be changed, so just hide it
                                final EditText code_input_text =
                                        alertView.findViewById(R.id.broadcast_code_input);
                                code_input_text.setVisibility(View.GONE);
                                // Public broadcast flag cannot be changed, so just hide it
                                final CheckBox public_input_checkbox =
                                        alertView.findViewById(R.id.is_public_checkbox);
                                public_input_checkbox.setVisibility(View.GONE);
                                // Context picker cannot be changed, so just hide it
                                final NumberPicker content_input_text =
                                        alertView.findViewById(R.id.context_picker);
                                content_input_text.setVisibility(View.GONE);
                                // Can't load when modify, so just hide buttons
                                final Button loadButton = alertView.findViewById(R.id.load_button);
                                loadButton.setVisibility(View.GONE);
                                final Button clearButton =
                                        alertView.findViewById(R.id.clear_button);
                                clearButton.setVisibility(View.GONE);

                                // Build a container: inflated form + two instant Join Control buttons
                                LinearLayout container = new LinearLayout(this);
                                container.setOrientation(LinearLayout.VERTICAL);
                                container.addView(alertView);

                                int dp8 = (int) (8 * getResources().getDisplayMetrics().density);
                                LinearLayout joinRow = new LinearLayout(this);
                                joinRow.setOrientation(LinearLayout.HORIZONTAL);
                                joinRow.setPadding(dp8, dp8, dp8, dp8);

                                Button btnJoinEnable = new Button(this);
                                btnJoinEnable.setText("Join Enable");
                                LinearLayout.LayoutParams p1 = new LinearLayout.LayoutParams(
                                        0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
                                p1.setMargins(dp8, 0, dp8, 0);
                                btnJoinEnable.setLayoutParams(p1);

                                Button btnJoinDisable = new Button(this);
                                btnJoinDisable.setText("Join Disable");
                                LinearLayout.LayoutParams p2 = new LinearLayout.LayoutParams(
                                        0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f);
                                p2.setMargins(dp8, 0, dp8, 0);
                                btnJoinDisable.setLayoutParams(p2);

                                joinRow.addView(btnJoinEnable);
                                joinRow.addView(btnJoinDisable);
                                container.addView(joinRow);

                                // Wrap the container in a ScrollView to handle landscape orientation
                                ScrollView scrollView = new ScrollView(this);
                                scrollView.addView(container);

                                modifyAlert
                                        .setView(scrollView)
                                        .setNegativeButton(
                                                "Cancel",
                                                (modifyDialog, modifyWhich) -> {
                                                    // Do nothing
                                                })
                                        .setPositiveButton(
                                                "Update",
                                                (modifyDialog, modifyWhich) -> {
                                                    BluetoothLeAudioContentMetadata.Builder
                                                            contentBuilder =
                                                                    new BluetoothLeAudioContentMetadata
                                                                            .Builder();
                                                    String programInfo =
                                                            program_info_input_text
                                                                    .getText()
                                                                    .toString();
                                                    if (!programInfo.isEmpty()) {
                                                        contentBuilder.setProgramInfo(programInfo);
                                                    }

                                                    final BluetoothLeAudioContentMetadata.Builder
                                                            publicContentBuilder =
                                                                    new BluetoothLeAudioContentMetadata
                                                                            .Builder();
                                                    final String publicContent =
                                                            public_content_input_text
                                                                    .getText()
                                                                    .toString();
                                                    if (!publicContent.isEmpty()) {
                                                        publicContentBuilder.setProgramInfo(
                                                                publicContent);
                                                    }

                                                    BluetoothLeBroadcastSubgroupSettings.Builder
                                                            subgroupBuilder =
                                                                    new BluetoothLeBroadcastSubgroupSettings
                                                                                    .Builder()
                                                                            .setContentMetadata(
                                                                                    contentBuilder
                                                                                            .build());

                                                    final String broadcastName =
                                                            broadcast_name_input_text
                                                                    .getText()
                                                                    .toString();
                                                    BluetoothLeBroadcastSettings.Builder builder =
                                                            new BluetoothLeBroadcastSettings
                                                                            .Builder()
                                                                    .setBroadcastName(
                                                                            broadcastName.isEmpty()
                                                                                    ? null
                                                                                    : broadcastName)
                                                                    .setPublicBroadcastMetadata(
                                                                            publicContentBuilder
                                                                                    .build());

                                                    // builder expect at least one subgroup setting
                                                    builder.addSubgroupSettings(
                                                            subgroupBuilder.build());

                                                    if (mViewModel.updateBroadcast(
                                                            broadcastId, builder.build()))
                                                        Toast.makeText(
                                                                        BroadcasterActivity.this,
                                                                        "Broadcast was updated.",
                                                                        Toast.LENGTH_SHORT)
                                                                .show();
                                                });

                                AlertDialog modifyDialog = modifyAlert.show();

                                // Show only the button that represents the action the user can take next.
                                //   Join Control enabled  → only "Join Disable" is visible
                                //   Join Control disabled → only "Join Enable"  is visible
                                if (mJoinControlEnabled) {
                                    btnJoinEnable.setVisibility(View.GONE);
                                    btnJoinDisable.setVisibility(View.VISIBLE);
                                } else {
                                    btnJoinDisable.setVisibility(View.GONE);
                                    btnJoinEnable.setVisibility(View.VISIBLE);
                                }

                                // "Join Enable" – visible only when join control is currently disabled
                                btnJoinEnable.setOnClickListener(v -> {
                                boolean result = mViewModel.setJoinControl(true);
                                    Log.d(TAG, "DBIG Join Enable: result=" + result);
                                    if (result) {
                                        mJoinControlEnabled = true;
                                        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit()
                                                .putBoolean(KEY_JOIN_CONTROL_ENABLED, true).apply();
                                        btnJoinEnable.setVisibility(View.GONE);
                                        btnJoinDisable.setVisibility(View.VISIBLE);
                                    }
                                    Toast.makeText(BroadcasterActivity.this,
                                            result ? "DBIG Join Control: Enabled"
                                                   : "Failed to enable DBIG Join Control",
                                            Toast.LENGTH_SHORT).show();
                                });

                                // "Join Disable" – visible only when join control is currently enabled
                                btnJoinDisable.setOnClickListener(v -> {
                                    boolean result = mViewModel.setJoinControl(false);
                                    Log.d(TAG, "DBIG Join Disable: result=" + result);
                                    if (result) {
                                        mJoinControlEnabled = false;
                                        getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit()
                                                .putBoolean(KEY_JOIN_CONTROL_ENABLED, false).apply();
                                        btnJoinDisable.setVisibility(View.GONE);
                                        btnJoinEnable.setVisibility(View.VISIBLE);
                                    }
                                    Toast.makeText(BroadcasterActivity.this,
                                            result ? "DBIG Join Control: Disabled"
                                                   : "Failed to disable DBIG Join Control",
                                            Toast.LENGTH_SHORT).show();
                                });
                            });

                    // Acquire / Release button – mutually exclusive, uses negative slot
                    if (mLocalOccupyingBis) {
                        // bit1 == 1 → local device occupies a BIS → show Release
                        alert.setNegativeButton("Release", (dialog, which) -> {
                            if (mAudioManager != null) {
                                mAudioManager.setParameters("achat_tx_acquire=false");
                                Toast.makeText(this,
                                        "achat_tx_acquire=false sent to AHAL for broadcast "
                                                + broadcastId,
                                        Toast.LENGTH_SHORT).show();
                            }
                            Log.d(TAG, "Acquire:False");
                            Toast.makeText(this,
                                    "Release BIS for broadcast " + broadcastId,
                                    Toast.LENGTH_SHORT).show();
                            // Optimistically update state: BIS released, wait for DBIG update
                            mLocalOccupyingBis = false;
                            mBisAvailability = BisAvailability.UNAVAILABLE;
                        });
                    } else if (mBisAvailability == BisAvailability.AVAILABLE) {
                        // bit1 == 0 && bit0 == 1 → BIS free → show Acquire
                        alert.setNegativeButton("Acquire", (dialog, which) -> {
                            if (mAudioManager != null) {
                                mAudioManager.setParameters("achat_tx_acquire=true");
                                Toast.makeText(this,
                                        "achat_tx_acquire=true sent to AHAL for broadcast "
                                                + broadcastId,
                                        Toast.LENGTH_SHORT).show();
                            }
                            Log.d(TAG, "Acquire:True");
                            Toast.makeText(this,
                                    "Acquiring BIS for broadcast " + broadcastId,
                                    Toast.LENGTH_SHORT).show();
                            // Optimistically update state: local device now occupies BIS
                            mLocalOccupyingBis = true;
                            mBisAvailability = BisAvailability.UNAVAILABLE;
                        });
                    }

                    mCurrentInfoDialog = alert.show();
                    Log.d("CC", "Num broadcasts: " + mViewModel.getBroadcastCount());
                });
        recyclerView.setAdapter(itemsAdapter);

        // Get the initial state
        mViewModel = ViewModelProviders.of(this).get(BroadcasterViewModel.class);
        final List<BluetoothLeBroadcastMetadata> metadata = mViewModel.getAllBroadcastMetadata();
        itemsAdapter.updateBroadcastsMetadata(metadata.isEmpty() ? new ArrayList<>() : metadata);

        // Put a watch on updates
        mViewModel
                .getBroadcastUpdateMetadataLive()
                .observe(
                        this,
                        audioBroadcast -> {
                            itemsAdapter.updateBroadcastMetadata(audioBroadcast);

                            Toast.makeText(
                                            BroadcasterActivity.this,
                                            "Updated broadcast " + audioBroadcast.getBroadcastId(),
                                            Toast.LENGTH_SHORT)
                                    .show();
                        });

        // Put a watch on any error reports
        mViewModel
                .getBroadcastStatusMutableLive()
                .observe(
                        this,
                        msg -> {
                            Toast.makeText(BroadcasterActivity.this, msg, Toast.LENGTH_SHORT)
                                    .show();
                        });

        // Put a watch on broadcast playback states
        mViewModel
                .getBroadcastPlaybackStartedMutableLive()
                .observe(
                        this,
                        reasonAndBidPair -> {
                            Toast.makeText(
                                            BroadcasterActivity.this,
                                            "Playing broadcast "
                                                    + reasonAndBidPair.second
                                                    + ", reason "
                                                    + reasonAndBidPair.first,
                                            Toast.LENGTH_SHORT)
                                    .show();

                            itemsAdapter.updateBroadcastPlayback(reasonAndBidPair.second, true);
                            int broadcastId = reasonAndBidPair.second;
                            // Automatically enable Join Control once per broadcast session.
                            // Guard against duplicate STREAMING callbacks (duplex fires TX-only
                            // then TX+RX) so join control is sent exactly once.
                            if (!mJoinControlAutoEnabledIds.contains(broadcastId)) {
                                mJoinControlAutoEnabledIds.add(broadcastId);
                                Log.d(TAG, "Broadcast playing - auto-enabling DBIG Join Control");
                                boolean joinResult = mViewModel.setJoinControl(true);
                                Log.d(TAG, "Auto Join Control enable: result=" + joinResult);
                                String playMsg = "Playing broadcast " + broadcastId;
                                if (joinResult) {
                                    mJoinControlEnabled = true;
                                    getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit()
                                            .putBoolean(KEY_JOIN_CONTROL_ENABLED, true).apply();
                                    playMsg += "\nJoin Control auto-enabled";
                                }
                                Toast.makeText(BroadcasterActivity.this, playMsg,
                                        Toast.LENGTH_SHORT).show();
                            } else {
                                Log.d(TAG, "Broadcast playing (duplicate callback for broadcastId="
                                        + broadcastId + ") — skipping join control");
                            }
                            int enhancedCap = mViewModel.getEnhancedBroadcastCap();
                            Log.i(TAG, "getEnhancedBroadcastCap: 0x" + Integer.toHexString(enhancedCap)
                                    + " [Terminate_in_PGO=" + ((enhancedCap & 0x01) != 0 ? "supported" : "not_supported")
                                    + ", Remove_in_PGO=" + ((enhancedCap & 0x02) != 0 ? "supported" : "not_supported") + "]");
                        });

        mViewModel
                .getBroadcastPlaybackStoppedMutableLive()
                .observe(
                        this,
                        reasonAndBidPair -> {
                            int broadcastId = reasonAndBidPair.second;
                            // Clear the deduplication entry so join control is re-enabled
                            // if the same broadcast ID is restarted in a future session.
                            mJoinControlAutoEnabledIds.remove(broadcastId);
                            Toast.makeText(
                                            BroadcasterActivity.this,
                                            "Paused broadcast "
                                                    + broadcastId
                                                    + ", reason "
                                                    + reasonAndBidPair.first,
                                            Toast.LENGTH_SHORT)
                                    .show();

                            itemsAdapter.updateBroadcastPlayback(reasonAndBidPair.second, false);
                        });

        mViewModel
                .getBroadcastAddedMutableLive()
                .observe(
                        this,
                        broadcastId -> {
                            itemsAdapter.addBroadcasts(broadcastId);

                            Toast.makeText(
                                            BroadcasterActivity.this,
                                            "Broadcast was added broadcastId: " + broadcastId,
                                            Toast.LENGTH_SHORT)
                                    .show();
                        });

        // Put a watch on broadcast removal
        mViewModel
                .getBroadcastRemovedMutableLive()
                .observe(
                        this,
                        reasonAndBidPair -> {
                            itemsAdapter.removeBroadcast(reasonAndBidPair.second);

                            Toast.makeText(
                                            BroadcasterActivity.this,
                                            "Broadcast was removed "
                                                    + " broadcastId: "
                                                    + reasonAndBidPair.second
                                                    + ", reason: "
                                                    + reasonAndBidPair.first,
                                            Toast.LENGTH_SHORT)
                                    .show();
                        });

        // Prevent destruction when loses focus
        this.setFinishOnTouchOutside(false);
    }

    @Override
    protected void onStart() {
        super.onStart();
        registerReceiver(
                mDbigStatusReceiver,
                new IntentFilter(ACTION_DBIG_STATUS_CHANGED),
                Context.RECEIVER_EXPORTED);
    }

    @Override
    protected void onStop() {
        unregisterReceiver(mDbigStatusReceiver);
        super.onStop();
    }

    @Override
    public void onBackPressed() {
        Intent intent = new Intent(this, MainActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivity(intent);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        Log.d(TAG, "Configuration changed - orientation: " + newConfig.orientation);

        if (newConfig.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            Log.d(TAG, "Switched to landscape mode");
        } else if (newConfig.orientation == Configuration.ORIENTATION_PORTRAIT) {
            Log.d(TAG, "Switched to portrait mode");
        }

        if (mCurrentInfoDialog != null && mCurrentInfoDialog.isShowing()) {
            Log.d(TAG, "Refreshing dialog due to orientation change");
            refreshDialogIfVisible();
        }
    }

    /**
     * If the broadcast-info dialog is currently visible, update its Acquire/Release
     * button in-place to reflect the latest DBIG status without recreating the dialog.
     */
    private void refreshDialogIfVisible() {
        if (mCurrentInfoDialog == null || !mCurrentInfoDialog.isShowing()) return;

        View deviceAddrView = mCurrentInfoDialog.findViewById(R.id.device_addr_text);
        if (deviceAddrView == null || !(deviceAddrView.getTag() instanceof Integer)) return;

        int broadcastId = (Integer) deviceAddrView.getTag();
        android.widget.Button negativeButton =
                mCurrentInfoDialog.getButton(AlertDialog.BUTTON_NEGATIVE);

        if (mLocalOccupyingBis) {
            if (negativeButton != null) {
                negativeButton.setText("Release");
                negativeButton.setVisibility(View.VISIBLE);
                negativeButton.setOnClickListener(v -> {
                    if (mAudioManager != null) {
                        mAudioManager.setParameters("achat_tx_acquire=false");
                        Toast.makeText(this,
                                "achat_tx_acquire=false sent to AHAL for broadcast " + broadcastId,
                                Toast.LENGTH_SHORT).show();
                    }
                    Log.d(TAG, "Acquire:False");
                    Toast.makeText(this, "Release BIS for broadcast " + broadcastId,
                            Toast.LENGTH_SHORT).show();
                    // Optimistically update state and refresh dialog in-place
                    mLocalOccupyingBis = false;
                    mBisAvailability = BisAvailability.UNAVAILABLE;
                    refreshDialogIfVisible();
                });
            }
        } else if (mBisAvailability == BisAvailability.AVAILABLE) {
            if (negativeButton != null) {
                negativeButton.setText("Acquire");
                negativeButton.setVisibility(View.VISIBLE);
                negativeButton.setOnClickListener(v -> {
                    if (mAudioManager != null) {
                        mAudioManager.setParameters("achat_tx_acquire=true");
                        Toast.makeText(this,
                                "achat_tx_acquire=true sent to AHAL for broadcast " + broadcastId,
                                Toast.LENGTH_SHORT).show();
                    }
                    Log.d(TAG, "Acquire:True");
                    Toast.makeText(this, "Acquiring BIS for broadcast " + broadcastId,
                            Toast.LENGTH_SHORT).show();
                    // Optimistically update state and refresh dialog in-place
                    mLocalOccupyingBis = true;
                    mBisAvailability = BisAvailability.UNAVAILABLE;
                    refreshDialogIfVisible();
                });
            }
        } else {
            if (negativeButton != null) {
                negativeButton.setVisibility(View.GONE);
            }
        }
    }

    private BluetoothLeBroadcastSettings createBroadcastSettingsFromUI(
            String programInfo,
            String publicContent,
            int contextTypeUI,
            boolean isPublic,
            String broadcastName,
            String broadcastCode,
            int preferredQuality) {

        final BluetoothLeAudioContentMetadata.Builder contentBuilder =
                new BluetoothLeAudioContentMetadata.Builder();
        if (!programInfo.isEmpty()) {
            contentBuilder.setProgramInfo(programInfo);
        }

        final BluetoothLeAudioContentMetadata.Builder publicContentBuilder =
                new BluetoothLeAudioContentMetadata.Builder();
        if (!publicContent.isEmpty()) {
            publicContentBuilder.setProgramInfo(publicContent);
        }

        // Extract raw metadata
        byte[] metaBuffer = contentBuilder.build().getRawMetadata();
        ByteArrayOutputStream stream = new ByteArrayOutputStream();
        stream.write(metaBuffer, 0, metaBuffer.length);

        // Extend raw metadata with context type
        final int contextValue = 1 << (contextTypeUI - 1);
        stream.write((byte) 0x03); // Length
        stream.write((byte) 0x02); // Type for the Streaming Audio Context
        stream.write((byte) (contextValue & 0x00FF)); // Value LSB
        stream.write((byte) ((contextValue & 0xFF00) >> 8)); // Value MSB

        BluetoothLeBroadcastSubgroupSettings.Builder subgroupBuilder =
                new BluetoothLeBroadcastSubgroupSettings.Builder()
                        .setContentMetadata(
                                BluetoothLeAudioContentMetadata.fromRawBytes(stream.toByteArray()));
        subgroupBuilder.setPreferredQuality(preferredQuality);
        BluetoothLeBroadcastSettings.Builder builder =
                new BluetoothLeBroadcastSettings.Builder()
                        .setPublicBroadcast(isPublic)
                        .setBroadcastName(broadcastName.isEmpty() ? null : broadcastName)
                        .setBroadcastCode(broadcastCode.isEmpty() ? null : broadcastCode.getBytes(java.nio.charset.StandardCharsets.UTF_8))
                        .setPublicBroadcastMetadata(publicContentBuilder.build());

        // builder expect at least one subgroup setting
        builder.addSubgroupSettings(subgroupBuilder.build());
        return builder.build();
    }

    private boolean saveBroadcastToSharedPref(
            String programInfo,
            String publicContent,
            int contextTypeUI,
            boolean isPublic,
            String broadcastName,
            String broadcastCode) {

        SharedPreferences broadcastsPrefs = getSharedPreferences(BROADCAST_PREFS_KEY, 0);
        if (broadcastsPrefs.contains(broadcastName)) {
            return false;
        } else {
            String toStore =
                    programInfo
                            + PREF_SEP
                            + publicContent
                            + PREF_SEP
                            + contextTypeUI
                            + PREF_SEP
                            + isPublic
                            + PREF_SEP
                            + broadcastName
                            + PREF_SEP;
            if (broadcastCode.isEmpty()) {
                toStore += VALUE_NOT_SET;
            } else {
                toStore += broadcastCode;
            }
            SharedPreferences.Editor editor = broadcastsPrefs.edit();
            editor.putString(broadcastName, toStore);
            editor.commit();
        }
        return true;
    }

    private final void showSelectSavedBroadcastAlert(
            final EditText code_input_text,
            final EditText program_info,
            final NumberPicker contextPicker,
            final EditText broadcast_name,
            final CheckBox publicCheckbox,
            final EditText public_content) {

        ArrayList<String> listSavedBroadcast = new ArrayList();

        final SharedPreferences broadcastsPrefs = getSharedPreferences(BROADCAST_PREFS_KEY, 0);
        Map<String, ?> allEntries = broadcastsPrefs.getAll();
        for (Map.Entry<String, ?> entry : allEntries.entrySet()) {
            listSavedBroadcast.add(entry.getKey());
        }

        AlertDialog.Builder alertDialog = new AlertDialog.Builder(this);
        alertDialog.setTitle("Select saved broadcast");
        alertDialog
                .setSingleChoiceItems(
                        listSavedBroadcast.toArray(new String[listSavedBroadcast.size()]),
                        0,
                        (dialog, which) -> {
                            String[] broadcastValues =
                                    broadcastsPrefs
                                            .getString(listSavedBroadcast.get(which), "")
                                            .split(PREF_SEP);
                            if (broadcastValues.length != 6) {
                                Toast.makeText(
                                                this,
                                                "Could not retrieve "
                                                        + listSavedBroadcast.get(which)
                                                        + ".",
                                                Toast.LENGTH_SHORT)
                                        .show();
                                return;
                            }
                            program_info.setText(broadcastValues[0]);
                            public_content.setText(broadcastValues[1]);
                            contextPicker.setValue(Integer.valueOf(broadcastValues[2]));
                            publicCheckbox.setChecked(Boolean.parseBoolean(broadcastValues[3]));
                            broadcast_name.setText(broadcastValues[4]);
                            if (!VALUE_NOT_SET.equals(broadcastValues[5])) {
                                code_input_text.setText(broadcastValues[5]);
                            }
                            dialog.dismiss();
                        })
                .setNegativeButton("Cancel", (dialog, which) -> {});
        AlertDialog savedBroadcastsAlertDialog = alertDialog.create();
        savedBroadcastsAlertDialog.show();
    }
}
