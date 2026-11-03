/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "Vibrator.h"

#include <android-base/logging.h>

#include <fstream>
#include <map>
#include <thread>
#include <unistd.h>

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {

namespace {

const std::string kVibratorState = "/sys/class/leds/vibrator/state";
const std::string kVibratorDuration = "/sys/class/leds/vibrator/duration";
const std::string kVibratorActivate = "/sys/class/leds/vibrator/activate";

// Effect durations (ms). Kept in line with the in-tree MTK HAL defaults.
const std::map<Effect, int32_t> kVibEffects = {
        {Effect::CLICK, 50},
        {Effect::TICK, 32},
        {Effect::DOUBLE_CLICK, 80},
        {Effect::HEAVY_CLICK, 60},
        {Effect::TEXTURE_TICK, 25},
};

/*
 * Frequency profile for the MT6789 "regulator-vibrator" motor.
 *
 * The motor is a fixed-frequency ERM/LRA driven through a regulator, so the
 * stock MTK HAL reports no frequency curve at all (getResonantFrequency()
 * returns EX_UNSUPPORTED_OPERATION) and getCapabilities() does not advertise
 * CAP_FREQUENCY_CONTROL. The Android framework still constructs a
 * VibratorInfo.FrequencyProfile on every boot and logs
 * "Invalid frequency profile received from HAL" when the HAL provides none.
 *
 * We therefore publish the mechanical resonance and a synthetic
 * acceleration-vs-frequency curve centred on it. This is purely informational
 * for VibratorInfo consumers: the device does not advertise
 * CAP_COMPOSE_PWLE_EFFECTS/V2 or CAP_AMPLITUDE_CONTROL, so no frequency-driven
 * rendering path is ever selected.
 */
constexpr float RESONANT_FREQUENCY_HZ = 150.0f;
constexpr float PWLE_FREQUENCY_MIN_HZ = 130.0f;
constexpr float PWLE_FREQUENCY_MAX_HZ = 170.0f;
constexpr float PWLE_FREQUENCY_RESOLUTION_HZ = 1.0f;
constexpr float PWLE_LEVEL_MAX = 1.0f;
constexpr int32_t PWLE_BW_MAP_SIZE =
        1 + static_cast<int32_t>((PWLE_FREQUENCY_MAX_HZ - PWLE_FREQUENCY_MIN_HZ) /
                                 PWLE_FREQUENCY_RESOLUTION_HZ);

void setNode(const std::string& path, int32_t value) {
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG(ERROR) << "Failed to write " << value << " to " << path;
        return;
    }
    file << value << std::endl;
}

}  // namespace

