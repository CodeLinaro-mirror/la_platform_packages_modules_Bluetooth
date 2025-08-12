/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
package com.android.bluetooth.a2dp;
import android.annotation.RequiresPermission;
import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothDevice;
import android.car.Car;
import android.car.media.CarAudioManager;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.Intent;
import android.content.IntentFilter;
import android.media.AudioAttributes;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.text.TextUtils;
import android.util.Log;
import java.util.HashMap;
import java.util.Map;
import java.util.Objects;

public final class A2dpAudioZone {
    private static final String TAG = "A2dpAudioZone";
    private static final int MAX_A2DP_AUDIO_ZONE = 2;
    private final Context mContext;
    private final BluetoothDevice mDevice;
    private static final HashMap<Integer, CarAudioZone> sCarAudioZone = new HashMap<>();
    private static final Object sLock = new Object();
    private static Car sCar;
    private static CarAudioManager sCarAudioManager;
    A2dpAudioZone(Context context, BluetoothDevice device) {
        mContext = Objects.requireNonNull(context);
        mDevice = Objects.requireNonNull(device);
    }
    public static boolean isAudioZoneAvailable(Context context) {
        synchronized (sLock) {
            if (sCarAudioZone.size() == 0) {
                initAudioZone(context);
            }
            return sCarAudioZone.size() > 0;
        }
    }
    public static void clear() {
        Car car;
        synchronized (sLock) {
            sCarAudioZone.clear();
            sCarAudioManager = null;
            car = sCar;
            sCar = null;
        }
        if (car != null) {
            car.disconnect();
        }
    }
    private static void initAudioZone(Context context) {
        CarAudioManager carAudioManager = getCarAudioManager(context);
        if (carAudioManager == null) {
            return;
        }
        for (int audioZoneId: carAudioManager.getAudioZoneIds()) {
            AudioDeviceInfo deviceInfo = carAudioManager
                    .getOutputDeviceForUsage(audioZoneId, AudioAttributes.USAGE_MEDIA);
            if (deviceInfo == null) {
                continue;
            }
            String address = deviceInfo.getAddress();
            debugLog("audio zone id: " + audioZoneId +
                    ", type: " + deviceInfo.getType() +
                    ", id: " + deviceInfo.getId() +
                    ", address: " + address);
            if (!isA2dpAudioZone(deviceInfo)) {
                continue;
            }
            // Derive the index from the bus address instead of the iteration order, so it
            // matches the stream index the native stack hardcodes per bus in
            // DevicePortProxy.cpp: BUS18_BT_HEADPHONE_1 is 0, BUS19_BT_HEADPHONE_2 is 1.
            int audioZoneIndex = getA2dpZoneIndexForAddress(address);
            if (!validZoneIndex(audioZoneIndex)) {
                Log.e(TAG, "initAudioZone: bad A2DP bus address " + address);
                continue;
            }
            debugLog("Add A2DP audio zone " + audioZoneIndex);
            sCarAudioZone.put(audioZoneIndex, new CarAudioZone(audioZoneId, address));
        }
        debugLog("A2DP audio zone number: " + sCarAudioZone.size());
    }
    private static boolean isA2dpAudioZone(AudioDeviceInfo deviceInfo) {
        return deviceInfo.getAddress().contains("BT_HEADPHONE");
    }
    private static int getA2dpZoneIndexForAddress(String address) {
        int lastUnderscore = address.lastIndexOf('_');
        if (lastUnderscore < 0) {
            return -1;
        }
        try {
            return Integer.parseInt(address.substring(lastUnderscore + 1)) - 1;
        } catch (NumberFormatException e) {
            return -1;
        }
    }
    private static Car getCar(Context context) {
        if (sCar == null) {
            sCar = Car.createCar(context);
        }
        return sCar;
    }
    private static CarAudioManager getCarAudioManager(Context context) {
        if (sCarAudioManager == null) {
            Car car = getCar(context);
            if (car == null) {
                return null;
            }
            sCarAudioManager = (CarAudioManager) Objects.requireNonNull(
                    car.getCarManager(Car.AUDIO_SERVICE));
        }
        return sCarAudioManager;
    }
    public boolean setMediaPlayer(String mediaPlayer, int zoneIndex) {
        if (!bindMediaPlayer(mediaPlayer, zoneIndex)) {
            Log.e(TAG, "setMediaPlayer: fail to bind media player: " +
                    mediaPlayer + ", in audio zone index: " + zoneIndex);
            return false;
        }
        return mapAudioZone(zoneIndex);
    }
    public static boolean validMediaPlayer(Context context, String mediaPlayer) {
        if (mediaPlayer == null || mediaPlayer.isEmpty())
            return false;
        return getApplicationInfo(context, mediaPlayer) != null;
    }
    private static ApplicationInfo getApplicationInfo(Context context, String mediaPlayer) {
        try {
            return context.getPackageManager().getApplicationInfo(mediaPlayer, 0);
        } catch (PackageManager.NameNotFoundException e) {
            e.printStackTrace();
            return null;
        }
    }
    private ApplicationInfo getApplicationInfo(String mediaPlayer) {
        return getApplicationInfo(mContext, mediaPlayer);
    }
    private boolean bindMediaPlayer(String mediaPlayer, int zoneIndex) {
        synchronized (sLock) {
            CarAudioZone audioZone = sCarAudioZone.get(zoneIndex);
            if (audioZone == null) {
                // validZoneIndex() is only a bounds check, so an in-range index can still have
                // no zone, e.g. when the device exposes BUS19_BT_HEADPHONE_2 alone.
                Log.e(TAG, "bindMediaPlayer: no audio zone for index " + zoneIndex);
                return false;
            }
            int zone = audioZone.getZone();
            ApplicationInfo appInfo = getApplicationInfo(mediaPlayer);
            if (appInfo == null) {
                return false;
            }
            int uid = appInfo.uid;
            debugLog("bindMediaPlayer: media player: " + mediaPlayer +
                    ", uid: " + uid + ", audio zone: " + zone);
            CarAudioManager carAudioManager = getCarAudioManager(mContext);
            if (carAudioManager == null) {
                return false;
            }
            String oldMediaPlayer = getMediaPlayerLocked(mDevice);
            if (!carAudioManager.setZoneIdForUid(zone, uid)) {
                return false;
            }
            clearOldMediaPlayerZoneIdLocked(carAudioManager, oldMediaPlayer, mediaPlayer, uid);
            clearMediaPlayerFromOtherZonesLocked(mDevice, zoneIndex);
            audioZone.storeMediaPlayer(mDevice, mediaPlayer);
            return true;
        }
    }

