/*
 * Copyright (C) 2024 The Android Open Source Project
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
  * ​​​​​Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries. 
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.channelsoundingtestapp;

import android.bluetooth.BluetoothDevice;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Button;
import androidx.annotation.NonNull;
import androidx.fragment.app.Fragment;
import androidx.fragment.app.FragmentTransaction;
import androidx.lifecycle.ViewModelProvider;
import androidx.navigation.Navigation;
import java.text.DecimalFormat;
import java.util.List;

/** The fragment holds the initiator of channel sounding. */
@SuppressWarnings("SetTextI18n")
public class InitiatorFragment extends Fragment {

  private static final DecimalFormat DISTANCE_DECIMAL_FMT = new DecimalFormat("0.0");

  private BleConnectionViewModel mBleConnectionViewModel;
  private InitiatorViewModel mInitiatorViewModel;
  private LinearLayout mLayoutConnectedDevices;

  @Override
  public View onCreateView(
      @NonNull LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
    View root = inflater.inflate(R.layout.fragment_initiator, container, false);
    
    Fragment bleConnectionFragment = new BleConnectionFragment();
    FragmentTransaction transaction = getChildFragmentManager().beginTransaction();
    transaction.replace(R.id.init_ble_connection_container, bleConnectionFragment).commit();
    
    mLayoutConnectedDevices = root.findViewById(R.id.layout_connected_devices_container);

    return root;
    }

    public void onViewCreated(@NonNull View view, Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);

        // Use requireActivity() to share with BleConnectionFragment and DeviceControlFragment
        mBleConnectionViewModel = new ViewModelProvider(requireActivity()).get(BleConnectionViewModel.class);
        mInitiatorViewModel = new ViewModelProvider(requireActivity()).get(InitiatorViewModel.class);

        mInitiatorViewModel.getAllDistances().observe(getViewLifecycleOwner(), distanceMap -> {
            if (distanceMap != null) {
                updateAllDeviceDistances(distanceMap);
            }
        });

        mBleConnectionViewModel.getConnectedDevices().observe(getViewLifecycleOwner(), devices -> {
            mLayoutConnectedDevices.removeAllViews();
            if (devices != null) {
                for (BluetoothDevice device : devices) {
                    addDeviceRow(device);
                }
            }
            // After adding rows, force an update with the latest known distances.
            updateAllDeviceDistances(mInitiatorViewModel.getAllDistances().getValue());
        });
    }

    private void addDeviceRow(BluetoothDevice device) {
        View rowView = LayoutInflater.from(getContext()).inflate(R.layout.row_connected_device, mLayoutConnectedDevices, false);
        rowView.setTag(device.getAddress()); // Use address as a unique tag for the view.
        TextView deviceAddress = rowView.findViewById(R.id.device_address);
        Button btnOpenControl = rowView.findViewById(R.id.btn_open_control);

        deviceAddress.setText(device.getAddress());
        
        btnOpenControl.setOnClickListener(v -> {
            Bundle bundle = new Bundle();
            bundle.putString("device_address", device.getAddress());
            Navigation.findNavController(requireView()).navigate(R.id.action_InitiatorFragment_to_DeviceControlFragment, bundle);
        });

        mLayoutConnectedDevices.addView(rowView);
    }

    private void updateAllDeviceDistances(java.util.Map<String, Double> distanceMap) {
        if (distanceMap == null) {
            return;
        }
        for (int i = 0; i < mLayoutConnectedDevices.getChildCount(); i++) {
            View rowView = mLayoutConnectedDevices.getChildAt(i);
            String deviceAddress = (String) rowView.getTag();
            TextView distanceText = rowView.findViewById(R.id.distance_text);
            if (deviceAddress != null && distanceText != null && distanceMap.containsKey(deviceAddress)) {
                Double distance = distanceMap.get(deviceAddress);
                if (distance != null) {
                    distanceText.setText(DISTANCE_DECIMAL_FMT.format(distance) + " m");
                }
            }
        }
    }

    @Override
    public void onDestroyView() {
        super.onDestroyView();
    }
}
