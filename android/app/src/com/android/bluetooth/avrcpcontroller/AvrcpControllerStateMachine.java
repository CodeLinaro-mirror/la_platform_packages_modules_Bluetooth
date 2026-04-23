/*
 * Copyright (C) 2016 The Android Open Source Project
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
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package com.android.bluetooth.avrcpcontroller;

import static android.Manifest.permission.BLUETOOTH_CONNECT;
import static android.bluetooth.BluetoothProfile.STATE_CONNECTED;
import static android.bluetooth.BluetoothProfile.STATE_CONNECTING;
import static android.bluetooth.BluetoothProfile.STATE_DISCONNECTED;
import static android.bluetooth.BluetoothProfile.STATE_DISCONNECTING;

import static java.util.Objects.requireNonNull;

import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAvrcpController;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.content.Intent;
import android.media.AudioManager;
import android.media.MediaMetadata;
import android.net.Uri;
import android.os.Bundle;
import android.os.Message;
import android.support.v4.media.MediaBrowserCompat.MediaItem;
import android.support.v4.media.session.MediaSessionCompat;
import android.support.v4.media.session.PlaybackStateCompat;
import android.util.Log;
import android.util.SparseArray;

import com.android.bluetooth.R;
import com.android.bluetooth.Utils;
import com.android.bluetooth.a2dpsink.A2dpSinkService;
import com.android.bluetooth.btservice.AdapterService;
import com.android.bluetooth.btservice.ProfileService;
import com.android.bluetooth.flags.Flags;
import com.android.bluetooth.hfpclient.HeadsetClientStateMachine;
import com.android.internal.annotations.VisibleForTesting;
import com.android.internal.util.State;
import com.android.internal.util.StateMachine;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;

/**
 * Provides Bluetooth AVRCP Controller State Machine responsible for all remote control connections
 * and interactions with a remote controllable device.
 */
@SuppressLint("all")
class AvrcpControllerStateMachine extends StateMachine {
    private static final String TAG = AvrcpControllerStateMachine.class.getSimpleName();

    // 0->99 Events from Outside
    public static final int CONNECT = 1;
    public static final int DISCONNECT = 2;
    public static final int ACTIVE_DEVICE_CHANGE = 3;
    public static final int AUDIO_FOCUS_STATE_CHANGE = 4;

    // 100->199 Internal Events
    protected static final int CLEANUP = 100;
    private static final int CONNECT_TIMEOUT = 101;
    static final int MESSAGE_INTERNAL_ABS_VOL_TIMEOUT = 102;

    // 200->299 Events from Native
    static final int STACK_EVENT = 200;
    static final int MESSAGE_INTERNAL_CMD_TIMEOUT = 201;

    static final int MESSAGE_PROCESS_SET_ABS_VOL_CMD = 203;
    static final int MESSAGE_PROCESS_REGISTER_ABS_VOL_NOTIFICATION = 204;
    static final int MESSAGE_PROCESS_TRACK_CHANGED = 205;
    static final int MESSAGE_PROCESS_PLAY_POS_CHANGED = 206;
    static final int MESSAGE_PROCESS_PLAY_STATUS_CHANGED = 207;
    static final int MESSAGE_PROCESS_VOLUME_CHANGED_NOTIFICATION = 208;
    static final int MESSAGE_PROCESS_GET_FOLDER_ITEMS = 209;
    static final int MESSAGE_PROCESS_GET_FOLDER_ITEMS_OUT_OF_RANGE = 210;
    static final int MESSAGE_PROCESS_GET_PLAYER_ITEMS = 211;
    static final int MESSAGE_PROCESS_FOLDER_PATH = 212;
    static final int MESSAGE_PROCESS_SET_BROWSED_PLAYER = 213;
    static final int MESSAGE_PROCESS_SET_ADDRESSED_PLAYER = 214;
    static final int MESSAGE_PROCESS_ADDRESSED_PLAYER_CHANGED = 215;
    static final int MESSAGE_PROCESS_NOW_PLAYING_CONTENTS_CHANGED = 216;
    static final int MESSAGE_PROCESS_SUPPORTED_APPLICATION_SETTINGS = 217;
    static final int MESSAGE_PROCESS_CURRENT_APPLICATION_SETTINGS = 218;
    static final int MESSAGE_PROCESS_AVAILABLE_PLAYER_CHANGED = 219;
    static final int MESSAGE_PROCESS_RECEIVED_COVER_ART_PSM = 220;
    static final int MESSAGE_PROCESS_SEARCH_RESP = 221;
    static final int MESSAGE_PROCESS_UIDS_CHANGED = 222;
    static final int MESSAGE_PROCESS_RC_FEATURES = 223;
    static final int MESSAGE_PROCESS_ADD_TO_NOW_PLAYING = 224;

    // 300->399 Events for Browsing
    static final int MESSAGE_GET_FOLDER_ITEMS = 300;
    static final int MESSAGE_PLAY_ITEM = 301;
    static final int MSG_AVRCP_PASSTHRU = 302;
    static final int MSG_AVRCP_SET_SHUFFLE = 303;
    static final int MSG_AVRCP_SET_REPEAT = 304;
    static final int MSG_AVRCP_SEARCH = 305;
    static final int MSG_AVRCP_GET_ITEM_ATTR = 306;
    static final int MSG_AVRCP_GET_FOLDER_ITEMS_PTS = 308;
    static final int MSG_AVRCP_ADD_TO_NOW_PLAYING = 309;
    static final int MSG_AVRCP_SET_ADDRESSED_PLAYER_PTS = 310;
    static final int MSG_AVRCP_REQUEST_CONTINUING_RESPONSE = 311;
    static final int MSG_AVRCP_ABORT_CONTINUING_RESPONSE = 312;

    // 400->499 Events for Cover Artwork
    //Internal
    static final int MESSAGE_PROCESS_IMAGE_DOWNLOADED = 400;

    //External
    static final int MSG_AVRCP_FETCH_COVER_ART = 450;

    // Base value for absolute volume from JNI
    private static final int ABS_VOL_BASE = 127;

    // Notification types for Avrcp protocol JNI.
    private static final byte NOTIFICATION_RSP_TYPE_INTERIM = 0x00;

    private final AdapterService mAdapterService;

    // The value of UTF-8 as defined in IANA character set document
    private static final int AVRC_CHARSET_UTF8 = 0x006A;

    private final AudioManager mAudioManager;
    private final GetFolderList mGetFolderList;
    private final boolean mIsVolumeFixed;
    private final SparseArray<AvrcpPlayer> mAvailablePlayerList;

    @VisibleForTesting final BrowseTree mBrowseTree;

    protected final BluetoothDevice mDevice;
    protected final byte[] mDeviceAddress;
    protected final AvrcpControllerService mService;
    protected final AvrcpControllerNativeInterface mNativeInterface;
    protected final AvrcpCoverArtManager mCoverArtManager;
    protected final Disconnected mDisconnected;
    protected final Connecting mConnecting;
    protected final Connected mConnected;
    protected final Disconnecting mDisconnecting;
    protected final Search mSearch;

    protected int mCoverArtPsm;
    protected int mMostRecentState = STATE_DISCONNECTED;

    private boolean mShouldSendPlayOnFocusRecovery = false;
    private boolean mRemoteControlConnected = false;
    private boolean mBrowsingConnected = false;

    private AvrcpPlayer mAddressedPlayer;
    private int mAddressedPlayerId;
    // Set default value as zero based on assumption that TG is database unaware player
    // Refresh this value once receiving UIDS_CHANGED_EVENT interim/resp_changed from TG
    private int mUidCounter = 0;
    private int mVolumeNotificationLabel = -1;
    private int mRemoteFeatures;

    /**
     * Custom action to get item attributes.
     *
     * <p>This is called in {@link MediaController.TransportControls.sendCustomAction}
     *
     * <p>This is an asynchronous call: it will return immediately.
     *
     * <p>Intent {@link AvrcpControllerService.ACTION_TRACK_EVENT} will be broadcast.
     * to notify the item attributes retrieved.
     *
     * @param Bundle wrapped with {@link MediaMetadata.METADATA_KEY_MEDIA_ID}
     *
     * @return void
     *
     * @See {@link android.media.session.MediaController}
     *      {@link android.media.MediaMetadata}
     *      {@link com.android.bluetooth.avrcpcontroller.AvrcpControllerService}
     */
    public static final String CUSTOM_ACTION_GET_ITEM_ATTR =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_GET_ITEM_ATTR";
    public static final String KEY_BROWSE_SCOPE = "scope";
    public static final String KEY_ATTRIBUTE_ID = "attribute_id";

    /**
     * Custom action to get folder items.
     *
     * <p>This is called in {@link MediaController.TransportControls.sendCustomAction}
     *
     * <p>This is an asynchronous call: it will return immediately.
     *
     * <p>Intent {@link AvrcpControllerService.EXTRA_FOLDER_LIST} will be broadcast.
     * to notify the items(player or folder/item) retrieved.
     *
     * @param Bundle wrapped with KEY_BROWSE_SCOPE and KEY_ATTRIBUTE_ID
     *
     * @return void
     *
     * @See {@link android.media.session.MediaController}
     *      {@link android.media.MediaMetadata}
     *      {@link com.android.bluetooth.avrcpcontroller.AvrcpControllerService}
     */
    public static final String CUSTOM_ACTION_GET_FOLDER_ITEM =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_GET_FOLDER_ITEM";
    public static final String KEY_START = "start";
    public static final String KEY_END = "end";

    // Intent used to broadcast A2DP/AVRCP custom action result
    // Requires {@link android.Manifest.permission#BLUETOOTH} permission to receive
    public static final String ACTION_CUSTOM_ACTION_RESULT =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_RESULT";
    public static final String EXTRA_CUSTOM_ACTION =
        "android.bluetooth.avrcp-controller.profile.extra.CUSTOM_ACTION";
    public static final String EXTRA_CUSTOM_ACTION_RESULT =
        "android.bluetooth.avrcp-controller.profile.extra.CUSTOM_ACTION_RESULT";
    public static final String EXTRA_NUM_OF_ITEMS =
        "android.bluetooth.avrcp-controller.profile.extra.NUM_OF_ITEMS";

    /**
     * Custom action to add item into NowPlaying.
     *
     * <p>This is called in {@link MediaController.TransportControls.sendCustomAction}
     *
     * <p>This is an asynchronous call: it will return immediately.
     *
     * <p>Intent {@link #ACTION_CUSTOM_ACTION_RESULT} will be broadcast to notify the result.
     * {@link AvrcpControllerService} will update NowPlaying list if succeed.
     *
     * @param Bundle wrapped with {@link #MediaMetadata.METADATA_KEY_MEDIA_ID}
     *
     * @return void
     *
     * @See {@link android.media.session.MediaController}
     *      {@link com.android.bluetooth.avrcpcontroller.AvrcpControllerService}
     */
    public static final String CUSTOM_ACTION_ADD_TO_NOW_PLAYING =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_ADD_TO_NOW_PLAYING";

    // Result code
    public static final int RESULT_SUCCESS = 0;
    public static final int RESULT_ERROR = 1;
    public static final int RESULT_INVALID_PARAMETER = 2;
    public static final int RESULT_NOT_SUPPORTED = 3;
    public static final int RESULT_TIMEOUT = 4;

    AddToNowPlaying mAddToNowPlaying = null;

