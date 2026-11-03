// android.hardware.biometrics.fingerprint-service.jiiov
// In-tree AIDL fingerprint HAL for TECNO CM6 (JV0307 optical UDFPS).
//
// Hosts the source-built engine (vendor/lib64/anc.hal.so, loaded via dlopen)
// and supplies the glue the stack expected from the touch controller:
// FOD arming via the sysfs/proc nodes. On-screen illumination stays on the
// AOSP UDFPS display path; screen-off puts the panel into DOZE here so the
// DRM HBM node can latch (this AOSP tree has no Transsion AOD app).
#include "Fingerprint.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

using aidl::android::hardware::biometrics::fingerprint::jiiov::Fingerprint;

int main() {
    // No vendor libbinder/libgui here. SurfaceFlinger is reached through
    // libbinder_ndk + raw android.gui.ISurfaceComposer transactions in
    // SfDisplay.cpp; libbinder_ndk is the /system copy and talks to the same
    // servicemanager this HAL registers with.
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    ABinderProcess_startThreadPool();

    std::shared_ptr<Fingerprint> hal = ndk::SharedRefBase::make<Fingerprint>();

    const char* instance =
            "android.hardware.biometrics.fingerprint.IFingerprint/default";
    binder_status_t status =
            AServiceManager_addService(hal->asBinder().get(), instance);
    if (status != STATUS_OK) {
        LOG(ERROR) << "failed to register " << instance << " status=" << status;
        return 1;
    }
    LOG(INFO) << instance << " registered";

    ABinderProcess_joinThreadPool();
    return 0;
}