ndk::ScopedAStatus Vibrator::activate(int32_t timeoutMs) {
    /* timeoutMs under 1 = turn off vibrator */
    if (timeoutMs < 1) {
        return off();
    }

    setNode(kVibratorState, 1);
    setNode(kVibratorDuration, timeoutMs);
    setNode(kVibratorActivate, 1);

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getCapabilities(int32_t* _aidl_return) {
    LOG(VERBOSE) << "Vibrator reporting capabilities";
    std::lock_guard lock(mMutex);
    if (mCapabilities == 0) {
        mCapabilities = IVibrator::CAP_ON_CALLBACK | IVibrator::CAP_PERFORM_CALLBACK |
                        IVibrator::CAP_GET_RESONANT_FREQUENCY | IVibrator::CAP_FREQUENCY_CONTROL;
    }
    *_aidl_return = mCapabilities;
    return ndk::ScopedAStatus::ok();
}

void Vibrator::dispatchVibrate(int32_t timeoutMs,
                               const std::shared_ptr<IVibratorCallback>& callback) {
    {
        std::lock_guard lock(mMutex);
        if (mIsVibrating) {
            // Already vibrating, ignore new request.
            return;
        }
        mVibrationCallback = std::shared_ptr<IVibratorCallback>(callback);
        mIsVibrating = true;
    }

    activate(timeoutMs);

    // Note: the thread lambda captures a strong reference to "this" instead of
    // the raw pointer, to avoid a use-after-free if the object is destroyed.
    std::thread([timeoutMs, callback, sharedThis = this->ref<Vibrator>()] {
        LOG(VERBOSE) << "Starting delayed callback on another thread";
        usleep(timeoutMs * 1000);

        if (sharedThis) {
            std::shared_ptr<IVibratorCallback> vibrationCallback, globalCallback;
            {
                std::lock_guard lock(sharedThis->mMutex);
                globalCallback = std::move(sharedThis->mGlobalVibrationCallback);
                sharedThis->mIsVibrating = false;
                // make sure any delayed call will not trigger this again.
                sharedThis->mGlobalVibrationCallback = nullptr;
                if (sharedThis->mVibrationCallback == callback) {
                    vibrationCallback = std::move(sharedThis->mVibrationCallback);
                    sharedThis->mVibrationCallback = nullptr;
                }
            }
            if (vibrationCallback) {
                LOG(VERBOSE) << "Notifying callback onComplete";
                if (!vibrationCallback->onComplete().isOk()) {
                    LOG(ERROR) << "Failed to call onComplete";
                }
            }
            if (globalCallback) {
                LOG(VERBOSE) << "Notifying global callback onComplete";
                if (!globalCallback->onComplete().isOk()) {
                    LOG(ERROR) << "Failed to call onComplete";
                }
            }
        }
    }).detach();
}

void Vibrator::setGlobalVibrationCallback(const std::shared_ptr<IVibratorCallback>& callback) {
    std::shared_ptr<IVibratorCallback> immediateCallback = nullptr;
    {
        std::lock_guard lock(mMutex);
        if (mIsVibrating) {
            mGlobalVibrationCallback = callback;
        } else {
            immediateCallback = callback;
        }
    }
    if (immediateCallback) {
        std::thread([callback] {
            LOG(VERBOSE) << "Notifying global callback onComplete";
            if (!callback->onComplete().isOk()) {
                LOG(ERROR) << "Failed to call onComplete";
            }
        }).detach();
    }
}

ndk::ScopedAStatus Vibrator::off() {
    LOG(VERBOSE) << "Vibrator off";
    std::shared_ptr<IVibratorCallback> callback, globalCallback;
    {
        std::lock_guard lock(mMutex);
        callback = std::move(mVibrationCallback);
        globalCallback = std::move(mGlobalVibrationCallback);
        mIsVibrating = false;
        // make sure any delayed call will not trigger these again.
        mVibrationCallback = nullptr;
        mGlobalVibrationCallback = nullptr;
    }

    setNode(kVibratorActivate, 0);

    if (callback || globalCallback) {
        std::thread([callback, globalCallback] {
            if (callback) {
                LOG(VERBOSE) << "Notifying callback onComplete";
                if (!callback->onComplete().isOk()) {
                    LOG(ERROR) << "Failed to call onComplete";
                }
            }
            if (globalCallback) {
                LOG(VERBOSE) << "Notifying global callback onComplete";
                if (!globalCallback->onComplete().isOk()) {
                    LOG(ERROR) << "Failed to call onComplete";
                }
            }
        }).detach();
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::on(int32_t timeoutMs,
                                const std::shared_ptr<IVibratorCallback>& callback) {
    LOG(VERBOSE) << "Vibrator on for timeoutMs: " << timeoutMs;
    dispatchVibrate(timeoutMs, callback);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::perform(Effect effect, EffectStrength strength,
                                     const std::shared_ptr<IVibratorCallback>& callback,
                                     int32_t* _aidl_return) {
    LOG(VERBOSE) << "Vibrator perform " << toString(effect) << ", " << toString(strength);

    if (kVibEffects.find(effect) == kVibEffects.end()) {
        return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
    }
    if (strength != EffectStrength::LIGHT && strength != EffectStrength::MEDIUM &&
        strength != EffectStrength::STRONG) {
        return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
    }

    int32_t timeoutMs = kVibEffects.at(effect);
    dispatchVibrate(timeoutMs, callback);
    *_aidl_return = timeoutMs;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::performVendorEffect(
        const VendorEffect& effect __unused,
        const std::shared_ptr<IVibratorCallback>& callback __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getSupportedEffects(std::vector<Effect>* _aidl_return) {
    for (const auto& pair : kVibEffects) _aidl_return->push_back(pair.first);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::setAmplitude(float amplitude __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::setExternalControl(bool enabled __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getCompositionDelayMax(int32_t* maxDelayMs __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getCompositionSizeMax(int32_t* maxSize __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getSupportedPrimitives(
        std::vector<CompositePrimitive>* supported __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getPrimitiveDuration(CompositePrimitive primitive __unused,
                                                  int32_t* durationMs __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::compose(const std::vector<CompositeEffect>& composite __unused,
                                     const std::shared_ptr<IVibratorCallback>& callback __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getSupportedAlwaysOnEffects(
        std::vector<Effect>* _aidl_return __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::alwaysOnEnable(int32_t id __unused, Effect effect __unused,
                                            EffectStrength strength __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::alwaysOnDisable(int32_t id __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getResonantFrequency(float* resonantFreqHz) {
    *resonantFreqHz = RESONANT_FREQUENCY_HZ;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getQFactor(float* qFactor __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getFrequencyResolution(float* freqResolutionHz) {
    *freqResolutionHz = PWLE_FREQUENCY_RESOLUTION_HZ;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getFrequencyMinimum(float* freqMinimumHz) {
    *freqMinimumHz = PWLE_FREQUENCY_MIN_HZ;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getBandwidthAmplitudeMap(std::vector<float>* _aidl_return) {
    // Symmetric bandwidth curve peaking at RESONANT_FREQUENCY_HZ, mirroring the
    // shape used by the AOSP reference vibrator HAL.
    int halfMapSize = PWLE_BW_MAP_SIZE / 2;
    std::vector<float> bandwidthAmplitudeMap(PWLE_BW_MAP_SIZE, PWLE_LEVEL_MAX);
    for (int i = 0; i < halfMapSize; ++i) {
        bandwidthAmplitudeMap[halfMapSize + i + 1] =
                bandwidthAmplitudeMap[halfMapSize + i] - 0.01f;
        bandwidthAmplitudeMap[halfMapSize - i - 1] =
                bandwidthAmplitudeMap[halfMapSize - i] - 0.01f;
    }
    *_aidl_return = bandwidthAmplitudeMap;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getPwlePrimitiveDurationMax(int32_t* durationMs __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getPwleCompositionSizeMax(int32_t* maxSize __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getSupportedBraking(std::vector<Braking>* supported __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::composePwle(const std::vector<PrimitivePwle>& composite __unused,
                                         const std::shared_ptr<IVibratorCallback>& callback
                                                 __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getFrequencyToOutputAccelerationMap(
        std::vector<FrequencyAccelerationMapEntry>* _aidl_return) {
    // Synthetic acceleration-vs-frequency curve for the fixed-resonance
    // regulator-vibrator motor, peaking at RESONANT_FREQUENCY_HZ. Magnitudes are
    // in the range typical for a phone vibration motor.
    std::vector<std::pair<float, float>> frequencyToOutputAccelerationData = {
            {130.0f, 0.60f}, {135.0f, 1.35f}, {140.0f, 2.10f}, {145.0f, 2.70f},
            {148.0f, 2.94f}, {150.0f, 3.00f}, {152.0f, 2.94f}, {155.0f, 2.70f},
            {160.0f, 2.10f}, {165.0f, 1.35f}, {170.0f, 0.60f}};

    std::vector<FrequencyAccelerationMapEntry> frequencyToOutputAccelerationMap;
    for (const auto& entry : frequencyToOutputAccelerationData) {
        frequencyToOutputAccelerationMap.emplace_back(/*frequency=*/entry.first,
                                                      /*maxOutputAcceleration=*/entry.second);
    }

    *_aidl_return = frequencyToOutputAccelerationMap;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getPwleV2PrimitiveDurationMaxMillis(int32_t* maxDurationMs __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getPwleV2PrimitiveDurationMinMillis(int32_t* minDurationMs __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::getPwleV2CompositionSizeMax(int32_t* maxSize __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

ndk::ScopedAStatus Vibrator::composePwleV2(const CompositePwleV2& composite __unused,
                                           const std::shared_ptr<IVibratorCallback>& callback
                                                   __unused) {
    return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
}

}  // namespace vibrator
}  // namespace hardware
}  // namespace android
}  // namespace aidl