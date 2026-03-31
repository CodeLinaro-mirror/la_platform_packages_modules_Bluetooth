/*
 * Copyright (C) 2012 The Android Open Source Project
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

package com.android.bluetooth.hfp;

import static com.android.bluetooth.Utils.BackgroundExecutor;

import static java.util.Objects.requireNonNull;

import android.bluetooth.BluetoothDevice;
import android.os.Handler;
import android.os.Looper;
import android.telephony.PhoneStateListener;
import android.telephony.ServiceState;
import android.telephony.SignalStrength;
import android.telephony.SignalStrengthUpdateRequest;
import android.telephony.SubscriptionManager;
import android.telephony.SubscriptionManager.OnSubscriptionsChangedListener;
import android.telephony.SubscriptionInfo;
import android.telephony.TelephonyManager;
import android.util.Log;

import com.android.bluetooth.btservice.AdapterService;
import com.android.internal.annotations.GuardedBy;
import com.android.internal.annotations.VisibleForTesting;

import com.android.bluetooth.Utils;
import java.util.Collections;
import java.util.HashMap;
import java.util.Objects;
import java.util.List;
import java.util.concurrent.ExecutionException;

/**
 * Class that manages Telephony states
 *
 * <p>Note: The methods in this class are not thread safe, don't call them from multiple threads.
 * Call them from the HeadsetPhoneStateMachine message handler only.
 */
public class HeadsetPhoneState {
    private static final String TAG = HeadsetPhoneState.class.getSimpleName();

    private final HeadsetService mHeadsetService;
    private final TelephonyManager mTelephonyManager;
    private final SubscriptionManager mSubscriptionManager;
    private final Handler mHandler;

    private ServiceState mServiceState;

    // HFP 1.6 CIND service value
    private int mCindService = HeadsetHalConstants.NETWORK_STATE_NOT_AVAILABLE;
    // Number of active (foreground) calls
    private int mNumActive;
    // Current Call Setup State
    private int mCallState = HeadsetHalConstants.CALL_STATE_IDLE;
    // Number of held (background) calls
    private int mNumHeld;
    // HFP 1.6 CIND signal value
    private int mCindSignal;
    // HFP 1.6 CIND roam value
    private int mCindRoam = HeadsetHalConstants.SERVICE_TYPE_HOME;
    // HFP 1.6 CIND battchg value
    private int mCindBatteryCharge;
    // Current Call Number
    private String mCindNumber;
    //Current Phone Number Type
    private int mType = 0;

    public static final int BEARER_TECHNOLOGY_3G = 0x01;
    public static final int BEARER_TECHNOLOGY_4G = 0x02;
    public static final int BEARER_TECHNOLOGY_LTE = 0x03;
    public static final int BEARER_TECHNOLOGY_WIFI = 0x04;
    public static final int BEARER_TECHNOLOGY_5G = 0x05;
    public static final int BEARER_TECHNOLOGY_GSM = 0x06;
    public static final int BEARER_TECHNOLOGY_CDMA = 0x07;
    public static final int BEARER_TECHNOLOGY_2G = 0x08;
    public static final int BEARER_TECHNOLOGY_WCDMA = 0x09;


    private final HashMap<BluetoothDevice, Integer> mDeviceEventMap = new HashMap<>();

    private final OnSubscriptionsChangedListener mOnSubscriptionsChangedListener =
            new HeadsetPhoneStateOnSubscriptionChangedListener();

    private HeadsetPhoneStateListener mPhoneStateListener;

    HeadsetPhoneState(AdapterService adapterService, HeadsetService headsetService, Looper looper) {
        mHeadsetService = requireNonNull(headsetService);
        mTelephonyManager = requireNonNull(adapterService.getSystemService(TelephonyManager.class));
        // Register for SubscriptionInfo list changes which is guaranteed to invoke
        // onSubscriptionInfoChanged and which in turns calls loadInBackground.
        mSubscriptionManager =
                requireNonNull(adapterService.getSystemService(SubscriptionManager.class));

        // Initialize subscription on the handler thread
        mHandler = new Handler(looper);
        mSubscriptionManager.addOnSubscriptionsChangedListener(
                mHandler::post, mOnSubscriptionsChangedListener);
    }

