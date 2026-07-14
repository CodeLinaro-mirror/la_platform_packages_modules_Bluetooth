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

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothLeAudioContentMetadata;
import android.bluetooth.BluetoothLeBroadcastChannel;
import android.bluetooth.BluetoothLeBroadcastMetadata;
import android.bluetooth.BluetoothLeBroadcastSubgroup;
import android.bluetooth.BluetoothLeBroadcast;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.res.Configuration;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.EditText;
import android.text.TextUtils;
import android.widget.TextView;
import android.widget.Toast;
import android.media.AudioManager;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.ViewModelProviders;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.util.Objects;
import java.util.List;
import android.util.Log;


public class BroadcastScanActivity extends AppCompatActivity {
    /* --------------------------------------------------------------
    *  BIS connectivity state
    * -------------------------------------------------------------- */
   /** Tri‑state representation of BIS availability. */
   private enum BisAvailability { UNKNOWN, AVAILABLE, UNAVAILABLE }

   /** Current BIS availability as reported by the system. */
   private BisAvailability mBisAvailability = BisAvailability.UNKNOWN;

   /** True when this device already occupies a BIS (i.e. we are the source). */
   private boolean mLocalOccupyingBis = false;

    private BluetoothDevice device;
    private BroadcastScanViewModel mViewModel;
    private BroadcastItemsAdapter adapter;
    private BluetoothLeBroadcast mBluetoothLeBroadcast;
    private BluetoothAdapter mBluetoothAdapter;
    private BluetoothProfile.ServiceListener mProfileListener;
    private static final String TAG = "BroadcastScanActivity";
    private AudioManager mAudioManager;

    /** Holds a reference to the last broadcast‑info dialog that we displayed. */
    private AlertDialog mCurrentInfoDialog = null;

    /** Track whether broadcast source has been added */
    private boolean mBroadcastSourceAdded = false;

    /** Track whether broadcast source has been removed */
    private boolean mBroadcastSourceRemoved = false;

    /**
     * True when the DBIG status reported a new device added whose DevID matches
     * the AGP DevID stored in SharedPreferences. The Acquire button is only shown
     * when this flag is true.
     */
    private boolean mAgpDeviceJoined = false;

