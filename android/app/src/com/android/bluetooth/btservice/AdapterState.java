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
 *
 * Changes from Qualcomm Technologies, Inc. are provided under the following license:
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package com.android.bluetooth.btservice;

import android.bluetooth.State;
import android.os.Build;
import android.os.Looper;
import android.os.Message;
import android.os.SystemProperties;
import android.util.Log;

import com.android.bluetooth.Util;
import com.android.internal.util.StateMachine;

// This state machine handles Bluetooth Adapter states.
// Stable States:
//      {@link Off}: Initial State
//      {@link BleOn} : Bluetooth Low Energy, Including GATT, is on
//      {@link On} : Bluetooth is on (All supported profiles)
//
// Transition States:
//      {@link TurningBleOn} : Off to BleOn
//      {@link TurningBleOff} : BleOn to Off
//      {@link TurningOn} : BleOn to On
//      {@link TurningOff} : On to TurningBleOff
//
//           OFF ⮜─────────────────╮
//             ⮟                   │
//             │                   ⮝
//  TurningBleOn ➤─── Timeout ➤─── TurningBleOff
//             ⮟                   │
//             │                   ⮝
//        BLE_ON ➤─────────────────┤
//             ⮟                   │
//             │                   ⮝
//     TurningOn ➤─── Timeout ➤─── TurningOff
//             ⮟                   │
//             │                   │
//            ON ➤─────────────────╯
//
final class AdapterState extends StateMachine {
    private static final String TAG = Util.BT_PREFIX + AdapterState.class.getSimpleName();

    static final int USER_TURN_ON = 1;
    static final int USER_TURN_OFF = 2;
    static final int BLE_TURN_ON = 3;
    static final int BLE_TURN_OFF = 4;
    static final int BREDR_STARTED = 5;
    static final int BREDR_STOPPED = 6;
    static final int BLE_STARTED = 7;
    static final int BLE_STOPPED = 8;
    static final int BREDR_START_TIMEOUT = 9;
    static final int BREDR_STOP_TIMEOUT = 10;
    static final int BLE_STOP_TIMEOUT = 11;
    static final int BLE_START_TIMEOUT = 12;
    static final int NEW_ADAPTER_STATE_CHANGED = 13;
    static final int NEW_ADAPTER_ENABLE_TIMEOUT = 14;
    static final int NEW_ADAPTER_DISABLE_TIMEOUT = 15;

    private static final boolean DEGRADED_PERFORMANCE =
            SystemProperties.getBoolean(
                    "bluetooth.hardware.degraded_performance_mode.enabled", false);
    // See android.os.Build.HW_TIMEOUT_MULTIPLIER. This should not be set on real hw
    private static final int HW_MULTIPLIER = SystemProperties.getInt("ro.hw_timeout_multiplier", 1);

    private static final int BLE_START_TIMEOUT_DELAY;
    private static final int BLE_STOP_TIMEOUT_DELAY;
    private static final int BREDR_START_TIMEOUT_DELAY;
    private static final int BREDR_STOP_TIMEOUT_DELAY;
    private static final int NEW_ADAPTER_ENABLE_TIMEOUT_DELAY;
    private static final int NEW_ADAPTER_DISABLE_TIMEOUT_DELAY;
    private static final boolean isAtMost25Q4 =
            Build.VERSION.SDK_INT_FULL <= Build.VERSION_CODES_FULL.BAKLAVA_1;