    /** Cleanup this instance. Instance can no longer be used after calling this method. */
    public void cleanup() {
        mSubscriptionManager.removeOnSubscriptionsChangedListener(mOnSubscriptionsChangedListener);
        synchronized (mDeviceEventMap) {
            mDeviceEventMap.clear();
        }
        stopListenForPhoneState();
    }

    @Override
    public String toString() {
        int telephonyEvents;
        synchronized (mDeviceEventMap) {
            telephonyEvents = getTelephonyEventsToListen();
        }
        return "HeadsetPhoneState "
                + ("[mTelephonyServiceAvailability=" + mCindService)
                + (", mNumActive=" + mNumActive)
                + (", mCallState=" + mCallState)
                + (", mNumHeld=" + mNumHeld)
                + (", mSignal=" + mCindSignal)
                + (", mRoam=" + mCindRoam)
                + (", mBatteryCharge=" + mCindBatteryCharge)
                + (", TelephonyEvents=" + telephonyEvents + "]");
    }

    @GuardedBy("mDeviceEventMap")
    private int getTelephonyEventsToListen() {
        return mDeviceEventMap.values().stream()
                .reduce(PhoneStateListener.LISTEN_NONE, (a, b) -> a | b);
    }

    /**
     * Start or stop listening for phone state change
     *
     * @param device remote device that subscribes to this phone state update
     * @param events events in {@link PhoneStateListener} to listen to
     */
    void listenForPhoneState(BluetoothDevice device, int events) {
        synchronized (mDeviceEventMap) {
            int prevEvents = getTelephonyEventsToListen();
            if (events == PhoneStateListener.LISTEN_NONE) {
                mDeviceEventMap.remove(device);
            } else {
                mDeviceEventMap.put(device, events);
            }
            int updatedEvents = getTelephonyEventsToListen();
            if (prevEvents != updatedEvents) {
                stopListenForPhoneState();
                startListenForPhoneState();
            }
			if (events != PhoneStateListener.LISTEN_NONE) {
               //push these events on registeration
               int networkType = mTelephonyManager.getDataNetworkType();
               int dataNetworkType = BEARER_TECHNOLOGY_GSM;
               switch (networkType) {
                  case TelephonyManager.NETWORK_TYPE_UNKNOWN,
                       TelephonyManager.NETWORK_TYPE_GSM -> {
                          Log.d(TAG, "inside GSM case:");
                          dataNetworkType = BEARER_TECHNOLOGY_GSM;
                  }
                  case TelephonyManager.NETWORK_TYPE_GPRS -> {
                      Log.d(TAG, "inside 2G case:");
                      dataNetworkType = BEARER_TECHNOLOGY_2G;
                  }
                  case TelephonyManager.NETWORK_TYPE_EDGE,
                        TelephonyManager.NETWORK_TYPE_EVDO_0,
                        TelephonyManager.NETWORK_TYPE_EVDO_A,
                        TelephonyManager.NETWORK_TYPE_HSDPA,
                        TelephonyManager.NETWORK_TYPE_HSUPA,
                        TelephonyManager.NETWORK_TYPE_HSPA,
                        TelephonyManager.NETWORK_TYPE_IDEN,
                        TelephonyManager.NETWORK_TYPE_EVDO_B -> {
                      Log.d(TAG, "inside 3G case:");
                      dataNetworkType = BEARER_TECHNOLOGY_3G;
                  }
                  case TelephonyManager.NETWORK_TYPE_UMTS,
                      TelephonyManager.NETWORK_TYPE_TD_SCDMA -> {
                      Log.d(TAG, "inside WCDMA case:");
                      dataNetworkType = BEARER_TECHNOLOGY_WCDMA;
                  }
                  case TelephonyManager.NETWORK_TYPE_LTE -> {
                      Log.d(TAG, "inside LTE case:");
                      dataNetworkType = BEARER_TECHNOLOGY_LTE;
                  }
                  case TelephonyManager.NETWORK_TYPE_EHRPD,
                      TelephonyManager.NETWORK_TYPE_CDMA,
                      TelephonyManager.NETWORK_TYPE_1xRTT -> {
                      Log.d(TAG, "inside CDMA case:");
                      dataNetworkType = BEARER_TECHNOLOGY_CDMA;
                  }
                  case TelephonyManager.NETWORK_TYPE_HSPAP -> {
                      Log.d(TAG, "inside 4G case:");
                      dataNetworkType = BEARER_TECHNOLOGY_4G;
                  }
                  case TelephonyManager.NETWORK_TYPE_IWLAN -> {
                      Log.d(TAG, "inside WIFI case:");
                      dataNetworkType = BEARER_TECHNOLOGY_WIFI;
                  }
                  case TelephonyManager.NETWORK_TYPE_NR -> {
                      Log.d(TAG, "inside 5G case:");
                      dataNetworkType = BEARER_TECHNOLOGY_5G;
                  }
                  default -> {
                      Log.d(TAG, "inside default case:");
                      dataNetworkType = BEARER_TECHNOLOGY_GSM;
                  }
               }
               //int networkType = mTelephonyManager.getNetworkType();
               Log.d(TAG, "Adv Audio enabled: updateBearerTech:" +  dataNetworkType);
               if (Utils.isTbsPtsTestMode()) {
                  mHeadsetService.updateBearerTechnology(dataNetworkType);
                  if (mSubscriptionManager != null) {
                     List<SubscriptionInfo> subInfos = mSubscriptionManager.getActiveSubscriptionInfoList();
                     if (subInfos == null || subInfos.isEmpty()) {
                        Log.d(TAG, "no subs info");
                        return;
                     }
                     SubscriptionInfo mFirstSubInfo = subInfos.get(0);
                     Log.d(TAG, "updateBearerName " + mFirstSubInfo.getDisplayName().toString());
                     mHeadsetService.updateBearerName(mFirstSubInfo.getDisplayName().toString());
                  }
               }
            }
        }
    }

