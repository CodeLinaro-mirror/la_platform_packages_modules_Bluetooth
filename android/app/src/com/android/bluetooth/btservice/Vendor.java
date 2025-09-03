/******************************************************************************
 * Copyright (C) 2016-2017 The Linux Foundation. All rights reserved
 * Not a Contribution.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 * * Neither the name of The Linux Foundation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
 ******************************************************************************/

/******************************************************************************
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 *
 ******************************************************************************/

package com.android.bluetooth.btservice;

import android.util.Log;

import static android.Manifest.permission.BLUETOOTH_CONNECT;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothStatusCodes;

import com.android.bluetooth.Utils;

import android.os.SystemProperties;
import android.content.BroadcastReceiver;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.net.wifi.SoftApConfiguration;
import android.net.wifi.SupplicantState;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.Context;

import java.util.UUID;

final class Vendor {
    private static final String TAG = "BluetoothVendorService";
    private final AdapterService mService;
    private String socName;
    private String a2dpOffloadCap;
    private boolean isPowerBackoffEnabled = false;
    private boolean isPowerBackoffSupported = false;
    // Split A2dp will be enabled by default
    private boolean splitA2dpEnabled = true;
    // SWB will be enabled by default
    private boolean isSwbEnabled = true;
    // SWB-PM will be enabled by default
    private boolean isSwbPmEnabled = true;

    public static final int SOFT_AP_BAND_DUAL = 1 << 3;

    static final int BT_VENDOR_PROPERTY_HOST_ADD_ON_FEATURES = 0x01;
    static final int BT_VENDOR_PROPERTY_SOC_ADD_ON_FEATURES = 0x02;

    // Vendor Add On features
    private boolean mWiPowerFastbootEnabled;
    private boolean mSplitA2DPScrambleDataRequired;
    private boolean mSplitA2DP44p1KhzSampleFreq;
    private boolean mSplitA2DP48KhzSampleFreq;
    private boolean mSplitA2DPSingleVSCommandSupport;
    private boolean mSplitA2DPSourceSBCEncoding;
    private boolean mSplitA2DPSourceSBC;
    private boolean mSplitA2DPSourceMP3;
    private boolean mSplitA2DPSourceAAC;
    private boolean mSplitA2DPSourceLDAC;
    private boolean mSplitA2DPSourceAPTX;
    private boolean mSplitA2DPSourceAPTXHD;
    private boolean mSplitA2DPSourceAPTXADAPTIVE;
    private boolean mSplitA2DPSourceAPTXTWSPLUS;
    private boolean mSplitA2DPSinkSBC;
    private boolean mSplitA2DPSinkMP3;
    private boolean mSplitA2DPSinkAAC;
    private boolean mSplitA2DPSinkLDAC;
    private boolean mSplitA2DPSinkAPTX;
    private boolean mSplitA2DPSinkAPTXHD;
    private boolean mSplitA2DPSinkAPTXADAPTIVE;
    private boolean mSplitA2DPSinkAPTXTWSPLUS;
    private boolean mVoiceDualSCO;
    private boolean mVoiceTWSPLUSeSCOAG;
    private boolean mSWBVoicewithAptxAdaptiveAG;
    private boolean mSplitA2DPSourceAACABR;
    private boolean mSplitA2DPSourceTxSplitAPTXADAPTIVE;
    private boolean mBroadcastAudioTxwithEC_2_5;
    private boolean mBroadcastAudioTxwithEC_3_9;
    private boolean mBroadcastAudioRxwithEC_2_5;
    private boolean mBroadcastAudioRxwithEC_3_9;
    private boolean mISOCIGParameterCalculator;
    private boolean mAddonFeaturesSupported;
    private boolean mHostAdvAudioUnicastFeatureSupported;
    private boolean mHostAdvAudioBCAFeatureSupported;
    private boolean mHostAdvAudioBCSFeatureSupported;
    private boolean mHostAdvAudioStereoRecordingFeatureSupported;
    private boolean mHostAdvAudioLC3QFeatureSupported;
    private boolean mHostQHSFeatureSupported;
    private boolean mHostAddonFeaturesSupported;

    static {
        classInitNative();
    }

    public Vendor(AdapterService service) {
        mService = service;
    }

    private final BroadcastReceiver mWifiStateBroadcastReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = (intent != null) ? intent.getAction() : null;

