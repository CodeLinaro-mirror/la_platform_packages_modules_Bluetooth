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
 * limitations under the License.、
 *
 * ​Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

package android.bluetooth.le;

import android.annotation.FlaggedApi;
import android.annotation.NonNull;
import android.os.Parcel;
import android.os.Parcelable;

import com.android.bluetooth.flags.Flags;

/**
 * Parameters for Periodic Advertising with Responses (PAwR).
 *
 * <p>All parameter ranges and semantics follow the Bluetooth Core Specification.
 * Framework default values are aligned with existing Android behavior.
 */
@FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
public final class PeriodicAdvertisingParametersV2 implements Parcelable {
    // Periodic advertising interval (in 1.25 ms units)
    private static final int PA_INTERVAL_MIN = 0x0006;   // 7.5 ms
    private static final int PA_INTERVAL_MAX = 0xFFFF;   // 81.91875 s

    // Num_Subevents
    private static final int NUM_SUBEVENTS_MIN = 0x00;
    private static final int NUM_SUBEVENTS_MAX = 0x80;   // 128

    // Subevent_Interval (in 1.25 ms units)
    private static final int SUBEVENT_INTERVAL_MIN = 0x06; // 7.5 ms
    private static final int SUBEVENT_INTERVAL_MAX = 0xFF; // 318.75 ms

    // Response_Slot_Delay (in 1.25 ms units)
    private static final int RESPONSE_SLOT_DELAY_NONE = 0x00;
    private static final int RESPONSE_SLOT_DELAY_MIN = 0x01;
    private static final int RESPONSE_SLOT_DELAY_MAX = 0xFE;

    // Response_Slot_Spacing (in 0.125 ms units)
    private static final int RESPONSE_SLOT_SPACING_NONE = 0x00;
    private static final int RESPONSE_SLOT_SPACING_MIN = 0x02;
    private static final int RESPONSE_SLOT_SPACING_MAX = 0xFF;

    // Num_Response_Slots
    private static final int NUM_RESPONSE_SLOTS_NONE = 0x00;
    private static final int NUM_RESPONSE_SLOTS_MAX = 0xFF;

    // Default periodic advertising interval range (in 1.25 ms units)
    private static final int INTERVAL_MIN = 80;      // 100 ms
    private static final int INTERVAL_MAX = 65519;   // 81.89875 s

    private final boolean mIncludeTxPower;

    // Periodic advertising interval range (in 1.25 ms units).
    private final int mIntervalMin;
    private final int mIntervalMax;

    // PAwR parameters
    private final int mNumSubevents;        // Number of subevents
    private final int mSubeventInterval;    // Subevent interval (in 1.25 ms units)
    private final int mResponseSlotDelay;   // Response slot delay (in 1.25 ms units)
    private final int mResponseSlotSpacing; // Response slot spacing (in 0.125 ms units)
    private final int mNumResponseSlots;    // Number of response slots

    private PeriodicAdvertisingParametersV2(
            boolean includeTxPower,
            int intervalMin,
            int intervalMax,
            int numSubevents,
            int subeventInterval,
            int responseSlotDelay,
            int responseSlotSpacing,
            int numResponseSlots) {

        mIncludeTxPower = includeTxPower;
        mIntervalMin = intervalMin;
        mIntervalMax = intervalMax;
        mNumSubevents = numSubevents;
        mSubeventInterval = subeventInterval;
        mResponseSlotDelay = responseSlotDelay;
        mResponseSlotSpacing = responseSlotSpacing;
        mNumResponseSlots = numResponseSlots;
    }

    private PeriodicAdvertisingParametersV2(Parcel in) {
        mIncludeTxPower = in.readInt() != 0;
        mIntervalMin = in.readInt();
        mIntervalMax = in.readInt();
        mNumSubevents = in.readInt();
        mSubeventInterval = in.readInt();
        mResponseSlotDelay = in.readInt();
        mResponseSlotSpacing = in.readInt();
        mNumResponseSlots = in.readInt();
    }