    @GuardedBy("mDeviceEventMap")
    private void startListenForPhoneState() {
        int events = getTelephonyEventsToListen();
        Runnable asyncRunnable =
                () -> {
                    if (mPhoneStateListener != null) {
                        Log.w(TAG, "startListenForPhoneState: already listening");
                        return;
                    }
                    if (events == PhoneStateListener.LISTEN_NONE) {
                        Log.w(TAG, "startListenForPhoneState: no event to listen");
                        return;
                    }
                    int subId = SubscriptionManager.getDefaultSubscriptionId();
                    if (!SubscriptionManager.isValidSubscriptionId(subId)) {
                        // Will retry listening for phone state in onSubscriptionsChanged() callback
                        Log.w(TAG, "startListenForPhoneState: invalid subId=" + subId);
                        return;
                    }
                    Log.i(TAG, "startListenForPhoneState: subId=" + subId + " events=" + events);
                    mPhoneStateListener = new HeadsetPhoneStateListener(events);
                };
        try {
            BackgroundExecutor.submit(asyncRunnable).get();
        } catch (ExecutionException | InterruptedException e) {
            Log.e(TAG, "Exception in startListenForPhoneState", e);
        }
    }

    @GuardedBy("mDeviceEventMap")
    private void stopListenForPhoneState() {
        Runnable asyncRunnable =
                () -> {
                    if (mPhoneStateListener == null) {
                        Log.i(TAG, "stopListenForPhoneState: no listener");
                        return;
                    }
                    mPhoneStateListener.stopListener();
                    mPhoneStateListener = null;
                };
        // We intentionally drop this future. If `start` is called afterward, it will implicitly
        // await completion. Otherwise, the stack is shutting down, making a wait unnecessary.
        var unusedFuture = BackgroundExecutor.submit(asyncRunnable);
    }

    int getCindService() {
        return mCindService;
    }

    int getNumActiveCall() {
        return mNumActive;
    }

    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void setNumActiveCall(int numActive) {
        mNumActive = numActive;
    }

    int getCallState() {
        return mCallState;
    }

    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void setCallState(int callState) {
        mCallState = callState;
    }

    int getNumHeldCall() {
        return mNumHeld;
    }

    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void setNumHeldCall(int numHeldCall) {
        mNumHeld = numHeldCall;
    }

    ServiceState getServiceState() {
        return mServiceState;
    }

    int getCindSignal() {
        return mCindSignal;
    }

    void setNumber(String mNumberCall ) {
        mCindNumber = mNumberCall;
    }

    String getNumber() {
        return mCindNumber;
    }

    void setType(int mTypeCall) {
        mType = mTypeCall;
    }

    int getType() {
        return mType;
    }