    /**
     * Custom action to set addressed player.
     *
     * <p>This is called in {@link MediaController.TransportControls.sendCustomAction}
     *
     * <p>This is an asynchronous call: it will return immediately.
     *
     * <p>Intent {@link #ACTION_CUSTOM_ACTION_RESULT} will be broadcast to notify the result.
     * {@link AvrcpControllerService} will update NowPlaying list if succeed.
     *
     * @param Bundle wrapped with {@link #MediaMetadata.METADATA_KEY_MEDIA_ID}
     *
     * @return void
     *
     * @See {@link android.media.session.MediaController}
     *      {@link com.android.bluetooth.avrcpcontroller.AvrcpControllerService}
     */
    public static final String CUSTOM_ACTION_SET_ADDRESSED_PLAYER =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_SET_ADDRESSED_PLAYER";
    public static final String KEY_PLAYER_ID = "player_id";

    /**
     * Custom action to request for continuing response packets.
     *
     * <p>This is called in {@link MediaController.TransportControls.sendCustomAction}
     *
     * <p>This is an asynchronous call: it will return immediately.
     *
     * @param Bundle wrapped with {@link #KEY_PDU_ID}
     *
     * @return void
     *
     * @See {@link android.media.session.MediaController}
     */
    public static final String CUSTOM_ACTION_REQUEST_CONTINUING_RESPONSE =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_REQUEST_CONTINUING_RESPONSE";
    public static final String KEY_PDU_ID = "pdu_id";

    /**
     * Custom action to abort continuing response.
     *
     * <p>This is called in {@link MediaController.TransportControls.sendCustomAction}
     *
     * <p>This is an asynchronous call: it will return immediately.
     *
     * @param Bundle wrapped with {@link #KEY_PDU_ID}
     *
     * @return void
     *
     * @See {@link android.media.session.MediaController}
     */
    public static final String CUSTOM_ACTION_ABORT_CONTINUING_RESPONSE =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_ABORT_CONTINUING_RESPONSE";

    // Custom actions for PTS testing.
    private static final String CUSTOM_ACTION_VOL_UP =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_VOL_UP";
    private static final String CUSTOM_ACTION_VOL_DN =
        "android.bluetooth.avrcp-controller.profile.action.CUSTOM_ACTION_VOL_DN";

    // Number of items to get in a single fetch
    static final int ITEM_PAGE_SIZE = 20;
    static final int CMD_TIMEOUT_MILLIS = 10000;
    static final int ABS_VOL_TIMEOUT_MILLIS = 1000; // 1s

    AvrcpControllerStateMachine(
            AdapterService adapterService,
            AvrcpControllerService service,
            BluetoothDevice device,
            AvrcpControllerNativeInterface nativeInterface,
            boolean isControllerAbsoluteVolumeEnabled) {
        super(TAG);
        mAdapterService = adapterService;
        mDevice = device;
        mDeviceAddress = Utils.getByteAddress(mDevice);
        mService = service;
        mNativeInterface = requireNonNull(nativeInterface);
        mRemoteFeatures = BluetoothAvrcpController.BTRC_FEAT_NONE;
        mCoverArtPsm = 0;
        mCoverArtManager = service.getCoverArtManager();

        mAvailablePlayerList = new SparseArray<>();
        mAddressedPlayerId = AvrcpPlayer.DEFAULT_ID;

        AvrcpPlayer.Builder apb = new AvrcpPlayer.Builder();
        apb.setDevice(mDevice);
        apb.setPlayerId(mAddressedPlayerId);
        apb.setSupportedFeature(AvrcpPlayer.FEATURE_PLAY);
        apb.setSupportedFeature(AvrcpPlayer.FEATURE_PAUSE);
        apb.setSupportedFeature(AvrcpPlayer.FEATURE_STOP);
        apb.setSupportedFeature(AvrcpPlayer.FEATURE_FORWARD);
        apb.setSupportedFeature(AvrcpPlayer.FEATURE_PREVIOUS);
        mAddressedPlayer = apb.build();
        mAvailablePlayerList.put(mAddressedPlayerId, mAddressedPlayer);

        mBrowseTree = new BrowseTree(mAdapterService, mDevice);
        mDisconnected = new Disconnected();
        mConnecting = new Connecting();
        mConnected = new Connected();
        mDisconnecting = new Disconnecting();

        addState(mDisconnected);
        addState(mConnecting);
        addState(mConnected);
        addState(mDisconnecting);

        mGetFolderList = new GetFolderList();
        addState(mGetFolderList, mConnected);
        mSearch = new Search();
        addState(mSearch, mConnected);
        mAddToNowPlaying = new AddToNowPlaying();
        addState(mAddToNowPlaying, mConnected);

        mAudioManager = mAdapterService.getSystemService(AudioManager.class);
        mIsVolumeFixed = mAudioManager.isVolumeFixed() || isControllerAbsoluteVolumeEnabled;

        setInitialState(mDisconnected);

        debug("State machine created");
    }

    BrowseTree.BrowseNode findNode(String parentMediaId) {
        debug("findNode(mediaId=" + parentMediaId + ")");
        return mBrowseTree.findBrowseNodeByID(parentMediaId);
    }

    /**
     * Get the current connection state
     *
     * @return current State
     */
    public int getState() {
        return mMostRecentState;
    }

    /**
     * Get the underlying device tracked by this state machine
     *
     * @return device in focus
     */
    public BluetoothDevice getDevice() {
        return mDevice;
    }

    public synchronized void setRemoteFeatures(int remoteFeatures) {
        mRemoteFeatures = remoteFeatures;
    }

    public synchronized int getRemoteFeatures() {
        return mRemoteFeatures;
    }

    /** send the connection event asynchronously */
    public boolean connect(StackEvent event) {
        if (event.mBrowsingConnected) {
            onBrowsingConnected();
        }
        mRemoteControlConnected = event.mRemoteControlConnected;
        sendMessage(CONNECT);
        return true;
    }

    /** send the Disconnect command asynchronously */
    public void disconnect() {
        sendMessage(DISCONNECT);
    }

    /** Get the current playing track */
    public AvrcpItem getCurrentTrack() {
        return mAddressedPlayer.getCurrentTrack();
    }

    @VisibleForTesting
    int getAddressedPlayerId() {
        return mAddressedPlayerId;
    }

    @VisibleForTesting
    SparseArray<AvrcpPlayer> getAvailablePlayers() {
        return mAvailablePlayerList;
    }

    /**
     * Dump the current State Machine to the string builder.
     *
     * @param sb output string
     */
    public void dump(StringBuilder sb) {
        ProfileService.println(sb, "mDevice: " + mDevice + "(" + mDevice + ") " + this.toString());
        ProfileService.println(sb, "isActive: " + isActive());
        ProfileService.println(sb, "Control: " + mRemoteControlConnected);
        ProfileService.println(sb, "Browsing: " + mBrowsingConnected);
        ProfileService.println(
                sb,
                "Cover Art: "
                        + (mCoverArtManager != null
                                ? mCoverArtManager.getState(mDevice) == STATE_CONNECTED
                                : "false, mCoverArtManager is null"));

        ProfileService.println(sb, "Addressed Player ID: " + mAddressedPlayerId);
        ProfileService.println(sb, "Browsed Player ID: " + mBrowseTree.getCurrentBrowsedPlayer());
        ProfileService.println(sb, "Available Players (" + mAvailablePlayerList.size() + "): ");
        for (int i = 0; i < mAvailablePlayerList.size(); i++) {
            AvrcpPlayer player = mAvailablePlayerList.valueAt(i);
            boolean isAddressed = (player.getId() == mAddressedPlayerId);
            ProfileService.println(sb, "\t" + (isAddressed ? "(Addressed) " : "") + player);
        }

        List<MediaItem> queue = null;
        if (mBrowseTree.mNowPlayingNode != null) {
            queue = mBrowseTree.mNowPlayingNode.getContents();
        }
        ProfileService.println(sb, "Queue (" + (queue == null ? 0 : queue.size()) + "): " + queue);
    }

    @VisibleForTesting
    boolean isActive() {
        return mDevice.equals(mService.getActiveDevice());
    }

    /** Attempt to set the active status for this device */
    public void setDeviceState(int state) {
        sendMessage(ACTIVE_DEVICE_CHANGE, state);
    }

    @Override
    protected void unhandledMessage(Message msg) {
        warn(
                "Unhandled message, state="
                        + getCurrentState()
                        + "msg.what="
                        + eventToString(msg.what));
    }

    synchronized void onBrowsingConnected() {
        mBrowsingConnected = true;
        requestContents(mBrowseTree.mRootNode);
    }

    synchronized void onBrowsingDisconnected() {
        if (!mBrowsingConnected) return;
        mAddressedPlayer.setPlayStatus(PlaybackStateCompat.STATE_ERROR);
        AvrcpItem previousTrack = mAddressedPlayer.getCurrentTrack();
        String previousTrackUuid = previousTrack != null ? previousTrack.getCoverArtUuid() : null;
        mAddressedPlayer.updateCurrentTrack(null);
        mBrowseTree.mNowPlayingNode.setCached(false);
        mBrowseTree.mRootNode.setCached(false);
        if (isActive()) {
            BluetoothMediaBrowserService.onNowPlayingQueueChanged(mBrowseTree.mNowPlayingNode);
            BluetoothMediaBrowserService.onBrowseNodeChanged(mBrowseTree.mRootNode);
        }
        removeUnusedArtwork(previousTrackUuid);
        removeUnusedArtworkFromBrowseTree();
        mBrowsingConnected = false;
    }

    synchronized void connectCoverArt() {
        // Called from "connected" state, which assumes either control or browse is connected
        if (mCoverArtManager != null
                && mCoverArtPsm != 0
                && mCoverArtManager.getState(mDevice) != STATE_CONNECTED) {
            debug("Attempting to connect to AVRCP BIP, psm: " + mCoverArtPsm);
            mCoverArtManager.connect(mDevice, /* psm */ mCoverArtPsm);
        }
    }

    synchronized void refreshCoverArt() {
        if (mCoverArtManager != null
                && mCoverArtPsm != 0
                && mCoverArtManager.getState(mDevice) == STATE_CONNECTED) {
            debug("Attempting to refresh AVRCP BIP OBEX session, psm: " + mCoverArtPsm);
            mCoverArtManager.refreshSession(mDevice);
        }
    }

    synchronized void disconnectCoverArt() {
        // Safe to call even if we're not connected
        if (mCoverArtManager != null) {
            debug("Disconnect BIP cover artwork");
            mCoverArtManager.disconnect(mDevice);
        }
    }

    /**
     * Remove an unused cover art image from storage if it's unused by the browse tree and the
     * current track.
     */
    synchronized void removeUnusedArtwork(String previousTrackUuid) {
        debug("removeUnusedArtwork(" + previousTrackUuid + ")");
        if (mCoverArtManager == null) return;
        AvrcpItem currentTrack = getCurrentTrack();
        String currentTrackUuid = currentTrack != null ? currentTrack.getCoverArtUuid() : null;
        if (previousTrackUuid != null) {
            if (!previousTrackUuid.equals(currentTrackUuid)
                    && mBrowseTree.getNodesUsingCoverArt(previousTrackUuid).isEmpty()) {
                mCoverArtManager.removeImage(mDevice, previousTrackUuid);
            }
        }
    }

