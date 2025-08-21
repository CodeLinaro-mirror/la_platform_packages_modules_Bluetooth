/*
 * Copyright 2009-2016 The Android Open Source Project
 * Copyright 2015 Samsung LSI
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

package android.bluetooth;

import static android.Manifest.permission.BLUETOOTH_CONNECT;
import static android.Manifest.permission.BLUETOOTH_ADVERTISE;
import static android.Manifest.permission.BLUETOOTH_SCAN;
import static android.Manifest.permission.LOCAL_MAC_ADDRESS;
import static java.util.Objects.requireNonNull;

import android.annotation.BroadcastBehavior;
import android.annotation.Nullable;
import android.annotation.RequiresNoPermission;
import android.annotation.RequiresPermission;
import android.annotation.SdkConstant;
import android.annotation.SdkConstant.SdkConstantType;
import android.annotation.SuppressLint;
import android.bluetooth.annotations.RequiresBluetoothAdvertisePermission;
import android.bluetooth.annotations.RequiresBluetoothConnectPermission;
import android.bluetooth.annotations.RequiresBluetoothScanPermission;
import android.bluetooth.annotations.RequiresLegacyBluetoothPermission;
import android.content.AttributionSource;
import android.os.BluetoothServiceManager;
import android.os.IBinder;
import android.os.RemoteException;
import android.os.ServiceManager;
import android.util.Log;
import java.util.concurrent.TimeoutException;
import java.util.Objects;

/**
 * Represents new local device Bluetooth adapter. The {@link BluetoothAdapterExt}
 * lets you perform fundamental Bluetooth tasks, such as initiate
 * device discovery, query a list of bonded (paired) devices,
 * instantiate a {@link BluetoothDevice} using a known MAC address, and create
 * a {@link BluetoothServerSocket} to listen for connection requests from other
 * devices, and start a scan for Bluetooth LE devices.
 *
 * <p>To get a {@link BluetoothAdapterExt} representing the local Bluetooth
 * adapter, call the {@link BluetoothManager#getAdapter} function on {@link BluetoothManager}.
 * On JELLY_BEAN_MR1 and below you will need to use the static {@link #getDefaultAdapter}
 * method instead.
 * </p><p>
 * Fundamentally, this is your starting point for all
 * Bluetooth actions. Once you have the local adapter, you can get a set of
 * {@link BluetoothDevice} objects representing all paired devices with
 * {@link BluetoothAdapter#getBondedDevices()}; start device discovery with
 * {@link BluetoothAdapter#startDiscovery()}; or create a {@link BluetoothServerSocket} to
 * listen for incoming RFComm connection requests with {@link
 * BluetoothAdapter#listenUsingRfcommWithServiceRecord(String, UUID)}; listen for incoming L2CAP
 * Connection-oriented
 * Channels (CoC) connection requests with {@link BluetoothAdapter#listenUsingL2capChannel()};
 * or start a scan for Bluetooth LE devices with {@link BluetoothAdapter#startLeScan(LeScanCallback callback)}.
 * </p>
 * <p>This class is thread safe.</p>
 * <div class="special reference">
 * <h3>Developer Guides</h3>
 * <p>
 * For more information about using Bluetooth, read the <a href=
 * "{@docRoot}guide/topics/connectivity/bluetooth.html">Bluetooth</a> developer
 * guide.
 * </p>
 * </div>
 *
 * @see BluetoothDevice
 * @see BluetoothServerSocket
 */
@SuppressLint("UnflaggedApi")
public final class BluetoothAdapterExt {
    private static final String TAG = "BluetoothAdapterExt";

    /**
     * Broadcast Action: The state of the local Bluetooth adapter has been
     * changed.
     * <p>For example, Bluetooth has been turned on or off.
     * <p>Always contains the extra fields {@link #EXTRA_STATE} and {@link
     * #EXTRA_PREVIOUS_STATE} containing the new and old states
     * respectively.
     */
    @RequiresLegacyBluetoothPermission
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    @BroadcastBehavior(registeredOnly = true, protectedBroadcast = true)
    public static final String ACTION_STATE_CHANGED =
            "android.bluetooth.adapterext.action.STATE_CHANGED";

