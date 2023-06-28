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

import android.bluetooth.BluetoothAdapter;
import android.os.Looper;
import android.os.Message;
import android.os.SystemProperties;
import android.util.Log;

import com.android.bluetooth.Utils;
import com.android.bluetooth.flags.Flags;
import com.android.internal.util.State;
import com.android.internal.util.StateMachine;

// This state machine handles Bluetooth Adapter State.
// Stable States:
//      {@link OffState}: Initial State
//      {@link BleOnState} : Bluetooth Low Energy, Including GATT, is on
//      {@link OnState} : Bluetooth is on (All supported profiles)
//
// Transition States:
//      {@link TurningBleOnState} : OffState to BleOnState
//      {@link TurningBleOffState} : BleOnState to OffState
//      {@link TurningOnState} : BleOnState to OnState
//      {@link TurningOffState} : OnState to BleOnState
//
//        +------   Off  <-----+
//        |                    |
//        v                    |
// TurningBleOn   TO--->   TurningBleOff
//        |                  ^ ^
//        |                  | |
//        +----->        ----+ |
//                 BleOn       |
//        +------        <---+ O
//        v                  | T
//    TurningOn  TO---->  TurningOff
//        |                    ^
//        |                    |
//        +----->   On   ------+
final class AdapterState extends StateMachine {
    private static final String TAG = Utils.BT_PREFIX + AdapterState.class.getSimpleName();

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

    static final String BLE_START_TIMEOUT_DELAY_PROPERTY = "ro.bluetooth.ble_start_timeout_delay";
    static final String BLE_STOP_TIMEOUT_DELAY_PROPERTY = "ro.bluetooth.ble_stop_timeout_delay";

    static final int BLE_START_TIMEOUT_DELAY =
            4000 * SystemProperties.getInt("ro.hw_timeout_multiplier", 1);
    static final int BLE_STOP_TIMEOUT_DELAY =
            4000 * SystemProperties.getInt("ro.hw_timeout_multiplier", 1);
    static final int BREDR_START_TIMEOUT_DELAY =
            4000 * SystemProperties.getInt("ro.hw_timeout_multiplier", 1);
    static final int BREDR_STOP_TIMEOUT_DELAY =
            4000 * SystemProperties.getInt("ro.hw_timeout_multiplier", 1);
    static final int NEW_ADAPTER_ENABLE_TIMEOUT_DELAY =
            2000 * SystemProperties.getInt("ro.hw_timeout_multiplier", 1);
    static final int NEW_ADAPTER_DISABLE_TIMEOUT_DELAY =
            2000 * SystemProperties.getInt("ro.hw_timeout_multiplier", 1);

    private AdapterService mAdapterService;
    private final TurningOnState mTurningOnState = new TurningOnState();
    private final TurningBleOnState mTurningBleOnState = new TurningBleOnState();
    private final TurningOffState mTurningOffState = new TurningOffState();
    private final TurningBleOffState mTurningBleOffState = new TurningBleOffState();
    private final OnState mOnState = new OnState();
    private final OffState mOffState = new OffState();
    private final BleOnState mBleOnState = new BleOnState();
    private final NewAdapterState mNewAdapterState = new NewAdapterState();

    private int mPrevState = BluetoothAdapter.STATE_OFF;

    private boolean mPendingOn = false;
    private boolean mPendingOff = false;
    private final int mAdapterIndex;

