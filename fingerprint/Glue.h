// Device glue: FOD touch arming and the stock optical-HBM node bridge.
//
// Stock behaviour (fingerprintservice.jar decompiled + trantp-service):
//  - special_area (the IC FOD spot region) is written ONCE at boot by
//    vendor.hardware.trantp-service (ITrantp default) restoring it from
//    Settings.Global. With the region configured, the touch IC can detect
//    FOD-region touches and report key 195 while the panel is in AOD/doze,
//    which drives the engine's TouchEventDetect (and AOD fingerprint
//    unlock). That HAL is NOT shipped in this ROM, so we replicate the
//    one-time boot restore here (armSpecialAreaOnce) - never per-op, since
//    writing special_area after a suspend clears the IC gesture-mode bit
//    (D0) and kills DT2W (verified: flood "gesture not enable in fw").
//  - /proc/fingerprint_status "2" / "0" is the per-op FOD-detection toggle.
//    The driver (fts_gesture_suspend) auto-clears reg 0xCF from that
//    global at every suspend; status=2 at suspend does NOT break gesture
//    mode (D0 stays 1) - only post-suspend special_area writes do. We arm
//    status=2 while an enroll/authenticate/detectInteraction op is live
//    (including AOD keyguard ops where onPointerDown never arrives), and
//    disarm only at true op end (error / cancel / complete / close / fpctl
//    purge), never on individual pointer-up/cancel.
#pragma once

#include <cutils/properties.h>
#include <utils/Log.h>

#include "SfDisplay.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace aidl {
namespace android {
namespace hardware {
namespace biometrics {
namespace fingerprint {
namespace jiiov {

// The focaltech FOD node appears at different sysfs paths depending on when
// the spi slave device probes relative to the master (observed both as
// .../11019000.spi5/special_area and nested under
// .../11019000.spi5/spi_master/spi5/spi5.0/). Resolve once per process.
inline const char* specialAreaPath() {
    static const char* candidates[] = {
            "/sys/devices/platform/soc/11019000.spi5/special_area",
            "/sys/devices/platform/soc/11019000.spi5/spi_master/spi5/spi5.0/"
            "special_area",
    };
    static const char* resolved = nullptr;
    if (!resolved) {
        for (const char* p : candidates) {
            if (access(p, W_OK) == 0) {
                resolved = p;
                break;
            }
        }
        resolved = resolved ? resolved : candidates[0];
    }
    return resolved;
}
static constexpr const char* kSpecialAreaArm =
        "1 0 0 0 7920 34544 9360 35984 1";
static constexpr const char* kFingerprintStatus = "/proc/fingerprint_status";
static constexpr const char* kLcmHbmState = "/sys/kernel/tran_display/lcm_hbm_state";
static constexpr const char* kMtkFb = "/proc/mtkfb";

// Stock puts the panel into DOZE before fingerprint HBM and keeps the CRTC
// active; the DRM HBM path (mtkfb "panel_hbm") only latches in that state.
// Stock does this from the Transsion AOD app, which this AOSP build does not
// ship. The HAL performs the same transition itself.
//
// This goes through SfDisplay (libbinder_ndk + raw
// android.gui.ISurfaceComposer transactions), not libgui: libgui in a vendor
// process pulls /vendor/lib64/libbinder.so, whose servicemanager AIDL returns
// BAD_TYPE against the system servicemanager and used to null-deref in
// getPhysicalDisplayIds() on every screen-off touch.
inline bool enterFingerprintDoze() {
    if (!SfDisplay::get().setPowerMode(1 /* DOZE */)) {
        ALOGE("fingerprint doze: SurfaceFlinger setPowerMode(DOZE) failed");
        return false;
    }
    usleep(50000);
    return true;
}

inline void leaveFingerprintDoze() {
    SfDisplay::get().setPowerMode(0 /* OFF */);
}

inline bool writeNode(const char* path, const std::string& val) {
    FILE* f = fopen(path, "w");
    if (!f) {
        ALOGE("writeNode %s failed: %s", path, strerror(errno));
        return false;
    }
    size_t n = fwrite(val.data(), 1, val.size(), f);
    bool ok = (n == val.size());
    if (!ok) ALOGE("writeNode %s short write (%zu/%zu)", path, n, val.size());
    fclose(f);
    return ok;
}

inline void setFingerprintStatus(int v) {
    writeNode(kFingerprintStatus, std::to_string(v));
}

// One-time boot restore of the IC FOD spot region, matching what stock's
// trantp-service (vendor.hardware.trantp.ITrantp/default, cmd 22 ->
// /sys/devices/platform/hot-area/special_area) does at boot. This gives the
// touch IC the coordinates of the FOD circle so it can detect touches there
// and report key 195 while the panel is in AOD/doze (needed for off-screen
// / AOD fingerprint unlock). MUST be called before the first suspend and
// never again per-op: a second write after suspend clears D0 and kills DT2W.
//
// The HAL can be restarted while the screen is off (init does that on a
// crash). A per-process static would re-arm after suspend and silently kill
// FOD detection, which is exactly what happened after the old libgui crash.
// Use a non-persistent property so the write happens once per boot and a
// restart skips it; the touch IC keeps the arm from the first process.
inline void armSpecialAreaOnce() {
    static bool armed = false;
    if (armed || property_get_bool("vendor.fp.special_area_armed", false)) {
        armed = true;
        ALOGI("special_area: already armed this boot, skip");
        return;
    }
    bool ok = writeNode(specialAreaPath(), kSpecialAreaArm);
    ALOGI("special_area: %s arm %s", kSpecialAreaArm, ok ? "ok" : "failed");
    if (ok) {
        property_set("vendor.fp.special_area_armed", "1");
        armed = true;
    }
}

inline void armFod() {
    setFingerprintStatus(2);
}

inline void disarmFod() {
    setFingerprintStatus(0);
}

// Stock OpticalSetHbm's actuator. For screen-on it writes lcm_hbm_state,
// which the kernel only accepts while the display output is active. For
// screen-off it first enters DOZE (keeping the CRTC active) and then uses the
// DRM mtkfb HBM entry point; that is the same panel_hbm_set_cmdq path stock
// uses and it fires the real TRAN_FULL_HBM_EVENT=FULL_HBM_SET notifier.
inline bool hbmOn(bool screenOff) {
    if (screenOff) {
        if (!enterFingerprintDoze()) return false;
        if (writeNode(kMtkFb, "panel_hbm:1")) return true;
        leaveFingerprintDoze();
        ALOGE("fingerprint HBM: DRM endpoint unavailable");
        return false;
    }
    return writeNode(kLcmHbmState, "1");
}

inline void hbmOff(bool screenOff) {
    if (screenOff) {
        writeNode(kMtkFb, "panel_hbm:0");
        leaveFingerprintDoze();
        return;
    }
    writeNode(kLcmHbmState, "0");
}

inline void initOnce() {
    armSpecialAreaOnce();
}

}  // namespace jiiov
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace hardware
}  // namespace android
}  // namespace aidl