    private void clearOldMediaPlayerZoneIdLocked(
            CarAudioManager carAudioManager, String oldMediaPlayer, String newMediaPlayer,
            int newUid) {
        if (oldMediaPlayer == null || oldMediaPlayer.isEmpty()
                || oldMediaPlayer.equals(newMediaPlayer)
                || isMediaPlayerBoundToOtherDeviceLocked(oldMediaPlayer, mDevice)) {
            return;
        }
        ApplicationInfo oldAppInfo = getApplicationInfo(oldMediaPlayer);
        if (oldAppInfo != null && oldAppInfo.uid != newUid) {
            carAudioManager.clearZoneIdForUid(oldAppInfo.uid);
        }
    }

    private static void clearMediaPlayerFromOtherZonesLocked(
            BluetoothDevice device, int targetZoneIndex) {
        for (Map.Entry<Integer, CarAudioZone> entry : sCarAudioZone.entrySet()) {
            if (entry.getKey() == targetZoneIndex) {
                continue;
            }
            entry.getValue().clearMediaPlayer(device);
        }
    }

    private boolean mapAudioZone(int zoneIndex) {
        String addr = mDevice.getAddress();
        synchronized (sLock) {
            CarAudioZone audioZone = sCarAudioZone.get(zoneIndex);
            if (audioZone == null) {
                Log.e(TAG, "mapAudioZone: no audio zone for index " + zoneIndex);
                return false;
            }
            String bus = audioZone.getBus();
            debugLog("mapAudioZone: bt addr: " + addr + ", audio bus: " + bus);
            audioZone.setMediaPlayerMapped(mDevice);
        }
        return true;
    }
    public void clearMediaPlayer() {
        clearZoneId(mDevice);
    }
    private void clearZoneId(BluetoothDevice device) {
        synchronized (sLock) {
            for (CarAudioZone audioZone : sCarAudioZone.values()) {
                String mediaPlayer = audioZone.getMediaPlayer(device);
                if (mediaPlayer == null || mediaPlayer.isEmpty()) {
                    continue;
                }
                if (!isMediaPlayerBoundToOtherDeviceLocked(mediaPlayer, device)) {
                    ApplicationInfo appInfo = getApplicationInfo(mediaPlayer);
                    if (appInfo != null) {
                        CarAudioManager carAudioManager = getCarAudioManager(mContext);
                        if (carAudioManager != null) {
                            carAudioManager.clearZoneIdForUid(appInfo.uid);
                        }
                    }
                }
                audioZone.clearMediaPlayer(device);
            }
        }
    }

    private static boolean isMediaPlayerBoundToOtherDeviceLocked(
            String mediaPlayer, BluetoothDevice device) {
        for (CarAudioZone audioZone : sCarAudioZone.values()) {
            if (audioZone.hasMediaPlayerForOtherDevice(mediaPlayer, device)) {
                return true;
            }
        }
        return false;
    }
    public static void clearMediaPlayer(BluetoothDevice device) {
        synchronized (sLock) {
            for (CarAudioZone audioZone : sCarAudioZone.values()) {
                audioZone.clearMediaPlayer(device);
            }
        }
    }
    public static String getMediaPlayer(BluetoothDevice device) {
        synchronized (sLock) {
            return getMediaPlayerLocked(device);
        }
    }