    AdapterState(AdapterService service, Looper looper) {
        super(TAG, looper);
        addState(mOnState);
        addState(mBleOnState);
        addState(mOffState);
        addState(mTurningOnState);
        addState(mTurningOffState);
        addState(mTurningBleOnState);
        addState(mTurningBleOffState);
        if (isDualAdapterMode()) {
            addState(mNewAdapterState);
        }
        mAdapterService = service;
        mAdapterIndex = AdapterUtil.getAdapterIndex();
        setInitialState(mOffState);
        start();
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

    private abstract class BaseAdapterState extends State {

        abstract int getStateValue();

        @Override
        public void enter() {
            int currState = getStateValue();
            infoLog("entered ");
            mAdapterService.updateAdapterState(mPrevState, currState);
            mPrevState = currState;
        }

        void infoLog(String msg) {
            Log.i(TAG + mAdapterIndex, BluetoothAdapter.nameForState(getStateValue()) + " : " + msg);
        }

        void errorLog(String msg) {
            Log.e(TAG + mAdapterIndex, BluetoothAdapter.nameForState(getStateValue()) + " : " + msg);
        }
    }

    private class OffState extends BaseAdapterState {

        @Override
        int getStateValue() {
            return BluetoothAdapter.STATE_OFF;
        }

        @Override
        public void enter() {
            int prevState = mPrevState;
            super.enter();
            if (prevState == BluetoothAdapter.STATE_BLE_TURNING_OFF) {
                mAdapterService.cleanup();
            }
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case BLE_TURN_ON -> transitionTo(mTurningBleOnState);
                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class BleOnState extends BaseAdapterState {

        @Override
        int getStateValue() {
            return BluetoothAdapter.STATE_BLE_ON;
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case USER_TURN_ON -> transitionTo(mTurningOnState);
                case BLE_TURN_OFF -> transitionTo(mTurningBleOffState);
                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class OnState extends BaseAdapterState {

        @Override
        int getStateValue() {
            return BluetoothAdapter.STATE_ON;
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case USER_TURN_OFF -> transitionTo(mTurningOffState);
                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class TurningBleOnState extends BaseAdapterState {

        @Override
        int getStateValue() {
            return BluetoothAdapter.STATE_BLE_TURNING_ON;
        }

        @Override
        public void enter() {
            super.enter();
            final int timeoutDelay =
                    SystemProperties.getInt(
                            BLE_START_TIMEOUT_DELAY_PROPERTY, BLE_START_TIMEOUT_DELAY);
            Log.d(TAG, "Start Timeout Delay: " + timeoutDelay);
            sendMessageDelayed(BLE_START_TIMEOUT, timeoutDelay);
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
                case BLE_STARTED -> transitionTo(mBleOnState);

                case BLE_START_TIMEOUT -> {
                    errorLog(messageString(msg.what));
                    transitionTo(mTurningBleOffState);
                }
                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class TurningOnState extends BaseAdapterState {

        @Override
        int getStateValue() {
            return BluetoothAdapter.STATE_TURNING_ON;
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
                    transitionTo(mTurningOffState);
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
                transitionTo(mOnState);
            }
        }
    }

    private class TurningOffState extends BaseAdapterState {

        @Override
        int getStateValue() {
            return BluetoothAdapter.STATE_TURNING_OFF;
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
            if (Flags.disconnectAclsByBredrDisabled()) {
                if (mAdapterService != null) {
                    Log.i(TAG, "Disconnecting all ACLs with BREDR Stopped");
                    mAdapterService.disconnectAllAcls();
                }
            }

            super.exit();
        }

        @Override
        public boolean processMessage(Message msg) {
            switch (msg.what) {
                case BREDR_STOPPED -> transitionTo(mBleOnState);

                case BREDR_STOP_TIMEOUT -> {
                    errorLog(messageString(msg.what));
                    transitionTo(mTurningBleOffState);
                }

                default -> {
                    infoLog("Unhandled message - " + messageString(msg.what));
                    return false;
                }
            }
            return true;
        }
    }

    private class TurningBleOffState extends BaseAdapterState {

        @Override
        int getStateValue() {
            return BluetoothAdapter.STATE_BLE_TURNING_OFF;
        }

        @Override
        public void enter() {
            super.enter();
            final int timeoutDelay =
                    SystemProperties.getInt(
                            BLE_STOP_TIMEOUT_DELAY_PROPERTY, BLE_STOP_TIMEOUT_DELAY);
            Log.d(TAG, "Stop Timeout Delay: " + timeoutDelay);
            sendMessageDelayed(BLE_STOP_TIMEOUT, timeoutDelay);
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
                transitionTo(mOffState);
            }
        }

        private void handleTimeoutOff() {
            if (isDualAdapterMode() &&
                !AdapterExt.isOff(AdapterExt.getState())) {
                mPendingOff = true;
                AdapterExt.disable();
                transitionTo(mNewAdapterState);
            } else {
                transitionTo(mOffState);
            }
        }
    }

    private class NewAdapterState extends BaseAdapterState {
        private static final String STATE_NAME = "NEW_ADAPTER_STATE";

        @Override
        int getStateValue() {
            // NOT matched with any state in default adapter
            return BluetoothAdapter.ERROR;
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
                transitionTo(mOnState);
            }
        }

        private void continueOff() {
            if (mPendingOff) {
                mPendingOff = false;
                transitionTo(mOffState);
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
            Log.i(TAG + mAdapterIndex, STATE_NAME + " : " + msg);
        }

        @Override
        void errorLog(String msg) {
            Log.e(TAG + mAdapterIndex, STATE_NAME + " : " + msg);
        }
    }
}
