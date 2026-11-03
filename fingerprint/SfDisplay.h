/*
 * Minimal SurfaceFlinger display-power client for the fingerprint HAL.
 *
 * This is deliberately NOT libgui/SurfaceComposerClient. The HAL is a vendor
 * binary, so libgui links /vendor/lib64/libbinder.so, whose servicemanager
 * AIDL is incompatible with the system servicemanager on this API-36 build:
 *
 *   Failed to getService in waitForService for SurfaceFlingerAIDL:
 *   Status(-129, EX_TRANSACTION_FAILED): 'BAD_TYPE: '
 *
 * libbinder_ndk is the /system copy and already backs this HAL's AIDL service
 * registration, so it resolves SurfaceFlingerAIDL correctly. We only need
 * three android.gui.ISurfaceComposer transactions, so issue them directly.
 */
#pragma once

#include <android/binder_ibinder.h>

#include <mutex>
#include <vector>

namespace aidl {
namespace android {
namespace hardware {
namespace biometrics {
namespace fingerprint {
namespace jiiov {

class SfDisplay {
  public:
    static SfDisplay& get();

    // mode follows android::hal::PowerMode: 0 OFF, 1 DOZE, 2 ON.
    bool setPowerMode(int mode);

  private:
    SfDisplay() = default;
    ~SfDisplay();

    bool resolveLocked();
    bool getIdsLocked(std::vector<int64_t>* ids);
    AIBinder* getTokenLocked(int64_t displayId);
    bool setPowerLocked(AIBinder* token, int mode);
    void resetLocked();

    std::mutex mMutex;
    AIBinder* mSf = nullptr;
    AIBinder* mToken = nullptr;
};

}  // namespace jiiov
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace hardware
}  // namespace android
}  // namespace aidl
