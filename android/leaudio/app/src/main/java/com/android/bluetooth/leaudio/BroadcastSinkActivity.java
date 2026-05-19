/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.leaudio;

import android.app.AlertDialog;
import android.bluetooth.BluetoothLeBroadcast;
import android.bluetooth.BluetoothLeBroadcastChannel;
import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSubgroup;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioManager;
import android.os.Bundle;
import android.text.TextUtils;
import android.util.Log;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.ViewModelProviders;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.util.ArrayList;
import java.util.List;

public class BroadcastSinkActivity extends AppCompatActivity {
    private static final String TAG = "BroadcastSinkActivity";

    /* ------------------------------------------------------------------
     *  BIS connectivity state (updated via ACTION_DBIG_STATUS_CHANGED)
     * ------------------------------------------------------------------ */
    private enum BisAvailability { UNKNOWN, AVAILABLE, UNAVAILABLE }
    private BisAvailability mBisAvailability = BisAvailability.UNKNOWN;
    private boolean mLocalOccupyingBis = false;

    private AudioManager mAudioManager;

    private final BroadcastReceiver mDbigStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            Log.d(TAG, "Received broadcast action: " + action);
            if (BluetoothLeBroadcast.ACTION_DBIG_STATUS_CHANGED.equals(action)) {
                int status = intent.getIntExtra(BluetoothLeBroadcast.EXTRA_DBIG_STATUS, -1);
                boolean bisAvailable   = (status & 0x0001) != 0;
                boolean localOccupying = (status & 0x0002) != 0;

                mBisAvailability = (bisAvailable && !localOccupying)
                        ? BisAvailability.AVAILABLE
                        : BisAvailability.UNAVAILABLE;
                mLocalOccupyingBis = localOccupying;

                Log.d(TAG, "DBIG status – availability: " + mBisAvailability
                        + ", local occupying: " + mLocalOccupyingBis);

                if ((mBisAvailability == BisAvailability.AVAILABLE) || localOccupying) {
                    Toast.makeText(context, "BIS is available, user can speak now",
                            Toast.LENGTH_SHORT).show();
                } else if (!bisAvailable && !localOccupying) {
                    Toast.makeText(context,
                            "BIS is not available, please wait until BIS is available",
                            Toast.LENGTH_SHORT).show();
                }

                // Update adapter button labels to reflect new state
                if (mAdapter != null) {
                    mAdapter.setLocalOccupyingBis(mLocalOccupyingBis);
                }
            }
        }
    };

    private BroadcastSinkViewModel mViewModel;
    private BroadcastSinkAdapter mAdapter;
    private Button mStartSearchButton;
    private Button mStopSearchButton;
    private TextView mStatusText;
    private TextView mSyncedBroadcastsText;
    private RecyclerView mFoundBroadcastsRecyclerView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.broadcast_sink_activity);

        mAudioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);

        // Register for DBIG status changes
        IntentFilter dbigFilter = new IntentFilter();
        dbigFilter.addAction(BluetoothLeBroadcast.ACTION_DBIG_STATUS_CHANGED);
        registerReceiver(mDbigStatusReceiver, dbigFilter, Context.RECEIVER_EXPORTED);
        Log.d(TAG, "Registered mDbigStatusReceiver");

        initializeViews();
        setupViewModel();
        setupRecyclerView();
        setupObservers();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        unregisterReceiver(mDbigStatusReceiver);
        if (mViewModel != null) {
            mViewModel.cleanup();
        }
    }

    private void initializeViews() {
        mStartSearchButton = findViewById(R.id.start_search_button);
        mStopSearchButton = findViewById(R.id.stop_search_button);
        mStatusText = findViewById(R.id.status_text);
        mSyncedBroadcastsText = findViewById(R.id.synced_broadcasts_text);
        mFoundBroadcastsRecyclerView = findViewById(R.id.found_broadcasts_recycler_view);

        mStartSearchButton.setOnClickListener(v -> {
            Log.d(TAG, "Start search button clicked");
            mViewModel.startSearchingForSources();
        });

        mStopSearchButton.setOnClickListener(v -> {
            Log.d(TAG, "Stop search button clicked");
            mViewModel.stopSearchingForSources();
        });

        Button getAllSyncedStatesButton = findViewById(R.id.get_all_synced_states_button);
        getAllSyncedStatesButton.setOnClickListener(v -> {
            Log.d(TAG, "Get all synced states button clicked");
            mViewModel.getAllSyncedSinkStates();
        });

        // Initially disable stop button
        mStartSearchButton.setEnabled(true);
        mStopSearchButton.setEnabled(false);
    }

    private void setupViewModel() {
        mViewModel = ViewModelProviders.of(this).get(BroadcastSinkViewModel.class);
    }

    private void setupRecyclerView() {
        mAdapter = new BroadcastSinkAdapter(new BroadcastSinkAdapter.OnBroadcastActionListener() {
            @Override
            public void onAddSource(int broadcastId) {
                BroadcastSinkActivity.this.onAddSource(broadcastId);
            }

            @Override
            public void onStartEnhancedBroadcastSink(BroadcastSinkViewModel.FoundBroadcastItem item) {
                BroadcastSinkActivity.this.onStartEnhancedBroadcastSink(item);
            }

            @Override
            public void onBisAcquire(BroadcastSinkViewModel.FoundBroadcastItem item) {
                BroadcastSinkActivity.this.onBisAcquire(item);
            }

            @Override
            public void onStopEnhancedBroadcastSink(int broadcastId) {
                BroadcastSinkActivity.this.onStopEnhancedBroadcastSink(broadcastId);
            }

            @Override
            public void onRemoveSource(int broadcastId) {
                BroadcastSinkActivity.this.onRemoveSource(broadcastId);
            }
        });
        mFoundBroadcastsRecyclerView.setLayoutManager(new LinearLayoutManager(this));
        mFoundBroadcastsRecyclerView.setAdapter(mAdapter);
    }

    private void setupObservers() {
        // Observe search state
        mViewModel.getIsSearching().observe(this, isSearching -> {
            mStartSearchButton.setEnabled(!isSearching);
            mStopSearchButton.setEnabled(isSearching);
            updateStatusText("Search " + (isSearching ? "in progress..." : "stopped"));
        });

        // Observe found broadcasts
        mViewModel.getFoundBroadcasts().observe(this, broadcasts -> {
            Log.d(TAG, "Found broadcasts updated: " + broadcasts.size());
            mAdapter.updateBroadcasts(broadcasts);
        });

        // Observe synced broadcasts
        mViewModel.getSyncedBroadcasts().observe(this, syncedBroadcasts -> {
            updateSyncedBroadcastsText(syncedBroadcasts);
        });

        // Observe status messages
        mViewModel.getStatusMessage().observe(this, message -> {
            if (message != null && !message.isEmpty()) {
                updateStatusText(message);
                Toast.makeText(this, message, Toast.LENGTH_SHORT).show();
            }
        });

        // Observe broadcast sink support
        mViewModel.isBroadcastSinkSupported().observe(this, isSupported -> {
            Log.d(TAG, "isBroadcastSinkSupported: " + isSupported);
            if (!isSupported) {
                updateStatusText("Broadcast Sink not supported on this device");
                mStartSearchButton.setEnabled(false);
                mStopSearchButton.setEnabled(false);
            } else {
                mStartSearchButton.setEnabled(true);
            }
        });
    }

    private void onAddSource(int broadcastId) {
        Log.d(TAG, "Add source (PA sync): broadcastId=" + broadcastId);
        mViewModel.addSource(broadcastId);
        Toast.makeText(this, "Adding source (PA sync) for broadcast ID: " + broadcastId, Toast.LENGTH_SHORT).show();
    }

    private void onStartEnhancedBroadcastSink(BroadcastSinkViewModel.FoundBroadcastItem item) {
        Log.d(TAG, "Join source (BIG sync): " + item.getBroadcastName()
                + ", encrypted: " + item.isEncrypted()
                + ", isEnhanced: " + item.isEnhanced);

        if (item.metadata == null) {
            Toast.makeText(this, "Metadata not available yet", Toast.LENGTH_SHORT).show();
            return;
        }

        if (item.isEnhanced) {
            // Enhanced broadcast source (>= 3 BISes): no channel selection needed.
            // startEnhancedBroadcastSink() syncs to all BISes autonomously.
            if (item.isEncrypted()) {
                showEnhancedBroadcastCodeDialog(item.metadata);
            } else {
                mViewModel.startEnhancedBroadcastSink(item.metadata, null);
                Toast.makeText(this, "Joining enhanced broadcast: " + item.getBroadcastName(),
                        Toast.LENGTH_SHORT).show();
            }
        } else {
            // Standard broadcast source: show channel selection dialog
            showChannelSelectionDialog(item.metadata);
        }
    }

    private void onBisAcquire(BroadcastSinkViewModel.FoundBroadcastItem item) {
        Log.d(TAG, "BIS acquire/release: " + item.getBroadcastName()
                + ", localOccupying=" + mLocalOccupyingBis
                + ", bisAvailability=" + mBisAvailability);

        if (mLocalOccupyingBis) {
            // Local device already occupies a BIS → Release
            if (mAudioManager != null) {
                mAudioManager.setParameters("achat_tx_acquire=false");
                Log.d(TAG, "Acquire:False – sent achat_tx_acquire=false to AHAL");
                Toast.makeText(this,
                        "Release BIS for broadcast " + item.broadcastId,
                        Toast.LENGTH_SHORT).show();
            } else {
                Log.e(TAG, "AudioManager is null, cannot release BIS");
            }
            // Update state and adapter immediately
            mLocalOccupyingBis = false;
            mBisAvailability = BisAvailability.UNKNOWN;
            if (mAdapter != null) mAdapter.setLocalOccupyingBis(false);
        } else if (mBisAvailability == BisAvailability.AVAILABLE) {
            // BIS is free → Acquire
            if (mAudioManager != null) {
                mAudioManager.setParameters("achat_tx_acquire=true");
                Log.d(TAG, "Acquire:True – sent achat_tx_acquire=true to AHAL");
                Toast.makeText(this,
                        "Acquiring BIS for broadcast " + item.broadcastId,
                        Toast.LENGTH_SHORT).show();
            } else {
                Log.e(TAG, "AudioManager is null, cannot acquire BIS");
            }
            // Update state and adapter immediately
            mLocalOccupyingBis = true;
            mBisAvailability = BisAvailability.UNAVAILABLE;
            if (mAdapter != null) mAdapter.setLocalOccupyingBis(true);
        } else {
            // BIS not available yet
            Toast.makeText(this,
                    "BIS not available for broadcast " + item.broadcastId
                            + " (status: " + mBisAvailability + ")",
                    Toast.LENGTH_SHORT).show();
            Log.d(TAG, "BIS not available, ignoring acquire request");
        }
    }

    private void onStopEnhancedBroadcastSink(int broadcastId) {
        Log.d(TAG, "Leave source (stop BIG sync): broadcastId=" + broadcastId);
        mViewModel.stopEnhancedBroadcastSink(broadcastId);
        Toast.makeText(this, "Leaving broadcast ID: " + broadcastId, Toast.LENGTH_SHORT).show();
    }

    private void onRemoveSource(int broadcastId) {
        Log.d(TAG, "Remove source (stop PA sync): broadcastId=" + broadcastId);
        mViewModel.removeSource(broadcastId);
        Toast.makeText(this, "Removing broadcast ID: " + broadcastId, Toast.LENGTH_SHORT).show();
    }

    private void showChannelSelectionDialogForBisAcquire(BluetoothLeBroadcastMetadata metadata) {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View dialogView = LayoutInflater.from(this).inflate(R.layout.channel_selection_dialog, null);
        builder.setView(dialogView);

        AlertDialog dialog = builder.create();

        TextView broadcastNameText = dialogView.findViewById(R.id.broadcast_name_text);
        LinearLayout channelsContainer = dialogView.findViewById(R.id.channels_container);
        Button selectAllButton = dialogView.findViewById(R.id.select_all_button);
        Button clearAllButton = dialogView.findViewById(R.id.clear_all_button);
        Button cancelButton = dialogView.findViewById(R.id.cancel_button);
        Button joinButton = dialogView.findViewById(R.id.join_button);

        // Change button text to "Update" for metadata update
        joinButton.setText("Update");

        broadcastNameText.setText(metadata.getBroadcastName() != null ? metadata.getBroadcastName() : "Unknown Broadcast");

        // Create checkboxes for each channel
        List<CheckBox> channelCheckBoxes = new ArrayList<>();
        List<Integer> channelIndices = new ArrayList<>();

        for (BluetoothLeBroadcastSubgroup subgroup : metadata.getSubgroups()) {
            // Add subgroup header if there are multiple subgroups
            if (metadata.getSubgroups().size() > 1) {
                TextView subgroupHeader = new TextView(this);
                subgroupHeader.setText("Subgroup " + (metadata.getSubgroups().indexOf(subgroup) + 1));
                subgroupHeader.setTextSize(14);
                subgroupHeader.setTypeface(null, android.graphics.Typeface.BOLD);
                subgroupHeader.setPadding(0, 16, 0, 8);
                channelsContainer.addView(subgroupHeader);
            }

            for (BluetoothLeBroadcastChannel channel : subgroup.getChannels()) {
                CheckBox checkBox = new CheckBox(this);
                checkBox.setText("Channel " + channel.getChannelIndex());
                checkBox.setChecked(channel.isSelected()); // Use current selection state
                checkBox.setPadding(16, 8, 16, 8);

                channelCheckBoxes.add(checkBox);
                channelIndices.add(channel.getChannelIndex());
                channelsContainer.addView(checkBox);
            }
        }

        // Select All button
        selectAllButton.setOnClickListener(v -> {
            for (CheckBox checkBox : channelCheckBoxes) {
                checkBox.setChecked(true);
            }
        });

        // Clear All button
        clearAllButton.setOnClickListener(v -> {
            for (CheckBox checkBox : channelCheckBoxes) {
                checkBox.setChecked(false);
            }
        });

        // Cancel button
        cancelButton.setOnClickListener(v -> {
            Log.d(TAG, "Channel selection dialog cancelled");
            dialog.dismiss();
        });

        // Update button
        joinButton.setOnClickListener(v -> {
            // Collect selected channel indices
            List<Integer> selectedChannelIndices = new ArrayList<>();
            for (int i = 0; i < channelCheckBoxes.size(); i++) {
                if (channelCheckBoxes.get(i).isChecked()) {
                    selectedChannelIndices.add(channelIndices.get(i));
                }
            }

            if (selectedChannelIndices.isEmpty()) {
                Toast.makeText(this, "Please select at least one channel", Toast.LENGTH_SHORT).show();
                return;
            }

            Log.d(TAG, "Selected channels for update: " + selectedChannelIndices);
            dialog.dismiss();

            // Channel selection update not supported for enhanced broadcast sink (metadata is stable)
        });

        dialog.show();
    }

    private void showChannelSelectionDialog(BluetoothLeBroadcastMetadata metadata) {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View dialogView = LayoutInflater.from(this).inflate(R.layout.channel_selection_dialog, null);
        builder.setView(dialogView);

        AlertDialog dialog = builder.create();

        TextView broadcastNameText = dialogView.findViewById(R.id.broadcast_name_text);
        LinearLayout channelsContainer = dialogView.findViewById(R.id.channels_container);
        Button selectAllButton = dialogView.findViewById(R.id.select_all_button);
        Button clearAllButton = dialogView.findViewById(R.id.clear_all_button);
        Button cancelButton = dialogView.findViewById(R.id.cancel_button);
        Button joinButton = dialogView.findViewById(R.id.join_button);

        broadcastNameText.setText(metadata.getBroadcastName() != null ? metadata.getBroadcastName() : "Unknown Broadcast");

        // Create checkboxes for each channel
        List<CheckBox> channelCheckBoxes = new ArrayList<>();
        List<Integer> channelIndices = new ArrayList<>();

        for (BluetoothLeBroadcastSubgroup subgroup : metadata.getSubgroups()) {
            // Add subgroup header if there are multiple subgroups
            if (metadata.getSubgroups().size() > 1) {
                TextView subgroupHeader = new TextView(this);
                subgroupHeader.setText("Subgroup " + (metadata.getSubgroups().indexOf(subgroup) + 1));
                subgroupHeader.setTextSize(14);
                subgroupHeader.setTypeface(null, android.graphics.Typeface.BOLD);
                subgroupHeader.setPadding(0, 16, 0, 8);
                channelsContainer.addView(subgroupHeader);
            }

            for (BluetoothLeBroadcastChannel channel : subgroup.getChannels()) {
                CheckBox checkBox = new CheckBox(this);
                checkBox.setText("Channel " + channel.getChannelIndex());
                checkBox.setChecked(true); // Select all by default
                checkBox.setPadding(16, 8, 16, 8);

                channelCheckBoxes.add(checkBox);
                channelIndices.add(channel.getChannelIndex());
                channelsContainer.addView(checkBox);
            }
        }

        // Select All button
        selectAllButton.setOnClickListener(v -> {
            for (CheckBox checkBox : channelCheckBoxes) {
                checkBox.setChecked(true);
            }
        });

        // Clear All button
        clearAllButton.setOnClickListener(v -> {
            for (CheckBox checkBox : channelCheckBoxes) {
                checkBox.setChecked(false);
            }
        });

        // Cancel button
        cancelButton.setOnClickListener(v -> {
            Log.d(TAG, "Channel selection dialog cancelled");
            dialog.dismiss();
        });

        // Join button
        joinButton.setOnClickListener(v -> {
            // Collect selected channel indices
            List<Integer> selectedChannelIndices = new ArrayList<>();
            for (int i = 0; i < channelCheckBoxes.size(); i++) {
                if (channelCheckBoxes.get(i).isChecked()) {
                    selectedChannelIndices.add(channelIndices.get(i));
                }
            }

            if (selectedChannelIndices.isEmpty()) {
                Toast.makeText(this, "Please select at least one channel", Toast.LENGTH_SHORT).show();
                return;
            }

            Log.d(TAG, "Selected channels: " + selectedChannelIndices);
            dialog.dismiss();

            // Check if broadcast is encrypted and show broadcast code dialog if needed
            if (metadata.isEncrypted()) {
                showBroadcastCodeDialog(metadata, selectedChannelIndices);
            } else {
                // Join directly for unencrypted broadcasts
                mViewModel.startEnhancedBroadcastSinkWithChannelSelection(metadata, null, selectedChannelIndices);
            }
        });

        dialog.show();
    }

    /**
     * Shows a broadcast code dialog for encrypted enhanced broadcast sources.
     * No channel selection is needed — startEnhancedBroadcastSink syncs to all BISes.
     */
    private void showEnhancedBroadcastCodeDialog(BluetoothLeBroadcastMetadata metadata) {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View dialogView = LayoutInflater.from(this).inflate(R.layout.broadcast_code_dialog, null);
        builder.setView(dialogView);

        AlertDialog dialog = builder.create();

        EditText codeInput = dialogView.findViewById(R.id.broadcast_code_input);
        Button okButton = dialogView.findViewById(R.id.ok_button);
        Button cancelButton = dialogView.findViewById(R.id.cancel_button);

        okButton.setOnClickListener(v -> {
            String codeStr = codeInput.getText().toString();
            if (TextUtils.isEmpty(codeStr)) {
                Toast.makeText(this, "Please enter a broadcast code", Toast.LENGTH_SHORT).show();
                return;
            }
            if (codeStr.length() < 1 || codeStr.length() > 16) {
                Toast.makeText(this, "Broadcast code must be 1-16 characters", Toast.LENGTH_SHORT).show();
                return;
            }
            byte[] broadcastCode = codeStr.getBytes(java.nio.charset.StandardCharsets.UTF_8);
            Log.d(TAG, "Joining encrypted enhanced broadcast with code length: " + broadcastCode.length);
            mViewModel.startEnhancedBroadcastSink(metadata, broadcastCode);
            dialog.dismiss();
        });

        cancelButton.setOnClickListener(v -> {
            Log.d(TAG, "Enhanced broadcast code dialog cancelled");
            dialog.dismiss();
        });

        dialog.show();
    }

    private void showBroadcastCodeDialog(BluetoothLeBroadcastMetadata metadata, List<Integer> selectedChannelIndices) {
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        View dialogView = LayoutInflater.from(this).inflate(R.layout.broadcast_code_dialog, null);
        builder.setView(dialogView);

        AlertDialog dialog = builder.create();

        EditText codeInput = dialogView.findViewById(R.id.broadcast_code_input);
        Button okButton = dialogView.findViewById(R.id.ok_button);
        Button cancelButton = dialogView.findViewById(R.id.cancel_button);

        okButton.setOnClickListener(v -> {
            String codeStr = codeInput.getText().toString();

            if (TextUtils.isEmpty(codeStr)) {
                Toast.makeText(this, "Please enter a broadcast code", Toast.LENGTH_SHORT).show();
                return;
            }

            // Validate broadcast code length (1-16 characters)
            if (codeStr.length() < 1 || codeStr.length() > 16) {
                Toast.makeText(this, "Broadcast code must be 1-16 characters",
                        Toast.LENGTH_SHORT).show();
                return;
            }

            // Convert ASCII string to byte array (UTF-8 encoding)
            byte[] broadcastCode = codeStr.getBytes(java.nio.charset.StandardCharsets.UTF_8);

            Log.d(TAG, "Joining encrypted broadcast with code: " + codeStr +
                    " (length: " + broadcastCode.length + " bytes) and selected channels: " + selectedChannelIndices);
            mViewModel.startEnhancedBroadcastSinkWithChannelSelection(metadata, broadcastCode, selectedChannelIndices);
            dialog.dismiss();
        });

        cancelButton.setOnClickListener(v -> {
            Log.d(TAG, "Broadcast code dialog cancelled");
            dialog.dismiss();
        });

        dialog.show();
    }

    /**
     * Convert hex string to byte array
     * @param hexString hex string (e.g., "01020304")
     * @return byte array or null if invalid
     */
    private byte[] hexStringToByteArray(String hexString) {
        try {
            // Remove any spaces or separators
            hexString = hexString.replaceAll("[^0-9A-Fa-f]", "");

            int len = hexString.length();
            if (len % 2 != 0) {
                return null; // Invalid hex string
            }

            byte[] data = new byte[len / 2];
            for (int i = 0; i < len; i += 2) {
                data[i / 2] = (byte) ((Character.digit(hexString.charAt(i), 16) << 4)
                        + Character.digit(hexString.charAt(i + 1), 16));
            }
            return data;
        } catch (Exception e) {
            Log.e(TAG, "Error converting hex string to byte array", e);
            return null;
        }
    }


    private void updateStatusText(String status) {
        if (mStatusText != null) {
            mStatusText.setText("Status: " + status);
        }
    }

    private void updateSyncedBroadcastsText(List<BluetoothLeBroadcastMetadata> syncedBroadcasts) {
        if (mSyncedBroadcastsText != null) {
            String text = "Synced Broadcasts: " + syncedBroadcasts.size();
            if (!syncedBroadcasts.isEmpty()) {
                text += "\n";
                for (BluetoothLeBroadcastMetadata metadata : syncedBroadcasts) {
                    text += "- " + metadata.getBroadcastName() + " (ID: " + metadata.getBroadcastId() + ")\n";
                }
            }
            mSyncedBroadcastsText.setText(text);
        }
    }
}