    /**
     * Used as an int extra field in {@link #ACTION_STATE_CHANGED}
     * intents to request the current power state. Possible values are:
     * {@link BluetoothAdapter#STATE_OFF},
     * {@link BluetoothAdapter#STATE_TURNING_ON},
     * {@link BluetoothAdapter#STATE_ON},
     * {@link BluetoothAdapter#STATE_TURNING_OFF},
     */
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String EXTRA_STATE = BluetoothAdapter.EXTRA_STATE;
    /**
     * Used as an int extra field in {@link #ACTION_STATE_CHANGED}
     * intents to request the previous power state. Possible values are:
     * {@link BluetoothAdapter#STATE_OFF},
     * {@link BluetoothAdapter#STATE_TURNING_ON},
     * {@link BluetoothAdapter#STATE_ON},
     * {@link BluetoothAdapter#STATE_TURNING_OFF}
     */
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String EXTRA_PREVIOUS_STATE =
            BluetoothAdapter.EXTRA_PREVIOUS_STATE;

   /**
     * Activity Action: Show a system activity that requests discoverable mode.
     * This activity will also request the user to turn on Bluetooth if it
     * is not currently enabled.
     * <p>Discoverable mode is equivalent to {@link
     * BluetoothAdapter#SCAN_MODE_CONNECTABLE_DISCOVERABLE}. It allows remote devices to see
     * this Bluetooth adapter when they perform a discovery.
     * <p>For privacy, Android is not discoverable by default.
     * <p>The sender of this Intent can optionally use extra field {@link
     * #EXTRA_DISCOVERABLE_DURATION} to request the duration of
     * discoverability. Currently the default duration is 120 seconds, and
     * maximum duration is capped at 300 seconds for each request.
     * <p>Notification of the result of this activity is posted using the
     * {@link android.app.Activity#onActivityResult} callback. The
     * <code>resultCode</code>
     * will be the duration (in seconds) of discoverability or
     * {@link android.app.Activity#RESULT_CANCELED} if the user rejected
     * discoverability or an error has occurred.
     * <p>Applications can also listen for {@link BluetoothAdapter#ACTION_SCAN_MODE_CHANGED}
     * for global notification whenever the scan mode changes. For example, an
     * application can be notified when the device has ended discoverability.
     */
    @RequiresLegacyBluetoothPermission
    @RequiresBluetoothAdvertisePermission
    @RequiresPermission(BLUETOOTH_ADVERTISE)
    @SdkConstant(SdkConstantType.ACTIVITY_INTENT_ACTION)
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String ACTION_REQUEST_DISCOVERABLE =
            "android.bluetooth.adapterext.action.REQUEST_DISCOVERABLE";

    /**
     * Used as an optional int extra field in {@link
     * BluetoothAdapter#ACTION_REQUEST_DISCOVERABLE} intents to request a specific duration
     * for discoverability in seconds. The current default is 120 seconds, and
     * requests over 300 seconds will be capped. These values could change.
     */
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String EXTRA_DISCOVERABLE_DURATION =
            BluetoothAdapter.EXTRA_DISCOVERABLE_DURATION;

    /**
     * Activity Action: Show a system activity that allows the user to turn on
     * Bluetooth.
     * <p>This system activity will return once Bluetooth has completed turning
     * on, or the user has decided not to turn Bluetooth on.
     * <p>Notification of the result of this activity is posted using the
     * {@link android.app.Activity#onActivityResult} callback. The
     * <code>resultCode</code>
     * will be {@link android.app.Activity#RESULT_OK} if Bluetooth has been
     * turned on or {@link android.app.Activity#RESULT_CANCELED} if the user
     * has rejected the request or an error has occurred.
     * <p>Applications can also listen for {@link #ACTION_STATE_CHANGED}
     * for global notification whenever Bluetooth is turned on or off.
     */
    @RequiresLegacyBluetoothPermission
    @RequiresBluetoothConnectPermission
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SdkConstant(SdkConstantType.ACTIVITY_INTENT_ACTION)
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String ACTION_REQUEST_ENABLE =
            "android.bluetooth.adapterext.action.REQUEST_ENABLE";

    /**
     * Activity Action: Show a system activity that allows the user to turn off
     * Bluetooth. This is used only if permission review is enabled which is for
     * apps targeting API less than 23 require a permission review before any of
     * the app's components can run.
     * <p>This system activity will return once Bluetooth has completed turning
     * off, or the user has decided not to turn Bluetooth off.
     * <p>Notification of the result of this activity is posted using the
     * {@link android.app.Activity#onActivityResult} callback. The
     * <code>resultCode</code>
     * will be {@link android.app.Activity#RESULT_OK} if Bluetooth has been
     * turned off or {@link android.app.Activity#RESULT_CANCELED} if the user
     * has rejected the request or an error has occurred.
     * <p>Applications can also listen for {@link #ACTION_STATE_CHANGED}
     * for global notification whenever Bluetooth is turned on or off.
     *
     * @hide
     */
    @RequiresLegacyBluetoothPermission
    @RequiresBluetoothConnectPermission
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SdkConstant(SdkConstantType.ACTIVITY_INTENT_ACTION)
    public static final String ACTION_REQUEST_DISABLE =
            "android.bluetooth.adapterext.action.REQUEST_DISABLE";

