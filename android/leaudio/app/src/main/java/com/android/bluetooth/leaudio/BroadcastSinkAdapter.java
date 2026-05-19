/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.leaudio;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.util.ArrayList;
import java.util.List;

public class BroadcastSinkAdapter extends RecyclerView.Adapter<BroadcastSinkAdapter.BroadcastViewHolder> {

    public interface OnBroadcastActionListener {
        void onAddSource(int broadcastId);
        void onStartEnhancedBroadcastSink(BroadcastSinkViewModel.FoundBroadcastItem item);
        void onBisAcquire(BroadcastSinkViewModel.FoundBroadcastItem item);
        void onStopEnhancedBroadcastSink(int broadcastId);
        void onRemoveSource(int broadcastId);
    }

    private List<BroadcastSinkViewModel.FoundBroadcastItem> mBroadcasts = new ArrayList<>();
    private final OnBroadcastActionListener mActionListener;
    /** Reflects the current BIS occupancy state from DBIG status. */
    private boolean mLocalOccupyingBis = false;

    public BroadcastSinkAdapter(OnBroadcastActionListener actionListener) {
        mActionListener = actionListener;
    }

    /**
     * Called by the activity when DBIG status changes so the button label
     * switches between "Acquire" and "Release" without recreating items.
     */
    public void setLocalOccupyingBis(boolean localOccupyingBis) {
        mLocalOccupyingBis = localOccupyingBis;
        notifyDataSetChanged();
    }

    @NonNull
    @Override
    public BroadcastViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext())
                .inflate(R.layout.broadcast_sink_item, parent, false);
        return new BroadcastViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull BroadcastViewHolder holder, int position) {
        BroadcastSinkViewModel.FoundBroadcastItem item = mBroadcasts.get(position);
        holder.bind(item);
    }

    @Override
    public int getItemCount() {
        return mBroadcasts.size();
    }

    public void updateBroadcasts(List<BroadcastSinkViewModel.FoundBroadcastItem> broadcasts) {
        mBroadcasts = new ArrayList<>(broadcasts);
        notifyDataSetChanged();
    }

    class BroadcastViewHolder extends RecyclerView.ViewHolder {
        private final TextView mBroadcastNameText;
        private final TextView mBroadcastIdText;
        private final TextView mBroadcastDetailsText;
        private final Button mAddSourceButton;
        private final Button mStartEnhancedBroadcastSinkButton;
        private final Button mBisAcquireButton;
        private final Button mStopEnhancedBroadcastSinkButton;
        private final Button mRemoveSourceButton;

        public BroadcastViewHolder(@NonNull View itemView) {
            super(itemView);
            mBroadcastNameText = itemView.findViewById(R.id.broadcast_name_text);
            mBroadcastIdText = itemView.findViewById(R.id.broadcast_id_text);
            mBroadcastDetailsText = itemView.findViewById(R.id.broadcast_details_text);
            mAddSourceButton = itemView.findViewById(R.id.add_source_button);
            mStartEnhancedBroadcastSinkButton = itemView.findViewById(R.id.start_enhanced_sink_button);
            mBisAcquireButton = itemView.findViewById(R.id.bis_acquire_button);
            mStopEnhancedBroadcastSinkButton = itemView.findViewById(R.id.stop_enhanced_sink_button);
            mRemoveSourceButton = itemView.findViewById(R.id.remove_source_button);
        }

        public void bind(BroadcastSinkViewModel.FoundBroadcastItem item) {
            // Set broadcast name
            mBroadcastNameText.setText(item.getBroadcastName());

            // Set broadcast ID
            mBroadcastIdText.setText("ID: " + item.broadcastId);

            // Set broadcast details
            StringBuilder details = new StringBuilder();

            // Show PA sync status
            if (item.hasPASync()) {
                details.append("PA Synced: Yes\n");
                if (item.isEnhanced) {
                    details.append("Type: Enhanced Broadcast\n");
                }
                details.append("Encrypted: ").append(item.isEncrypted() ? "Yes" : "No");

                if (item.metadata != null) {
                    if (item.metadata.getSubgroups() != null && !item.metadata.getSubgroups().isEmpty()) {
                        details.append("\nSubgroups: ").append(item.metadata.getSubgroups().size());
                        int totalBis = 0;
                        for (android.bluetooth.BluetoothLeBroadcastSubgroup sg : item.metadata.getSubgroups()) {
                            totalBis += sg.getChannels().size();
                        }
                        details.append(" (").append(totalBis).append(" BISes)");
                    }
                    if (item.metadata.getPresentationDelayMicros() != 0) {
                        details.append("\nDelay: ").append(item.metadata.getPresentationDelayMicros()).append("µs");
                    }
                }
            } else {
                details.append("PA Synced: No\n");
                details.append("RSSI: ").append(item.scanResult.getRssi()).append(" dBm");
            }

            mBroadcastDetailsText.setText(details.toString());

            // BIS acquire button label for enhanced sources
            if (item.isEnhanced) {
                mStartEnhancedBroadcastSinkButton.setText("Start Enhanced Sink");
            } else {
                mStartEnhancedBroadcastSinkButton.setText("Join");
            }

            // Acquire / Release label driven by DBIG occupancy state
            mBisAcquireButton.setText(mLocalOccupyingBis ? "Release" : "Acquire");

            if (!item.hasPASync()) {
                // Not PA synced yet - only "Add Source" is available
                mAddSourceButton.setEnabled(true);
                mStartEnhancedBroadcastSinkButton.setEnabled(false);
                mBisAcquireButton.setEnabled(false);
                mStopEnhancedBroadcastSinkButton.setEnabled(false);
                mRemoveSourceButton.setEnabled(false);
            } else {
                // PA synced - "Join", "Leave", and "Remove" are available
                mAddSourceButton.setEnabled(false);
                mStartEnhancedBroadcastSinkButton.setEnabled(true);
                mBisAcquireButton.setEnabled(!item.isEnhanced); // Not applicable for enhanced
                mStopEnhancedBroadcastSinkButton.setEnabled(true);
                mRemoveSourceButton.setEnabled(true);
            }

            // Set up click listeners
            mAddSourceButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onAddSource(item.broadcastId);
                }
            });

            mStartEnhancedBroadcastSinkButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onStartEnhancedBroadcastSink(item);
                }
            });

            mBisAcquireButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onBisAcquire(item);
                }
            });

            mStopEnhancedBroadcastSinkButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onStopEnhancedBroadcastSink(item.broadcastId);
                }
            });

            mRemoveSourceButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onRemoveSource(item.broadcastId);
                }
            });
        }
    }
}
