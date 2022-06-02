/*
 * Copyright 2017 The Android Open Source Project
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

import android.bluetooth.BluetoothGattService;

/**
 * Callback definitions for interacting with BLE / GATT
 * @hide
 */
oneway interface IBluetoothGattServerCallback {
    void onServerRegistered(in int status, in int serverIf);
    void onServerConnectionState(in int status, in int serverIf,
                                 in boolean connected, in String address);
    void onServiceAdded(in int status, in BluetoothGattService service);
    void onCharacteristicReadRequest(in String address, in int transId, in int offset,
                                     in boolean isLong, in int handle);
    void onDescriptorReadRequest(in String address, in int transId,
                                     in int offset, in boolean isLong,
                                     in int handle);
    void onCharacteristicWriteRequest(in String address, in int transId, in int offset,
                                     in int length, in boolean isPrep, in boolean needRsp,
                                     in int handle, in byte[] value);
    void onDescriptorWriteRequest(in String address, in int transId, in int offset,
                                     in int length, in boolean isPrep, in boolean needRsp,
                                     in int handle, in byte[] value);
    void onExecuteWrite(in String address, in int transId, in boolean execWrite);
    void onNotificationSent(in String address, in int status);
    void onMtuChanged(in String address, in int mtu);
    void onPhyUpdate(in String address, in int txPhy, in int rxPhy, in int status);
    void onPhyRead(in String address, in int txPhy, in int rxPhy, in int status);
    void onConnectionUpdated(in String address, in int interval, in int latency,
                             in int timeout, in int status);
    void onSubrateChange(in String address, in int subrateFactor, in int latency,
                         in int contNum, in int timeout, in int status);
}
