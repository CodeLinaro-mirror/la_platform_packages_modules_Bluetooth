/*
 * Copyright 2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Changes from Qualcomm Innovation Center are provided under the following license.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Caluse-Clear
 *
 */

package android.bluetooth;

import android.annotation.CallbackExecutor;
import android.annotation.IntDef;
import android.annotation.NonNull;
import android.annotation.Nullable;
import android.annotation.RequiresPermission;
import android.annotation.SdkConstant;
import android.annotation.SystemApi;
import android.bluetooth.annotations.RequiresBluetoothConnectPermission;
import android.bluetooth.annotations.RequiresBluetoothLocationPermission;
import android.bluetooth.annotations.RequiresBluetoothScanPermission;
import android.content.AttributionSource;
import android.content.Context;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.CloseGuard;
import android.util.Log;
import android.os.Handler;
import android.os.Looper;

import java.util.IdentityHashMap;

import java.lang.annotation.Retention;
import java.lang.annotation.RetentionPolicy;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.concurrent.Executor;

/**
 * This class provides the public APIs for BLE Angle of Arrival(AoA) receiver role, which
 * implements client side Asset Tracking Profile(ATP).
 *
 * <p>There are two roles for AoA - Locator and Asset Tag.
 * Asset Tag device is an AoA capable BLE transmitter and has one antenna.
 * Locator device device is an AoA capable BLE Receiver and has an antenna array.
 * Locator device acts as GATT client and Asset Tag device acts as GATT server with
 * CTES service.
 *
 * <p>Once a GATT connection is established between Locator and Asset Tag devices, Asset
 * Tag device can be configured by Locator device to transmit direction finding enabled
 * packets using a single antenna.
 *
 * <p>The Locator device which has antenna array and RF switch, captures in-phase and
 * quadrature(IQ) samples. The Locator uses this information to locate the direction of
 * the Asset Tag device.
 *
 * <p>BluetoothLeDirectionFinder is a proxy object for controlling the ATP Locator
 * service via IPC. Use {@link BluetoothAdapter#getProfileProxy} to get the
 * BluetoothLeDirectionFinder proxy object.
 *
 * @hide
 */
public final class BluetoothLeDirectionFinder implements BluetoothProfile {
    private static final String TAG = "BluetoothLeDirectionFinder";
    private static final boolean DBG = true;
    private Map<BluetoothLeDirectionFinderCallback,
            IBluetoothLeDirectionFinderCallback> mAppCallbackWrappers;

    /**
     * Intent used to broadcast the change in connection state of devices via ATP Locator
     * Service.
     *
     * <p>This intent will have 3 extras:
     * <ul>
     * <li> {@link #EXTRA_STATE} - The current state of the profile. </li>
     * <li> {@link #EXTRA_PREVIOUS_STATE}- The previous state of the profile.</li>
     * <li> {@link BluetoothDevice#EXTRA_DEVICE} - The remote device. </li>
     * </ul>
     *
     * <p>{@link #EXTRA_STATE} or {@link #EXTRA_PREVIOUS_STATE} can be any of
     * {@link #STATE_DISCONNECTED}, {@link #STATE_CONNECTING},
     * {@link #STATE_CONNECTED}, {@link #STATE_DISCONNECTING}.
     *
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    @SdkConstant(SdkConstant.SdkConstantType.BROADCAST_INTENT_ACTION)
    public static final String ACTION_CONNECTION_STATE_CHANGED =
            "android.bluetooth.df.profile.action.CONNECTION_STATE_CHANGED";

    private CloseGuard mCloseGuard;
    private Context mContext;
    private BluetoothAdapter mBluetoothAdapter;
    private final AttributionSource mAttributionSource;

    private final BluetoothProfileConnector<IBluetoothLeDirectionFinder> mProfileConnector =
            new BluetoothProfileConnector(this, BluetoothProfile.LE_DIRECTION_FINDING,
                    TAG, IBluetoothLeDirectionFinder.class.getName()) {
                @Override
                public IBluetoothLeDirectionFinder getServiceInterface(IBinder service) {
                    return IBluetoothLeDirectionFinder.Stub.asInterface(service);
                }
            };

    /**
     * Create a new instance of a BLE Direction Finder.
     *
     * @hide
     */
    /*package*/ BluetoothLeDirectionFinder(
            @NonNull Context context, @NonNull ServiceListener listener) {
        mContext = context;
        mBluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        mAttributionSource = mBluetoothAdapter.getAttributionSource();
        mProfileConnector.connect(context, listener);
        mAppCallbackWrappers = new IdentityHashMap<BluetoothLeDirectionFinderCallback,
                IBluetoothLeDirectionFinderCallback>();
        mCloseGuard = new CloseGuard();
        mCloseGuard.open("close");
    }

