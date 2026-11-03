/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "VibrationSession.h"

#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {

static constexpr int32_t SESSION_END_DELAY_MS = 20;

ndk::ScopedAStatus VibrationSession::close() {
    LOG(VERBOSE) << "Vibration Session close";
    mManager->closeSession(SESSION_END_DELAY_MS);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus VibrationSession::abort() {
    LOG(VERBOSE) << "Vibration Session abort";
    mManager->abortSession();
    return ndk::ScopedAStatus::ok();
}

}  // namespace vibrator
}  // namespace hardware
}  // namespace android
}  // namespace aidl