    static {
        // Values must not be lower than the one in stack.cc
        int defaultDelay = 4_000 * HW_MULTIPLIER;
        // Validate the configuration when property is enabled or for new devices after 25Q4.
        if ((DEGRADED_PERFORMANCE)
                && (!SystemProperties.get("ro.bluetooth.ble_start_timeout_delay").isEmpty()
                        || !SystemProperties.get("ro.bluetooth.ble_stop_timeout_delay").isEmpty()
                        || !SystemProperties.get("bluetooth.gd.start_timeout").isEmpty()
                        || !SystemProperties.get("bluetooth.gd.stop_timeout").isEmpty())) {
            throw new IllegalStateException("Bluetooth timeout properties are incorrect");
        }
        if (DEGRADED_PERFORMANCE || HW_MULTIPLIER != 1) {
            defaultDelay = 8_000 * HW_MULTIPLIER;
            BLE_START_TIMEOUT_DELAY = defaultDelay;
            BLE_STOP_TIMEOUT_DELAY = defaultDelay;
        } else {
            defaultDelay = 4_000;
            // Tolerate property usage on older devices
            if (isAtMost25Q4) {
                BLE_START_TIMEOUT_DELAY =
                        SystemProperties.getInt(
                                "ro.bluetooth.ble_start_timeout_delay", defaultDelay);
                BLE_STOP_TIMEOUT_DELAY =
                        SystemProperties.getInt(
                                "ro.bluetooth.ble_stop_timeout_delay", defaultDelay);
            } else {
                BLE_START_TIMEOUT_DELAY = defaultDelay;
                BLE_STOP_TIMEOUT_DELAY = defaultDelay;
            }
        }
        BREDR_START_TIMEOUT_DELAY = defaultDelay;
        BREDR_STOP_TIMEOUT_DELAY = defaultDelay;
        NEW_ADAPTER_ENABLE_TIMEOUT_DELAY = defaultDelay/2;
        NEW_ADAPTER_DISABLE_TIMEOUT_DELAY = defaultDelay/2;
    }

    private AdapterService mAdapterService;
    private final TurningOn mTurningOn = new TurningOn(State.TURNING_ON);
    private final TurningBleOn mTurningBleOn = new TurningBleOn(State.BLE_TURNING_ON);
    private final TurningOff mTurningOff = new TurningOff(State.TURNING_OFF);
    private final TurningBleOff mTurningBleOff = new TurningBleOff(State.BLE_TURNING_OFF);
    private final On mOn = new On(State.ON);
    private final Off mOff = new Off(State.OFF);
    private final BleOn mBleOn = new BleOn(State.BLE_ON);
    private final NewAdapterState mNewAdapterState = new NewAdapterState(State.NEW_ADAPTER);

    private int mState = State.OFF;
    private int mPrevState = State.OFF;

    private boolean mPendingOn = false;
    private boolean mPendingOff = false;
    private final int mAdapterIndex;

    AdapterState(AdapterService service, Looper looper) {
        super(TAG, looper);
        addState(mOn);
        addState(mBleOn);
        addState(mOff);
        addState(mTurningOn);
        addState(mTurningOff);
        addState(mTurningBleOn);
        addState(mTurningBleOff);
        if (isDualAdapterMode()) {
            addState(mNewAdapterState);
        }
        mAdapterService = service;
        mAdapterIndex = AdapterUtil.getAdapterIndex();
        setInitialState(mOff);
        start();
    }

    int getState() {
        return mState;
    }

    private static String messageString(int message) {
        return switch (message) {
            case BLE_TURN_ON -> "BLE_TURN_ON";
            case USER_TURN_ON -> "USER_TURN_ON";
            case BREDR_STARTED -> "BREDR_STARTED";
            case BLE_STARTED -> "BLE_STARTED";
            case USER_TURN_OFF -> "USER_TURN_OFF";
            case BLE_TURN_OFF -> "BLE_TURN_OFF";
            case BLE_STOPPED -> "BLE_STOPPED";
            case BREDR_STOPPED -> "BREDR_STOPPED";
            case BLE_START_TIMEOUT -> "BLE_START_TIMEOUT";
            case BLE_STOP_TIMEOUT -> "BLE_STOP_TIMEOUT";
            case BREDR_START_TIMEOUT -> "BREDR_START_TIMEOUT";
            case BREDR_STOP_TIMEOUT -> "BREDR_STOP_TIMEOUT";
            case NEW_ADAPTER_STATE_CHANGED -> "NEW_ADAPTER_STATE_CHANGED";
            case NEW_ADAPTER_ENABLE_TIMEOUT -> "NEW_ADAPTER_ENABLE_TIMEOUT";
            case NEW_ADAPTER_DISABLE_TIMEOUT -> "NEW_ADAPTER_DISABLE_TIMEOUT";
            default -> "Unknown message (" + message + ")";
        };
    }

    public void doQuit() {
        quitNow();
    }

    private void cleanup() {
        if (mAdapterService != null) {
            mAdapterService = null;
        }
    }

    private static boolean isDualAdapterMode() {
        // Dual adapter mode is only valid with default adapter.
        return AdapterUtil.isDualAdapterMode() &&
                AdapterUtil.isAdapterDefault();
    }

    @Override
    protected void onQuitting() {
        cleanup();
    }

