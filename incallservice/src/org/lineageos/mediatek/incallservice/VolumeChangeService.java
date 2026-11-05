/*
 * Copyright (C) 2023 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.mediatek.incallservice;

import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.media.AudioDeviceInfo;
import android.media.AudioManager;
import android.media.AudioSystem;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.os.SystemClock;
import android.util.Log;

public class VolumeChangeService extends Service {
    public static final String LOG_TAG = "MediatekInCallService";
    private static final long GAIN_POLL_MS = 250;
    private static final long CALL_SETTLE_MS = 700;

    private AudioManager mAudioManager;
    private Handler mHandler;
    private int mLastDevice = -1;
    private int mLastVolume = -1;
    private int mLastMode = -1;
    private int mAppliedDevice = -1;
    private int mAppliedVolume = -1;
    private long mApplyAt;

    private final Runnable mGainPoller = new Runnable() {
        @Override
        public void run() {
            int mode = mAudioManager.getMode();
            long now = SystemClock.uptimeMillis();
            if (mode != mLastMode) {
                Log.i(LOG_TAG, "Gain poller mode=" + mode);
                mLastMode = mode;
                if (isCallMode(mode)) {
                    mLastDevice = -1;
                    mLastVolume = -1;
                    mAppliedDevice = -1;
                    mAppliedVolume = -1;
                    mApplyAt = now + CALL_SETTLE_MS;
                } else {
                    resetState();
                }
            }

            if (isCallMode(mode)) {
                AudioDeviceInfo device = mAudioManager.getCommunicationDevice();
                if (device != null) {
                    int deviceType = device.getPort().type();
                    int volume = mAudioManager.getStreamVolume(AudioManager.STREAM_VOICE_CALL);

                    if (deviceType != mLastDevice) {
                        mLastDevice = deviceType;
                        mAppliedDevice = -1;
                        mAppliedVolume = -1;
                        mApplyAt = now + CALL_SETTLE_MS;
                    }
                    if (volume != mLastVolume) {
                        mLastVolume = volume;
                        if (now >= mApplyAt) {
                            mAppliedVolume = -1;
                        }
                    }

                    if (now >= mApplyAt
                            && (mAppliedDevice != deviceType || mAppliedVolume != volume)) {
                        if (GainUtils.isSupportedCallDevice(deviceType)) {
                            Log.i(LOG_TAG, "Applying gain: device=" + deviceType
                                    + " volume=" + volume);
                            GainUtils.setGainLevel(deviceType, volume,
                                    AudioSystem.STREAM_VOICE_CALL);
                            mAppliedDevice = deviceType;
                            mAppliedVolume = volume;
                        } else {
                            Log.i(LOG_TAG, "Skipping unsupported call device="
                                    + deviceType + " volume=" + volume);
                            mAppliedDevice = deviceType;
                            mAppliedVolume = volume;
                        }
                    }
                }
            } else {
                resetState();
            }
            mHandler.postDelayed(this, GAIN_POLL_MS);
        }
    };

    private static boolean isCallMode(int mode) {
        return mode == AudioManager.MODE_IN_CALL
                || mode == AudioManager.MODE_IN_COMMUNICATION;
    }

    private void resetState() {
        mLastDevice = -1;
        mLastVolume = -1;
        mAppliedDevice = -1;
        mAppliedVolume = -1;
        mApplyAt = 0;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onDestroy() {
        if (mHandler != null) {
            mHandler.removeCallbacks(mGainPoller);
        }
        super.onDestroy();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startid) {
        if (mHandler != null) {
            return START_STICKY;
        }

        mAudioManager = (AudioManager) getSystemService(Context.AUDIO_SERVICE);
        mHandler = new Handler(Looper.getMainLooper());

        Log.i(LOG_TAG, "Service is starting...");
        mHandler.post(mGainPoller);

        return START_STICKY;
    }
}