    private final BroadcastReceiver mBluetoothStateReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            Log.d(TAG, "Bluetooth state receiver action: " + action);
            if (BluetoothAdapter.ACTION_STATE_CHANGED.equals(action)) {
                final int state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR);
                switch (state) {
                    case BluetoothAdapter.STATE_OFF:
                        Log.d(TAG, "Bluetooth turned OFF - stopping scan and clearing broadcasts");
                        if (mViewModel != null) {
                            mViewModel.scanForBroadcasts(device, false);
                            mViewModel.clearBroadcastList();
                        }
                        break;
                    case BluetoothAdapter.STATE_ON:
                        Log.d(TAG, "Bluetooth turned ON - reinitializing");
                        if (mViewModel != null) {
                            mViewModel.reinitializeAfterBluetoothToggle();
                            mViewModel.scanForBroadcasts(device, true);
                            mViewModel.refreshBroadcasts();
                        }
                        break;
                    default:
                        break;
                }
            }
        }
    };

    private BroadcastReceiver mDbigStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            Log.d(TAG, "Received broadcast action: " + action);
            Log.d(TAG,action);
            if (action != null && action.equals(BluetoothLeBroadcast.ACTION_DBIG_STATUS_CHANGED)) {
                // Extract the status from the intent
                int status = intent.getIntExtra(BluetoothLeBroadcast.EXTRA_DBIG_STATUS, -1);
                boolean newDeviceAdded = (status & 0x0100) != 0;
                if (newDeviceAdded) {
                    int devId = intent.getIntExtra(
                            "android.bluetooth.extra.DBIG_DEV_ID", -1);
                    byte[] nameBytes = intent.getByteArrayExtra(
                            "android.bluetooth.extra.DBIG_NAME");
                    String nameStr = (nameBytes != null)
                            ? new String(nameBytes,
                                    java.nio.charset.StandardCharsets.UTF_8).trim()
                            : "";
                    Log.i(TAG, "New device added to DBIG: devId=" + devId + ", name=" + nameStr);
                    Toast.makeText(context,
                            "New device joined DBIG: DevID=" + devId
                                    + ", Name=" + nameStr,
                            Toast.LENGTH_LONG).show();

                    // Check if the joined device's DevID matches our stored AGP DevID
                    int agpDevId = getSharedPreferences("achat_prefs", MODE_PRIVATE)
                            .getInt("agp_dev_id", -1);
                    if (agpDevId != -1 && devId == agpDevId) {
                        Log.i(TAG, "AGP device joined DBIG (devId=" + devId
                                + " matches stored AGP devId=" + agpDevId + ")");
                        mAgpDeviceJoined = true;
                    } else {
                        Log.d(TAG, "Joined devId=" + devId
                                + " does not match AGP devId=" + agpDevId);
                    }
                }
                // bit 9 (0x0200) – device is exiting / removed from DBIG
                boolean deviceRemoved = (status & 0x0200) != 0;
                Log.d(TAG, "Device removed bit"+ newDeviceAdded);
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
                boolean bisAvailable = (status & 0x0001) != 0;
                boolean localOccupying = (status & 0x0002) != 0;
                int bis_is_out_of_range = (status & 0x0004);

                // Update the model used by the UI
                mBisAvailability = (bisAvailable && !localOccupying)
                                                ? BisAvailability.AVAILABLE
                                                : BisAvailability.UNAVAILABLE;
                mLocalOccupyingBis = localOccupying;
                Toast.makeText(context, "DBIG status changed: " + status, Toast.LENGTH_SHORT).show();
                if((mBisAvailability == BisAvailability.AVAILABLE) ||
                    localOccupying) {
                    Toast.makeText(context, "BIS is available, user can speak now", Toast.LENGTH_SHORT).show();
                } else if (!bisAvailable && !localOccupying) {
                    Toast.makeText(context, "BIS is not available, please wait until BIS is available", Toast.LENGTH_SHORT).show();
                }

                if(bis_is_out_of_range == 1) {
                    Toast.makeText(context, "DBIG is out of range", Toast.LENGTH_SHORT).show();
                } else {
                    Toast.makeText(context, "DBIG is in range", Toast.LENGTH_SHORT).show();
                }
                // If a broadcast‑info dialog is currently on‑screen, rebuild it
                refreshDialogIfVisible();
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.broadcast_scan_activity);
        mAudioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter();

        // Register for DBIG status changes
        IntentFilter filter = new IntentFilter(BluetoothLeBroadcast.ACTION_DBIG_STATUS_CHANGED);
        registerReceiver(mDbigStatusReceiver, filter, Context.RECEIVER_EXPORTED);
        Log.d(TAG, "Registered mDbigStatusReceiver");

        // Register for Bluetooth adapter state changes
        IntentFilter btStateFilter = new IntentFilter(BluetoothAdapter.ACTION_STATE_CHANGED);
        registerReceiver(mBluetoothStateReceiver, btStateFilter, Context.RECEIVER_EXPORTED);
        Log.d(TAG, "Registered mBluetoothStateReceiver");

        RecyclerView recyclerView = findViewById(R.id.broadcast_recycler_view);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        recyclerView.setHasFixedSize(true);

        adapter = new BroadcastItemsAdapter();
        adapter.setOnItemClickListener(broadcastId -> {
            mViewModel.scanForBroadcasts(device, false);

            BluetoothLeBroadcastMetadata broadcast = null;
            for (BluetoothLeBroadcastMetadata b : mViewModel.getAllBroadcasts().getValue()) {
                if (Objects.equals(b.getBroadcastId(), broadcastId)) {
                    broadcast = b;
                    break;
                }
            }

            if (broadcast == null) {
                Toast.makeText(this, "Matching broadcast not found. broadcastId=" + broadcastId, Toast.LENGTH_SHORT).show();
                return;
            }

            // Show the dialog with the broadcast metadata
            showBroadcastInfoDialog(broadcast, mAudioManager);
        });
        recyclerView.setAdapter(adapter);
        mViewModel = ViewModelProviders.of(this).get(BroadcastScanViewModel.class);
        mViewModel.getAllBroadcasts().observe(this, audioBroadcasts -> {
            // Update Broadcast list in the adapter
            adapter.setBroadcasts(audioBroadcasts);
        });
        Intent intent = getIntent();
        device = BluetoothAdapter.getDefaultAdapter().getRemoteDevice("FA:CE:FA:CE:FA:CE");
    }

    /**
     * Shows the detailed information dialog for a specific broadcast.
     * The dialog always contains "Remove" button and depending on the DBIG status
     * it also shows "Acquire" or "Release".
     *
     * @param broadcast the broadcast metadata to display
     */
    private void showBroadcastInfoDialog(BluetoothLeBroadcastMetadata broadcast,AudioManager audioManager) {
        AlertDialog.Builder alert = new AlertDialog.Builder(this);
        LayoutInflater inflater = getLayoutInflater();

        alert.setTitle("Add/remove the Broadcast:");

        View alertView = inflater.inflate(R.layout.broadcast_scan_add_encrypted_source_dialog, null);

        boolean isPublic = broadcast.isPublicBroadcast();
        TextView addr_text = alertView.findViewById(R.id.broadcast_with_pbp_text);
        addr_text.setText("Broadcast with PBP: " + (isPublic ? "Yes" : "No"));

        String name = broadcast.getBroadcastName();
        addr_text = alertView.findViewById(R.id.broadcast_name_text);
        if (isPublic && name != null) {
            addr_text.setText("Public Name: " + name);
        } else {
            addr_text.setVisibility(View.INVISIBLE);
        }

        BluetoothLeAudioContentMetadata publicMetadata = broadcast.getPublicBroadcastMetadata();
        addr_text = alertView.findViewById(R.id.public_program_info_text);
        if (isPublic && publicMetadata != null) {
            addr_text.setText("Public Info: " + publicMetadata.getProgramInfo());
        } else {
            addr_text.setVisibility(View.INVISIBLE);
        }

        final EditText channels_input_text = alertView.findViewById(R.id.broadcast_channel_map);
        final EditText code_input_text = alertView.findViewById(R.id.broadcast_code_input);
        BluetoothLeBroadcastMetadata.Builder builder = new BluetoothLeBroadcastMetadata.Builder(broadcast);

        // Only show Remove button if broadcast source has been added and not removed yet
        if (mBroadcastSourceAdded && !mBroadcastSourceRemoved) {
            alert.setView(alertView).setNeutralButton("Remove", (dialog, which) -> {
                Toast.makeText(this, "Removing broadcast source", Toast.LENGTH_SHORT).show();
                mViewModel.removeBroadcastSource(device, 0);

                // Mark broadcast source as removed
                mBroadcastSourceRemoved = true;
                mBroadcastSourceAdded = false; // Reset add state

                Intent intent = new Intent(this, MainActivity.class);
                intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
                startActivity(intent);
                finish();
            });
        } else {
            alert.setView(alertView);
        }

        // Acquire / Relinquish - mutually exclusive, negative slot
        if (mLocalOccupyingBis) {
            // bit 1 == 1 → show Relinquish
            alert.setNegativeButton("Release", (dialog, which) -> {
                try {
                    audioManager.setParameters("achat_tx_acquire=false");
                    Toast.makeText(this,
                            "Command sent to audio system for broadcast " + broadcast.getBroadcastId(),
                            Toast.LENGTH_SHORT).show();
                } catch (RuntimeException e) {
                    Log.e(TAG, "Failed to set audio parameters", e);
                    Toast.makeText(this,
                            "Error: Could not send command to audio system",
                            Toast.LENGTH_SHORT).show();
                }
                Log.d(TAG, "Acquire:False");
                Toast.makeText(this,
                        "Release BIS for broadcast " + broadcast.getBroadcastId(),
                        Toast.LENGTH_SHORT).show();
            });
        } else if (mBisAvailability == BisAvailability.AVAILABLE && mAgpDeviceJoined) {
            // bit 1 == 0 && bit 0 == 1, AND our AGP device has joined → show Acquire
            alert.setNegativeButton("Acquire", (dialog, which) -> {
                try {
                    audioManager.setParameters("achat_tx_acquire=true");
                    Toast.makeText(this,
                            "Command sent to audio system for broadcast " + broadcast.getBroadcastId(),
                            Toast.LENGTH_SHORT).show();
                } catch (RuntimeException e) {
                    Log.e(TAG, "Failed to set audio parameters", e);
                    Toast.makeText(this,
                            "Error: Could not send command to audio system",
                            Toast.LENGTH_SHORT).show();
                }
                Log.d(TAG, "Acquire:True");
                Toast.makeText(this,
                        "Acquiring BIS for broadcast " + broadcast.getBroadcastId(),
                        Toast.LENGTH_SHORT).show();
            });
        }

        // Only show Add button if broadcast source hasn't been added yet
        if (!mBroadcastSourceAdded) {
            alert.setPositiveButton("Add", (dialog, which) -> {
                // Existing "Add" button logic remains the same
                BluetoothLeBroadcastMetadata metadata;
                if (code_input_text.getText() == null) {
                    Toast.makeText(this, "Invalid broadcast code", Toast.LENGTH_SHORT).show();
                    return;
                }
                if (code_input_text.getText().length() == 0) {
                    Toast.makeText(this, "Adding not encrypted broadcast source broadcastId=" + broadcast.getBroadcastId(), Toast.LENGTH_SHORT).show();
                    metadata = builder.setEncrypted(false).build();
                } else {
                    if ((code_input_text.getText().length() > 16) || (code_input_text.getText().length() < 4)) {
                        Toast.makeText(this, "Invalid Broadcast code length", Toast.LENGTH_SHORT).show();
                        return;
                    }

                    metadata = builder.setBroadcastCode(code_input_text.getText().toString().getBytes())
                            .setEncrypted(true)
                            .build();
                }

                if ((channels_input_text.getText() != null) && (channels_input_text.getText().length() != 0)) {
                    int channelMap = Integer.parseInt(channels_input_text.getText().toString());
                    for (BluetoothLeBroadcastSubgroup subGroup : metadata.getSubgroups()) {
                        List<BluetoothLeBroadcastChannel> channels = subGroup.getChannels();
                        for (int i = 0; i < channels.size(); i++) {
                            BluetoothLeBroadcastChannel channel = channels.get(i);
                            if (channel.getChannelIndex() != 0) {
                                if ((channelMap & (1 << (channel.getChannelIndex() - 1))) != 0) {
                                    BluetoothLeBroadcastChannel.Builder bob = new BluetoothLeBroadcastChannel.Builder(channel);
                                    bob.setSelected(true);
                                    channels.set(i, bob.build());
                                }
                            }
                        }
                    }
                }

                Toast.makeText(this, "Adding broadcast source broadcastId=" + broadcast.getBroadcastId(), Toast.LENGTH_SHORT).show();
                mViewModel.addBroadcastSource(device, metadata);

                // Mark broadcast source as added
                mBroadcastSourceAdded = true;
                mBroadcastSourceRemoved = false; // Reset remove state

                Log.d(TAG, "Broadcast source add requested, waiting for DBIG status update");
            });
        }

        // Store the broadcast ID as a tag for potential refresh - using the same approach as BroadcasterActivity
        TextView addr_text_for_tag = alertView.findViewById(R.id.broadcast_with_pbp_text);
        addr_text_for_tag.setTag(broadcast.getBroadcastId());

        // Show the dialog and store reference for potential refresh
        mCurrentInfoDialog = alert.show();
    }

    @Override
    protected void onPause() {
        super.onPause();
        mViewModel.scanForBroadcasts(device, false);
    }
    @Override
    protected void onResume() {
        super.onResume();

        // Check if Bluetooth is enabled before attempting to scan
        if (mBluetoothAdapter != null && mBluetoothAdapter.isEnabled()) {
            Log.d(TAG, "onResume: Bluetooth is enabled, starting scan");
            Log.d(TAG, "onResume: Scan delegator device: " + device);

            if (mViewModel.getAllBroadcasts().getValue() != null) {
                Log.d(TAG, "onResume: Current broadcasts count: " + mViewModel.getAllBroadcasts().getValue().size());
                adapter.setBroadcasts(mViewModel.getAllBroadcasts().getValue());
            } else {
                Log.d(TAG, "onResume: No broadcasts in ViewModel yet");
            }

            mViewModel.scanForBroadcasts(device, true);
            mViewModel.refreshBroadcasts();

            Toast.makeText(this, "Scanning for broadcasts... Please wait.", Toast.LENGTH_SHORT).show();
        } else {
            Log.w(TAG, "onResume: Bluetooth is not enabled, cannot start scan");
            Toast.makeText(this, "Bluetooth is not enabled. Please enable Bluetooth to scan for broadcasts.",
                          Toast.LENGTH_LONG).show();
            // Return to MainActivity
            Intent intent = new Intent(this, MainActivity.class);
            intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
            startActivity(intent);
            finish();
        }
    }
    @Override
    public void onBackPressed() {
        Intent intent = new Intent(this, MainActivity.class);
        intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
        startActivity(intent);
        finish();
    }
    @Override
    protected void onDestroy() {
        super.onDestroy();
        unregisterReceiver(mDbigStatusReceiver);
        unregisterReceiver(mBluetoothStateReceiver);
        if (mBluetoothAdapter != null && mProfileListener != null) {
            mBluetoothAdapter.closeProfileProxy(BluetoothProfile.LE_AUDIO_BROADCAST, mBluetoothLeBroadcast);
        }
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

    /** Updates the dialog buttons in-place without recreating the dialog to avoid lag. */
    private void refreshDialogIfVisible() {
        if (mCurrentInfoDialog != null && mCurrentInfoDialog.isShowing()) {
            View broadcastView = mCurrentInfoDialog.findViewById(R.id.broadcast_with_pbp_text);
            if (broadcastView != null && broadcastView.getTag() instanceof Integer) {
                int broadcastId = (Integer) broadcastView.getTag();

                // Find the broadcast again using the stored ID
                BluetoothLeBroadcastMetadata broadcast = null;
                if (mViewModel != null && mViewModel.getAllBroadcasts().getValue() != null) {
                    for (BluetoothLeBroadcastMetadata b : mViewModel.getAllBroadcasts().getValue()) {
                        if (Objects.equals(b.getBroadcastId(), broadcastId)) {
                            broadcast = b;
                            break;
                        }
                    }
                }

                if (broadcast != null) {
                    // Update buttons in-place instead of recreating dialog
                    updateDialogButtons(broadcast, broadcastId);
                }
            }
        }
    }

    /** Updates the dialog buttons without recreating the entire dialog. */
    private void updateDialogButtons(BluetoothLeBroadcastMetadata broadcast, int broadcastId) {
        // Get the current dialog's buttons
        android.widget.Button negativeButton = mCurrentInfoDialog.getButton(AlertDialog.BUTTON_NEGATIVE);
        android.widget.Button positiveButton = mCurrentInfoDialog.getButton(AlertDialog.BUTTON_POSITIVE);
        android.widget.Button neutralButton = mCurrentInfoDialog.getButton(AlertDialog.BUTTON_NEUTRAL);

        // Update Add button visibility
        if (positiveButton != null) {
            if (mBroadcastSourceAdded) {
                positiveButton.setVisibility(View.GONE);
            } else {
                positiveButton.setVisibility(View.VISIBLE);
            }
        }

        // Update Remove button visibility - only show if broadcast source has been added and not removed
        if (neutralButton != null) {
            if (mBroadcastSourceAdded && !mBroadcastSourceRemoved) {
                neutralButton.setVisibility(View.VISIBLE);
            } else {
                neutralButton.setVisibility(View.GONE);
            }
        }

        // Update Acquire/Release button
        if (mLocalOccupyingBis) {
            // Show Release button
            if (negativeButton != null) {
                negativeButton.setText("Release");
                negativeButton.setVisibility(View.VISIBLE);
                negativeButton.setOnClickListener(v -> {
                    try {
                        mAudioManager.setParameters("achat_tx_acquire=false");
                        Toast.makeText(this,
                                "Command sent to audio system for broadcast " + broadcastId,
                                Toast.LENGTH_SHORT).show();
                    } catch (RuntimeException e) {
                        Log.e(TAG, "Failed to set audio parameters", e);
                        Toast.makeText(this,
                                "Error: Could not send command to audio system",
                                Toast.LENGTH_SHORT).show();
                    }
                    Log.d(TAG, "Acquire:False");
                    Toast.makeText(this,
                            "Release BIS for broadcast " + broadcastId,
                            Toast.LENGTH_SHORT).show();
                });
            }
        } else if (mBisAvailability == BisAvailability.AVAILABLE && mAgpDeviceJoined) {
            // Show Acquire button only when our AGP device has joined
            if (negativeButton != null) {
                negativeButton.setText("Acquire");
                negativeButton.setVisibility(View.VISIBLE);
                negativeButton.setOnClickListener(v -> {
                    try {
                        mAudioManager.setParameters("achat_tx_acquire=true");
                        Toast.makeText(this,
                                "Command sent to audio system for broadcast " + broadcastId,
                                Toast.LENGTH_SHORT).show();
                    } catch (RuntimeException e) {
                        Log.e(TAG, "Failed to set audio parameters", e);
                        Toast.makeText(this,
                                "Error: Could not send command to audio system",
                                Toast.LENGTH_SHORT).show();
                    }
                    Log.d(TAG, "Acquire:True");
                    Toast.makeText(this,
                            "Acquiring BIS for broadcast " + broadcastId,
                            Toast.LENGTH_SHORT).show();
                });
            }
        } else {
            // Hide the button if neither condition is met
            if (negativeButton != null) {
                negativeButton.setVisibility(View.GONE);
            }
        }
    }
}
