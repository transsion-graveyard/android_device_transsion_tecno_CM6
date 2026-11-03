#include "Fingerprint.h"
#include "Glue.h"
#include "extension/AncFingerprintExtension.h"

#include <android-base/logging.h>

namespace aidl {
namespace android {
namespace hardware {
namespace biometrics {
namespace fingerprint {
namespace jiiov {

Fingerprint::Fingerprint() {
    // Arm the touch IC FOD region at service start (screen on at boot), not
    // on the first createSession. armSpecialAreaOnce() is per-boot idempotent
    // so a HAL restart while the screen is off cannot re-arm post-suspend and
    // kill FOD detection; see Glue.h.
    initOnce();
}

void engineNotifyThunk(uint32_t type, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4);

// CM6: JV0307 optical sensor under AMOLED, circle at (540, 2204) r=91
// (matches overlay FrameworksResTarget config_udfps_sensor_props).
static constexpr int32_t kSensorId = 0;
static constexpr int32_t kSensorStrength = 2;  // STRONG
static constexpr int32_t kMaxEnrollments = 5;// AOSP owns optical illumination and signals readiness through onUiReady().
static constexpr bool kHalControlsIllumination = false;
static constexpr bool kSupportsDetectInteraction = true;

ndk::ScopedAStatus Fingerprint::getSensorProps(std::vector<SensorProps>* out) {
    SensorProps p;

    p.commonProps.sensorId = kSensorId;
    p.commonProps.sensorStrength = static_cast<common::SensorStrength>(kSensorStrength);
    p.commonProps.maxEnrollmentsPerUser = kMaxEnrollments;

    p.sensorType = FingerprintSensorType::UNDER_DISPLAY_OPTICAL;
    p.supportsDetectInteraction = kSupportsDetectInteraction;
    p.halHandlesDisplayTouches = false;
    p.halControlsIllumination = kHalControlsIllumination;

    SensorLocation loc;
    loc.sensorLocationX = 540;
    loc.sensorLocationY = 2204;
    loc.sensorRadius = 91;
    loc.sensorShape = SensorShape::CIRCLE;
    p.sensorLocations = {loc};

    *out = {p};
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Fingerprint::createSession(
        int32_t sensorId, int32_t userId,
        const std::shared_ptr<ISessionCallback>& cb,
        std::shared_ptr<ISession>* out) {
    ALOGI("createSession sensor=%d user=%d", sensorId, userId);
    Engine& engine = Engine::get();
    if (!engine.ready() && !engine.init()) {
        ALOGE("engine init failed");
    } else if (engine.ready()) {
        engine.setNotify(&engineNotifyThunk);
        char path[64];
        snprintf(path, sizeof(path), "/data/vendor_de/%d/fpdata/", userId);
        engine.setActiveGroup(userId, path);
        initOnce();
        // Rebuilt ANC extension service (replaces
        // odm/lib64/libanc_extension_service.so): publish
        // vendor.anc.fingerprint.extension.IFingerprintExtension/default
        // against the live engine handle. Once per process.
        static bool sExtRegistered = false;
        if (!sExtRegistered) {
            sExtRegistered = true;
            if (vendor::anc::fingerprint::extension::AncExtensionRegister(
                        engine.handle()) != 0) {
                ALOGE("anc extension registration failed");
            }
        }
    }
    auto session = SharedRefBase::make<Session>(sensorId, userId);
    session->setCallback(cb);
    setActiveSession(session);
    *out = session;
    return ndk::ScopedAStatus::ok();
}

}  // namespace jiiov
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace hardware
}  // namespace android
}  // namespace aidl