    @Override
    protected String getLogRecString(Message msg) {
        return messageString(msg.what);
    }

    private abstract class BaseAdapterState extends com.android.internal.util.State {
        private static boolean isStableState(int state) {
            return switch (state) {
                case State.ON, State.OFF, State.BLE_ON -> true;
                default -> false;
            };
        }

        private final int mStateValue;

        BaseAdapterState(int state) {
            mStateValue = state;
        }

        @Override
        public void enter() {
            infoLog("State entered");
            mState = mStateValue;
            if (isStableState(mPrevState)) {
                // The SystemServer initiates transition from stable states
                // AdapterStates notifies only when initiating transitiong from any other state.
                // The destination transition may not be stable (ex: TURNING_OFF -> BLE_TURNING_OFF)
                return;
            }
            mAdapterService.updateAdapterState(mPrevState, mState);
        }

        @Override
        public void exit() {
            mPrevState = mState;
        }

        void infoLog(String msg) {
            Log.i(TAG + "[adapter_" + mAdapterIndex + "]", State.$.toString(mStateValue) + " : " + msg);
        }

        void errorLog(String msg) {
            Log.e(TAG + "[adapter_" + mAdapterIndex + "]", State.$.toString(mStateValue) + " : " + msg);
        }
    }

    private class Off extends BaseAdapterState {
        Off(int state) {
            super(state);
        }