    int getCindRoam() {
        return mCindRoam;
    }

    /**
     * Set battery level value used for +CIND result
     *
     * @param batteryLevel battery level value
     */
    @VisibleForTesting(visibility = VisibleForTesting.Visibility.PACKAGE)
    public void setCindBatteryCharge(int batteryLevel) {
        if (mCindBatteryCharge != batteryLevel) {
            mCindBatteryCharge = batteryLevel;
            sendDeviceStateChanged();
        }
    }

    int getCindBatteryCharge() {
        return mCindBatteryCharge;
    }

    boolean isInCall() {
        return (mNumActive >= 1);
    }

    private synchronized void sendDeviceStateChanged() {
        Log.d(
                TAG,
                "sendDeviceStateChanged. "
                        + ("mService=" + mCindService)
                        + (" mSignal=" + mCindSignal)
                        + (" mRoam=" + mCindRoam)
                        + (" mBatteryCharge=" + mCindBatteryCharge));
        mHeadsetService.onDeviceStateChanged(
                new HeadsetDeviceState(mCindService, mCindRoam, mCindSignal, mCindBatteryCharge));
    }

    private class HeadsetPhoneStateOnSubscriptionChangedListener
            extends OnSubscriptionsChangedListener {
        @Override
        public void onSubscriptionsChanged() {
            int simState = mTelephonyManager.getSimState();
            if (simState != TelephonyManager.SIM_STATE_READY) {
                mServiceState = null;
                mCindSignal = 0;
                mCindService = HeadsetHalConstants.NETWORK_STATE_NOT_AVAILABLE;
                sendDeviceStateChanged();
            }
            stopListenForPhoneState();
            startListenForPhoneState();
            int networkType = mTelephonyManager.getDataNetworkType();
            int dataNetworkType = BEARER_TECHNOLOGY_GSM;
            switch (networkType) {
               case TelephonyManager.NETWORK_TYPE_UNKNOWN,
                    TelephonyManager.NETWORK_TYPE_GSM -> {
                   Log.d(TAG, "inside GSM case:");
                   dataNetworkType = BEARER_TECHNOLOGY_GSM;
               }
               case TelephonyManager.NETWORK_TYPE_GPRS -> {
                   Log.d(TAG, "inside 2G case:");
                   dataNetworkType = BEARER_TECHNOLOGY_2G;
               }
               case TelephonyManager.NETWORK_TYPE_EDGE,
                    TelephonyManager.NETWORK_TYPE_EVDO_0,
                    TelephonyManager.NETWORK_TYPE_EVDO_A,
                    TelephonyManager.NETWORK_TYPE_HSDPA,
                    TelephonyManager.NETWORK_TYPE_HSUPA,
                    TelephonyManager.NETWORK_TYPE_HSPA,
                    TelephonyManager.NETWORK_TYPE_IDEN,
                    TelephonyManager.NETWORK_TYPE_EVDO_B -> {
                   Log.d(TAG, "inside 3G case:");
                   dataNetworkType = BEARER_TECHNOLOGY_3G;
               }
               case TelephonyManager.NETWORK_TYPE_UMTS,
                    TelephonyManager.NETWORK_TYPE_TD_SCDMA -> {
                   Log.d(TAG, "inside WCDMA case:");
                   dataNetworkType = BEARER_TECHNOLOGY_WCDMA;
               }
               case TelephonyManager.NETWORK_TYPE_LTE -> {
                  Log.d(TAG, "inside LTE case:");
                   dataNetworkType = BEARER_TECHNOLOGY_LTE;
               }
               case TelephonyManager.NETWORK_TYPE_EHRPD,
                    TelephonyManager.NETWORK_TYPE_CDMA,
                    TelephonyManager.NETWORK_TYPE_1xRTT -> {
                   Log.d(TAG, "inside CDMA case:");
                   dataNetworkType = BEARER_TECHNOLOGY_CDMA;
               }
               case TelephonyManager.NETWORK_TYPE_HSPAP -> {
                   Log.d(TAG, "inside 4G case:");
                   dataNetworkType = BEARER_TECHNOLOGY_4G;
               }
               case TelephonyManager.NETWORK_TYPE_IWLAN -> {
                   Log.d(TAG, "inside WIFI case:");
                   dataNetworkType = BEARER_TECHNOLOGY_WIFI;
               }
               case TelephonyManager.NETWORK_TYPE_NR -> {
                   Log.d(TAG, "inside 5G case:");
                   dataNetworkType = BEARER_TECHNOLOGY_5G;
               }
               default -> {
                   Log.d(TAG, "inside default case:");
                   dataNetworkType = BEARER_TECHNOLOGY_GSM;
               }
            }
           //int networkType = mTelephonyManager.getNetworkType();
           Log.d(TAG, "Adv Audio enabled: updateBearerTech:" + dataNetworkType);
           if (Utils.isTbsPtsTestMode()) {
              mHeadsetService.updateBearerTechnology(dataNetworkType);
              if (mSubscriptionManager != null) {
                 List<SubscriptionInfo> subInfos = mSubscriptionManager.getActiveSubscriptionInfoList();
                 if (subInfos == null || subInfos.isEmpty()) {
                    Log.d(TAG, "no subs info");
                    return;
                 }
                 SubscriptionInfo mFirstSubInfo = subInfos.get(0);
                 Log.d(TAG, "updateBearerName " +  mFirstSubInfo.getDisplayName().toString());
                 mHeadsetService.updateBearerName(mFirstSubInfo.getDisplayName().toString());
              }
           }
        }
    }

