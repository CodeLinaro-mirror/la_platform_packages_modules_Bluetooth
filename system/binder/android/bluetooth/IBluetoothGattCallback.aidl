/*
 * Copyright 2013 The Android Open Source Project
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
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *
 * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE
 *
 */
package android.bluetooth;

import android.os.ParcelUuid;
import android.bluetooth.BluetoothGattService;

/**
 * Callback definitions for interacting with BLE / GATT
 * @hide
 */
oneway interface IBluetoothGattCallback {
    void onClientRegistered(in int status, in int clientIf);
    void onClientConnectionState(in int status, in int clientIf,
                                 in boolean connected, in String address);
    void onPhyUpdate(in String address, in int txPhy, in int rxPhy, in int status);
    void onPhyRead(in String address, in int txPhy, in int rxPhy, in int status);
    void onSearchComplete(in String address, in List<BluetoothGattService> services, in int status);
    void onCharacteristicRead(in String address, in int status, in int handle, in byte[] value);
    void onCharacteristicWrite(in String address, in int status, in int handle, in byte[] value);
    void onExecuteWrite(in String address, in int status);
    void onDescriptorRead(in String address, in int status, in int handle, in byte[] value);
    void onDescriptorWrite(in String address, in int status, in int handle, in byte[] value);
    void onNotify(in String address, in int handle, in byte[] value);
    void onReadRemoteRssi(in String address, in int rssi, in int status);
    void onConfigureMTU(in String address, in int mtu, in int status);
    void onConnectionUpdated(in String address, in int interval, in int latency,
                             in int timeout, in int status);
    void onServiceChanged(in String address);
    void onSubrateChange(in String address, in int subrateFactor, in int latency,
                         in int contNum, in int timeout, in int status);
}
