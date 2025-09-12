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
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.EditText;
import android.text.TextUtils;
import android.view.LayoutInflater;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AlertDialog;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.ViewModelProviders;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.util.Objects;
import java.util.List;
import android.util.Log;


public class BroadcastScanActivity extends AppCompatActivity {
    private BluetoothDevice device;
    private BroadcastScanViewModel mViewModel;
    private BroadcastItemsAdapter adapter;
    private BluetoothLeBroadcast mBluetoothLeBroadcast;
    private BluetoothAdapter mBluetoothAdapter;
    private BluetoothProfile.ServiceListener mProfileListener;
    private static final String TAG = "BroadcastScanActivity";


    private BroadcastReceiver mDbigStatusReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            // Declare and initialize the action variable
            String action = intent.getAction();
            Log.d(TAG, "Received broadcast action: " + action);
            Log.d(TAG,action);
            if (action != null && action.equals(BluetoothLeBroadcast.ACTION_DBIG_STATUS_CHANGED)) {
                // Extract the status from the intent
                int status = intent.getIntExtra(BluetoothLeBroadcast.EXTRA_DBIG_STATUS, -1);
                boolean bit0Set = ((status & 0x0001) != 0);
                int bis_available_in_dbig = bit0Set ? 1 : 0;
                boolean bit1Set = ((status & 0x0002) != 0);
                int local_occupying_bis = bit1Set ? 1 : 0;
                boolean bit2set = ((status & 0x0004) != 0);
                int bis_is_out_of_range = bit2set ? 1 : 0;
                Toast.makeText(context, "DBIG status changed: " + status, Toast.LENGTH_SHORT).show();
                if((bis_available_in_dbig == 1 && local_occupying_bis == 0) || local_occupying_bis == 1) {
                  Toast.makeText(context, "BIS is available, user can speak now", Toast.LENGTH_SHORT).show();
                } else if (bis_available_in_dbig == 0 && local_occupying_bis == 0) {
                  Toast.makeText(context, "BIS is not available, please wait until BIS is available", Toast.LENGTH_SHORT).show();
                }
                if(bis_is_out_of_range == 1) {
                    Toast.makeText(context, "DBIG is out of range", Toast.LENGTH_SHORT).show();
                }
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.broadcast_scan_activity);

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
                Toast.makeText(recyclerView.getContext(), "Matching broadcast not found."
                                + " broadcastId=" + broadcastId, Toast.LENGTH_SHORT).show();
                return;
            }

            // Set broadcast source on peer only if scan delegator device context is available
            if (true) {
                // Start Dialog with the broadcast input details
                AlertDialog.Builder alert = new AlertDialog.Builder(this);
                LayoutInflater inflater = getLayoutInflater();

                alert.setTitle("Add/remove the Broadcast:");

                View alertView =
                        inflater.inflate(R.layout.broadcast_scan_add_encrypted_source_dialog,
                                         null);

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

                BluetoothLeAudioContentMetadata publicMetadata =
                        broadcast.getPublicBroadcastMetadata();
                addr_text = alertView.findViewById(R.id.public_program_info_text);
                if (isPublic && publicMetadata != null) {
                    addr_text.setText("Public Info: " + publicMetadata.getProgramInfo());
                } else {
                    addr_text.setVisibility(View.INVISIBLE);
                }

                final EditText channels_input_text =
                        alertView.findViewById(R.id.broadcast_channel_map);

                final EditText code_input_text =
                        alertView.findViewById(R.id.broadcast_code_input);
                BluetoothLeBroadcastMetadata.Builder builder = new
                        BluetoothLeBroadcastMetadata.Builder(broadcast);

                alert.setView(alertView).setNegativeButton("Remove", (dialog, which) -> {
                    Toast.makeText(recyclerView.getContext(), "Removing broadcast source",
                                     Toast.LENGTH_SHORT).show();
                    mViewModel.removeBroadcastSource(device, 0);

                    Intent intent = new Intent(this, MainActivity.class);
                    intent.addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
                    startActivity(intent);
                    finish();
                });

               alert.setView(alertView).setPositiveButton("Add", (dialog, which) -> {

                    BluetoothLeBroadcastMetadata metadata;
                    if (code_input_text.getText() == null) {
                        Toast.makeText(recyclerView.getContext(), "Invalid broadcast code",
                                Toast.LENGTH_SHORT).show();
                        return;
                    }
                    if (code_input_text.getText().length() == 0) {
                        Toast.makeText(recyclerView.getContext(), "Adding not encrypted broadcast "
                                       + "source broadcastId="
                                       + broadcastId, Toast.LENGTH_SHORT).show();
                        metadata = builder.setEncrypted(false).build();
                    } else {
                        if ((code_input_text.getText().length() > 16) ||
                                (code_input_text.getText().length() < 4)) {
                            Toast.makeText(recyclerView.getContext(),
                                           "Invalid Broadcast code length",
                                           Toast.LENGTH_SHORT).show();

                            return;
                        }

                        metadata = builder.setBroadcastCode(
                                        code_input_text.getText().toString().getBytes())
                               .setEncrypted(true)
                               .build();
                    }

                    if ((channels_input_text.getText() != null)
                            && (channels_input_text.getText().length() != 0)) {
                        int channelMap = Integer.parseInt(channels_input_text.getText().toString());
                        // Apply a single channel map preference to all subgroups
                        for (BluetoothLeBroadcastSubgroup subGroup : metadata.getSubgroups()) {
                            List<BluetoothLeBroadcastChannel> channels = subGroup.getChannels();
                            for (int i = 0; i < channels.size(); i++) {
                                BluetoothLeBroadcastChannel channel = channels.get(i);
                                // Set the channel preference value according to the map
                                if (channel.getChannelIndex() != 0) {
                                    if ((channelMap & (1 << (channel.getChannelIndex() - 1))) != 0) {
                                        BluetoothLeBroadcastChannel.Builder bob
                                                = new BluetoothLeBroadcastChannel.Builder(channel);
                                        bob.setSelected(true);
                                        channels.set(i, bob.build());
                                    }
                                }
                            }
                        }
                    }

                    Toast.makeText(recyclerView.getContext(), "Adding broadcast source"
                                    + " broadcastId=" + broadcastId, Toast.LENGTH_SHORT).show();
                    mViewModel.addBroadcastSource(device, metadata);
                });
               mProfileListener = new BluetoothProfile.ServiceListener() {
                    @Override
                    public void onServiceConnected(int profile, BluetoothProfile proxy) {
                        if (profile == BluetoothProfile.LE_AUDIO_BROADCAST) {
                            mBluetoothLeBroadcast = (BluetoothLeBroadcast) proxy;
                        }
                    }

                    @Override
                    public void onServiceDisconnected(int profile) {
                        if (profile == BluetoothProfile.LE_AUDIO_BROADCAST) {
                            mBluetoothLeBroadcast = null;
                        }
                    }
                };

                mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
                if (mBluetoothAdapter == null) {
                    Toast.makeText(this, "Bluetooth is not available", Toast.LENGTH_LONG).show();
                    finish();
                    return;
                }

                // Correct the method call to getProfileProxy
                mBluetoothAdapter.getProfileProxy(this, mProfileListener, BluetoothProfile.LE_AUDIO_BROADCAST);

                IntentFilter filter = new IntentFilter();
                filter.addAction(BluetoothLeBroadcast.ACTION_DBIG_STATUS_CHANGED);
                registerReceiver(mDbigStatusReceiver, filter, Context.RECEIVER_EXPORTED);
                Log.d(TAG, "venk");
                alert.show();
            }
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

    @Override
    protected void onPause() {
        super.onPause();

        mViewModel.scanForBroadcasts(device, false);
    }

    @Override
    protected void onResume() {
        super.onResume();

        if (mViewModel.getAllBroadcasts().getValue() != null)
            adapter.setBroadcasts(mViewModel.getAllBroadcasts().getValue());

        mViewModel.scanForBroadcasts(device, true);
        mViewModel.refreshBroadcasts();
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
        if (mBluetoothAdapter != null && mProfileListener != null) {
            mBluetoothAdapter.closeProfileProxy(BluetoothProfile.LE_AUDIO_BROADCAST, mBluetoothLeBroadcast);
        }
    }
}