    /**
     * Returns whether TX power will be included in periodic advertising packet.
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public boolean isIncludeTxPower() {
        return mIncludeTxPower;
    }

    /**
     * Returns the minimum periodic advertising interval (in 1.25 ms units).
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public int getIntervalMin() {
        return mIntervalMin;
    }

    /**
     * Returns the maximum periodic advertising interval (in 1.25 ms units).
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public int getIntervalMax() {
        return mIntervalMax;
    }

    /**
     * Returns PAwR-specific parameters.
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public int getNumSubevents() {
        return mNumSubevents;
    }

    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public int getSubeventInterval() {
        return mSubeventInterval;
    }

    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public int getResponseSlotDelay() {
        return mResponseSlotDelay;
    }

    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public int getResponseSlotSpacing() {
        return mResponseSlotSpacing;
    }

    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public int getNumResponseSlots() {
        return mNumResponseSlots;
    }

    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public @NonNull PawrParams getPawrParams() {
        return new PawrParams(
                mNumSubevents,
                mSubeventInterval,
                mResponseSlotDelay,
                mResponseSlotSpacing,
                mNumResponseSlots);
    }

    /**
     * A value object that holds PAwR-specific parameters corresponding to
     * {@link Builder#setPawrParams(int, int, int, int, int)}.
     * Immutable read-only view of PAwR-specific parameters.
     */
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public static final class PawrParams {
        private final int mNumSubevents;
        private final int mSubeventInterval;
        private final int mResponseSlotDelay;
        private final int mResponseSlotSpacing;
        private final int mNumResponseSlots;

        private PawrParams(
                int numSubevents,
                int subeventInterval,
                int responseSlotDelay,
                int responseSlotSpacing,
                int numResponseSlots) {
            mNumSubevents = numSubevents;
            mSubeventInterval = subeventInterval;
            mResponseSlotDelay = responseSlotDelay;
            mResponseSlotSpacing = responseSlotSpacing;
            mNumResponseSlots = numResponseSlots;
        }

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public int getNumSubevents() {
            return mNumSubevents;
        }

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public int getSubeventInterval() {
            return mSubeventInterval;
        }

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public int getResponseSlotDelay() {
            return mResponseSlotDelay;
        }

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public int getResponseSlotSpacing() {
            return mResponseSlotSpacing;
        }

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public int getNumResponseSlots() {
            return mNumResponseSlots;
        }
    }

    @Override
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public void writeToParcel(@NonNull Parcel dest, int flags) {
        dest.writeInt(mIncludeTxPower ? 1 : 0);
        dest.writeInt(mIntervalMin);
        dest.writeInt(mIntervalMax);
        dest.writeInt(mNumSubevents);
        dest.writeInt(mSubeventInterval);
        dest.writeInt(mResponseSlotDelay);
        dest.writeInt(mResponseSlotSpacing);
        dest.writeInt(mNumResponseSlots);
    }

    @Override
    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public int describeContents() {
        return 0;
    }

    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public static final @NonNull Creator<PeriodicAdvertisingParametersV2> CREATOR =
            new Creator<>() {
                @Override
                public PeriodicAdvertisingParametersV2 createFromParcel(Parcel in) {
                    return new PeriodicAdvertisingParametersV2(in);
                }

                @Override
                public PeriodicAdvertisingParametersV2[] newArray(int size) {
                    return new PeriodicAdvertisingParametersV2[size];
                }
            };

    @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
    public static final class Builder {

        private boolean mIncludeTxPower;

        private int mIntervalMin = INTERVAL_MIN;
        private int mIntervalMax = INTERVAL_MAX;

        private boolean mPawrSet;
        private int mNumSubevents;
        private int mSubeventInterval;
        private int mResponseSlotDelay;
        private int mResponseSlotSpacing;
        private int mNumResponseSlots;

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public Builder() {}

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public @NonNull Builder setIncludeTxPower(boolean includeTxPower) {
            mIncludeTxPower = includeTxPower;
            return this;
        }

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public @NonNull Builder setIntervalMin(int intervalMin) {
            if (intervalMin < PA_INTERVAL_MIN || intervalMin > mIntervalMax) {
                throw new IllegalArgumentException("intervalMin out of range");
            }
            mIntervalMin = intervalMin;
            return this;
        }

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public @NonNull Builder setIntervalMax(int intervalMax) {
            if (intervalMax < mIntervalMin || intervalMax > PA_INTERVAL_MAX) {
                throw new IllegalArgumentException("intervalMax out of range");
            }
            mIntervalMax = intervalMax;
            return this;
        }