            if (action == null) return;
            if ((action.equals(WifiManager.WIFI_STATE_CHANGED_ACTION) ||
                (action.equals(WifiManager.WIFI_AP_STATE_CHANGED_ACTION)))) {
                if (isPowerBackOffRequired()) {
                    setPowerBackoff(true);
                } else {
                    setPowerBackoff(false);
                }
            }
        }
    };

    public void init() {
        initNative();
        socName = SystemProperties.get("persist.vendor.qcom.bluetooth.soc");
        Log.d(TAG, "socName: " + socName);
        a2dpOffloadCap = SystemProperties.get("persist.vendor.qcom.bluetooth.a2dp_offload_cap");
        Log.d(TAG, "a2dpOffloadCap: " + a2dpOffloadCap);
        splitA2dpEnabled =
                SystemProperties.getBoolean("persist.vendor.qcom.bluetooth.enable.splita2dp", true);
        Log.d(TAG, "splitA2dpEnabled: " + splitA2dpEnabled);
        isSwbEnabled =
                SystemProperties.getBoolean("persist.vendor.qcom.bluetooth.enable.swb", true);
        Log.d(TAG, "isSwbEnabled: " + isSwbEnabled);
        isSwbPmEnabled =
                SystemProperties.getBoolean("persist.vendor.qcom.bluetooth.enable.swbpm", true);
        Log.d(TAG, "isSwbPmEnabled: " + isSwbPmEnabled);

        String max_power_support =
                SystemProperties.get("persist.vendor.qcom.bluetooth.max_power_support");
        Log.d(TAG, "max_power_support: " + max_power_support);
        if (!max_power_support.isEmpty()) {
            isPowerBackoffSupported = true;
            isPowerBackoffEnabled = false;
        }

    }

    private boolean isPowerBackOffRequired() {
        try {

            WifiManager mWifiManager = (WifiManager) (
                    mService.getApplicationContext().getSystemService(Context.WIFI_SERVICE));
            final SoftApConfiguration config = mWifiManager.getSoftApConfiguration();

            if (config != null)
                Log.d(TAG, "Soft AP is on band: " + config.getBand());
            if ((mWifiManager != null) && ((mWifiManager.isWifiEnabled() ||
                ((mWifiManager.getWifiApState() == WifiManager.WIFI_AP_STATE_ENABLED) &&
                    (config != null) &&
                    (((config.getBand() & SoftApConfiguration.BAND_5GHZ) != 0) ||
                        ((config.getBand() & SoftApConfiguration.BAND_6GHZ) != 0) ||
                        ((config.getBand() & SOFT_AP_BAND_DUAL) != 0)))))) {
                return true;
            }
            return false;
        } catch (SecurityException e) {
            Log.e(TAG, e.toString());
        }
        return false;
    }

    public void setWifiState(boolean status) {
        Log.d(TAG, "setWifiState to: " + status);
        setWifiStateNative(status);
    }

    public void setPowerBackoff(boolean status) {
        Log.d(TAG, "setPowerBackoff status: " + status
            + " isPowerBackoffEnabled: " + isPowerBackoffEnabled);
        if (isPowerBackoffEnabled != status) {
            isPowerBackoffEnabled = status;
            setPowerBackoffNative(status);
        }
    }

    public void cleanup() {
        if (isPowerBackoffSupported) {
            mService.getApplicationContext().unregisterReceiver(mWifiStateBroadcastReceiver);
            isPowerBackoffSupported = false;
            isPowerBackoffEnabled = false;
        }
        cleanupNative();
    }

    void ssr_cleanup_callback() {
        Log.e(TAG,"ssr_cleanup_callback");
        mService.ssrCleanupCallback();
    }

    void adapterPropertyChangedCallback(int[] types, byte[][] values) {
        byte[] val;
        int type;

        if (types.length <= 0) {
            Log.e(TAG, "No properties to update");
            return;
        }

        for (int j = 0; j < types.length; j++) {
            type = types[j];
            val = values[j];
            Log.d(TAG, "Property type: " + type);
            switch (type) {
                case BT_VENDOR_PROPERTY_HOST_ADD_ON_FEATURES:
                    updateHostFeatureSupport(val);
                    break;
                case BT_VENDOR_PROPERTY_SOC_ADD_ON_FEATURES:
                    updateSocFeatureSupport(val);
                    if (isPowerBackoffSupported) {
                        IntentFilter wifiFilter = new IntentFilter();

                        wifiFilter.addAction(WifiManager.WIFI_AP_STATE_CHANGED_ACTION);
                        wifiFilter.addAction(WifiManager.WIFI_STATE_CHANGED_ACTION);
                        mService.getApplicationContext()
                                .registerReceiver(mWifiStateBroadcastReceiver, wifiFilter);
                        if (isPowerBackOffRequired()) {
                            setPowerBackoff(true);
                        }
                    }
                    break;
            }
        }
    }

    public String getSocName() {
        return socName;
    }

    public String getA2apOffloadCapability() {
        return a2dpOffloadCap;
    }

    public boolean isSplitA2dpEnabled() {
        return splitA2dpEnabled;
    }

    public boolean isSwbEnabled() {
        return isSwbEnabled;
    }

    public boolean isSwbPmEnabled() {
        return isSwbPmEnabled;
    }

    public void informTimeoutToHidl() {
        informTimeoutToHidlNative();
    }

    /**
     * @return Wipower Fastboot status
     */
    public boolean isWipowerFastbootEnabled() {
        return mWiPowerFastbootEnabled;
    }

    /**
     * @return Split A2DP Scramble Data Support status
     */
    public boolean isSplitA2DPScrambleDataRequired() {
        return mSplitA2DPScrambleDataRequired;
    }

    /**
     * @return Split A2DP 44.1Khz Sample Freq status
     */
    public boolean isSplitA2DP44p1KhzSampleFreq() {
        return mSplitA2DP44p1KhzSampleFreq;
    }

    /**
     * @return Split A2DP 48Khz Sample Freq status
     */
    public boolean isSplitA2DP48KhzSampleFreq() {
        return mSplitA2DP48KhzSampleFreq;
    }

    /**
     * @return Split A2DP Single VS Command Support status
     */
    public boolean isSplitA2DPSingleVSCommandSupport() {
        return mSplitA2DPSingleVSCommandSupport;
    }

    /**
     * @return Split A2DP Source SBC Encoding status
     */
    public boolean isSplitA2DPSourceSBCEncoding() {
        return mSplitA2DPSourceSBCEncoding;
    }

    /**
     * @return Split A2DP Source SBC status
     */
    public boolean isSplitA2DPSourceSBC() {
        return mSplitA2DPSourceSBC;
    }

    /**
     * @return Split A2DP Source MP3 status
     */
    public boolean isSplitA2DPSourceMP3() {
        return mSplitA2DPSourceMP3;
    }

    /**
     * @return Split A2DP Source AAC status
     */
    public boolean isSplitA2DPSourceAAC() {
        return mSplitA2DPSourceAAC;
    }

    /**
     * @return Split A2DP Source LDAC status
     */
    public boolean isSplitA2DPSourceLDAC() {
        return mSplitA2DPSourceLDAC;
    }

    /**
     * @return Split A2DP Source APTX status
     */
    public boolean isSplitA2DPSourceAPTX() {
        return mSplitA2DPSourceAPTX;
    }

    /**
     * @return Split A2DP Source APTXHD status
     */
    public boolean isSplitA2DPSourceAPTXHD() {
        return mSplitA2DPSourceAPTXHD;
    }

    /**
     * @return Split A2DP Source APTXADAPTIVE status
     */
    public boolean isSplitA2DPSourceAPTXADAPTIVE() {
        return mSplitA2DPSourceAPTXADAPTIVE;
    }

    /**
     * @return Split A2DP Source APTXTWSPLUS status
     */
    public boolean isSplitA2DPSourceAPTXTWSPLUS() {
        return mSplitA2DPSourceAPTXTWSPLUS;
    }

    /**
     * @return Split A2DP Sink SBC status
     */
    public boolean isSplitA2DPSinkSBC() {
        return mSplitA2DPSinkSBC;
    }

    /**
     * @return Split A2DP Sink MP3 status
     */
    public boolean isSplitA2DPSinkMP3() {
        return mSplitA2DPSinkMP3;
    }

    /**
     * @return Split A2DP Sink AAC status
     */
    public boolean isSplitA2DPSinkAAC() {
        return mSplitA2DPSinkAAC;
    }

    /**
     * @return Split A2DP Sink LDAC status
     */
    public boolean isSplitA2DPSinkLDAC() {
        return mSplitA2DPSinkLDAC;
    }

    /**
     * @return Split A2DP Sink APTX status
     */
    public boolean isSplitA2DPSinkAPTX() {
        return mSplitA2DPSinkAPTX;
    }

    /**
     * @return Split A2DP Sink APTXHD status
     */
    public boolean isSplitA2DPSinkAPTXHD() {
        return mSplitA2DPSinkAPTXHD;
    }

    /**
     * @return Split A2DP Sink APTXADAPTIVE status
     */
    public boolean isSplitA2DPSinkAPTXADAPTIVE() {
        return mSplitA2DPSinkAPTXADAPTIVE;
    }

    /**
     * @return Split A2DP Sink APTXTWSPLUS status
     */
    public boolean isSplitA2DPSinkAPTXTWSPLUS() {
        return mSplitA2DPSinkAPTXTWSPLUS;
    }

    /**
     * @return mVoiceDualSCO status
     */
    public boolean isVoiceDualSCO() {
        return mVoiceDualSCO;
    }

    /**
     * @return Voice TWS+ eSCO AG status
     */
    public boolean isVoiceTWSPLUSeSCOAG() {
        return mVoiceTWSPLUSeSCOAG;
    }

    /**
     * @return SWB Voice with Aptx Adaptive AG status
     */
    public boolean isSWBVoicewithAptxAdaptiveAG() {
        return mSWBVoicewithAptxAdaptiveAG;
    }

    /**
     * @return Split A2DP Source AAC ABR status
     */
    public boolean isSplitA2DPSourceAACABR() {
        return mSplitA2DPSourceAACABR;
    }

    /**
     * @return Split A2DP Source Tx-Split APTX ADAPTIVE status
     */
    public boolean isSplitA2DPSourceTxSplitAPTXADAPTIVE() {
        return mSplitA2DPSourceTxSplitAPTXADAPTIVE;
    }

    /**
     * @return Broadcast Audio Tx with EC_2_5 status
     */
    public boolean isBroadcastAudioTxwithEC_2_5() {
        return mBroadcastAudioTxwithEC_2_5;
    }

    /**
     * @return Broadcast Audio Tx with EC_3_9 status
     */
    public boolean isBroadcastAudioTxwithEC_3_9() {
        return mBroadcastAudioTxwithEC_3_9;
    }

    /**
     * @return Broadcast Audio Rx with EC_2_5 status
     */
    public boolean isBroadcastAudioRxwithEC_2_5() {
        return mBroadcastAudioRxwithEC_2_5;
    }

    /**
     * @return Broadcast Audio Rx with EC_3_9 status
     */
    public boolean isBroadcastAudioRxwithEC_3_9() {
        return mBroadcastAudioRxwithEC_3_9;
    }

    /**
     * @return ISO CIG Parameter Calculator status
     */
    public boolean isISOCIGParameterCalculator() {
        return mISOCIGParameterCalculator;
    }

    /**
     * @return Broadcast AddonFeatures Cmd Support status
     */
    public boolean isAddonFeaturesCmdSupported() {
        return mAddonFeaturesSupported;
    }

    /**
     * @return Host Adv Audio Unicast feature supported
     */
    public boolean isHostAdvAudioUnicastFeatureSupported() {
        return mHostAdvAudioUnicastFeatureSupported;
    }

    /**
     * @return Host Adv Audio BCA feature supported
     */
    public boolean isHostAdvAudioBCAFeatureSupported() {
        return mHostAdvAudioBCAFeatureSupported;
    }

    /**
     * @return Host Adv Audio BCS feature supported
     */
    public boolean isHostAdvAudioBCSFeatureSupported() {
        return mHostAdvAudioBCSFeatureSupported;
    }

    /**
     * @return Host Adv Audio StereoRecording feature supported
     */
    public boolean isHostAdvAudioStereoRecordingFeatureSupported() {
        return mHostAdvAudioStereoRecordingFeatureSupported;
    }

    /**
     * @return Host Adv Audio LC3Q feature supported
     */
    public boolean isHostAdvAudioLC3QFeatureSupported() {
        return mHostAdvAudioLC3QFeatureSupported;
    }

    /**
     * @return Host QHS feature supported
     */
    public boolean isHostQHSFeatureSupported() {
        return mHostQHSFeatureSupported;
    }

    /**
     * @return Host AddonFeatures Support status
     */
    public boolean isHostAddonFeaturesSupported() {
        return mHostAddonFeaturesSupported;
    }

    void updateSocFeatureSupport(byte[] val) {
        mAddonFeaturesSupported = (val.length != 0);
        if (!mAddonFeaturesSupported) {
            Log.d(TAG, "BT_PROPERTY_ADD_ON_FEATURES: add-on features VSC is not supported");
        } else {
            mWiPowerFastbootEnabled = ((0x01 & ((int) val[0])) != 0);
            mSplitA2DPScrambleDataRequired = ((0x02 & ((int) val[0])) != 0);
            mSplitA2DP44p1KhzSampleFreq = ((0x04 & ((int) val[0])) != 0);
            mSplitA2DP48KhzSampleFreq = ((0x08 & ((int) val[0])) != 0);
            mSplitA2DPSingleVSCommandSupport = ((0x10 & ((int) val[0])) != 0);
            mSplitA2DPSourceSBCEncoding = ((0x20 & ((int) val[0])) != 0);
            mSplitA2DPSourceSBC = ((0x01 & ((int) val[1])) != 0);
            mSplitA2DPSourceMP3 = ((0x02 & ((int) val[1])) != 0);
            mSplitA2DPSourceAAC = ((0x04 & ((int) val[1])) != 0);
            mSplitA2DPSourceLDAC = ((0x08 & ((int) val[1])) != 0);
            mSplitA2DPSourceAPTX = ((0x10 & ((int) val[1])) != 0);
            mSplitA2DPSourceAPTXHD = ((0x20 & ((int) val[1])) != 0);
            mSplitA2DPSourceAPTXADAPTIVE = ((0x40 & ((int) val[1])) != 0);
            mSplitA2DPSourceAPTXTWSPLUS = ((0x80 & ((int) val[1])) != 0);
            mSplitA2DPSinkSBC = ((0x01 & ((int) val[2])) != 0);
            mSplitA2DPSinkMP3 = ((0x02 & ((int) val[2])) != 0);
            mSplitA2DPSinkAAC = ((0x04 & ((int) val[2])) != 0);
            mSplitA2DPSinkLDAC = ((0x08 & ((int) val[2])) != 0);
            mSplitA2DPSinkAPTX = ((0x10 & ((int) val[2])) != 0);
            mSplitA2DPSinkAPTXHD = ((0x20 & ((int) val[2])) != 0);
            mSplitA2DPSinkAPTXADAPTIVE = ((0x40 & ((int) val[2])) != 0);
            mSplitA2DPSinkAPTXTWSPLUS = ((0x80 & ((int) val[2])) != 0);
            mVoiceDualSCO = ((0x01 & ((int) val[3])) != 0);
            mVoiceTWSPLUSeSCOAG = ((0x02 & ((int) val[3])) != 0);
            mSWBVoicewithAptxAdaptiveAG = ((0x04 & ((int) val[3])) != 0);
            mSplitA2DPSourceAACABR = ((0x40 & ((int) val[3])) != 0);
            mSplitA2DPSourceTxSplitAPTXADAPTIVE = ((0x80 & ((int) val[3])) != 0);
            mBroadcastAudioTxwithEC_2_5 = ((0x01 & ((int) val[4])) != 0);
            mBroadcastAudioTxwithEC_3_9 = ((0x02 & ((int) val[4])) != 0);
            mBroadcastAudioRxwithEC_2_5 = ((0x04 & ((int) val[4])) != 0);
            mBroadcastAudioRxwithEC_3_9 = ((0x08 & ((int) val[4])) != 0);
            mISOCIGParameterCalculator = ((0x10 & ((int) val[4])) != 0);

            Log.d(TAG, "BT_PROPERTY_ADD_ON_FEATURES: update from BT controller"
                + "\n mWiPowerFastbootEnabled = "
                + mWiPowerFastbootEnabled + "\n SplitA2DPScrambleDataRequired = "
                + mSplitA2DPScrambleDataRequired + "\n mSplitA2DP44p1KhzSampleFreq = "
                + mSplitA2DP44p1KhzSampleFreq + "\n mSplitA2DP48KhzSampleFreq = "
                + mSplitA2DP48KhzSampleFreq + "\n mSplitA2DPSingleVSCommandSupport = "
                + mSplitA2DPSingleVSCommandSupport + "\n mSplitA2DPSourceSBCEncoding = "
                + mSplitA2DPSourceSBCEncoding + "\n mSplitA2DPSourceSBC = "
                + mSplitA2DPSourceSBC + "\n mSplitA2DPSourceMP3 = "
                + mSplitA2DPSourceMP3 + "\n mSplitA2DPSourceAAC = "
                + mSplitA2DPSourceAAC + "\n mSplitA2DPSourceLDAC = " + mSplitA2DPSourceLDAC
                + "\n mSplitA2DPSourceAPTX = " + mSplitA2DPSourceAPTX
                + "\n mSplitA2DPSourceAPTXHD = " + mSplitA2DPSourceAPTXHD
                + "\n mSplitA2DPSourceAPTXADAPTIVE = " + mSplitA2DPSourceAPTXADAPTIVE
                + "\n mSplitA2DPSourceAPTXTWSPLUS = " + mSplitA2DPSourceAPTXTWSPLUS
                + "\n mSplitA2DPSinkSBC = " + mSplitA2DPSinkSBC + "\n mSplitA2DPSinkMP3 = "
                + mSplitA2DPSinkMP3 + "\n mSplitA2DPSinkAAC = "
                + mSplitA2DPSinkAAC + "\n mSplitA2DPSinkLDAC = " + mSplitA2DPSinkLDAC
                + "\n mSplitA2DPSinkAPTX = " + mSplitA2DPSinkAPTX
                + "\n mSplitA2DPSinkAPTXHD = " + mSplitA2DPSinkAPTXHD
                + "\n mSplitA2DPSinkAPTXADAPTIVE = " + mSplitA2DPSinkAPTXADAPTIVE
                + "\n mSplitA2DPSinkAPTXTWSPLUS = " + mSplitA2DPSinkAPTXTWSPLUS
                + "\n mVoiceDualSCO = " + mVoiceDualSCO + "\n mVoiceTWSPLUSeSCOAG = "
                + mVoiceTWSPLUSeSCOAG + "\n mSWBVoicewithAptxAdaptiveAG = "
                + mSWBVoicewithAptxAdaptiveAG + "\n SplitA2DPSourceAACABR = "
                + mSplitA2DPSourceAACABR + "\n SplitA2DPSourceTxSplitAPTXADAPTIVE = "
                + mSplitA2DPSourceTxSplitAPTXADAPTIVE + "\n BroadcastAudioTxwithEC_2_5 = "
                + mBroadcastAudioTxwithEC_2_5 + "\n mBroadcastAudioTxwithEC_3_9 = "
                + mBroadcastAudioTxwithEC_3_9 + "\n mBroadcastAudioRxwithEC_2_5 = "
                + mBroadcastAudioRxwithEC_2_5 + "\n mBroadcastAudioRxwithEC_3_9= "
                + mBroadcastAudioRxwithEC_3_9 + "\n mISOCIGParameterCalculator= "
                + mISOCIGParameterCalculator);
        }
    }

    void updateHostFeatureSupport(byte[] val) {
        mHostAddonFeaturesSupported = (val.length != 0);
        if (!mHostAddonFeaturesSupported) {
            Log.d(TAG, "BT_PROPERTY_HOST_ADD_ON_FEATURES: host add-on features not supported");
        } else {
            mHostAdvAudioUnicastFeatureSupported = ((0x01 & ((int) val[0])) != 0);
            mHostAdvAudioBCAFeatureSupported = ((0x02 & ((int) val[0])) != 0);
            mHostAdvAudioBCSFeatureSupported = ((0x04 & ((int) val[0])) != 0);
            mHostAdvAudioStereoRecordingFeatureSupported = ((0x08 & ((int) val[0])) != 0);
            mHostAdvAudioLC3QFeatureSupported = ((0x10 & ((int) val[0])) != 0);
            mHostQHSFeatureSupported = ((0x20 & ((int) val[0])) != 0);
            /* bit 7 and 8 of first byte reserved for future use
             */
            Log.d(TAG, "BT_PROPERTY_HOST_ADD_ON_FEATURES: update from BT HAL"
                + "\n mHostAdvAudioUnicastFeatureSupported = "
                + mHostAdvAudioUnicastFeatureSupported
                + "\n mHostAdvAudioBCAFeatureSupported = "
                + mHostAdvAudioBCAFeatureSupported
                + "\n mHostAdvAudioBCSFeatureSupported = "
                + mHostAdvAudioBCSFeatureSupported
                + "\n mHostAdvAudioStereoRecordingFeatureSupported = "
                + mHostAdvAudioStereoRecordingFeatureSupported
                + "\n mHostAdvAudioLC3QFeatureSupported = "
                + mHostAdvAudioLC3QFeatureSupported
                + "\n mHostQHSFeatureSupported = "
                + mHostQHSFeatureSupported);
        }
    }

    private native void initNative();

    private native static void classInitNative();

    private native void cleanupNative();

    private native void setWifiStateNative(boolean status);

    private native void setPowerBackoffNative(boolean status);

    private native void informTimeoutToHidlNative();
}
