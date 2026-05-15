/*
 * Copyright (C) 2017 The Android Open Source Project
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
 * ​Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
package android.bluetooth.le;

/**
 * Callback definitions for interacting with Advertiser
 * @hide
 */
oneway interface IAdvertisingSetCallback {
  void onAdvertisingSetStarted(in IBinder advertiseBinder, in int advertiserId, in int tx_power, in int status);
  void onOwnAddressRead(in int advertiserId, in int addressType, in String address);
  void onAdvertisingSetStopped(in int advertiserId);
  void onAdvertisingEnabled(in int advertiserId, in boolean enable, in int status);
  void onAdvertisingDataSet(in int advertiserId, in int status);
  void onScanResponseDataSet(in int advertiserId, in int status);
  void onAdvertisingParametersUpdated(in int advertiserId, in int tx_power, in int status);
  void onPeriodicAdvertisingParametersUpdated(in int advertiserId, in int status);
  void onPeriodicAdvertisingParametersV2Updated(in int advertiserId, in int status);
  void onPeriodicAdvertisingDataSet(in int advertiserId, in int status);
  void onPeriodicAdvertisingSubeventDataSet(in int advertiserId, in int status);
  void onPeriodicAdvertisingEnabled(in int advertiserId, in boolean enable, in int status);
  void onPeriodicAdvertisingSubeventRequest(in int advertiserId, in int subeventStart, in int subeventCount);
  void onPeriodicAdvertisingSubeventResponse(in int advertiserId, in int subevent, in int txStatus,
                            in int numResponses, in byte[] payload);
}