    /**
     * Queries the browse tree for unused uuids and removes the associated images from storage if
     * the uuid is not used by the current track.
     */
    synchronized void removeUnusedArtworkFromBrowseTree() {
        debug("removeUnusedArtworkFromBrowseTree()");
        if (mCoverArtManager == null) return;
        AvrcpItem currentTrack = getCurrentTrack();
        String currentTrackUuid = currentTrack != null ? currentTrack.getCoverArtUuid() : null;
        List<String> unusedArtwork = mBrowseTree.getAndClearUnusedCoverArt();
        for (String uuid : unusedArtwork) {
            if (!uuid.equals(currentTrackUuid)) {
                mCoverArtManager.removeImage(mDevice, uuid);
            }
        }
    }

    private void notifyNodeChanged(BrowseTree.BrowseNode node) {
        // We should only notify now playing content updates if we're the active device. VFS
        // updates are fine at any time
        int scope = node.getScope();
        if (scope == AvrcpControllerService.BROWSE_SCOPE_NOW_PLAYING) {
            if (isActive()) {
                BluetoothMediaBrowserService.onNowPlayingQueueChanged(node);
            }
        } else {
            BluetoothMediaBrowserService.onBrowseNodeChanged(node);
        }
    }

    private void notifyPlaybackStateChanged(PlaybackStateCompat state) {
        if (isActive()) {
            BluetoothMediaBrowserService.onPlaybackStateChanged(state);
        }
    }

    void requestContents(BrowseTree.BrowseNode node) {
        sendMessage(MESSAGE_GET_FOLDER_ITEMS, node);
        debug("requestContents(node=" + node + ")");
    }

    public void playItem(BrowseTree.BrowseNode node) {
        sendMessage(MESSAGE_PLAY_ITEM, node);
    }

    void nowPlayingContentChanged() {
        removeUnusedArtworkFromBrowseTree();
        requestContents(mBrowseTree.mNowPlayingNode);
    }

    void refreshSearchNode(boolean isAddNode) {
        mBrowseTree.mSearchNode.setCached(false);

        BrowseTree.BrowseNode currBrPlayer = mBrowseTree.getCurrentBrowsedPlayer();
        if (currBrPlayer != null) {
            if (isAddNode) {
                currBrPlayer.addChild(mBrowseTree.mSearchNode);
            } else {
                currBrPlayer.removeChild(mBrowseTree.mSearchNode);
            }

            BluetoothMediaBrowserService.onBrowseNodeChanged(currBrPlayer);
        } else {
            Log.d(TAG, "currBrPlayer is NULL");
        }
    }

    protected class Disconnected extends State {
        @Override
        public void enter() {
            debug("Disconnected: Entered");

            if (isActive()) {
                refreshSearchNode(false);
            }

            if (mMostRecentState != STATE_DISCONNECTED) {
                sendMessage(CLEANUP);
            }
            broadcastConnectionStateChanged(STATE_DISCONNECTED);
        }

        @Override
        public boolean processMessage(Message message) {
            debug("Disconnected: processMessage " + eventToString(message.what));
            switch (message.what) {
                case MESSAGE_PROCESS_RECEIVED_COVER_ART_PSM -> mCoverArtPsm = message.arg1;
                case CONNECT -> {
                    debug("Connect");
                    transitionTo(mConnecting);
                }
                case CLEANUP -> mService.removeStateMachine(AvrcpControllerStateMachine.this);
                // Wait until we're connected to process this
                case ACTIVE_DEVICE_CHANGE -> deferMessage(message);
                default -> {} // Nothing to do
            }
            return true;
        }
    }

    protected class Connecting extends State {
        @Override
        public void enter() {
            debug("Connecting: Enter Connecting");
            broadcastConnectionStateChanged(STATE_CONNECTING);
            transitionTo(mConnected);
        }
    }

    class Connected extends State {
        private int mCurrentlyHeldKey = 0;

        @Override
        public void enter() {
            if (mMostRecentState == STATE_CONNECTING) {
                broadcastConnectionStateChanged(STATE_CONNECTED);
                mService.getBrowseTree().mRootNode.addChild(mBrowseTree.mRootNode);
                BluetoothMediaBrowserService.onBrowseNodeChanged(
                        mService.getBrowseTree().mRootNode);
                connectCoverArt(); // only works if we have a valid PSM
            } else {
                debug("Connected: Re-entering Connected ");
            }
            super.enter();
        }