    /**
     * Activity Action: Show a system activity that allows user to enable BLE scans even when
     * Bluetooth is turned off.<p>
     *
     * Notification of result of this activity is posted using
     * {@link android.app.Activity#onActivityResult}. The <code>resultCode</code> will be
     * {@link android.app.Activity#RESULT_OK} if BLE scan always available setting is turned on or
     * {@link android.app.Activity#RESULT_CANCELED} if the user has rejected the request or an
     * error occurred.
     *
     * @hide
     */
    @SdkConstant(SdkConstantType.ACTIVITY_INTENT_ACTION)
    public static final String ACTION_REQUEST_BLE_SCAN_ALWAYS_AVAILABLE =
            "android.bluetooth.adapterext.action.REQUEST_BLE_SCAN_ALWAYS_AVAILABLE";

    /**
     * Broadcast Action: Indicates the Bluetooth scan mode of the local Adapter
     * has changed.
     * <p>Always contains the extra fields {@link EXTRA_SCAN_MODE} and {@link
     * EXTRA_PREVIOUS_SCAN_MODE} containing the new and old scan modes
     * respectively.
     */
    @RequiresLegacyBluetoothPermission
    @RequiresBluetoothScanPermission
    @RequiresPermission(BLUETOOTH_SCAN)
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    @BroadcastBehavior(registeredOnly = true, protectedBroadcast = true)
    public static final String ACTION_SCAN_MODE_CHANGED =
            "android.bluetooth.adapterext.action.SCAN_MODE_CHANGED";

    /**
     * Used as an int extra field in {@link BluetoothAdapter#ACTION_SCAN_MODE_CHANGED}
     * intents to request the current scan mode. Possible values are:
     * {@link BluetoothAdapter#SCAN_MODE_NONE},
     * {@link BluetoothAdapter#SCAN_MODE_CONNECTABLE},
     * {@link BluetoothAdapter#SCAN_MODE_CONNECTABLE_DISCOVERABLE},
     */
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String EXTRA_SCAN_MODE = BluetoothAdapter.EXTRA_SCAN_MODE;
    /**
     * Used as an int extra field in {@link #ACTION_SCAN_MODE_CHANGED}
     * intents to request the previous scan mode. Possible values are:
     * {@link BluetoothAdapter#SCAN_MODE_NONE},
     * {@link BluetoothAdapter#SCAN_MODE_CONNECTABLE},
     * {@link BluetoothAdapter#SCAN_MODE_CONNECTABLE_DISCOVERABLE},
     */
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String EXTRA_PREVIOUS_SCAN_MODE =
            BluetoothAdapter.EXTRA_PREVIOUS_SCAN_MODE;

    /**
     * Broadcast Action: The local Bluetooth adapter has started the remote
     * device discovery process.
     * <p>This usually involves an inquiry scan of about 12 seconds, followed
     * by a page scan of each new device to retrieve its Bluetooth name.
     * <p>Register for {@link BluetoothDevice#ACTION_FOUND} to be notified as
     * remote Bluetooth devices are found.
     * <p>Device discovery is a heavyweight procedure. New connections to
     * remote Bluetooth devices should not be attempted while discovery is in
     * progress, and existing connections will experience limited bandwidth
     * and high latency. Use {@link BluetoothAdapter#cancelDiscovery()} to cancel an ongoing
     * discovery.
     */
    @RequiresLegacyBluetoothPermission
    @RequiresBluetoothScanPermission
    @RequiresPermission(BLUETOOTH_SCAN)
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    @BroadcastBehavior(registeredOnly = true, protectedBroadcast = true)
    public static final String ACTION_DISCOVERY_STARTED =
            "android.bluetooth.adapterext.action.DISCOVERY_STARTED";
    /**
     * Broadcast Action: The local Bluetooth adapter has finished the device
     * discovery process.
     */
    @RequiresLegacyBluetoothPermission
    @RequiresBluetoothScanPermission
    @RequiresPermission(BLUETOOTH_SCAN)
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    @BroadcastBehavior(registeredOnly = true, protectedBroadcast = true)
    public static final String ACTION_DISCOVERY_FINISHED =
            "android.bluetooth.adapterext.action.DISCOVERY_FINISHED";