    private class HeadsetPhoneStateListener extends PhoneStateListener {
        private static final SignalStrengthUpdateRequest SIGNAL_STRENGTH_UPDATE_REQUEST =
                new SignalStrengthUpdateRequest.Builder()
                        .setSignalThresholdInfos(Collections.EMPTY_LIST)
                        .setSystemThresholdReportingRequestedWhileIdle(true)
                        .build();

        private final int mEvents;

        HeadsetPhoneStateListener(int events) {
            super(mHandler::post);
            mEvents = events;

            Log.i(TAG, "startListener: events=" + mEvents);
            mTelephonyManager.listen(this, mEvents);
            if ((mEvents & PhoneStateListener.LISTEN_SIGNAL_STRENGTHS) != 0) {
                mTelephonyManager.setSignalStrengthUpdateRequest(SIGNAL_STRENGTH_UPDATE_REQUEST);
            }
        }

        void stopListener() {
            Log.i(TAG, "stopListener: events=" + mEvents);
            if ((mEvents & PhoneStateListener.LISTEN_SIGNAL_STRENGTHS) != 0) {
                mTelephonyManager.clearSignalStrengthUpdateRequest(SIGNAL_STRENGTH_UPDATE_REQUEST);
            }
            mTelephonyManager.listen(this, PhoneStateListener.LISTEN_NONE);
        }

        @Override
        public synchronized void onServiceStateChanged(ServiceState serviceState) {
            mServiceState = serviceState;
            int cindService =
                    (serviceState.getState() == ServiceState.STATE_IN_SERVICE)
                            ? HeadsetHalConstants.NETWORK_STATE_AVAILABLE
                            : HeadsetHalConstants.NETWORK_STATE_NOT_AVAILABLE;
            int newRoam =
                    serviceState.getRoaming()
                            ? HeadsetHalConstants.SERVICE_TYPE_ROAMING
                            : HeadsetHalConstants.SERVICE_TYPE_HOME;

            if (cindService == mCindService && newRoam == mCindRoam) {
                // De-bounce the state change
                return;
            }
            mCindService = cindService;
            if (mCindService == HeadsetHalConstants.NETWORK_STATE_NOT_AVAILABLE) {
                mCindSignal = 0;
            }
            mCindRoam = newRoam;
            sendDeviceStateChanged();
        }

        @Override
        public void onSignalStrengthsChanged(SignalStrength signalStrength) {
            if (mCindService == HeadsetHalConstants.NETWORK_STATE_NOT_AVAILABLE) {
                mCindSignal = 0;
                // sendDeviceStateChanged is sent in onServiceStateChanged for this case
                return;
            }

            int prevSignal = mCindSignal;

            // +CIND "signal" indicator is always between 0 to 5
            mCindSignal = Integer.max(Integer.min(signalStrength.getLevel() + 1, 5), 0);

            // This results in a lot of duplicate messages, hence this check
            if (prevSignal != mCindSignal) {
                sendDeviceStateChanged();
            }
        }
    }
}
