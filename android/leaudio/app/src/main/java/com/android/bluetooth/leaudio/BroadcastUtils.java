/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package com.android.bluetooth.leaudio;

import android.bluetooth.le.ScanRecord;
import android.bluetooth.le.ScanResult;
import android.util.Log;

import java.nio.charset.StandardCharsets;

/**
 * Utility class for parsing broadcast-related information from scan results.
 * Based on BassUtils.java implementation.
 */
public class BroadcastUtils {
    private static final String TAG = "BroadcastUtils";

    // Broadcast name AD type and length constraints (from BassConstants)
    private static final int BCAST_NAME_AD_TYPE = 0x30;
    private static final int BCAST_NAME_LEN_MIN = 4;
    private static final int BCAST_NAME_LEN_MAX = 32;

    /**
     * Helper class to hold Length-Type-Value entry
     */
    private static class TypeValueEntry {
        private final int type;
        private final byte[] value;

        TypeValueEntry(int type, byte[] value) {
            this.type = type;
            this.value = value;
        }

        int getType() {
            return type;
        }

        byte[] getValue() {
            return value;
        }
    }

    /**
     * Extracts the broadcast name from a scan result.
     * The broadcast name is encoded in the advertising data with AD type 0x30.
     *
     * @param scanResult The scan result to parse
     * @return The broadcast name, or null if not found or invalid
     */
    public static String getBroadcastName(ScanResult scanResult) {
        if (scanResult == null) {
            Log.e(TAG, "Null scan result");
            return null;
        }
        return getBroadcastName(scanResult.getScanRecord());
    }

    /**
     * Extracts the broadcast name from a scan record.
     * The broadcast name is encoded in the advertising data with AD type 0x30.
     *
     * @param scanRecord The scan record to parse
     * @return The broadcast name, or null if not found or invalid
     */
    public static String getBroadcastName(ScanRecord scanRecord) {
        if (scanRecord == null) {
            Log.e(TAG, "Null scan record");
            return null;
        }

        byte[] rawBytes = scanRecord.getBytes();
        if (rawBytes == null || rawBytes.length == 0) {
            Log.d(TAG, "Empty scan record bytes");
            return null;
        }

        // Parse the advertising data as Length-Type-Value (LTV) entries
        // Format: [Length][Type][Value...] where Length includes Type byte
        int currentPos = 0;
        while (currentPos < rawBytes.length) {
            // Length is unsigned int
            int length = rawBytes[currentPos] & 0xFF;
            if (length == 0) {
                break;
            }
            currentPos++;

            if (currentPos >= rawBytes.length) {
                Log.w(TAG, "No type and value after length");
                break;
            }

            // Note: length includes the type field itself
            int dataLength = length - 1;
            // Type is unsigned int
            int type = rawBytes[currentPos] & 0xFF;
            currentPos++;

            if (currentPos + dataLength > rawBytes.length) {
                Log.w(TAG, "Insufficient data for value, expected " + dataLength + " bytes");
                break;
            }

            // Check if this is the broadcast name entry
            if (type == BCAST_NAME_AD_TYPE) {
                // Validate broadcast name length
                if (dataLength < BCAST_NAME_LEN_MIN || dataLength > BCAST_NAME_LEN_MAX) {
                    Log.e(TAG, "Invalid broadcast name length: " + dataLength);
                    currentPos += dataLength;
                    continue;
                }

                // Extract and decode broadcast name
                byte[] nameBytes = new byte[dataLength];
                System.arraycopy(rawBytes, currentPos, nameBytes, 0, dataLength);
                String broadcastName = new String(nameBytes, StandardCharsets.UTF_8);
                Log.d(TAG, "Found broadcast name: " + broadcastName);
                return broadcastName;
            }

            currentPos += dataLength;
        }

        return null;
    }

    /**
     * Logs a debug message.
     *
     * @param msg The message to log
     */
    private static void log(String msg) {
        Log.d(TAG, msg);
    }
}
