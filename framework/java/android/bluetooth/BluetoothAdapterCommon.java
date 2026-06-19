/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear.
 */

package android.bluetooth;

/**
 * Bluetooth adapter common definition and utility functions.
 *
 * @hide
 */
public final class BluetoothAdapterCommon {

    // Fully-static utility classes must not have constructor
    private BluetoothAdapterCommon() {}

    /**
     * Index of the default (primary) Bluetooth adapter.
     *
     * @hide
     */
    public static final int ADAPTER_DEFAULT = 0;

    /**
     * Index of the first non-default Bluetooth adapter.
     *
     * <p>Kept as a named constant for the currently wired secondary adapter. Prefer
     * {@link #isNonDefaultAdapter(int)} for logic that should apply to all non-default adapters.
     *
     * @hide
     */
    public static final int ADAPTER_1 = 1;

    /**
     * Maximum number of Bluetooth adapters that can be supported simultaneously.
     *
     * <p>Adapter indices in the range [{@link #ADAPTER_DEFAULT}, {@code ADAPTER_NUMBER}) are
     * considered valid. Only adapters actually provisioned by the platform will be started;
     * changing this constant does not automatically enable additional hardware.
     *
     * @hide
     */
    public static final int ADAPTER_NUMBER = 2;

    /** @hide */
    public static boolean isAdapterDefault(int adapterIndex) {
        return adapterIndex == ADAPTER_DEFAULT;
    }

    /**
     * Returns {@code true} for any valid adapter index that is not the default adapter (i.e.
     * indices 1, 2, 3, ...).
     *
     * @hide
     */
    public static boolean isNonDefaultAdapter(int adapterIndex) {
        return validAdapter(adapterIndex) && adapterIndex != ADAPTER_DEFAULT;
    }

    /** @hide */
    public static boolean validAdapter(int adapterIndex) {
        return (adapterIndex >= ADAPTER_DEFAULT) && (adapterIndex < ADAPTER_NUMBER);
    }
}
