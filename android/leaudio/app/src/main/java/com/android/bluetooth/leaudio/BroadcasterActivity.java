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
import android.content.res.Configuration;
import android.os.Bundle;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.NumberPicker;
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

    /* --------------------------------------------------------------
     *  BIS connectivity state – global (Option A)
     * -------------------------------------------------------------- */
    /** Tri‑state representation of BIS availability. */
    private enum BisAvailability { UNKNOWN, AVAILABLE, UNAVAILABLE }

    /** Current BIS availability as reported by the system. */
    private BisAvailability mBisAvailability = BisAvailability.UNKNOWN;

    /** True when this device already occupies a BIS (i.e. we are the source). */
    private boolean mLocalOccupyingBis = false;

    private AudioManager mAudioManager;

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

        // Populate the context‑type picker
        contextPicker.setMinValue(1);
        contextPicker.setMaxValue(
                alertView.getResources().getStringArray(R.array.content_types).length - 1);
        contextPicker.setDisplayedValues(
                alertView.getResources().getStringArray(R.array.content_types));

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

                    BluetoothLeBroadcastSettings.Builder settingsBuilder =
                            new BluetoothLeBroadcastSettings.Builder()
                                    .setPublicBroadcast(isPublic)
                                    .setBroadcastName(broadcastName.isEmpty() ? null : broadcastName)
                                    .setBroadcastCode(broadcastCode)
                                    .setPublicBroadcastMetadata(publicContentBuilder.build());

                    settingsBuilder.addSubgroupSettings(subgroupBuilder.build());

                    // ---- Start the broadcast ----
                    if (mViewModel.startBroadcast(settingsBuilder.build())) {
                        // The framework creates a BIS for the source immediately.
                        // Mark it locally so the UI shows “Relinquish”.
                        mLocalOccupyingBis = false;
                        mBisAvailability = BisAvailability.UNKNOWN; // optional helper
                        Toast.makeText(this,
                                "Broadcast was created.", Toast.LENGTH_SHORT).show();
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

        modifyAlert.setView(alertView)
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

        modifyAlert.show();
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