    /**
     * Broadcast Action: The local Bluetooth adapter has changed its friendly
     * Bluetooth name.
     * <p>This name is visible to remote Bluetooth devices.
     * <p>Always contains the extra field {@link EXTRA_LOCAL_NAME} containing
     * the name.
     */
    @RequiresLegacyBluetoothPermission
    @RequiresBluetoothConnectPermission
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    @BroadcastBehavior(registeredOnly = true, protectedBroadcast = true)
    public static final String ACTION_LOCAL_NAME_CHANGED =
            "android.bluetooth.adapterext.action.LOCAL_NAME_CHANGED";
    /**
     * Used as a String extra field in {@link ACTION_LOCAL_NAME_CHANGED}
     * intents to request the local Bluetooth name.
     */
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String EXTRA_LOCAL_NAME = BluetoothAdapter.EXTRA_LOCAL_NAME;

    /**
     * Intent used to broadcast the change in connection state of the local
     * Bluetooth adapter to a profile of the remote device. When the adapter is
     * not connected to any profiles of any remote devices and it attempts a
     * connection to a profile this intent will be sent. Once connected, this intent
     * will not be sent for any more connection attempts to any profiles of any
     * remote device. When the adapter disconnects from the last profile its
     * connected to of any remote device, this intent will be sent.
     *
     * <p> This intent is useful for applications that are only concerned about
     * whether the local adapter is connected to any profile of any device and
     * are not really concerned about which profile. For example, an application
     * which displays an icon to display whether Bluetooth is connected or not
     * can use this intent.
     *
     * <p>This intent will have 3 extras:
     * {@link EXTRA_CONNECTION_STATE} - The current connection state.
     * {@link EXTRA_PREVIOUS_CONNECTION_STATE}- The previous connection state.
     * {@link BluetoothDevice#EXTRA_DEVICE} - The remote device.
     *
     * {@link EXTRA_CONNECTION_STATE} or {@link #EXTRA_PREVIOUS_CONNECTION_STATE}
     * can be any of {@link BluetoothAdapter#STATE_DISCONNECTED},
     * {@link BluetoothAdapter#STATE_CONNECTING},
     * {@link BluetoothAdapter#STATE_CONNECTED}, {@link BluetoothAdapter#STATE_DISCONNECTING}.
     */
    @RequiresLegacyBluetoothPermission
    @RequiresBluetoothConnectPermission
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String ACTION_CONNECTION_STATE_CHANGED =
            BluetoothAdapter.ACTION_CONNECTION_STATE_CHANGED;

    /**
     * Extra used by {@link ACTION_CONNECTION_STATE_CHANGED}
     *
     * This extra represents the current connection state.
     */
    @SuppressLint({"UnflaggedApi", "ActionValue"})
    public static final String EXTRA_CONNECTION_STATE =
            BluetoothAdapter.EXTRA_CONNECTION_STATE;

    /**
     * Extra used by {@link ACTION_CONNECTION_STATE_CHANGED}
     *
     * This extra represents the previous connection state.
     * @hide
     */
    public static final String EXTRA_PREVIOUS_CONNECTION_STATE =
            BluetoothAdapter.EXTRA_PREVIOUS_CONNECTION_STATE;

    /**
     * Broadcast Action: The Bluetooth adapter state has changed in LE only mode.
     *
     * @hide
     */
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    public static final String ACTION_BLE_STATE_CHANGED =
            "android.bluetooth.adapterext.action.BLE_STATE_CHANGED";

    /**
     * Intent used to broadcast the change in the Bluetooth address
     * of the local Bluetooth adapter.
     * <p>Always contains the extra field {@link
     * EXTRA_BLUETOOTH_ADDRESS} containing the Bluetooth address.
     *
     * Note: only system level processes are allowed to send this
     * defined broadcast.
     *
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    public static final String ACTION_BLUETOOTH_ADDRESS_CHANGED =
            "android.bluetooth.adapterext.action.BLUETOOTH_ADDRESS_CHANGED";

    /**
     * Used as a String extra field in {@link
     * ACTION_BLUETOOTH_ADDRESS_CHANGED} intent to store the local
     * Bluetooth address.
     *
     * @hide
     */
    public static final String EXTRA_BLUETOOTH_ADDRESS =
            BluetoothAdapter.EXTRA_BLUETOOTH_ADDRESS;