    /** @hide */
    protected void finalize() {
        if (mCloseGuard != null) {
            mCloseGuard.warnIfOpen();
        }
        close();
    }

    /**
     * @hide
     */
    public void close() {
        mProfileConnector.disconnect();
        mAppCallbackWrappers.clear();
    }

    private IBluetoothLeDirectionFinder getService() {
        return mProfileConnector.getService();
    }

    /**
     * Initiate connection to Bluetooth LE Direction Finder profile of the remote bluetooth device.
     *
     * <p> This API returns false in scenarios like the profile on the
     * device is already connected or Bluetooth is not turned on.
     * When this API returns true, it is guaranteed that
     * connection state intent for the profile will be broadcasted with
     * the state. Users can get the connection state of the profile
     * from this intent.
     *
     * <p>Requires {@link android.Manifest.permission#BLUETOOTH_CONNECT}
     * permission.
     *
     * @param device Remote Bluetooth Device
     * @return false on immediate error, true otherwise
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    public boolean connect(BluetoothDevice device) {
        log("connect(" + device + ")");
        final IBluetoothLeDirectionFinder service = getService();
        try {
            if (service != null && mBluetoothAdapter.isEnabled() && isValidDevice(device)) {
                return service.connect(device, mAttributionSource);
            }
            if (service == null) Log.w(TAG, "Proxy not attached to service");
            return false;
        } catch (RemoteException e) {
            Log.e(TAG, "Stack:" + Log.getStackTraceString(new Throwable()));
            return false;
        }
    }

    /**
     * Initiate disconnection from a profile
     *
     * <p> This API will return false in scenarios like the profile on the
     * Bluetooth device is not in connected state etc. When this API returns,
     * true, it is guaranteed that the connection state change
     * intent will be broadcasted with the state. Users can get the
     * disconnection state of the profile from this intent.
     *
     * <p> If the disconnection is initiated by a remote device, the state
     * will transition from {@link #STATE_CONNECTED} to
     * {@link #STATE_DISCONNECTED}. If the disconnect is initiated by the
     * host (local) device the state will transition from
     * {@link #STATE_CONNECTED} to state {@link #STATE_DISCONNECTING} to
     * state {@link #STATE_DISCONNECTED}. The transition to
     * {@link #STATE_DISCONNECTING} can be used to distinguish between the
     * two scenarios.
     *
     * <p>Requires {@link android.Manifest.permission#BLUETOOTH_ADMIN}
     * permission.
     *
     * @param device Remote Bluetooth Device
     * @return false on immediate error, true otherwise
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    public boolean disconnect(BluetoothDevice device) {
        log("disconnect(" + device + ")");
        final IBluetoothLeDirectionFinder service = getService();
        try {
            if (service != null && mBluetoothAdapter.isEnabled() && isValidDevice(device)) {
                return service.disconnect(device, mAttributionSource);
            }
            if (service == null) Log.w(TAG, "Proxy not attached to service");
            return false;
        } catch (RemoteException e) {
            Log.e(TAG, "Stack:" + Log.getStackTraceString(new Throwable()));
            return false;
        }
    }

    /**
     * {@inheritDoc}
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    @Override
    public @BluetoothProfile.BtProfileState int getConnectionState(@NonNull BluetoothDevice device) {
        log("getConnectionState(" + device + ")");
        Objects.requireNonNull(device, "device cannot be null");
        final IBluetoothLeDirectionFinder service = getService();
        final int defaultValue = BluetoothProfile.STATE_DISCONNECTED;
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled() && isValidDevice(device)) {
            try {
                return service.getConnectionState(device, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return defaultValue;
    }

    /**
     * {@inheritDoc}
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    @Override
    public @NonNull List<BluetoothDevice> getDevicesMatchingConnectionStates(
            @NonNull int[] states) {
        log("getDevicesMatchingConnectionStates()");
        Objects.requireNonNull(states, "states cannot be null");
        final IBluetoothLeDirectionFinder service = getService();
        final List<BluetoothDevice> defaultValue = new ArrayList<BluetoothDevice>();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                return service.getDevicesMatchingConnectionStates(states, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return defaultValue;
    }

    /**
     * {@inheritDoc}
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    @Override
    public @NonNull List<BluetoothDevice> getConnectedDevices() {
        log("getConnectedDevices()");
        final IBluetoothLeDirectionFinder service = getService();
        final List<BluetoothDevice> defaultValue = new ArrayList<BluetoothDevice>();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                return service.getConnectedDevices(mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return defaultValue;
    }

    /**
     * Set connection policy of the profile.
     *
     * <p> The device should already be paired. Connection policy can be one of {
     * @link #CONNECTION_POLICY_ALLOWED}, {@link #CONNECTION_POLICY_FORBIDDEN},
     * {@link #CONNECTION_POLICY_UNKNOWN}
     *
     * @param device Paired bluetooth device
     * @param connectionPolicy is the connection policy to set to for this profile
     * @return true if connectionPolicy is set, false on error
     * @throws NullPointerException if <var>device</var> is null
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    public boolean setConnectionPolicy(@NonNull BluetoothDevice device,
            @ConnectionPolicy int connectionPolicy) {
        log("setConnectionPolicy()");
        Objects.requireNonNull(device, "device cannot be null");
        final IBluetoothLeDirectionFinder service = getService();
        final boolean defaultValue = false;
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled() && isValidDevice(device)
                    && (connectionPolicy == BluetoothProfile.CONNECTION_POLICY_FORBIDDEN
                            || connectionPolicy == BluetoothProfile.CONNECTION_POLICY_ALLOWED)) {
            try {
                return service.setConnectionPolicy(device, connectionPolicy, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return defaultValue;
    }

    /**
     * Get the connection policy of the profile.
     *
     * <p> The connection policy can be any of:
     * {@link #CONNECTION_POLICY_ALLOWED}, {@link #CONNECTION_POLICY_FORBIDDEN},
     * {@link #CONNECTION_POLICY_UNKNOWN}
     *
     * @param device Bluetooth device
     * @return connection policy of the device
     * @throws NullPointerException if <var>device</var> is null
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    public @ConnectionPolicy int getConnectionPolicy(@NonNull BluetoothDevice device) {
        log("getConnectionPolicy()");
        Objects.requireNonNull(device, "device cannot be null");
        final IBluetoothLeDirectionFinder service = getService();
        final int defaultValue = BluetoothProfile.CONNECTION_POLICY_FORBIDDEN;
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled() && isValidDevice(device)) {
            try {
                return service.getConnectionPolicy(device, mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
        return defaultValue;
    }

    private IBluetoothLeDirectionFinderCallback wrap(BluetoothLeDirectionFinderCallback callback,
            Handler handler) {
        return new IBluetoothLeDirectionFinderCallback.Stub() {
            public void onEnableBleDirectionFinding(BluetoothDevice device, int status) {
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        log("calling onEnableBleDirectionFinding with " +
                                            "status:" + status);
                        callback.onEnableBleDirectionFinding(device, status);
                    }
                 });
            }

            public void onLeAoaResults(BluetoothDevice device, int status, double azimuth,
                    int azimuthUnc, double elevation, int elevationUnc) {
                handler.post(new Runnable() {
                    @Override
                    public void run() {
                        log("calling onLeAoaResults with " +
                             "status:" + status + " azimuth:" + azimuth
                             + " elevation:" + elevation);
                        callback.onLeAoaResults(device, status, azimuth,
                                azimuthUnc, elevation, elevationUnc);
                    }
                 });
            }
        };
    }

    public void registerAppCallback(BluetoothDevice device, BluetoothLeDirectionFinderCallback appCallback) {
        log("registerAppCallback device :" + device + "appCB: " + appCallback);
        Handler handler = new Handler(Looper.getMainLooper());

        IBluetoothLeDirectionFinderCallback wrapped = wrap(appCallback, handler);
        final IBluetoothLeDirectionFinder service = getService();
        try {
          if (service != null && mBluetoothAdapter.isEnabled()
                  && isValidDevice(device)) {
             service.registerAppCallback(device, wrapped, mAttributionSource);
             if (mAppCallbackWrappers != null) {
                 mAppCallbackWrappers.put(appCallback, wrapped);
             }
          }
          if (service == null) {
              Log.w(TAG, "Proxy not attached to service");
              return;
          }
        } catch (RemoteException e) {
          Log.e(TAG, "Stack:" + Log.getStackTraceString(new Throwable()));
          return;
        }
   }

   public void unregisterAppCallback(BluetoothDevice device, BluetoothLeDirectionFinderCallback appCallback) {
       log("unregisterAppCallback: device" + device + "appCB:" + appCallback);

       final IBluetoothLeDirectionFinder service = getService();
       IBluetoothLeDirectionFinderCallback cb = mAppCallbackWrappers.get(device);
       try {
          if (service != null && mBluetoothAdapter.isEnabled()
                  && isValidDevice(device)) {
              service.unregisterAppCallback(device, cb, mAttributionSource);
              mAppCallbackWrappers.remove(appCallback);
              return;
          }
          if (service == null) Log.w(TAG, "Proxy not attached to service");
          return;
        } catch (RemoteException e) {
          Log.e(TAG, "Stack:" + Log.getStackTraceString(new Throwable()));
          return;
        }
   }


    /**
     * Enable Ble Direction Finding
     *
     * Enables Direction Finder Receiver to either enable or disable receiving IQ sample report
     * On success, {@link Callback#onEnableBleDirectionFinding(int)} will be called with reason code
     * {@link BluetoothStatusCodes#REASON_LOCAL_APP_REQUEST}.
     *
     * On failure, {@link Callback#onEnableBleDirectionFinding(int)} will be called with reason code
     *
     * The implementation will send GATT write to CTE enable characteristic with Direction Finding
     * bit set and will then send 2 HCI commands to set Conn CTE receive params and CTE request
     * enable before passing the callback to the application using {@link
     * Callback#onEnableBleDirectionFinding(reason)}.
     *
     * @param device BluetoothDevice of Asset Tag device
     * @param samplingEnable Sampling Enable value to be used in
     *                       HCI LE Set Conn CTE receive params command
     * @param slotDurations Slot durations to be used in
     *                      HCI LE Set Conn CTE receive params command
     * @param enable Enable or Disable CTE Request to be used in HCI LE Connection
     *               CTE Request enable command
     * @param cteReqInt CTE Request Interval to be used in HCI LE Connection
     *                  CTE Request enable command
     * @param reqCteLen Requested CTE Length to be used in HCI LE Connection
     *                  CTE Request enable command
     * @param reqCteType Requested CTE type to be used in HCI LE Connection
     *                   CTE Request enable command
     *
     * @throws IllegalStateException when no callback is registered
     * @hide
     */
   @RequiresBluetoothConnectPermission
   @RequiresPermission(android.Manifest.permission.BLUETOOTH_CONNECT)
    public void enableBleDirectionFinding(BluetoothDevice device, int samplingEnable,
            int slotDurations, int enable, int cteReqInt, int reqCteLen, int dirFindingType) {
        log("enableBleDirectionFinding");
        final IBluetoothLeDirectionFinder service = getService();
        if (service == null) {
            Log.w(TAG, "Proxy not attached to service");
            if (DBG) log(Log.getStackTraceString(new Throwable()));
        } else if (mBluetoothAdapter.isEnabled()) {
            try {
                service.enableBleDirectionFinding(device, samplingEnable, slotDurations,
                        enable, cteReqInt, reqCteLen, dirFindingType,
                        mAttributionSource);
            } catch (RemoteException e) {
                Log.e(TAG, e.toString() + "\n" + Log.getStackTraceString(new Throwable()));
            }
        }
    }

    private static void log(@NonNull String msg) {
        if (DBG) {
            Log.d(TAG, msg);
        }
    }

    private static boolean isValidDevice(@Nullable BluetoothDevice device) {
        return device != null && BluetoothAdapter
                .checkBluetoothAddress(device.getAddress());
    }
}
