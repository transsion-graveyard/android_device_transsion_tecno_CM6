/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.mediatek.incallservice;

import android.media.AudioSystem;
import android.media.AudioDeviceInfo;
import android.util.Log;

public class GainUtils {
    public static final String LOG_TAG = "MtkInCallService";

    /*
     * The on-device ODM SpeechVol table has 15 dl_gain entries per
     * ParamUnit, i.e. vendor index 0..14. Stock AudioPolicyManagerCustomImpl
     * remaps the voice-call index in
     * gainNvram_remapIndexFromSetStreamVolumeIndex() by subtracting one.
     * Match that remap instead of scaling the 15 UI steps onto 0..17.
     */
    private static final int UI_VOLUME_MAX = 15;
    private static final int VENDOR_VOLUME_MAX = 14;

    private static int mapToVendorIndex(int uiIndex) {
        int clamped = Math.max(0, Math.min(UI_VOLUME_MAX, uiIndex));
        return Math.max(0, Math.min(VENDOR_VOLUME_MAX, clamped - 1));
    }

    public static boolean isSupportedCallDevice(int deviceType) {
        return deviceType == AudioDeviceInfo.TYPE_BUILTIN_EARPIECE
                || deviceType == AudioDeviceInfo.TYPE_BUILTIN_SPEAKER;
    }

    /**
     * Sets the gain level for a given audio device.
     * @param audioDevice The audio device to set the gain level for.
     * @param uiIndex The AudioManager stream volume index.
     * @param streamType The stream type to set the gain level for.
     */
    public static void setGainLevel(int audioDevice, int uiIndex, int streamType) {
        int vendorIndex = mapToVendorIndex(uiIndex);
        String parameters = String.format("volumeDevice=%d;volumeIndex=%d;volumeStreamType=%d",
                                          audioDevice, vendorIndex, streamType);
        Log.d(LOG_TAG, "Setting audio parameters to: " + parameters
                + " (UI volume " + uiIndex + " -> vendor " + vendorIndex + ")");
        AudioSystem.setParameters(parameters);
    }
}