        @Override
        public boolean processMessage(Message msg) {
            debug("Connected: processMessage " + eventToString(msg.what));
            switch (msg.what) {
                case ACTIVE_DEVICE_CHANGE -> {
                    int state = msg.arg1;
                    if (state == AvrcpControllerService.DEVICE_STATE_ACTIVE) {
                        // By default, sBrowseTree.mSearchNode is invalid because device is null.
                        // So it is necessary to update it when there is a active device.
                        mService.getBrowseTree().updateSearchNode(mBrowseTree.mSearchNode);

                        BluetoothMediaBrowserService.onAddressedPlayerChanged(mSessionCallbacks);
                        BluetoothMediaBrowserService.onTrackChanged(
                                mAddressedPlayer.getCurrentTrack());
                        BluetoothMediaBrowserService.onPlaybackStateChanged(
                                mAddressedPlayer.getPlaybackState());
                        BluetoothMediaBrowserService.onNowPlayingQueueChanged(
                                mBrowseTree.mNowPlayingNode);

                        // If we switch to a device that is playing and we don't have focus, pause
                        int focusState = getFocusState();
                        if (mAddressedPlayer.getPlaybackState().getState()
                                        == PlaybackStateCompat.STATE_PLAYING
                                && focusState == AudioManager.AUDIOFOCUS_NONE) {
                            sendMessage(
                                    MSG_AVRCP_PASSTHRU,
                                    AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                        }
                    } else {
                        // Always clear cache when device becomes inactive
                        refreshSearchNode(false);
                        sendMessage(
                                MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                        mShouldSendPlayOnFocusRecovery = false;
                    }
                }
                case AUDIO_FOCUS_STATE_CHANGE -> {
                    int newState = msg.arg1;
                    debug("Connected: Audio focus changed -> " + newState);
                    BluetoothMediaBrowserService.onAudioFocusStateChanged(newState);
                    switch (newState) {
                        case AudioManager.AUDIOFOCUS_GAIN -> {
                            // Begin playing audio again if we paused the remote
                            if (mShouldSendPlayOnFocusRecovery) {
                                debug("Connected: Regained focus, establishing play status");
                                sendMessage(
                                        MSG_AVRCP_PASSTHRU,
                                        AvrcpControllerService.PASS_THRU_CMD_ID_PLAY);
                            }
                            mShouldSendPlayOnFocusRecovery = false;
                        }
                        case AudioManager.AUDIOFOCUS_LOSS_TRANSIENT -> {
                            // Temporary loss of focus. Send a courtesy pause if we are playing and
                            // note we should recover
                            if (mAddressedPlayer.getPlaybackState().getState()
                                    == PlaybackStateCompat.STATE_PLAYING) {
                                debug(
                                        "Connected: Transient loss, temporarily pause with intent"
                                                + " to recover");
                                sendMessage(
                                        MSG_AVRCP_PASSTHRU,
                                        AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                                mShouldSendPlayOnFocusRecovery = true;
                            }
                        }
                        case AudioManager.AUDIOFOCUS_LOSS -> {
                            // Permanent loss of focus probably due to another audio app. Send a
                            // courtesy pause
                            debug("Connected: Lost focus, send a courtesy pause");
                            if (mAddressedPlayer.getPlaybackState().getState()
                                    == PlaybackStateCompat.STATE_PLAYING) {
                                sendMessage(
                                        MSG_AVRCP_PASSTHRU,
                                        AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                            }
                            mShouldSendPlayOnFocusRecovery = false;
                        }
                        default -> {} // Nothing to do
                    }
                }
                case MESSAGE_PROCESS_SET_ABS_VOL_CMD -> {
                    removeMessages(MESSAGE_INTERNAL_ABS_VOL_TIMEOUT);
                    sendMessageDelayed(MESSAGE_INTERNAL_ABS_VOL_TIMEOUT, ABS_VOL_TIMEOUT_MILLIS);
                    handleAbsVolumeRequest(msg.arg1, msg.arg2);
                }
                case MESSAGE_PROCESS_REGISTER_ABS_VOL_NOTIFICATION -> {
                    mVolumeNotificationLabel = msg.arg1;
                    mNativeInterface.sendRegisterAbsVolRsp(
                            mDeviceAddress,
                            NOTIFICATION_RSP_TYPE_INTERIM,
                            getAbsVolume(),
                            mVolumeNotificationLabel);
                }
                case MESSAGE_GET_FOLDER_ITEMS -> transitionTo(mGetFolderList);
                case MESSAGE_PLAY_ITEM -> processPlayItem((BrowseTree.BrowseNode) msg.obj);
                case MSG_AVRCP_PASSTHRU -> {
                    if (isPassThruAllowed(msg.arg1)) {
                        passThru(msg.arg1);
                    }
                    return true;
                }
                case MSG_AVRCP_SEARCH -> {
                    // Reset search node before processing new search request.
                    refreshSearchNode(false);
                    processSearchReq((String) msg.obj);
                    return true;
                }
                case MESSAGE_PROCESS_RC_FEATURES -> {
                    setRemoteFeatures(msg.arg1);
                    return true;
                }
                case MSG_AVRCP_SET_REPEAT -> setRepeat(msg.arg1);
                case MSG_AVRCP_SET_SHUFFLE -> setShuffle(msg.arg1);

                case MSG_AVRCP_GET_ITEM_ATTR -> {
                    getItemAttributes((Bundle) msg.obj);
                    return true;
                }

                case MSG_AVRCP_GET_FOLDER_ITEMS_PTS -> {
                    getFolderItems((Bundle) msg.obj);
                    transitionTo(mGetFolderList);
                    return true;
                }
                case MSG_AVRCP_ADD_TO_NOW_PLAYING -> {
                    transitionTo(mAddToNowPlaying);
                    return true;
                }
                case MSG_AVRCP_SET_ADDRESSED_PLAYER_PTS -> {
                    int playerId = ((Bundle) msg.obj).getInt(KEY_PLAYER_ID, 0);
                    setAddressedPlayer(playerId);
                    return true;
                }
                case MSG_AVRCP_REQUEST_CONTINUING_RESPONSE -> {
                    RequestContinuingResponse(msg.arg1);
                    return true;
                }
                case MSG_AVRCP_ABORT_CONTINUING_RESPONSE -> {
                    AbortContinuingResponse(msg.arg1);
                    return true;
                }
                case MESSAGE_PROCESS_TRACK_CHANGED -> {
                    AvrcpItem track = (AvrcpItem) msg.obj;
                    AvrcpItem previousTrack = mAddressedPlayer.getCurrentTrack();
                    downloadImageIfNeeded(track);
                    mAddressedPlayer.updateCurrentTrack(track);
                    if (isActive()) {
                        BluetoothMediaBrowserService.onTrackChanged(track);
                        BluetoothMediaBrowserService.onPlaybackStateChanged(
                                mAddressedPlayer.getPlaybackState());
                    }
                    if (previousTrack != null) {
                        removeUnusedArtwork(previousTrack.getCoverArtUuid());
                        removeUnusedArtworkFromBrowseTree();
                    }
                }
                case MESSAGE_PROCESS_PLAY_STATUS_CHANGED -> {
                    debug(
                            "Connected: Playback status = "
                                    + AvrcpControllerUtils.playbackStateToString(msg.arg1));
                    mAddressedPlayer.setPlayStatus(msg.arg1);

                    // Pause music when SCO is connected
                    if (msg.arg1 == PlaybackStateCompat.STATE_PLAYING
                            && HeadsetClientStateMachine.isAudioRouted()) {
                        sendMessage(MSG_AVRCP_PASSTHRU,
                                AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                        return true;
                    }

                    if (!isActive()) {
                        sendMessage(
                                MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                        return true;
                    }

                    BluetoothMediaBrowserService.onPlaybackStateChanged(
                            mAddressedPlayer.getPlaybackState());

                    int focusState = getFocusState();
                    if (focusState == AudioManager.ERROR) {
                        sendMessage(
                                MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                        return true;
                    }

                    if (mAddressedPlayer.getPlaybackState().getState()
                                    == PlaybackStateCompat.STATE_PLAYING
                            && focusState == AudioManager.AUDIOFOCUS_NONE) {
                        if (shouldRequestFocus()) {
                            mSessionCallbacks.onPrepare();
                        } else {
                            sendMessage(
                                    MSG_AVRCP_PASSTHRU,
                                    AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                        }
                    }
                }
                case MESSAGE_PROCESS_PLAY_POS_CHANGED -> {
                    if (msg.arg2 != -1) {
                        mAddressedPlayer.setPlayTime(msg.arg2);
                        notifyPlaybackStateChanged(mAddressedPlayer.getPlaybackState());
                    }
                }
                case MESSAGE_PROCESS_ADDRESSED_PLAYER_CHANGED -> {
                    int oldAddressedPlayerId = mAddressedPlayerId;
                    mAddressedPlayerId = msg.arg1;
                    debug(
                            "Connected: AddressedPlayer changed "
                                    + oldAddressedPlayerId
                                    + " -> "
                                    + mAddressedPlayerId);

                    // The now playing list is tied to the addressed player by specification in
                    // AVRCP 5.9.1. A new addressed player means our now playing content is now
                    // invalid
                    mBrowseTree.mNowPlayingNode.setCached(false);
                    if (isActive()) {
                        debug(
                                "Connected: Addressed player change has invalidated the now playing"
                                        + " list");
                        BluetoothMediaBrowserService.onNowPlayingQueueChanged(
                                mBrowseTree.mNowPlayingNode);
                    }
                    removeUnusedArtworkFromBrowseTree();

                    // For devices that support browsing, we *may* have an AvrcpPlayer with player
                    // metadata already. We could also be in the middle fetching it. If the player
                    // isn't there then we need to ensure that a default Addressed AvrcpPlayer is
                    // created to represent it. It can be updated if/when we do fetch the player.
                    if (!mAvailablePlayerList.contains(mAddressedPlayerId)) {
                        debug(
                                "Connected: Available player set does not contain the new Addressed"
                                        + " Player");
                        AvrcpPlayer.Builder apb = new AvrcpPlayer.Builder();
                        apb.setDevice(mDevice);
                        apb.setPlayerId(mAddressedPlayerId);
                        apb.setSupportedFeature(AvrcpPlayer.FEATURE_PLAY);
                        apb.setSupportedFeature(AvrcpPlayer.FEATURE_PAUSE);
                        apb.setSupportedFeature(AvrcpPlayer.FEATURE_STOP);
                        apb.setSupportedFeature(AvrcpPlayer.FEATURE_FORWARD);
                        apb.setSupportedFeature(AvrcpPlayer.FEATURE_PREVIOUS);
                        mAvailablePlayerList.put(mAddressedPlayerId, apb.build());
                    }

                    // Set our new addressed player object from our set of available players that's
                    // guaranteed to have the addressed player now.
                    mAddressedPlayer = mAvailablePlayerList.get(mAddressedPlayerId);

                    // Fetch metadata including the now playing list. The specification claims that
                    // the player feature bit only indicates if the player *natively* supports a now
                    // playing list. However, now playing is mandatory if browsing is supported,
                    // even if the player doesn't support it. A list of one item can be returned
                    // instead.
                    mNativeInterface.getCurrentMetadata(mDeviceAddress);
                    mNativeInterface.getPlaybackState(mDeviceAddress);
                    requestContents(mBrowseTree.mNowPlayingNode);
                    debug("Connected: AddressedPlayer = " + mAddressedPlayer);
                }
                case MESSAGE_PROCESS_SUPPORTED_APPLICATION_SETTINGS -> {
                    mAddressedPlayer.setSupportedPlayerApplicationSettings(
                            (PlayerApplicationSettings) msg.obj);
                    notifyPlaybackStateChanged(mAddressedPlayer.getPlaybackState());
                }
                case MESSAGE_PROCESS_CURRENT_APPLICATION_SETTINGS -> {
                    mAddressedPlayer.setCurrentPlayerApplicationSettings(
                            (PlayerApplicationSettings) msg.obj);
                    notifyPlaybackStateChanged(mAddressedPlayer.getPlaybackState());
                }
                case MESSAGE_PROCESS_AVAILABLE_PLAYER_CHANGED -> processAvailablePlayerChanged();
                case MESSAGE_PROCESS_RECEIVED_COVER_ART_PSM -> {
                    mCoverArtPsm = msg.arg1;
                    connectCoverArt();
                }
                case MESSAGE_PROCESS_UIDS_CHANGED -> {
                    processUIDSChange(msg);
                    return true;
                }
                case MESSAGE_PROCESS_IMAGE_DOWNLOADED -> {
                    AvrcpCoverArtManager.DownloadEvent event =
                            (AvrcpCoverArtManager.DownloadEvent) msg.obj;
                    String uuid = event.uuid();
                    Uri uri = event.uri();
                    debug("Connected: Received image for " + uuid + " at " + uri.toString());

                    // Let the addressed player know we got an image so it can see if the current
                    // track now has cover artwork
                    boolean addedArtwork = mAddressedPlayer.notifyImageDownload(uuid, uri);
                    if (addedArtwork && isActive()) {
                        BluetoothMediaBrowserService.onTrackChanged(
                                mAddressedPlayer.getCurrentTrack());
                    }

                    // Let the browse tree know of the newly downloaded image so it can attach it to
                    // all the items that need it. Notify of changed nodes accordingly
                    Set<BrowseTree.BrowseNode> nodes = mBrowseTree.notifyImageDownload(uuid, uri);
                    for (BrowseTree.BrowseNode node : nodes) {
                        notifyNodeChanged(node);
                    }

                    // Delete images that were downloaded and entirely unused
                    if (!addedArtwork && nodes.isEmpty()) {
                        removeUnusedArtwork(uuid);
                        removeUnusedArtworkFromBrowseTree();
                    }
                }
                case MSG_AVRCP_FETCH_COVER_ART -> {
                    // New scheme is retrieved through property
                    // AvrcpCoverArtManager.AVRCP_CONTROLLER_COVER_ART_SCHEME
                    mCoverArtManager.updateImageProperties();
                    AvrcpItem track = mAddressedPlayer.getCurrentTrack();
                    downloadImageIfNeeded(track, true);
                }
                case DISCONNECT -> transitionTo(mDisconnecting);
                default -> {
                    return super.processMessage(msg);
                }
            }
            return true;
        }

        private void processPlayItem(BrowseTree.BrowseNode node) {
            if (node == null) {
                warn("Connected: Invalid item to play");
                return;
            }
            mNativeInterface.playItem(mDeviceAddress, node.getScope(), node.getBluetoothID(), 0);
        }

        private void setAddressedPlayer(int playerId) {
            mNativeInterface.setAddressedPlayer(mDeviceAddress, playerId);
        }

        private synchronized void passThru(int cmd) {
            debug(
                    "Connected: Send passthrough command, id= "
                            + cmd
                            + ", key="
                            + AvrcpControllerUtils.passThruIdToString(cmd));
            // Some keys should be held until the next event.
            if (mCurrentlyHeldKey != 0) {
                mNativeInterface.sendPassThroughCommand(
                        mDeviceAddress,
                        mCurrentlyHeldKey,
                        AvrcpControllerService.KEY_STATE_RELEASED);

                if (mCurrentlyHeldKey == cmd) {
                    // Return to prevent starting FF/FR operation again
                    mCurrentlyHeldKey = 0;
                    return;
                } else {
                    // FF/FR is in progress and other operation is desired
                    // so after stopping FF/FR, not returning so that command
                    // can be sent for the desired operation.
                    mCurrentlyHeldKey = 0;
                }
            }

            // Send the pass through.
            mNativeInterface.sendPassThroughCommand(
                    mDeviceAddress, cmd, AvrcpControllerService.KEY_STATE_PRESSED);

            if (isHoldableKey(cmd)) {
                // Release cmd next time a command is sent.
                mCurrentlyHeldKey = cmd;
            } else {
                mNativeInterface.sendPassThroughCommand(
                        mDeviceAddress, cmd, AvrcpControllerService.KEY_STATE_RELEASED);
            }
        }

        private static boolean isHoldableKey(int cmd) {
            return (cmd == AvrcpControllerService.PASS_THRU_CMD_ID_REWIND)
                    || (cmd == AvrcpControllerService.PASS_THRU_CMD_ID_FF);
        }

        private void setRepeat(int repeatMode) {
            mNativeInterface.setPlayerApplicationSettingValues(
                    mDeviceAddress,
                    (byte) 1,
                    new byte[] {PlayerApplicationSettings.REPEAT_STATUS},
                    new byte[] {
                        PlayerApplicationSettings.mapAvrcpPlayerSettingsToBTattribVal(
                                PlayerApplicationSettings.REPEAT_STATUS, repeatMode)
                    });
        }

        private void setShuffle(int shuffleMode) {
            mNativeInterface.setPlayerApplicationSettingValues(
                    mDeviceAddress,
                    (byte) 1,
                    new byte[] {PlayerApplicationSettings.SHUFFLE_STATUS},
                    new byte[] {
                        PlayerApplicationSettings.mapAvrcpPlayerSettingsToBTattribVal(
                                PlayerApplicationSettings.SHUFFLE_STATUS, shuffleMode)
                    });
        }

        private synchronized void getItemAttributes(Bundle extras) {
            int scope = extras.getInt(KEY_BROWSE_SCOPE, 0);
            String mediaId = extras.getString(MediaMetadata.METADATA_KEY_MEDIA_ID);
            int [] attributeId = extras.getIntArray(KEY_ATTRIBUTE_ID);
            if (mediaId != null) {
                BrowseTree.BrowseNode currItem = mBrowseTree.findBrowseNodeByID(mediaId);
                debug("processGetItemAttrReq mediaId=" + mediaId + " node=" + currItem);
                if (currItem != null) {
                    int features = getRemoteFeatures();
                    if ((features & BluetoothAvrcpController.BTRC_FEAT_BROWSE) != 0) {
                        AvrcpControllerService.getItemAttributesNative(
                            mDeviceAddress, (byte) scope,
                            currItem.getBluetoothID(),
                            mUidCounter, (byte) attributeId.length, attributeId);
                    } else {
                        debug("Browsing channel not supported!!!");
                    }
                }
            } else {
                debug("processGetItemAttrReq GetElementAttributes");
            }
        }

        private synchronized void getFolderItems(Bundle extras) {
            int scope = extras.getInt(KEY_BROWSE_SCOPE, 0);
            int start = extras.getInt(KEY_START, 0);
            int end = extras.getInt(KEY_END, 0xFF);
            int [] attributeId = extras.getIntArray(KEY_ATTRIBUTE_ID);
            AvrcpControllerService.getFolderItemsNative(
                mDeviceAddress, (byte) scope, (byte) start, (byte) end,
                (byte) attributeId.length, attributeId);
        }

        private void RequestContinuingResponse(int pduId) {
            debug("processRequestContinuingResponse pduId=" + pduId);
            mNativeInterface.requestContinuingResponseNative(
                mDeviceAddress, (byte) pduId);
        }

        private void AbortContinuingResponse(int pduId) {
            debug("processAbortContinuingResponse pduId=" + pduId);
            mNativeInterface.abortContinuingResponseNative(
                mDeviceAddress, (byte) pduId);
        }

        private void processAvailablePlayerChanged() {
            debug("Connected: processAvailablePlayerChanged");
            mBrowseTree.mRootNode.setCached(false);
            mBrowseTree.mRootNode.setExpectedChildren(BrowseTree.DEFAULT_FOLDER_SIZE);
            BluetoothMediaBrowserService.onBrowseNodeChanged(mBrowseTree.mRootNode);
            removeUnusedArtworkFromBrowseTree();
            requestContents(mBrowseTree.mRootNode);
        }

        private void processSearchReq(String query) {
            if (mSearch.isSearchingSupported()) {
                debug("processSearchReq search: " + query);
                mNativeInterface.search(mDeviceAddress,
                        AVRC_CHARSET_UTF8, query.length(), query);
                transitionTo(mSearch);
            } else {
                Log.w(TAG, "Search not supported");
            }
        }
    }

    // Handle the get folder listing action
    // a) Fetch the listing of folders
    // b) Once completed return the object listing
    class GetFolderList extends State {
        boolean mAbort;
        byte mScope = AvrcpControllerService.BROWSE_SCOPE_VFS;
        BrowseTree.BrowseNode mBrowseNode;
        BrowseTree.BrowseNode mNextStep;

        @Override
        public void enter() {
            debug("GetFolderList: Entering GetFolderList");
            // Setup the timeouts.
            sendMessageDelayed(MESSAGE_INTERNAL_CMD_TIMEOUT, CMD_TIMEOUT_MILLIS);
            super.enter();
            mAbort = false;
            Message msg = getCurrentMessage();
            if (msg.what == MESSAGE_GET_FOLDER_ITEMS) {
                mBrowseNode = (BrowseTree.BrowseNode) msg.obj;
                debug("GetFolderList: new fetch request, node=" + mBrowseNode);
            } else if (msg.what == MSG_AVRCP_GET_FOLDER_ITEMS_PTS)  {
                Bundle extras = (Bundle) msg.obj;
                int scope = extras.getInt(KEY_BROWSE_SCOPE, 0);
                if (scope == AvrcpControllerService.BROWSE_SCOPE_SEARCH) {
                    mBrowseNode = mBrowseTree.mSearchNode;
                } else if (scope == AvrcpControllerService.BROWSE_SCOPE_NOW_PLAYING) {
                    mBrowseNode = mBrowseTree.mNowPlayingNode;
                } else if (scope == AvrcpControllerService.BROWSE_SCOPE_PLAYER_LIST) {
                    mBrowseNode = mBrowseTree.mRootNode;
                } else {
                    mBrowseNode = mBrowseTree.getCurrentBrowsedFolder();
                }
            }

            if (mBrowseNode == null) {
                setScope(AvrcpControllerService.BROWSE_SCOPE_VFS);
                transitionTo(mConnected);
            } else if (!mBrowsingConnected) {
                warn("GetFolderList: Browsing not connected, node=" + mBrowseNode);
                transitionTo(mConnected);
            } else {
                if (mBrowseNode.equals(mBrowseTree.mSearchNode)) {
                    setScope(AvrcpControllerService.BROWSE_SCOPE_SEARCH);
                } else if (mBrowseNode.equals(mBrowseTree.mNowPlayingNode)) {
                    setScope(AvrcpControllerService.BROWSE_SCOPE_NOW_PLAYING);
                } else if (mBrowseNode.equals(mBrowseTree.mRootNode)) {
                    setScope(AvrcpControllerService.BROWSE_SCOPE_PLAYER_LIST);
                } else {
                    setScope(AvrcpControllerService.BROWSE_SCOPE_VFS);
                }
                int scope = mBrowseNode.getScope();
                if (scope == AvrcpControllerService.BROWSE_SCOPE_PLAYER_LIST
                        || scope == AvrcpControllerService.BROWSE_SCOPE_NOW_PLAYING) {
                    mBrowseNode.setExpectedChildren(BrowseTree.DEFAULT_FOLDER_SIZE);
                }
                mBrowseNode.setCached(false);

                if (msg.what != MSG_AVRCP_GET_FOLDER_ITEMS_PTS) {
                    navigateToFolderOrRetrieve(mBrowseNode);
                }
            }
        }

        public void setScope(byte scope) {
            mScope = scope;
        }

        @Override
        public boolean processMessage(Message msg) {
            debug("GetFolderList: processMessage " + eventToString(msg.what));
            switch (msg.what) {
                case MESSAGE_PROCESS_GET_FOLDER_ITEMS -> {
                    ArrayList<AvrcpItem> folderList = (ArrayList<AvrcpItem>) msg.obj;
                    int endIndicator = mBrowseNode.getExpectedChildren() - 1;
                    debug("GetFolderList: End " + endIndicator + " received " + folderList.size());

                    // Queue up image download if the item has an image and we don't have it yet
                    // Only do this if the feature is enabled.
                    for (AvrcpItem track : folderList) {
                        if (shouldDownloadBrowsedImages()) {
                            if (Utils.isPtsTestMode()) {
                              downloadImageIfNeeded(track, true);
                            } else {
                              downloadImageIfNeeded(track);
                            }
                        } else {
                            track.setCoverArtUuid(null);
                        }
                    }

                    // Always update the node so that the user does not wait forever
                    // for the list to populate.
                    int newSize = mBrowseNode.addChildren(folderList, mScope);
                    debug("GetFolderList: Added " + newSize + " items to the browse tree");
                    notifyNodeChanged(mBrowseNode);

                    if (mBrowseNode.getChildrenCount() >= endIndicator
                            || folderList.size() == 0
                            || mAbort) {
                        // If we have fetched all the elements or if the remotes sends us 0 elements
                        // (which can lead us into a loop since mCurrInd does not proceed) we simply
                        // abort.
                        mBrowseNode.setCached(true);
                        sendFolderBroadcastAndUpdateNode();
                        transitionTo(mConnected);
                    } else {
                        // Fetch the next set of items.
                        fetchContents(mBrowseNode);
                        // Reset the timeout message since we are doing a new fetch now.
                        removeMessages(MESSAGE_INTERNAL_CMD_TIMEOUT);
                        sendMessageDelayed(MESSAGE_INTERNAL_CMD_TIMEOUT, CMD_TIMEOUT_MILLIS);
                    }
                }
                case MESSAGE_PROCESS_SET_BROWSED_PLAYER -> {
                    BrowseTree.BrowseNode preBrPlayer = mBrowseTree.getCurrentBrowsedPlayer();
                    mBrowseTree.setCurrentBrowsedPlayer(mNextStep.getID(), msg.arg1, msg.arg2);
                    BrowseTree.BrowseNode currBrPlayer = mBrowseTree.getCurrentBrowsedPlayer();
                    // Reset search node if browsed player is invalid or changed.
                    if (currBrPlayer == null ||
                            (currBrPlayer != null && (!currBrPlayer.equals(preBrPlayer)))) {
                        if (isActive()) {
                            refreshSearchNode(false);
                        }
                    }
                    removeMessages(MESSAGE_INTERNAL_CMD_TIMEOUT);
                    sendMessageDelayed(MESSAGE_INTERNAL_CMD_TIMEOUT, CMD_TIMEOUT_MILLIS);
                    navigateToFolderOrRetrieve(mBrowseNode);
                }
                case MESSAGE_PROCESS_FOLDER_PATH -> {
                    mBrowseTree.setCurrentBrowsedFolder(mNextStep.getID());
                    mBrowseTree.getCurrentBrowsedFolder().setExpectedChildren(msg.arg1);

                    // AVRCP Specification says, if we're not database aware, we must disconnect and
                    // reconnect our BIP client each time we successfully change path
                    refreshCoverArt();

                    if (mAbort) {
                        transitionTo(mConnected);
                    } else {
                        removeMessages(MESSAGE_INTERNAL_CMD_TIMEOUT);
                        sendMessageDelayed(MESSAGE_INTERNAL_CMD_TIMEOUT, CMD_TIMEOUT_MILLIS);
                        navigateToFolderOrRetrieve(mBrowseNode);
                    }
                }
                case MESSAGE_PROCESS_GET_PLAYER_ITEMS -> {
                    debug("GetFolderList: Received new available player items");
                    BrowseTree.BrowseNode rootNode = mBrowseTree.mRootNode;

                    // The specification is not firm on what receiving available player changes
                    // means relative to the existing player IDs, the addressed player and any
                    // currently saved play status, track or now playing list metadata. We're going
                    // to assume nothing and act verbosely, as some devices are known to reuse
                    // Player IDs.
                    if (!rootNode.isCached()) {
                        List<AvrcpPlayer> playerList = (List<AvrcpPlayer>) msg.obj;

                        // Since players hold metadata, including cover art handles that point to
                        // stored images, be sure to save image UUIDs so we can see if we can
                        // remove them from storage after setting our new player object
                        ArrayList<String> coverArtUuids = new ArrayList<>();
                        for (int i = 0; i < mAvailablePlayerList.size(); i++) {
                            AvrcpPlayer player = mAvailablePlayerList.valueAt(i);
                            AvrcpItem track = player.getCurrentTrack();
                            if (track != null && track.getCoverArtUuid() != null) {
                                coverArtUuids.add(track.getCoverArtUuid());
                            }
                        }

                        mAvailablePlayerList.clear();
                        for (AvrcpPlayer player : playerList) {
                            mAvailablePlayerList.put(player.getId(), player);
                        }

                        // If our new set of players contains our addressed player again then we
                        // will replace it and re-download metadata. If not, we'll re-use the old
                        // player to save the metadata queries.
                        if (!mAvailablePlayerList.contains(mAddressedPlayerId)) {
                            debug(
                                    "GetFolderList: Available player set doesn't contain the"
                                            + " addressed player");
                            mAvailablePlayerList.put(mAddressedPlayerId, mAddressedPlayer);
                        } else {
                            debug(
                                    "GetFolderList: Update addressed player with new available"
                                            + " player metadata");
                            mAddressedPlayer = mAvailablePlayerList.get(mAddressedPlayerId);
                            mNativeInterface.getCurrentMetadata(mDeviceAddress);
                            mNativeInterface.getPlaybackState(mDeviceAddress);
                            requestContents(mBrowseTree.mNowPlayingNode);
                        }
                        debug("GetFolderList: AddressedPlayer = " + mAddressedPlayer);

                        // Check old cover art UUIDs for deletion
                        for (String uuid : coverArtUuids) {
                            removeUnusedArtwork(uuid);
                        }

                        // Make sure our browse tree matches our received Available Player set only
                        rootNode.addChildren(playerList);
                        mBrowseTree.setCurrentBrowsedFolder(BrowseTree.ROOT);
                        rootNode.setExpectedChildren(playerList.size());
                        rootNode.setCached(true);
                        // mBrowseNode could be null when doing PTS test
                        // E.g. When flag mPTSTag is set to true.
                        if (mBrowseNode == null) {
                            mBrowseNode = rootNode;
                        }
                        sendFolderBroadcastAndUpdateNode();
                        notifyNodeChanged(rootNode);
                    }
                    transitionTo(mConnected);
                }
                case MESSAGE_INTERNAL_CMD_TIMEOUT -> {
                    // We have timed out to execute the request, we should simply send
                    // whatever listing we have gotten until now.
                    warn("GetFolderList: Timeout waiting for download, node=" + mBrowseNode);
                    transitionTo(mConnected);
                }
                case MESSAGE_PROCESS_GET_FOLDER_ITEMS_OUT_OF_RANGE -> {
                    // If we have gotten an error for OUT OF RANGE we have
                    // already sent all the items to the client hence simply
                    // transition to Connected state here.
                    transitionTo(mConnected);
                }
                case MESSAGE_GET_FOLDER_ITEMS -> {
                    BrowseTree.BrowseNode requested = (BrowseTree.BrowseNode) msg.obj;
                    if (!mBrowseNode.equals(requested) || requested.isNowPlaying()) {
                        if (shouldAbort(mBrowseNode.getScope(), requested.getScope())) {
                            mAbort = true;
                        }
                        deferMessage(msg);
                        debug(
                                "GetFolderList: Enqueue new request for node="
                                        + requested
                                        + ", abort="
                                        + mAbort);
                    } else {
                        debug("GetFolderList: Ignore request, node=" + requested);
                    }
                }

                case MSG_AVRCP_SEARCH -> {
                    mAbort = true;
                    deferMessage(msg);
                }

                default -> {
                    // All of these messages should be handled by parent state immediately.
                    debug(
                            "GetFolderList: Passing message to parent state, type="
                                    + eventToString(msg.what));
                    return false;
                }
            }
            return true;
        }

        /**
         * shouldAbort calculates the cases where fetching the current directory is no longer
         * necessary.
         *
         * @return true: a new folder in the same scope a new player while fetching contents of a
         *     folder false: other cases, specifically Now Playing while fetching a folder
         */
        private static boolean shouldAbort(int currentScope, int fetchScope) {
            if ((currentScope == fetchScope)
                    || (currentScope == AvrcpControllerService.BROWSE_SCOPE_VFS
                            && fetchScope == AvrcpControllerService.BROWSE_SCOPE_PLAYER_LIST)) {
                return true;
            }
            return false;
        }

        private void fetchContents(BrowseTree.BrowseNode target) {
            int start = target.getChildrenCount();
            int end =
                    Math.min(
                                    target.getExpectedChildren(),
                                    target.getChildrenCount() + ITEM_PAGE_SIZE)
                            - 1;
            debug(
                    "GetFolderList: fetchContents(title="
                            + target.getID()
                            + ", scope="
                            + target.getScope()
                            + ", start="
                            + start
                            + ", end="
                            + end
                            + ", expected="
                            + target.getExpectedChildren()
                            + ")");
            switch (target.getScope()) {
                case AvrcpControllerService.BROWSE_SCOPE_PLAYER_LIST ->
                        mNativeInterface.getPlayerList(mDeviceAddress, start, end);
                case AvrcpControllerService.BROWSE_SCOPE_NOW_PLAYING ->
                        mNativeInterface.getNowPlayingList(mDeviceAddress, start, end);
                case AvrcpControllerService.BROWSE_SCOPE_VFS ->
                        mNativeInterface.getFolderList(mDeviceAddress, start, end);
                case AvrcpControllerService.BROWSE_SCOPE_SEARCH ->
                        mNativeInterface.getSearchList(mDeviceAddress, start, end);

                default ->
                        error(
                                "GetFolderList: Scope "
                                        + target.getScope()
                                        + " cannot be handled here.");
            }
        }

        /* One of several things can happen when trying to get a folder list
         *
         *
         * 0: The folder handle is no longer valid
         * 1: The folder contents can be retrieved directly (NowPlaying, Root, Current)
         * 2: The folder is a browsable player
         * 3: The folder is a non browsable player
         * 4: The folder is not a child of the current folder
         * 5: The folder is a child of the current folder
         *
         */
        private void navigateToFolderOrRetrieve(BrowseTree.BrowseNode target) {
            mNextStep = mBrowseTree.getNextStepToFolder(target);
            debug(
                    "GetFolderList: NAVIGATING From "
                            + mBrowseTree.getCurrentBrowsedFolder().toString()
                            + ", NAVIGATING Toward "
                            + target.toString());
            if (mNextStep == null) {
                return;
            } else if (target.equals(mBrowseTree.mNowPlayingNode)
                    || target.equals(mBrowseTree.mRootNode)
                    || mNextStep.equals(mBrowseTree.getCurrentBrowsedFolder())
                    || target.equals(mBrowseTree.mSearchNode)) {
                fetchContents(mNextStep);
            } else if (mNextStep.isPlayer()) {
                debug("GetFolderList: NAVIGATING Player " + mNextStep.toString());
                BrowseTree.BrowseNode currentBrowsedPlayer = mBrowseTree.getCurrentBrowsedPlayer();
                if (currentBrowsedPlayer != null) {
                    debug(
                            "GetFolderList: Uncache current browsed player, player="
                                    + currentBrowsedPlayer);
                    mBrowseTree.getCurrentBrowsedPlayer().setCached(false);
                } else {
                    debug(
                            "GetFolderList: Browsed player unset, no need to uncache the"
                                    + " previous player");
                }

                if (mNextStep.isBrowsable()) {
                    debug(
                            "GetFolderList: Set browsed player, old="
                                    + currentBrowsedPlayer
                                    + ", new="
                                    + mNextStep);
                    mNativeInterface.setBrowsedPlayer(
                            mDeviceAddress, (int) mNextStep.getBluetoothID());
                } else {
                    debug("GetFolderList: Target player doesn't support browsing");
                    mNextStep.setCached(true);
                    transitionTo(mConnected);
                }
            } else if (mNextStep.equals(mBrowseTree.mNavigateUpNode)) {
                debug("GetFolderList: NAVIGATING UP " + mNextStep.toString());
                mNextStep = mBrowseTree.getCurrentBrowsedFolder().getParent();
                mBrowseTree.getCurrentBrowsedFolder().setCached(false);
                removeUnusedArtworkFromBrowseTree();
                mNativeInterface.changeFolderPath(
                        mDeviceAddress, mUidCounter,
                        AvrcpControllerService.FOLDER_NAVIGATION_DIRECTION_UP, 0);

            } else {
                debug("GetFolderList: NAVIGATING DOWN " + mNextStep.toString());
                mNativeInterface.changeFolderPath(
                        mDeviceAddress, mUidCounter,
                        AvrcpControllerService.FOLDER_NAVIGATION_DIRECTION_DOWN,
                        mNextStep.getBluetoothID());
            }
        }

        // Broadcast results into BTTestApp for PTS verification
        private void sendFolderBroadcastAndUpdateNode() {
            // This broadcast is for PTS test only
            if (!Utils.isPtsTestMode()) {
                return;
            }

            String id = mBrowseNode.getID();
            debug("sendFolderBroadcastAndUpdateNode, folderID: " + id
                    + ", size: " + mBrowseNode.getChildrenCount());

            List<MediaItem> list = mBrowseNode.getContents();
            ArrayList<MediaItem> folderList = new ArrayList<MediaItem>(0);
            for(MediaItem folder: list) {
                folderList.add(folder);
            }

            Intent intent = new Intent(AvrcpControllerService.ACTION_FOLDER_LIST);
            intent.putExtra(AvrcpControllerService.EXTRA_FOLDER_ID, id);
            intent.putParcelableArrayListExtra(
                    AvrcpControllerService.EXTRA_FOLDER_LIST, folderList);
            mService.sendBroadcast(
                    intent, BLUETOOTH_CONNECT,
                    Utils.getTempBroadcastBundle());

            return;
        }

        @Override
        public void exit() {
            debug("GetFolderList: fetch complete, node=" + mBrowseNode);
            removeMessages(MESSAGE_INTERNAL_CMD_TIMEOUT);

            // Whatever we have, notify on it so the UI doesn't hang
            if (mBrowseNode != null) {
                mBrowseNode.setCached(true);
                notifyNodeChanged(mBrowseNode);
            }

            mBrowseNode = null;
            super.exit();
        }
    }

    // Handle the search action
    class Search extends State {
        private static final String STATE_TAG = "Avrcp.Search";

        public boolean isSearchingSupported() {
            boolean supported = false;
            BrowseTree.BrowseNode currBrPlayer =
                mBrowseTree.getCurrentBrowsedPlayer();
            if (currBrPlayer != null && currBrPlayer.mItem != null) {
                long UID = currBrPlayer.mItem.getUid();
                AvrcpPlayer player = mAvailablePlayerList.get((int)UID);
                supported = player.isSearchingSupported();
            }
            return supported;
        }

        @Override
        public void enter() {
            super.enter();
            sendMessageDelayed(MESSAGE_INTERNAL_CMD_TIMEOUT, CMD_TIMEOUT_MILLIS);
        }

        @Override
        public boolean processMessage(Message msg) {
            Log.d(STATE_TAG, "processMessage " + msg);
            switch (msg.what) {
                case MESSAGE_PROCESS_SEARCH_RESP -> {
                    int status = msg.arg1;
                    int items = msg.arg2;
                    Log.d(STATE_TAG, "search response, status: " + status + ", items: " + items);
                    mBrowseTree.mSearchNode.setExpectedChildren(items);
                    if (isActive()) {
                        refreshSearchNode(true);
                    }
                    transitionTo(mConnected);
                }
                case MESSAGE_INTERNAL_CMD_TIMEOUT -> {
                    Log.e(STATE_TAG, "search timeout");
                    transitionTo(mConnected);
                }
                default -> {
                    Log.d(STATE_TAG, "deferring message " + msg + " to connected!");
                    deferMessage(msg);
                }
            }
            return true;
        }

        @Override
        public void exit() {
            removeMessages(MESSAGE_INTERNAL_CMD_TIMEOUT);
            super.exit();
        }
    }

    class AddToNowPlaying extends State {
        private String STATE_TAG = "Avrcp.AddToNowPlaying";
        private String mMediaId = null;
        private int mScope = AvrcpControllerService.BROWSE_SCOPE_VFS;;

        private boolean isSupported() {
            boolean supported = false;
            BrowseTree.BrowseNode currBrPlayer =
                mBrowseTree.getCurrentBrowsedPlayer();

            if (currBrPlayer != null) {
                int playerId = (int)(currBrPlayer.getBluetoothID());
                Log.d(STATE_TAG, " current browsed playerId " + playerId);
                for (int i = 0; i < mAvailablePlayerList.size(); i++) {
                    AvrcpPlayer player = mAvailablePlayerList.valueAt(i);
                    if (player.getId() == playerId) {
                        supported = player.supportsFeature(AvrcpPlayer.FEATURE_ADD_TO_NOWPLAYING);
                        break;
                    }
                }
            }
            return supported;
        }

        @Override
        public void enter() {
            Message msg = getCurrentMessage();
            if (msg.what == MSG_AVRCP_ADD_TO_NOW_PLAYING) {
                mMediaId = ((Bundle) msg.obj).getString(MediaMetadata.METADATA_KEY_MEDIA_ID);
                mScope = ((Bundle) msg.obj).getInt(KEY_BROWSE_SCOPE, 0);
            }

            BrowseTree.BrowseNode currItem = mBrowseTree.findBrowseNodeByID(mMediaId);
            Log.d(STATE_TAG, " processAddToNowPlayingReq mediaId=" + mMediaId
                    + " node=" + currItem + " scope=" + mScope);

            if (currItem != null) {
                if (isSupported()) {
                    Log.d(STATE_TAG, " Add to now playing, scope: " + mScope);

                    if (mScope != AvrcpControllerService.BROWSE_SCOPE_PLAYER_LIST) {
                        mService.addToNowPlayingNative(
                                mDeviceAddress, (byte)mScope,
                                currItem.getBluetoothID(), mUidCounter);
                        sendMessageDelayed(MESSAGE_INTERNAL_CMD_TIMEOUT, CMD_TIMEOUT_MILLIS);
                    } else {
                        Log.w(STATE_TAG, "Add to now playing invalid scope: " + mScope);
                        broadcastAddToNowPlayingResult(
                                AvrcpControllerService.JNI_AVRC_STS_INVALID_SCOPE);
                        transitionTo(mConnected);
                    }
                } else {
                    Log.w(STATE_TAG, "Add to now playing not supported");
                    broadcastAddToNowPlayingResult(
                            AvrcpControllerService.JNI_AVRC_STS_INVALID_CMD);
                    transitionTo(mConnected);
                }
            } else {
                transitionTo(mConnected);
            }
        }

        @Override
        public boolean processMessage(Message msg) {
            Log.d(STATE_TAG, " processMessage " + msg);
            switch (msg.what) {
                case MESSAGE_PROCESS_ADD_TO_NOW_PLAYING:
                    removeMessages(MESSAGE_INTERNAL_CMD_TIMEOUT);
                    broadcastAddToNowPlayingResult(msg.arg1);
                    transitionTo(mConnected);
                    break;

                case MESSAGE_INTERNAL_CMD_TIMEOUT:
                    transitionTo(mConnected);
                    break;

                case MESSAGE_PROCESS_UIDS_CHANGED:
                    processUIDSChange(msg);
                    break;

                default:
                    Log.d(STATE_TAG, " deferring message " + msg + " to connected!");
                    deferMessage(msg);
            }
            return true;
        }

        private void broadcastAddToNowPlayingResult(int status) {
            Log.d(STATE_TAG, "broadcastAddToNowPlayingResult status: " + status);
            broadcastResult(CUSTOM_ACTION_ADD_TO_NOW_PLAYING, status);
        }

        private void broadcastResult(String cmd, int status) {
            int result = getResult(status);
            Log.d(STATE_TAG, "broadcastResult cmd: " + cmd + ", result: " +
                    result + ", status: " + status);

            Intent intent = new Intent(ACTION_CUSTOM_ACTION_RESULT);
            intent.putExtra(EXTRA_CUSTOM_ACTION, cmd);
            intent.putExtra(EXTRA_CUSTOM_ACTION_RESULT, result);

            mService.sendBroadcast(
                    intent, BLUETOOTH_CONNECT, Utils.getTempBroadcastBundle());
        }
    }

    protected class Disconnecting extends State {
        @Override
        public void enter() {
            debug("Disconnecting: Entered Disconnecting");
            disconnectCoverArt();
            onBrowsingDisconnected();
            mService.getBrowseTree().mRootNode.removeChild(mBrowseTree.mRootNode);
            BluetoothMediaBrowserService.onBrowseNodeChanged(mService.getBrowseTree().mRootNode);
            broadcastConnectionStateChanged(STATE_DISCONNECTING);
            transitionTo(mDisconnected);
        }
    }

    /**
     * Handle a request to align our local volume with the volume of a remote device. If we're
     * assuming the source volume is fixed then a response of ABS_VOL_MAX will always be sent and no
     * volume adjustment action will be taken on the sink side.
     *
     * @param absVol A volume level based on a domain of [0, ABS_VOL_MAX]
     * @param label Volume notification label
     */
    private void handleAbsVolumeRequest(int absVol, int label) {
        debug("handleAbsVolumeRequest: absVol = " + absVol + ", label = " + label);
        if (mIsVolumeFixed) {
            debug("Source volume is assumed to be fixed, responding with max volume");
            absVol = ABS_VOL_BASE;
        } else {
            removeMessages(MESSAGE_INTERNAL_ABS_VOL_TIMEOUT);
            sendMessageDelayed(MESSAGE_INTERNAL_ABS_VOL_TIMEOUT, ABS_VOL_TIMEOUT_MILLIS);
            setAbsVolume(absVol);
        }
        mNativeInterface.sendAbsVolRsp(mDeviceAddress, absVol, label);
    }

    /**
     * Align our volume with a requested absolute volume level
     *
     * @param absVol A volume level based on a domain of [0, ABS_VOL_MAX]
     */
    private void setAbsVolume(int absVol) {
        int maxLocalVolume = mAudioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        int curLocalVolume = mAudioManager.getStreamVolume(AudioManager.STREAM_MUSIC);
        int reqLocalVolume = (maxLocalVolume * absVol) / ABS_VOL_BASE;
        debug(
                "setAbsVolume: absVol = "
                        + absVol
                        + ", reqLocal = "
                        + reqLocalVolume
                        + ", curLocal = "
                        + curLocalVolume
                        + ", maxLocal = "
                        + maxLocalVolume);

        /*
         * In some cases change in percentage is not sufficient enough to warrant
         * change in index values which are in range of 0-15. For such cases
         * no action is required
         */
        if (reqLocalVolume != curLocalVolume) {
            mAudioManager.setStreamVolume(
                    AudioManager.STREAM_MUSIC, reqLocalVolume, AudioManager.FLAG_SHOW_UI);
        }
    }

    private int getAbsVolume() {
        if (mIsVolumeFixed) {
            return ABS_VOL_BASE;
        }
        int maxVolume = mAudioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC);
        int currIndex = mAudioManager.getStreamVolume(AudioManager.STREAM_MUSIC);
        int newIndex = (currIndex * ABS_VOL_BASE) / maxVolume;
        return newIndex;
    }

    private void processUIDSChange(Message msg) {
        BluetoothDevice device = (BluetoothDevice) msg.obj;
        int uidCounter = msg.arg1;
        Log.d(TAG, "processUIDSChange device: " + device + ", uidCounter: " + uidCounter);
        mUidCounter = uidCounter;

        refreshCoverArt();
        // Refresh Root node and Now Playing List
        mBrowseTree.mRootNode.setCached(false);
        requestContents(mBrowseTree.mRootNode);
        mBrowseTree.mNowPlayingNode.setCached(false);
        requestContents(mBrowseTree.mNowPlayingNode);
    }

    private boolean shouldDownloadBrowsedImages() {
        return mService.getResources().getBoolean(R.bool.avrcp_controller_cover_art_browsed_images);
    }

    private void downloadImageIfNeeded(AvrcpItem track) {
        downloadImageIfNeeded(track, false);
    }

    private void downloadImageIfNeeded(AvrcpItem track, boolean forced) {
        if (mCoverArtManager == null) return;
        String uuid = track.getCoverArtUuid();
        Uri imageUri = null;
        if (uuid != null) {
            if (forced) {
                if (mCoverArtManager.getHandleForUuid(mDevice, uuid) != null) {
                    mCoverArtManager.downloadImage(mDevice, uuid, forced);
                } else {
                    mNativeInterface.getElementAttributesNative(mDeviceAddress, (byte)0, null);
                }
            } else {
                imageUri = mCoverArtManager.getImageUri(mDevice, uuid);
                if (imageUri != null) {
                    track.setCoverArtLocation(imageUri);
                } else {
                    mCoverArtManager.downloadImage(mDevice, uuid);
                }
            }
        }
    }

    private int getFocusState() {
        return mAdapterService
                .getA2dpSinkService()
                .map(A2dpSinkService::getFocusState)
                .orElse(AudioManager.ERROR);
    }

    MediaSessionCompat.Callback mSessionCallbacks =
            new MediaSessionCompat.Callback() {
                @Override
                public void onPlay() {
                    debug("onPlay");
                    onPrepare();
                    sendMessage(MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_PLAY);
                }

                @Override
                public void onPause() {
                    debug("onPause");
                    // If we receive a local pause/stop request and send it out then we need to
                    // signal that
                    // the intent is to stay paused if we recover focus from a transient loss
                    if (getFocusState() == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) {
                        debug(
                                "Received a pause while in a transient loss. Do not recover"
                                        + " anymore.");
                        mShouldSendPlayOnFocusRecovery = false;
                    }
                    sendMessage(MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                }

                @Override
                public void onSkipToNext() {
                    debug("onSkipToNext");
                    onPrepare();
                    sendMessage(
                            MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_FORWARD);
                }

                @Override
                public void onSkipToPrevious() {
                    debug("onSkipToPrevious");
                    onPrepare();
                    sendMessage(
                            MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_BACKWARD);
                }

                @Override
                public void onSkipToQueueItem(long id) {
                    debug("onSkipToQueueItem(id=" + id + ")");
                    onPrepare();
                    BrowseTree.BrowseNode node = mBrowseTree.getTrackFromNowPlayingList((int) id);
                    if (node != null) {
                        sendMessage(MESSAGE_PLAY_ITEM, node);
                    }
                }

                @Override
                public void onStop() {
                    debug("onStop");
                    // If we receive a local pause/stop request and send it out then we need to
                    // signal that
                    // the intent is to stay paused if we recover focus from a transient loss
                    if (getFocusState() == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) {
                        debug("Received a stop while in a transient loss. Do not recover anymore.");
                        mShouldSendPlayOnFocusRecovery = false;
                    }
                    sendMessage(MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_STOP);
                }

                @Override
                public void onPrepare() {
                    debug("onPrepare");
                    mAdapterService
                            .getA2dpSinkService()
                            .ifPresent(a2dpSink -> a2dpSink.requestAudioFocus(mDevice, true));
                }

                @Override
                public void onRewind() {
                    debug("onRewind");
                    sendMessage(MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_REWIND);
                }

                @Override
                public void onFastForward() {
                    debug("onFastForward");
                    sendMessage(MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_FF);
                }

                @Override
                public void onPlayFromMediaId(String mediaId, Bundle extras) {
                    debug("onPlayFromMediaId(mediaId=" + mediaId + ")");
                    // Play the item if possible.
                    onPrepare();
                    BrowseTree.BrowseNode node = mBrowseTree.findBrowseNodeByID(mediaId);
                    if (node != null) {
                        // node was found on this bluetooth device
                        sendMessage(MESSAGE_PLAY_ITEM, node);
                    } else {
                        // node was not found on this device, pause here, and play on another device
                        sendMessage(
                                MSG_AVRCP_PASSTHRU, AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE);
                        mService.playItem(mediaId);
                    }
                }

                @Override
                public void onCustomAction(String action, Bundle extras) {
                    debug("onCustomAction:" + action);
                    if (BluetoothAvrcpController.CUSTOM_ACTION_SEARCH.equals(action)) {
                        handleCustomActionSearch(extras);
                    } else if (CUSTOM_ACTION_GET_ITEM_ATTR.equals(action)) {
                        handleCustomActionGetItemAttributes(extras);
                    } else if (CUSTOM_ACTION_GET_FOLDER_ITEM.equals(action)) {
                        handleCustomActionGetFolderItems(extras);
                    } else if (CUSTOM_ACTION_ADD_TO_NOW_PLAYING.equals(action)) {
                        handleCustomActionAddToNowPlaying(extras);
                    } else if (CUSTOM_ACTION_SET_ADDRESSED_PLAYER.equals(action)) {
                        handleCustomActionSetAddressedPlayer(extras);
                    } else if (CUSTOM_ACTION_REQUEST_CONTINUING_RESPONSE.equals(action)) {
                        handleCustomActionRequestContinuingResponse(extras);
                    } else if (CUSTOM_ACTION_ABORT_CONTINUING_RESPONSE.equals(action)) {
                        handleCustomActionAbortContinuingResponse(extras);
                    } else if (CUSTOM_ACTION_VOL_UP.equals(action)) {
                        sendMessage(MSG_AVRCP_PASSTHRU,
                                AvrcpControllerService.PASS_THRU_CMD_ID_VOL_UP);
                    } else if (CUSTOM_ACTION_VOL_DN.equals(action)) {
                        sendMessage(MSG_AVRCP_PASSTHRU,
                                AvrcpControllerService.PASS_THRU_CMD_ID_VOL_DOWN);
                    } else {
                        Log.w(TAG, "Custom action " + action + " not supported.");
                    }
                }

                @Override
                public void onSetRepeatMode(int repeatMode) {
                    debug("onSetRepeatMode(repeatMode=" + repeatMode + ")");
                    sendMessage(MSG_AVRCP_SET_REPEAT, repeatMode);
                }

                @Override
                public void onSetShuffleMode(int shuffleMode) {
                    debug("onSetShuffleMode(shuffleMode=" + shuffleMode + ")");
                    sendMessage(MSG_AVRCP_SET_SHUFFLE, shuffleMode);
                }
            };

    protected void broadcastConnectionStateChanged(int currentState) {
        if (mMostRecentState == currentState) {
            return;
        }

        mAdapterService.updateProfileConnectionAdapterProperties(
                mDevice, BluetoothProfile.AVRCP_CONTROLLER, currentState, mMostRecentState);

        debug("Connection state : " + mMostRecentState + "->" + currentState);
        Intent intent = new Intent(BluetoothAvrcpController.ACTION_CONNECTION_STATE_CHANGED);
        intent.putExtra(BluetoothProfile.EXTRA_PREVIOUS_STATE, mMostRecentState);
        intent.putExtra(BluetoothProfile.EXTRA_STATE, currentState);
        intent.putExtra(BluetoothDevice.EXTRA_DEVICE, mDevice);
        intent.addFlags(Intent.FLAG_RECEIVER_REGISTERED_ONLY_BEFORE_BOOT);
        mMostRecentState = currentState;
        mService.sendBroadcast(intent, BLUETOOTH_CONNECT, Utils.getTempBroadcastBundle());
    }

    private boolean shouldRequestFocus() {
        return mService.getResources()
                .getBoolean(R.bool.a2dp_sink_automatically_request_audio_focus);
    }

    private void debug(String message) {
        Log.d(TAG, "[" + mDevice + "]: " + message);
    }

    private void warn(String message) {
        Log.w(TAG, "[" + mDevice + "]: " + message);
    }

    private void error(String message) {
        Log.e(TAG, "[" + mDevice + "]: " + message);
    }

    private static String eventToString(int event) {
        return switch (event) {
            case CONNECT -> "CONNECT";
            case DISCONNECT -> "DISCONNECT";
            case ACTIVE_DEVICE_CHANGE -> "ACTIVE_DEVICE_CHANGE";
            case AUDIO_FOCUS_STATE_CHANGE -> "AUDIO_FOCUS_STATE_CHANGE";
            case CLEANUP -> "CLEANUP";
            case CONNECT_TIMEOUT -> "CONNECT_TIMEOUT";
            case MESSAGE_INTERNAL_ABS_VOL_TIMEOUT -> "MESSAGE_INTERNAL_ABS_VOL_TIMEOUT";
            case STACK_EVENT -> "STACK_EVENT";
            case MESSAGE_INTERNAL_CMD_TIMEOUT -> "MESSAGE_INTERNAL_CMD_TIMEOUT";
            case MESSAGE_PROCESS_SET_ABS_VOL_CMD -> "MESSAGE_PROCESS_SET_ABS_VOL_CMD";
            case MESSAGE_PROCESS_REGISTER_ABS_VOL_NOTIFICATION ->
                    "MESSAGE_PROCESS_REGISTER_ABS_VOL_NOTIFICATION";
            case MESSAGE_PROCESS_TRACK_CHANGED -> "MESSAGE_PROCESS_TRACK_CHANGED";
            case MESSAGE_PROCESS_PLAY_POS_CHANGED -> "MESSAGE_PROCESS_PLAY_POS_CHANGED";
            case MESSAGE_PROCESS_PLAY_STATUS_CHANGED -> "MESSAGE_PROCESS_PLAY_STATUS_CHANGED";
            case MESSAGE_PROCESS_VOLUME_CHANGED_NOTIFICATION ->
                    "MESSAGE_PROCESS_VOLUME_CHANGED_NOTIFICATION";
            case MESSAGE_PROCESS_GET_FOLDER_ITEMS -> "MESSAGE_PROCESS_GET_FOLDER_ITEMS";
            case MESSAGE_PROCESS_GET_FOLDER_ITEMS_OUT_OF_RANGE ->
                    "MESSAGE_PROCESS_GET_FOLDER_ITEMS_OUT_OF_RANGE";
            case MESSAGE_PROCESS_GET_PLAYER_ITEMS -> "MESSAGE_PROCESS_GET_PLAYER_ITEMS";
            case MESSAGE_PROCESS_FOLDER_PATH -> "MESSAGE_PROCESS_FOLDER_PATH";
            case MESSAGE_PROCESS_SET_BROWSED_PLAYER -> "MESSAGE_PROCESS_SET_BROWSED_PLAYER";
            case MESSAGE_PROCESS_SET_ADDRESSED_PLAYER -> "MESSAGE_PROCESS_SET_ADDRESSED_PLAYER";
            case MESSAGE_PROCESS_ADDRESSED_PLAYER_CHANGED ->
                    "MESSAGE_PROCESS_ADDRESSED_PLAYER_CHANGED";
            case MESSAGE_PROCESS_NOW_PLAYING_CONTENTS_CHANGED ->
                    "MESSAGE_PROCESS_NOW_PLAYING_CONTENTS_CHANGED";
            case MESSAGE_PROCESS_SUPPORTED_APPLICATION_SETTINGS ->
                    "MESSAGE_PROCESS_SUPPORTED_APPLICATION_SETTINGS";
            case MESSAGE_PROCESS_CURRENT_APPLICATION_SETTINGS ->
                    "MESSAGE_PROCESS_CURRENT_APPLICATION_SETTINGS";
            case MESSAGE_PROCESS_AVAILABLE_PLAYER_CHANGED ->
                    "MESSAGE_PROCESS_AVAILABLE_PLAYER_CHANGED";
            case MESSAGE_PROCESS_RECEIVED_COVER_ART_PSM -> "MESSAGE_PROCESS_RECEIVED_COVER_ART_PSM";
            case MESSAGE_GET_FOLDER_ITEMS -> "MESSAGE_GET_FOLDER_ITEMS";
            case MESSAGE_PLAY_ITEM -> "MESSAGE_PLAY_ITEM";
            case MSG_AVRCP_PASSTHRU -> "MSG_AVRCP_PASSTHRU";
            case MSG_AVRCP_SET_SHUFFLE -> "MSG_AVRCP_SET_SHUFFLE";
            case MSG_AVRCP_SET_REPEAT -> "MSG_AVRCP_SET_REPEAT";
            case MSG_AVRCP_SEARCH -> "MSG_AVRCP_SEARCH";
            case MESSAGE_PROCESS_IMAGE_DOWNLOADED -> "MESSAGE_PROCESS_IMAGE_DOWNLOADED";
            default -> "UNKNOWN_EVENT_ID_" + event;
        };
    }

    private static boolean isPassThruAllowed(int cmd) {
        if (!(HeadsetClientStateMachine.isAudioRouted())) {
            return true;
        } else {
            // Only pause/stop are allowed when SCO is connected
            if (cmd == AvrcpControllerService.PASS_THRU_CMD_ID_PAUSE
                    || cmd == AvrcpControllerService.PASS_THRU_CMD_ID_STOP) {
                return true;
            }
        }

        return false;
    }

    private void handleCustomActionSearch(Bundle extras) {
        debug("handleCustomActionSearch extras: " + extras);
        if (extras == null) {
            return;
        }

        String searchQuery = extras.getString(BluetoothAvrcpController.KEY_SEARCH);
        sendMessage(MSG_AVRCP_SEARCH, searchQuery);
    }

    private void handleCustomActionGetItemAttributes(Bundle extras) {
        debug("handleCustomActionGetItemAttributes extras: " + extras);
        if (extras == null) {
            return;
        }
        sendMessage(MSG_AVRCP_GET_ITEM_ATTR, extras);
    }

    public void handleCustomActionGetFolderItems(Bundle extras) {
        debug("handleCustomActionGetFolderItems extras: " + extras);
        if (extras == null) {
            return;
        }

        sendMessage(MSG_AVRCP_GET_FOLDER_ITEMS_PTS, extras);
    }

    public void handleCustomActionAddToNowPlaying(Bundle extras) {
        debug("handleCustomActionAddToNowPlaying extras: " + extras);
        if (extras == null) {
            return;
        }

        sendMessage(MSG_AVRCP_ADD_TO_NOW_PLAYING, extras);
    }

    private static int getResult(int status) {
        switch (status) {
            case AvrcpControllerService.JNI_AVRC_STS_NO_ERROR:
                return RESULT_SUCCESS;
            case AvrcpControllerService.JNI_AVRC_STS_INVALID_CMD:
                return RESULT_NOT_SUPPORTED;
            case AvrcpControllerService.JNI_AVRC_STS_INVALID_PARAMETER:
            case AvrcpControllerService.JNI_AVRC_STS_INVALID_SCOPE:
            case AvrcpControllerService.JNI_AVRC_INV_RANGE:
                return RESULT_INVALID_PARAMETER;
            default:
                return RESULT_ERROR;
        }
    }

    public void handleCustomActionSetAddressedPlayer(Bundle extras) {
        debug("handleCustomActionSetAddressedPlayer extras: " + extras);
        if (extras == null) {
            return;
        }

        sendMessage(MSG_AVRCP_SET_ADDRESSED_PLAYER_PTS, extras);
    }

    public void handleCustomActionRequestContinuingResponse(Bundle extras) {
        debug("handleCustomActionRequestContinuingResponse extras: " + extras);
        if (extras == null) {
            return;
        }

        int pduId = extras.getInt(KEY_PDU_ID, 0);
        sendMessage(MSG_AVRCP_REQUEST_CONTINUING_RESPONSE, pduId, 0);
    }

    public void handleCustomActionAbortContinuingResponse(Bundle extras) {
        debug("handleCustomActionAbortContinuingResponse extras: " + extras);
        if (extras == null) {
            return;
        }

        int pduId = extras.getInt(KEY_PDU_ID, 0);
        sendMessage(MSG_AVRCP_ABORT_CONTINUING_RESPONSE, pduId, 0);
    }

}