        @Override
        public void enter() {
            super.enter();
            if (mPrevState == State.BLE_TURNING_OFF) {
                mAdapterService.cleanup();
            }
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case BLE_TURN_ON -> transitionTo(mTurningBleOn);
                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class BleOn extends BaseAdapterState {
        BleOn(int state) {
            super(state);
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case USER_TURN_ON -> transitionTo(mTurningOn);
                case BLE_TURN_OFF -> transitionTo(mTurningBleOff);
                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class On extends BaseAdapterState {
        On(int state) {
            super(state);
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case USER_TURN_OFF -> transitionTo(mTurningOff);
                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class TurningBleOn extends BaseAdapterState {
        TurningBleOn(int state) {
            super(state);
        }

        @Override
        public void enter() {
            super.enter();
            infoLog("Start Timeout Delay: " + BLE_START_TIMEOUT_DELAY);
            sendMessageDelayed(BLE_START_TIMEOUT, BLE_START_TIMEOUT_DELAY);
            mAdapterService.bringUpBle();
        }

        @Override
        public void exit() {
            removeMessages(BLE_START_TIMEOUT);
            super.exit();
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case BLE_STARTED -> transitionTo(mBleOn);

                case BLE_START_TIMEOUT -> {
                    errorLog(messageString(msg.what));
                    transitionTo(mTurningBleOff);
                }
                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class TurningOn extends BaseAdapterState {
        TurningOn(int state) {
            super(state);
        }

        @Override
        public void enter() {
            super.enter();
            sendMessageDelayed(BREDR_START_TIMEOUT, BREDR_START_TIMEOUT_DELAY);
            mAdapterService.startProfileServices();
        }

        @Override
        public void exit() {
            removeMessages(BREDR_START_TIMEOUT);
            super.exit();
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case BREDR_STARTED -> handleOn();

                case BREDR_START_TIMEOUT -> {
                    errorLog(messageString(msg.what));
                    transitionTo(mTurningOff);
                }

                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }

        private void handleOn() {
            if (isDualAdapterMode() &&
                mAdapterService.canEnableNewAdapter()) {
                mPendingOn = true;
                transitionTo(mNewAdapterState);
            } else {
                transitionTo(mOn);
            }
        }
    }

    private class TurningOff extends BaseAdapterState {
        TurningOff(int state) {
            super(state);
        }

        @Override
        public void enter() {
            super.enter();
            sendMessageDelayed(BREDR_STOP_TIMEOUT, BREDR_STOP_TIMEOUT_DELAY);
            mAdapterService.stopProfileServices();
        }

        @Override
        public void exit() {
            removeMessages(BREDR_STOP_TIMEOUT);
            if (mAdapterService != null) {
                Log.i(TAG, "Disconnecting all ACLs with BREDR Stopped");
                mAdapterService.disconnectAllAcls();
            }

            super.exit();
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case BREDR_STOPPED -> transitionTo(mTurningBleOff);

                case BREDR_STOP_TIMEOUT -> {
                    errorLog(messageString(msg.what));
                    transitionTo(mTurningBleOff);
                }

                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class TurningBleOff extends BaseAdapterState {
        TurningBleOff(int state) {
            super(state);
        }

        @Override
        public void enter() {
            super.enter();
            infoLog("Stop Timeout Delay: " + BLE_STOP_TIMEOUT_DELAY);
            sendMessageDelayed(BLE_STOP_TIMEOUT, BLE_STOP_TIMEOUT_DELAY);
            mAdapterService.bringDownBle();
        }

        @Override
        public void exit() {
            removeMessages(BLE_STOP_TIMEOUT);
            super.exit();
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case BLE_STOPPED -> handleOff();

                case BLE_STOP_TIMEOUT -> {
                    handleTimeoutOff();
                    errorLog(messageString(msg.what));
                }

                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }

        private void handleOff() {
            if (isDualAdapterMode() &&
                mAdapterService.canDisableNewAdapter()) {
                mPendingOff = true;
                transitionTo(mNewAdapterState);
            } else {
                transitionTo(mOff);
            }
        }

        private void handleTimeoutOff() {
            if (isDualAdapterMode() &&
                !AdapterExt.isOff(AdapterExt.getState())) {
                mPendingOff = true;
                AdapterExt.disable();
                transitionTo(mNewAdapterState);
            } else {
                transitionTo(mOff);
            }
        }
    }

    private class NewAdapterState extends BaseAdapterState {
        private static final String STATE_NAME = "NEW_ADAPTER_STATE";

        NewAdapterState(int state) {
            super(state);
        }

        @Override
        public void enter() {
            infoLog("entered ");
            int state = AdapterExt.getState();
            if (AdapterExt.isOn(state)) {
                handleStateChanged(true);
            } else if (AdapterExt.isOff(state)) {
                handleStateChanged(false);
            }

            if (mPendingOn) {
                sendMessageDelayed(NEW_ADAPTER_ENABLE_TIMEOUT,
                        NEW_ADAPTER_ENABLE_TIMEOUT_DELAY);
            } else if (mPendingOff) {
                sendMessageDelayed(NEW_ADAPTER_DISABLE_TIMEOUT,
                        NEW_ADAPTER_DISABLE_TIMEOUT_DELAY);
            }
        }

        @Override
        public void exit() {
            infoLog("exited ");
            removeMessages(NEW_ADAPTER_ENABLE_TIMEOUT);
            removeMessages(NEW_ADAPTER_DISABLE_TIMEOUT);
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case NEW_ADAPTER_STATE_CHANGED -> handleStateChanged(msg.arg1 == 1);

                case NEW_ADAPTER_ENABLE_TIMEOUT -> {
                    errorLog(messageString(msg.what));
                    handleEnableTimeout();
                }

                case NEW_ADAPTER_DISABLE_TIMEOUT -> {
                    errorLog(messageString(msg.what));
                    handleDisableTimeout();
                }

                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }

        private void continueOn() {
            if (mPendingOn) {
                mPendingOn = false;
                transitionTo(mOn);
            }
        }

        private void continueOff() {
            if (mPendingOff) {
                mPendingOff = false;
                transitionTo(mOff);
            }
        }

        private void handleStateChanged(boolean isOn) {
            infoLog("handleStateChanged isOn: " + String.valueOf(isOn));

            if (isOn) {
                // If default adapter is off, still disable new adapter
                // although it's on. This is to guarantee that new adapter
                // is kept same off state with default adapter.
                if (mPendingOff) {
                    AdapterExt.disable();
                } else {
                    // Both 2 adapter are enabled. Notify On state to upper layer.
                    continueOn();
                }
            } else {
                // Transit to Off state for default adapter, since new
                // adapter is turned off.
                continueOff();
            }
        }

        private void handleEnableTimeout() {
            // Transit to On state for default adapter, although new adapter
            // can't be enabled in timeout.
            continueOn();
        }

        private void handleDisableTimeout() {
            // Transit to Off state for default adapter, although new adapter
            // can't be disabled in timeout.
            continueOff();
        }

        @Override
        void infoLog(String msg) {
            Log.i(TAG + "[adapter_" + mAdapterIndex + "]", STATE_NAME + " : " + msg);
        }

        @Override
        void errorLog(String msg) {
            Log.e(TAG + "[adapter_" + mAdapterIndex + "]", STATE_NAME + " : " + msg);
        }
    }
}