        /**
         * Sets PAwR-specific parameters.
         *
         * <p>Performs basic range validation. Full semantic validation is done in build().
         */
        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public @NonNull Builder setPawrParams(
                int numSubevents,
                int subeventInterval,
                int responseSlotDelay,
                int responseSlotSpacing,
                int numResponseSlots) {

            if (numSubevents < NUM_SUBEVENTS_MIN || numSubevents > NUM_SUBEVENTS_MAX) {
                throw new IllegalArgumentException("numSubevents out of range");
            }
            if (subeventInterval < SUBEVENT_INTERVAL_MIN
                    || subeventInterval > SUBEVENT_INTERVAL_MAX) {
                throw new IllegalArgumentException("subeventInterval out of range");
            }
            if (responseSlotDelay < RESPONSE_SLOT_DELAY_NONE
                    || responseSlotDelay > RESPONSE_SLOT_DELAY_MAX) {
                throw new IllegalArgumentException("responseSlotDelay out of range");
            }
            if (responseSlotSpacing < RESPONSE_SLOT_SPACING_NONE
                    || responseSlotSpacing > RESPONSE_SLOT_SPACING_MAX) {
                throw new IllegalArgumentException("responseSlotSpacing out of range");
            }
            if (numResponseSlots < NUM_RESPONSE_SLOTS_NONE
                    || numResponseSlots > NUM_RESPONSE_SLOTS_MAX) {
                throw new IllegalArgumentException("numResponseSlots out of range");
            }

            mNumSubevents = numSubevents;
            mSubeventInterval = subeventInterval;
            mResponseSlotDelay = responseSlotDelay;
            mResponseSlotSpacing = responseSlotSpacing;
            mNumResponseSlots = numResponseSlots;
            mPawrSet = true;
            return this;
        }

        @FlaggedApi(Flags.FLAG_PAWR_ADVERTISER_EXTENSION)
        public @NonNull PeriodicAdvertisingParametersV2 build() {
            if (!mPawrSet) {
                throw new IllegalArgumentException("PAwR parameters must be set");
            }

            validateIntervalOrThrow();
            validatePawrOrThrow();

            return new PeriodicAdvertisingParametersV2(
                    mIncludeTxPower,
                    mIntervalMin,
                    mIntervalMax,
                    mNumSubevents,
                    mSubeventInterval,
                    mResponseSlotDelay,
                    mResponseSlotSpacing,
                    mNumResponseSlots);
        }

        private void validateIntervalOrThrow() {
            if (mIntervalMin < PA_INTERVAL_MIN || mIntervalMin > mIntervalMax) {
                throw new IllegalArgumentException("intervalMin out of range");
            }
            if (mIntervalMax < mIntervalMin || mIntervalMax > PA_INTERVAL_MAX) {
                throw new IllegalArgumentException("intervalMax out of range");
            }
        }

        private void validatePawrOrThrow() {
            // No response slots
            if (mNumResponseSlots == NUM_RESPONSE_SLOTS_NONE) {
                if (mResponseSlotDelay != RESPONSE_SLOT_DELAY_NONE
                        || mResponseSlotSpacing != RESPONSE_SLOT_SPACING_NONE) {
                    throw new IllegalArgumentException(
                            "response slot params must be 0 when numResponseSlots == 0");
                }
                return;
            }

            // Has response slots
            if (mNumResponseSlots > NUM_RESPONSE_SLOTS_NONE) {
                if (mResponseSlotDelay < RESPONSE_SLOT_DELAY_MIN
                        || mResponseSlotDelay > RESPONSE_SLOT_DELAY_MAX) {
                    throw new IllegalArgumentException("responseSlotDelay out of range");
                }
                if (mResponseSlotSpacing < RESPONSE_SLOT_SPACING_MIN
                        || mResponseSlotSpacing > RESPONSE_SLOT_SPACING_MAX) {
                    throw new IllegalArgumentException("responseSlotSpacing out of range");
                }
            }

            // Timing fit check
            int availTime125 = mSubeventInterval - mResponseSlotDelay;
            if (availTime125 <= 0) {
                throw new IllegalArgumentException(
                        "response slot delay exceed subevent interval");
            }

            int availTime0125 = availTime125 * 10; // 1.25 ms = 10 * 0.125 ms
            int reqTime0125 = (mNumResponseSlots - 1) * mResponseSlotSpacing;

            if (reqTime0125 >= availTime0125) {
                throw new IllegalArgumentException(
                        "response slots exceed subevent interval");
            }
        }
    }
}
