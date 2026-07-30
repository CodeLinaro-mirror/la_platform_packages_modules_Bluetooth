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
import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSettings;
import android.bluetooth.BluetoothLeBroadcastSubgroupSettings;
import android.bluetooth.BluetoothLeBroadcast;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.NumberPicker;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;
import android.media.AudioManager;


import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.ViewModelProviders;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.floatingactionbutton.FloatingActionButton;
import com.android.bluetooth.leaudio.R;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;

/**
 * Activity that lets the user create, modify, start and stop LE‑Audio broadcasts.
 * It also shows the current DBIG (Broadcast Isochronous Group) status and
 * allows the user to acquire / relinquish a BIS when appropriate.
 *
 * **Option A** – UI shows “Relinquish” when the local device already occupies
 * a BIS (bit 1) and “Acquire” when the device does not own a BIS but a BIS is
 * available (bit 0 = 1, bit 1 = 0).  The decision is made from the global
 * flags {@code mLocalOccupyingBis} and {@code mBisAvailability}.
 */
public class BroadcasterActivity extends AppCompatActivity {

    private BroadcasterViewModel mViewModel;
    private static final String TAG = "BroadcasterActivity";

    /* PHY type constants */
    private static final int PHY_LE_2M = 0;
    private static final int PHY_CODED = 1;

    /* --------------------------------------------------------------
     *  BIS connectivity state – global (Option A)
     * -------------------------------------------------------------- */
    /** Tri‑state representation of BIS availability. */
    private enum BisAvailability { UNKNOWN, AVAILABLE, UNAVAILABLE }

    /** Current BIS availability as reported by the system. */
    private BisAvailability mBisAvailability = BisAvailability.UNKNOWN;

    /** True when this device already occupies a BIS (i.e. we are the source). */
    private boolean mLocalOccupyingBis = false;

    /** Last known broadcast features – used to suppress duplicate DBIG info toasts. */
    private int mLastBroadcastFeatures = -1;
    /** Last known BIS DevID array – used to suppress duplicate DBIG info toasts. */
    private int[] mLastBisDevIds = null;

    private AudioManager mAudioManager;

    /* --------------------------------------------------------------
     *  Join Control state – persisted across app restarts / crashes
     * -------------------------------------------------------------- */
    private static final String PREFS_NAME = "leaudio_prefs";
    private static final String KEY_JOIN_CONTROL_ENABLED = "join_control_enabled";

    /**
     * Tracks whether DBIG Join Control is currently enabled.
     * Default is {@code true} (join control on by default).
     * Persisted in SharedPreferences so the state survives crashes.
     */
    private boolean mJoinControlEnabled = true;

