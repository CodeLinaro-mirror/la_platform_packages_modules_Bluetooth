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
        void onJoinSource(BroadcastSinkViewModel.FoundBroadcastItem item);
        void onUpdateSource(BroadcastSinkViewModel.FoundBroadcastItem item);
        void onLeaveSource(int broadcastId);
        void onRemoveSource(int broadcastId);
    }

    private List<BroadcastSinkViewModel.FoundBroadcastItem> mBroadcasts = new ArrayList<>();
    private final OnBroadcastActionListener mActionListener;

    public BroadcastSinkAdapter(OnBroadcastActionListener actionListener) {
        mActionListener = actionListener;
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
        private final Button mJoinSourceButton;
        private final Button mUpdateSourceButton;
        private final Button mLeaveSourceButton;
        private final Button mRemoveSourceButton;

        public BroadcastViewHolder(@NonNull View itemView) {
            super(itemView);
            mBroadcastNameText = itemView.findViewById(R.id.broadcast_name_text);
            mBroadcastIdText = itemView.findViewById(R.id.broadcast_id_text);
            mBroadcastDetailsText = itemView.findViewById(R.id.broadcast_details_text);
            mAddSourceButton = itemView.findViewById(R.id.add_source_button);
            mJoinSourceButton = itemView.findViewById(R.id.join_source_button);
            mUpdateSourceButton = itemView.findViewById(R.id.update_source_button);
            mLeaveSourceButton = itemView.findViewById(R.id.leave_source_button);
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
                details.append("Encrypted: ").append(item.isEncrypted() ? "Yes" : "No");

                if (item.metadata != null) {
                    if (item.metadata.getSubgroups() != null && !item.metadata.getSubgroups().isEmpty()) {
                        details.append("\nSubgroups: ").append(item.metadata.getSubgroups().size());
                    }

                    // Add presentation delay if available
                    if (item.metadata.getPresentationDelayMicros() != 0) {
                        details.append("\nDelay: ").append(item.metadata.getPresentationDelayMicros()).append("μs");
                    }
                }
            } else {
                details.append("PA Synced: No\n");
                details.append("RSSI: ").append(item.scanResult.getRssi()).append(" dBm");
            }

            mBroadcastDetailsText.setText(details.toString());

            // Set up button states and click listeners based on sync status
            // Note: We don't have BIG sync state in FoundBroadcastItem, so we enable Leave/Join
            // based on PA sync status. The actual state will be managed by the service.

            if (!item.hasPASync()) {
                // Not PA synced yet - only "Add Source" is available
                mAddSourceButton.setEnabled(true);
                mJoinSourceButton.setEnabled(false);
                mUpdateSourceButton.setEnabled(false);
                mLeaveSourceButton.setEnabled(false);
                mRemoveSourceButton.setEnabled(false);
            } else {
                // PA synced - "Join", "Update", "Leave", and "Remove" are available
                // Note: In a real implementation, you'd track BIG sync state to enable/disable Leave/Update
                mAddSourceButton.setEnabled(false);
                mJoinSourceButton.setEnabled(true);
                mUpdateSourceButton.setEnabled(true);  // Enable if BIG synced
                mLeaveSourceButton.setEnabled(true);  // Enable if BIG synced
                mRemoveSourceButton.setEnabled(true);
            }

            // Set up click listeners
            mAddSourceButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onAddSource(item.broadcastId);
                }
            });

            mJoinSourceButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onJoinSource(item);
                }
            });

            mUpdateSourceButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onUpdateSource(item);
                }
            });

            mLeaveSourceButton.setOnClickListener(v -> {
                if (mActionListener != null) {
                    mActionListener.onLeaveSource(item.broadcastId);
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