    private static String getMediaPlayerLocked(BluetoothDevice device) {
        for (CarAudioZone audioZone : sCarAudioZone.values()) {
            String mediaPlayer = audioZone.getMediaPlayer(device);
            if (mediaPlayer != null && !mediaPlayer.isEmpty()) {
                return mediaPlayer;
            }
        }
        return null;
    }
    public static boolean isMediaPlayerMapped(BluetoothDevice device, String mediaPlayer) {
        synchronized (sLock) {
            for (CarAudioZone audioZone : sCarAudioZone.values()) {
                if (audioZone.isMediaPlayerMapped(device, mediaPlayer)) {
                    return true;
                }
            }
            return false;
        }
    }
    public static int getAudioZoneAvailable(String mediaPlayer) {
        synchronized (sLock) {
            for (int zoneIndex = 0; zoneIndex < MAX_A2DP_AUDIO_ZONE; zoneIndex++) {
                CarAudioZone audioZone = sCarAudioZone.get(zoneIndex);
                if (audioZone == null) {
                    continue;
                }
                if (audioZone.isEmpty() || audioZone.existMediaPlayer(mediaPlayer)) {
                    return zoneIndex;
                }
            }
        }
        // NOT found audio zone available
        return MAX_A2DP_AUDIO_ZONE;
    }

    public static boolean validZoneIndex(int zoneIndex) {
        return (zoneIndex >= 0) && (zoneIndex < MAX_A2DP_AUDIO_ZONE);
    }
    private static class CarAudioZone {
        private final int mZone;
        private final String mBus;
        private final HashMap<BluetoothDevice, MediaPlayerInfo> mMediaPlayerList = new HashMap<>();
        CarAudioZone(int zone, String bus) {
            mZone = zone;
            mBus = bus;
        }
        int getZone() {
            return mZone;
        }
        String getBus() {
            return mBus;
        }
        boolean isEmpty() {
            return mMediaPlayerList.isEmpty();
        }
        String getMediaPlayer(BluetoothDevice device) {
            MediaPlayerInfo mpInfo = mMediaPlayerList.get(device);
            return mpInfo != null ? mpInfo.getMediaPlayer() : null;
        }
        boolean existMediaPlayer(String mediaPlayer) {
            for (MediaPlayerInfo mpInfo : mMediaPlayerList.values()) {
                // Disallow to map different media player into same audio zone
                if (mediaPlayer.equals(mpInfo.getMediaPlayer())) {
                    return true;
                }
            }
            return false;
        }
        boolean hasMediaPlayerForOtherDevice(String mediaPlayer, BluetoothDevice device) {
            for (Map.Entry<BluetoothDevice, MediaPlayerInfo> entry : mMediaPlayerList.entrySet()) {
                if (entry.getKey().equals(device)) {
                    continue;
                }
                if (mediaPlayer.equals(entry.getValue().getMediaPlayer())) {
                    return true;
                }
            }
            return false;
        }
        void storeMediaPlayer(BluetoothDevice device, String mediaPlayer) {
            MediaPlayerInfo existing = mMediaPlayerList.get(device);
            if (existing != null && mediaPlayer.equals(existing.getMediaPlayer())) {
                return;
            }
            mMediaPlayerList.put(device, new MediaPlayerInfo(mediaPlayer));
        }
        void setMediaPlayerMapped(BluetoothDevice device) {
            MediaPlayerInfo mpInfo = mMediaPlayerList.get(device);
            if (mpInfo != null) {
                mpInfo.setMapped();
            } else {
                Log.w(TAG, "No MediaPlayerInfo for " + device);
            }
        }
        boolean isMediaPlayerMapped(BluetoothDevice device, String mediaPlayer) {
            MediaPlayerInfo mpInfo = mMediaPlayerList.get(device);
            if (mpInfo != null && mediaPlayer.equals(mpInfo.getMediaPlayer())) {
                return mpInfo.isMapped();
            }
            return false;
        }
        void clearMediaPlayer(BluetoothDevice device) {
            mMediaPlayerList.remove(device);
        }
    }
    private static class MediaPlayerInfo {
        private final String mMediaPlayer;
        private boolean mIsMapped;
        MediaPlayerInfo(String mediaPlayer) {
            this(mediaPlayer, false);
        }
        MediaPlayerInfo(String mediaPlayer, boolean isMapped) {
            mMediaPlayer = mediaPlayer;
            mIsMapped = isMapped;
        }
        String getMediaPlayer() {
            return mMediaPlayer;
        }
        boolean isMapped() {
            return mIsMapped;
        }
        void setMapped() {
            mIsMapped = true;
        }
    }
    private static void debugLog(String msg) {
        Log.d(TAG, msg);
    }
}