    /**
     * Broadcast Action: The notifys Bluetooth ACL connected event. This will be
     * by BLE Always on enabled application to know the ACL_CONNECTED event
     * when Bluetooth state in STATE_BLE_ON. This denotes GATT connection
     * as Bluetooth LE is the only feature available in STATE_BLE_ON
     *
     * This is counterpart of {@link BluetoothDevice#ACTION_ACL_CONNECTED} which
     * works in Bluetooth state STATE_ON
     *
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    public static final String ACTION_BLE_ACL_CONNECTED =
            "android.bluetooth.adapterext.action.BLE_ACL_CONNECTED";

    /**
     * Broadcast Action: The notifys Bluetooth ACL connected event. This will be
     * by BLE Always on enabled application to know the ACL_DISCONNECTED event
     * when Bluetooth state in STATE_BLE_ON. This denotes GATT disconnection as Bluetooth
     * LE is the only feature available in STATE_BLE_ON
     *
     * This is counterpart of {@link BluetoothDevice#ACTION_ACL_DISCONNECTED} which
     * works in Bluetooth state STATE_ON
     *
     * @hide
     */
    @RequiresBluetoothConnectPermission
    @RequiresPermission(BLUETOOTH_CONNECT)
    @SdkConstant(SdkConstantType.BROADCAST_INTENT_ACTION)
    @SuppressLint("UnflaggedApi")
    public static final String ACTION_BLE_ACL_DISCONNECTED =
            "android.bluetooth.adapterext.action.BLE_ACL_DISCONNECTED";

    /** @hide */
    public static final String BLUETOOTH_MANAGER_SERVICE = "bluetooth_manager_ext";

    /**
     * Lazily initialized singleton. Guaranteed final after first object
     * constructed.
     */
    private static BluetoothAdapterExt sAdapter;

    private BluetoothAdapter mAdapter = null;

    /**
     * Get a handle to the default local Bluetooth adapter.
     * <p>
     * Currently Android only supports one Bluetooth adapter, but the API could
     * be extended to support more. This will always return the default adapter.
     * </p>
     *
     * @return the default local adapter, or null if Bluetooth is not supported
     *         on this hardware platform
     * @deprecated this method will continue to work, but developers are
     *             strongly encouraged to migrate to using
     *             {@link BluetoothManager#getAdapter()}, since that approach
     *             enables support for {@link Context#createAttributionContext}.
     */
    @Deprecated
    @RequiresNoPermission
    @SuppressLint("UnflaggedApi")
    public static synchronized BluetoothAdapterExt getDefaultAdapter() {
        if (sAdapter == null) {
            sAdapter = createAdapter(AttributionSource.myAttributionSource());
        }
        return sAdapter;
    }

    /** @hide */
    @Nullable
    public static BluetoothAdapterExt createAdapter(AttributionSource attributionSource) {
        IBluetoothManager service = getBluetoothManagerExtService();
        if (service != null) {
            return new BluetoothAdapterExt(service, attributionSource);
        } else {
            Log.e(TAG, "Bluetooth service(Ext) is null");
            return null;
        }
    }

    /**
     * Use {@link #getDefaultAdapter} to get the BluetoothAdapterExt instance.
     * @hide
     */
    BluetoothAdapterExt(IBluetoothManager managerExtService, AttributionSource attributionSource) {
        mAdapter = new BluetoothAdapter(managerExtService,
                attributionSource, BluetoothAdapterCommon.ADAPTER_1);
    }

    @Nullable
    @RequiresNoPermission
    @SuppressLint("UnflaggedApi")
    public BluetoothAdapter getBluetoothAdapter() {
        return mAdapter;
    }

    @SuppressLint("UnflaggedApi")
    public static boolean isSupported() {
        return getBluetoothManagerExtService() != null;
    }

    private static IBluetoothManager getBluetoothManagerExtService() {
        BluetoothServiceManager manager =
                BluetoothFrameworkInitializer.getBluetoothServiceManager();
        if (manager == null) {
            Log.e(TAG, "BluetoothServiceManager is null");
            return null;
        }
        IBluetoothManager service = IBluetoothManager.Stub.asInterface(
                manager.getBluetoothManagerServiceRegisterer().getExt());
        return service;
    }
}