    /**
     * Receiver for {@link BluetoothLeBroadcast#ACTION_DBIG_STATUS_CHANGED}.
     * The broadcast contains a bit‑field in {@link BluetoothLeBroadcast#EXTRA_DBIG_STATUS}
     * where:
     * <ul>
     *   <li>bit 0 (0x0001) – BIS available in DBIG</li>
     *   <li>bit 1 (0x0002) – BIS occupied by the local device</li>
     * </ul>
     */
    private final BroadcastReceiver mDbigStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            Log.d(TAG, "Received broadcast action: " + action);
            if (BluetoothLeBroadcast.ACTION_DBIG_STATUS_CHANGED.equals(action)) {
                int status = intent.getIntExtra(BluetoothLeBroadcast.EXTRA_DBIG_STATUS, -1);
                int[] bisDevIds = intent.getIntArrayExtra(
                        "android.bluetooth.extra.DBIG_BIS_DEV_IDS");
                int broadcastFeatures = intent.getIntExtra(
                        "android.bluetooth.extra.DBIG_BROADCAST_FEATURES", 0);

                int totalBis = (bisDevIds != null) ? bisDevIds.length : 0;
                int occupiedCount = 0;
                int availableCount = 0;
                if (bisDevIds != null) {
                    for (int devIdEntry : bisDevIds) {
                        if (devIdEntry != 0) { // Non-zero means acquired/occupied BIS slot
                            occupiedCount++;
                        } else { // 0 means free BIS slot
                            availableCount++;
                        }
                    }
                }

                Log.i(TAG, "DBIG: total BIS =" + totalBis
                        + ", BIS occupied=" + occupiedCount
                        + ", BIS available=" + availableCount
                        + ", features=0x" + Integer.toHexString(broadcastFeatures)
                        + ", bisDevIds=" + (bisDevIds != null
                            ? java.util.Arrays.toString(bisDevIds) : "null"));

                boolean featuresChanged = (broadcastFeatures != mLastBroadcastFeatures);
                boolean bisDevIdsChanged = !java.util.Arrays.equals(bisDevIds, mLastBisDevIds);
                if (featuresChanged || bisDevIdsChanged) {
                    mLastBroadcastFeatures = broadcastFeatures;
                    mLastBisDevIds = (bisDevIds != null) ? java.util.Arrays.copyOf(bisDevIds, bisDevIds.length) : null;
                    Toast.makeText(context,
                            "DBIG: totalBis=" + totalBis
                                    + ", occupiedBis=" + occupiedCount
                                    + ", availableBis=" + availableCount
                                    + ", features=0x" + Integer.toHexString(broadcastFeatures),
                            Toast.LENGTH_SHORT).show();
                }

                boolean newDeviceAdded = (status & 0x0100) != 0;
                Log.d(TAG, "Device added bit"+ newDeviceAdded);
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
                // bit 9 (0x0200) – device is exiting / removed from DBIG
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
                                    +  devId
                                    + ", Name=" + nameStr,
                            Toast.LENGTH_LONG).show();
                }

                // -------------------------------------------------------------
                // Bit definitions (as defined by the framework)
                //   bit0 (0x0001) – at least one BIS is AVAILABLE
                //   bit1 (0x0002) – a BIS is OCCUPIED by the LOCAL device
                // -------------------------------------------------------------
                boolean bisAvailable   = (status & 0x0001) != 0;
                boolean localOccupying = (status & 0x0002) != 0;

                // Update the model used by the UI
                mBisAvailability = (bisAvailable && !localOccupying)
                                                ? BisAvailability.AVAILABLE
                                                : BisAvailability.UNAVAILABLE;
                mLocalOccupyingBis = localOccupying;

                if((mBisAvailability == BisAvailability.AVAILABLE) ||
                    localOccupying) {
                    Toast.makeText(context, "BIS is available, user can speak now", Toast.LENGTH_SHORT).show();
                } else if (!bisAvailable && !localOccupying) {
                    Toast.makeText(context, "BIS is not available, please wait until BIS is available", Toast.LENGTH_SHORT).show();
                }

                Log.d(TAG, "DBIG status – availability: " + mBisAvailability
                        + ", local occupying: " + mLocalOccupyingBis);

                // If a broadcast‑info dialog is currently on‑screen, rebuild it
                refreshDialogIfVisible();
            }
        }
    };

    /* --------------------------------------------------------------
     *  Activity lifecycle
     * -------------------------------------------------------------- */
    @Override
    protected void onCreate(Bundle savedInstanceState) {
    super.onCreate(savedInstanceState);
    setContentView(R.layout.broadcaster_activity);

    // Initialize AudioManager once
    mAudioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);

    // Restore persisted Join Control state (default: enabled = true)
    mJoinControlEnabled = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
            .getBoolean(KEY_JOIN_CONTROL_ENABLED, true);
    Log.d(TAG, "Restored Join Control state: enabled=" + mJoinControlEnabled);

    /* ---------- Floating‑action button (Add new broadcast) ---------- */
    FloatingActionButton fab = findViewById(R.id.broadcast_fab);
    fab.setOnClickListener(view -> launchAddBroadcastDialog());

    /* ---------- RecyclerView that lists all active broadcasts ---------- */
    RecyclerView recyclerView = findViewById(R.id.broadcaster_recycle_view);
    recyclerView.setLayoutManager(new LinearLayoutManager(this));
    recyclerView.setHasFixedSize(true);

    final BroadcastItemsAdapter itemsAdapter = new BroadcastItemsAdapter();
    itemsAdapter.setOnItemClickListener(broadcastId -> {
        showBroadcastInfoDialog(broadcastId, mAudioManager);
    });

    recyclerView.setAdapter(itemsAdapter);

    /* ---------- ViewModel & LiveData observers -------------------- */
    mViewModel = ViewModelProviders.of(this).get(BroadcasterViewModel.class);
    itemsAdapter.updateBroadcastsMetadata(mViewModel.getAllBroadcastMetadata());

    mViewModel.getBroadcastUpdateMetadataLive().observe(this, audioBroadcast -> {
        itemsAdapter.updateBroadcastMetadata(audioBroadcast);
        Toast.makeText(this,
                "Updated broadcast " + audioBroadcast.getBroadcastId(),
                Toast.LENGTH_SHORT).show();
    });

    mViewModel.getBroadcastStatusMutableLive().observe(this,
            msg -> Toast.makeText(this, msg, Toast.LENGTH_SHORT).show());

    mViewModel.getBroadcastPlaybackStartedMutableLive().observe(this, pair -> {
        Toast.makeText(this,
                "Playing broadcast " + pair.second + ", reason " + pair.first,
                Toast.LENGTH_SHORT).show();
        itemsAdapter.updateBroadcastPlayback(pair.second, true);

        // Log Enhanced Broadcast Source capabilities when broadcast enters playing state
        int enhancedCap = mViewModel.getEnhancedBroadcastCap();
        Log.i(TAG, "getEnhancedBroadcastCap: 0x" + Integer.toHexString(enhancedCap)
                + " [Terminate_in_PGO=" + ((enhancedCap & 0x01) != 0 ? "supported" : "not_supported")
                + ", Remove_in_PGO=" + ((enhancedCap & 0x02) != 0 ? "supported" : "not_supported") + "]");

        // Automatically enable DBIG Join Control when broadcast enters playing state
        Log.d(TAG, "Broadcast playing – auto-enabling Join Control");
        boolean joinResult = mViewModel.setJoinControl(true);
        Log.d(TAG, "Auto Join Control enable: result=" + joinResult);
        if (joinResult) {
            mJoinControlEnabled = true;
            getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit()
                    .putBoolean(KEY_JOIN_CONTROL_ENABLED, true).apply();
            Toast.makeText(this, "Join Control auto-enabled (broadcast playing)",
                    Toast.LENGTH_SHORT).show();
        }
    });

    mViewModel.getBroadcastPlaybackStoppedMutableLive().observe(this, pair -> {
        Toast.makeText(this,
                "Paused broadcast " + pair.second + ", reason " + pair.first,
                Toast.LENGTH_SHORT).show();
        itemsAdapter.updateBroadcastPlayback(pair.second, false);
    });

    /* ---------- Broadcast added / removed (no ownership tracking) ----- */
    mViewModel.getBroadcastAddedMutableLive().observe(this, broadcastId -> {
        itemsAdapter.addBroadcasts(broadcastId);
        Toast.makeText(this,
                "Broadcast added (id=" + broadcastId + ")", Toast.LENGTH_SHORT).show();
    });

    mViewModel.getBroadcastRemovedMutableLive().observe(this, pair -> {
        itemsAdapter.removeBroadcast(pair.second);
        Toast.makeText(this,
                "Broadcast removed (id=" + pair.second + ", reason=" + pair.first + ")",
                Toast.LENGTH_SHORT).show();
    });

    /* ---------- Register DBIG status receiver --------------------- */
    IntentFilter filter = new IntentFilter();
    filter.addAction(BluetoothLeBroadcast.ACTION_DBIG_STATUS_CHANGED);
    registerReceiver(mDbigStatusReceiver, filter, Context.RECEIVER_EXPORTED);
    Log.d(TAG, "Registered mDbigStatusReceiver");

    // Prevent the activity from being dismissed when the user touches outside
    setFinishOnTouchOutside(false);
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
        unregisterReceiver(mDbigStatusReceiver);
    }

    @Override
    public void onConfigurationChanged(Configuration newConfig) {
        super.onConfigurationChanged(newConfig);
        Log.d(TAG, "Configuration changed - orientation: " + newConfig.orientation);

        // Handle orientation-specific changes if needed
        if (newConfig.orientation == Configuration.ORIENTATION_LANDSCAPE) {
            Log.d(TAG, "Switched to landscape mode");
            // Add landscape-specific logic here if needed
            // Example: Adjust RecyclerView span count, modify dialog sizes, etc.
        } else if (newConfig.orientation == Configuration.ORIENTATION_PORTRAIT) {
            Log.d(TAG, "Switched to portrait mode");
            // Add portrait-specific logic here if needed
            // Example: Reset to single column layout, adjust button sizes, etc.
        }

        // Handle AlertDialog rotation - refresh the dialog if it's currently showing
        if (mCurrentInfoDialog != null && mCurrentInfoDialog.isShowing()) {
            Log.d(TAG, "Refreshing dialog due to orientation change");
            refreshDialogIfVisible();
        }
    }

    /* --------------------------------------------------------------
     *  UI helpers
     * -------------------------------------------------------------- */

    /** Shows the dialog used to create a new broadcast. */
    private void launchAddBroadcastDialog() {
        if (mViewModel.getBroadcastCount() >= mViewModel.getMaximumNumberOfBroadcast()) {
            Toast.makeText(this,
                    "Maximum number of broadcasts reached: " +
                            mViewModel.getMaximumNumberOfBroadcast(),
                    Toast.LENGTH_SHORT).show();
            return;
        }

        AlertDialog.Builder alert = new AlertDialog.Builder(this);
        LayoutInflater inflater = getLayoutInflater();
        alert.setTitle("Add the Broadcast:");

        View alertView = inflater.inflate(R.layout.broadcaster_add_broadcast_dialog, null);
        final EditText code_input_text = alertView.findViewById(R.id.broadcast_code_input);
        final EditText program_info = alertView.findViewById(R.id.broadcast_program_info_input);
        final NumberPicker contextPicker = alertView.findViewById(R.id.context_picker);
        final EditText broadcast_name = alertView.findViewById(R.id.broadcast_name_input);
        final CheckBox publicCheckbox = alertView.findViewById(R.id.is_public_checkbox);
        final EditText public_content = alertView.findViewById(R.id.broadcast_public_content_input);
        final Spinner phyTypeSpinner = alertView.findViewById(R.id.phy_type_spinner);
        final EditText iso_interval_input = alertView.findViewById(R.id.iso_interval_input);
        // Populate the context‑type picker
        contextPicker.setMinValue(1);
        contextPicker.setMaxValue(
                alertView.getResources().getStringArray(R.array.content_types).length - 1);
        contextPicker.setDisplayedValues(
                alertView.getResources().getStringArray(R.array.content_types));

        // Populate the PHY type spinner
        ArrayAdapter<String> phyAdapter = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item,
                new String[]{"LE 2M PHY", "Coded PHY"});
        phyAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        phyTypeSpinner.setAdapter(phyAdapter);

        // Add listener to update ISO interval hint based on PHY selection
        phyTypeSpinner.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override
            public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
                if (position == PHY_LE_2M) {
                    iso_interval_input.setHint("7.5, 10, 20, or 30");
                } else {
                    iso_interval_input.setHint("7.5, 15, 25, or 35");
                }
            }

            @Override
            public void onNothingSelected(AdapterView<?> parent) {
                // Default to LE 2M hint
                iso_interval_input.setHint("7.5, 10, 20, or 30");
            }
        });

        alert.setView(alertView)
                .setNegativeButton("Cancel", (dialog, which) -> {/* no‑op */ })
                .setPositiveButton("Start", (dialog, which) -> {
                    // ---- Private content metadata ----
                    BluetoothLeAudioContentMetadata.Builder contentBuilder =
                            new BluetoothLeAudioContentMetadata.Builder();
                    String programInfo = program_info.getText().toString();
                    if (!programInfo.isEmpty()) {
                        contentBuilder.setProgramInfo(programInfo);
                    }

                    // ---- Public content metadata (optional) ----
                    BluetoothLeAudioContentMetadata.Builder publicContentBuilder =
                            new BluetoothLeAudioContentMetadata.Builder();
                    String publicInfo = public_content.getText().toString();
                    if (!publicInfo.isEmpty()) {
                        publicContentBuilder.setProgramInfo(publicInfo);
                    }

                    // ---- Assemble raw metadata (private + context) ----
                    byte[] metaBuffer = contentBuilder.build().getRawMetadata();
                    ByteArrayOutputStream stream = new ByteArrayOutputStream();
                    stream.write(metaBuffer, 0, metaBuffer.length);

                    int contextValue = 1 << (contextPicker.getValue() - 1);
                    stream.write((byte) 0x03);                 // Length
                    stream.write((byte) 0x02);                 // Type = Streaming Audio Context
                    stream.write((byte) (contextValue & 0x00FF));
                    stream.write((byte) ((contextValue & 0xFF00) >> 8));

                    // ---- Sub‑group settings ----
                    BluetoothLeBroadcastSubgroupSettings.Builder subgroupBuilder =
                            new BluetoothLeBroadcastSubgroupSettings.Builder()
                                    .setContentMetadata(
                                            BluetoothLeAudioContentMetadata.fromRawBytes(
                                                    stream.toByteArray()));

                    // ---- Broadcast settings ----
                    boolean isPublic = publicCheckbox.isChecked();
                    String broadcastName = broadcast_name.getText().toString();
                    byte[] broadcastCode = (code_input_text.getText() == null ||
                                            code_input_text.getText().length() == 0)
                            ? null : code_input_text.getText().toString().getBytes();

                    // Get selected PHY type
                    int selectedPhy = phyTypeSpinner.getSelectedItemPosition();

                    // Parse and validate ISO interval based on PHY type
                    float isoInterval = 0.0f; // Default value
                    String isoIntervalStr = iso_interval_input.getText().toString();
                    if (!isoIntervalStr.isEmpty()) {
                        try {
                            isoInterval = Float.parseFloat(isoIntervalStr);

                            // Validate based on PHY type
                            boolean isValidInterval = false;
                            String validIntervalsMsg = "";

                            if (selectedPhy == PHY_LE_2M) {
                                // LE 2M PHY: 7.5, 10, 20, 30
                                isValidInterval = (isoInterval == 7.5f || isoInterval == 10.0f ||
                                                  isoInterval == 20.0f || isoInterval == 30.0f);
                                validIntervalsMsg = "For LE 2M PHY, valid ISO intervals are: 7.5, 10, 20, 30";
                            } else {
                                // CODED PHY: 7.5, 15, 25, 35
                                isValidInterval = (isoInterval == 7.5f || isoInterval == 15.0f ||
                                                  isoInterval == 25.0f || isoInterval == 35.0f);
                                validIntervalsMsg = "For CODED PHY, valid ISO intervals are: 7.5, 15, 25, 35";
                            }

                            if (!isValidInterval) {
                                Toast.makeText(this, validIntervalsMsg, Toast.LENGTH_LONG).show();
                                return; // Don't proceed with broadcast creation
                            }
                        } catch (NumberFormatException e) {
                            Log.w(TAG, "Invalid ISO interval format: " + e.getMessage());
                            String phyName = (selectedPhy == PHY_LE_2M) ? "LE 2M" : "CODED";
                            String validValues = (selectedPhy == PHY_LE_2M) ?
                                "7.5, 10, 20, 30" : "7.5, 15, 25, 35";
                            Toast.makeText(this,
                                "Invalid ISO interval format. For " + phyName +
                                " PHY, valid values are: " + validValues,
                                Toast.LENGTH_LONG).show();
                            return; // Don't proceed with broadcast creation
                        }
                    }

                    BluetoothLeBroadcastSettings.Builder settingsBuilder =
                            new BluetoothLeBroadcastSettings.Builder()
                                    .setPublicBroadcast(isPublic)
                                    .setBroadcastName(broadcastName.isEmpty() ? null : broadcastName)
                                    .setBroadcastCode(broadcastCode)
                                    .setPublicBroadcastMetadata(publicContentBuilder.build());

                    settingsBuilder.addSubgroupSettings(subgroupBuilder.build());

                    // ---- Start the broadcast ----
                    boolean broadcastStarted;
                    if (isoInterval > 0) {
                        // Use the enhanced broadcast API with ISO interval
                        broadcastStarted = mViewModel.startEnhancedBroadcast(settingsBuilder.build(), isoInterval);
                    } else {
                        // Use the standard API without ISO interval
                        broadcastStarted = mViewModel.startBroadcast(settingsBuilder.build());
                    }

                    if (broadcastStarted) {
                        // The framework creates a BIS for the source immediately.
                        // Mark it locally so the UI shows "Relinquish".
                        mLocalOccupyingBis = false;
                        mBisAvailability = BisAvailability.UNKNOWN; // optional helper
                        String message = "Broadcast was created";
                        if (isoInterval > 0) {
                            message += " with ISO interval: " + isoInterval;
                        }
                        Toast.makeText(this, message, Toast.LENGTH_SHORT).show();
                    }
                });

        alert.show();
    }

    /**
     * Shows the detailed information dialog for a specific broadcast.
     * The dialog always contains **Stop** and **Modify** buttons.
     * Depending on the DBIG status it also shows **Acquire** or **Relinquish**.
     *
     * @param broadcastId the id of the broadcast the user tapped
     */
    private void showBroadcastInfoDialog(int broadcastId,AudioManager audioManager) {
        AlertDialog.Builder alert = new AlertDialog.Builder(this);
        alert.setTitle("Broadcast Info:");

        // --------------------------------------------------------------
        // Inflate the layout that displays the metadata
        // --------------------------------------------------------------
        final View metaLayout = getLayoutInflater()
                .inflate(R.layout.broadcast_metadata, null);
        alert.setView(metaLayout);

        // --------------------------------------------------------------
        // Find the BroadcastMetadata object for the requested id
        // --------------------------------------------------------------
        BluetoothLeBroadcastMetadata metadata = null;
        for (BluetoothLeBroadcastMetadata b : mViewModel.getAllBroadcastMetadata()) {
            if (b.getBroadcastId() == broadcastId) {
                metadata = b;
                break;
            }
        }

        // --------------------------------------------------------------
        // Populate the UI with the metadata (if we found it)
        // --------------------------------------------------------------
        if (metadata != null) {
            TextView tv = metaLayout.findViewById(R.id.device_addr_text);
            tv.setText("Device Address: " + metadata.getSourceDevice());

            // Store the broadcast id – used by refreshDialogIfVisible()
            tv.setTag(broadcastId);

            tv = metaLayout.findViewById(R.id.adv_sid_text);
            tv.setText("Advertising SID: " + metadata.getSourceAdvertisingSid());

            tv = metaLayout.findViewById(R.id.pasync_interval_text);
            tv.setText("Pa Sync Interval: " + metadata.getPaSyncInterval());

            tv = metaLayout.findViewById(R.id.is_encrypted_text);
            tv.setText("Is Encrypted: " + (metadata.isEncrypted() ? "Yes" : "No"));

            boolean isPublic = metadata.isPublicBroadcast();
            tv = metaLayout.findViewById(R.id.is_public_text);
            tv.setText("Is Public Broadcast: " + (isPublic ? "Yes" : "No"));

            // Broadcast name (only for public broadcasts)
            tv = metaLayout.findViewById(R.id.broadcast_name_text);
            String name = metadata.getBroadcastName();
            if (isPublic && name != null) {
                tv.setText("Public Name: " + name);
            } else {
                tv.setVisibility(View.INVISIBLE);
            }

            // Public program info (only for public broadcasts)
            tv = metaLayout.findViewById(R.id.public_program_info_text);
            BluetoothLeAudioContentMetadata publicMeta = metadata.getPublicBroadcastMetadata();
            if (isPublic && publicMeta != null) {
                tv.setText("Public Info: " + publicMeta.getProgramInfo());
            } else {
                tv.setVisibility(View.INVISIBLE);
            }

            // Broadcast code (optional)
            tv = metaLayout.findViewById(R.id.broadcast_code_text);
            byte[] code = metadata.getBroadcastCode();
            if (code != null) {
                tv.setText("Broadcast Code: " + new String(code, StandardCharsets.UTF_8));
            } else {
                tv.setVisibility(View.INVISIBLE);
            }

            // Presentation delay
            tv = metaLayout.findViewById(R.id.presentation_delay_text);
            tv.setText("Presentation Delay: " +
                    metadata.getPresentationDelayMicros() + " [us]");
        }

        // --------------------------------------------------------------
        // Bottom buttons (Stop / Modify / Acquire / Relinquish)
        // --------------------------------------------------------------

        // 1 Stop – always present (neutral)
        alert.setNeutralButton("Stop",
                (dialog, which) -> mViewModel.stopBroadcast(broadcastId));

        // 2 Modify – always present (positive)
        alert.setPositiveButton("Modify",
                (dialog, which) -> launchModifyBroadcastDialog(broadcastId));

        // 3 Acquire / Relinquish – mutually exclusive, negative slot
        //    * Relinquish when we already occupy a BIS (bit 1 == 1)
        //    * Acquire   when no local BIS (bit 1 == 0) but a BIS is free (bit 0 == 1)
        if (mLocalOccupyingBis) {
            // bit 1 == 1 → show Relinquish
            alert.setNegativeButton("Release", (dialog, which) -> {

                if(audioManager != null) {
                    audioManager.setParameters("achat_tx_acquire=false");
                    Toast.makeText(this,
                    "achat_tx_acquire sent to AHAL " + broadcastId,
                    Toast.LENGTH_SHORT).show();
                }
                Log.d(TAG, "Acquire:False");
                Toast.makeText(this,
                    "Release BIS for broadcast " + broadcastId,
                    Toast.LENGTH_SHORT).show();
            });
        } else if (mBisAvailability == BisAvailability.AVAILABLE) {
            // bit 1 == 0 && bit 0 == 1 → show Acquire
            alert.setNegativeButton("Acquire", (dialog, which) -> {
                if(audioManager != null) {
                    audioManager.setParameters("achat_tx_acquire=true");
                    Toast.makeText(this,
                    "achat_tx_acquire sent to AHAL " + broadcastId,
                    Toast.LENGTH_SHORT).show();
                }
                Log.d(TAG, "Acquire:True");
                Toast.makeText(this,
                    "Acquiring BIS for broadcast " + broadcastId,
                    Toast.LENGTH_SHORT).show();
            });
        }
        // If neither condition matches (no BIS at all) the negative slot stays empty.

        // --------------------------------------------------------------
        // Show the dialog and store reference for potential refresh
        // --------------------------------------------------------------
        mCurrentInfoDialog = alert.show();

        Log.d(TAG, "Num broadcasts: " + mViewModel.getBroadcastCount());
    }

    /** Shows the “Modify broadcast” dialog (program info, name, public content). */
    private void launchModifyBroadcastDialog(int broadcastId) {
        AlertDialog.Builder modifyAlert = new AlertDialog.Builder(this);
        modifyAlert.setTitle("Modify the Broadcast:");

        LayoutInflater inflater = getLayoutInflater();
        View alertView = inflater.inflate(R.layout.broadcaster_add_broadcast_dialog, null);
        EditText programInfoInput = alertView.findViewById(R.id.broadcast_program_info_input);
        EditText broadcastNameInput = alertView.findViewById(R.id.broadcast_name_input);
        EditText publicContentInput = alertView.findViewById(R.id.broadcast_public_content_input);
        // Hide fields that cannot be changed for an existing broadcast
        alertView.findViewById(R.id.broadcast_code_input).setVisibility(View.GONE);
        alertView.findViewById(R.id.is_public_checkbox).setVisibility(View.GONE);
        alertView.findViewById(R.id.context_picker).setVisibility(View.GONE);
        // Hide PHY and ISO interval (not modifiable on existing broadcast)
        alertView.findViewById(R.id.phy_type_spinner).setVisibility(View.GONE);
        alertView.findViewById(R.id.textView_phy).setVisibility(View.GONE);
        alertView.findViewById(R.id.iso_interval_input).setVisibility(View.GONE);
        alertView.findViewById(R.id.textView23).setVisibility(View.GONE);
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

        modifyAlert.setView(container)
                .setNegativeButton("Cancel", (d, w) -> {/* no‑op */ })
                .setPositiveButton("Update", (d, w) -> {
                    // Build new private content metadata
                    BluetoothLeAudioContentMetadata.Builder contentBuilder =
                            new BluetoothLeAudioContentMetadata.Builder();
                    String progInfo = programInfoInput.getText().toString();
                    if (!progInfo.isEmpty()) {
                        contentBuilder.setProgramInfo(progInfo);
                    }

                    // Build new public content metadata (optional)
                    BluetoothLeAudioContentMetadata.Builder publicBuilder =
                            new BluetoothLeAudioContentMetadata.Builder();
                    String pubInfo = publicContentInput.getText().toString();
                    if (!pubInfo.isEmpty()) {
                        publicBuilder.setProgramInfo(pubInfo);
                    }

                    // Sub‑group (private) settings
                    BluetoothLeBroadcastSubgroupSettings.Builder subgroupBuilder =
                            new BluetoothLeBroadcastSubgroupSettings.Builder()
                                    .setContentMetadata(contentBuilder.build());

                    // Broadcast settings (name + public metadata)
                    String broadcastName = broadcastNameInput.getText().toString();
                    BluetoothLeBroadcastSettings.Builder settingsBuilder =
                            new BluetoothLeBroadcastSettings.Builder()
                                    .setBroadcastName(broadcastName.isEmpty() ? null : broadcastName)
                                    .setPublicBroadcastMetadata(publicBuilder.build());

                    settingsBuilder.addSubgroupSettings(subgroupBuilder.build());

                    if (mViewModel.updateBroadcast(broadcastId, settingsBuilder.build())) {
                        Toast.makeText(this,
                                "Broadcast was updated.", Toast.LENGTH_SHORT).show();
                    }
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
            Toast.makeText(this,
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
            Toast.makeText(this,
                    result ? "DBIG Join Control: Disabled"
                           : "Failed to disable DBIG Join Control",
                    Toast.LENGTH_SHORT).show();
        });
    }

    /** Re‑creates the broadcast‑info dialog if it is currently visible. */
    private void refreshDialogIfVisible() {
        if (mCurrentInfoDialog != null && mCurrentInfoDialog.isShowing()) {
            View deviceAddrView = mCurrentInfoDialog.findViewById(R.id.device_addr_text);
            if (deviceAddrView != null && deviceAddrView.getTag() instanceof Integer) {
                int broadcastId = (Integer) deviceAddrView.getTag();
                mCurrentInfoDialog.dismiss();
                showBroadcastInfoDialog(broadcastId, mAudioManager);
            }
        }
    }


    /** Holds a reference to the last broadcast‑info dialog that we displayed. */
    private AlertDialog mCurrentInfoDialog = null;
}
