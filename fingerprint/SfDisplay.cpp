/*
 * Minimal android.gui.ISurfaceComposer client over libbinder_ndk.
 *
 * Transaction codes are from the generated BnSurfaceComposer for this tree
 * (android.gui ISurfaceComposer.aidl order):
 *   FIRST_CALL_TRANSACTION + 5  getPhysicalDisplayIds()
 *   FIRST_CALL_TRANSACTION + 6  getPhysicalDisplayToken(long)
 *   FIRST_CALL_TRANSACTION + 8  setPowerMode(IBinder, int)
 *
 * The interface token is written by AIBinder_prepareTransaction from the
 * descriptor associated with the AIBinder class below.
 */
#include "SfDisplay.h"

#include <android/binder_manager.h>
#include <android/binder_parcel.h>
#include <android/binder_status.h>
#include <utils/Log.h>

namespace aidl {
namespace android {
namespace hardware {
namespace biometrics {
namespace fingerprint {
namespace jiiov {

namespace {

constexpr transaction_code_t kGetPhysicalDisplayIds = 1 + 5;
constexpr transaction_code_t kGetPhysicalDisplayToken = 1 + 6;
constexpr transaction_code_t kSetPowerMode = 1 + 8;
constexpr const char* kInterface = "android.gui.ISurfaceComposer";

void* sfOnCreate(void*) { return nullptr; }
void sfOnDestroy(void*) {}
binder_status_t sfOnTransact(AIBinder*, transaction_code_t, const AParcel*, AParcel*) {
    // This class is only used on remote proxy binders.
    return STATUS_UNKNOWN_ERROR;
}

AIBinder_Class* sfClass() {
    static AIBinder_Class* clazz =
            AIBinder_Class_define(kInterface, sfOnCreate, sfOnDestroy, sfOnTransact);
    return clazz;
}

bool readStatus(const AParcel* reply) {
    AStatus* status = nullptr;
    binder_status_t rc = AParcel_readStatusHeader(reply, &status);
    bool ok = rc == STATUS_OK && status != nullptr && AStatus_isOk(status);
    if (!ok) {
        ALOGE("SfDisplay: transaction status rc=%d exception=%d", rc,
              status ? AStatus_getExceptionCode(status) : -1);
    }
    if (status != nullptr) AStatus_delete(status);
    return ok;
}

AParcel* beginTransaction(AIBinder* binder) {
    AParcel* in = nullptr;
    binder_status_t rc = AIBinder_prepareTransaction(binder, &in);
    if (rc != STATUS_OK) {
        ALOGE("SfDisplay: prepareTransaction failed rc=%d", rc);
        return nullptr;
    }
    return in;
}

}  // namespace

SfDisplay& SfDisplay::get() {
    static SfDisplay instance;
    return instance;
}

SfDisplay::~SfDisplay() {
    resetLocked();
}

void SfDisplay::resetLocked() {
    if (mToken != nullptr) {
        AIBinder_decStrong(mToken);
        mToken = nullptr;
    }
    if (mSf != nullptr) {
        AIBinder_decStrong(mSf);
        mSf = nullptr;
    }
}

bool SfDisplay::resolveLocked() {
    if (mSf == nullptr) {
        mSf = AServiceManager_checkService("SurfaceFlingerAIDL");
        if (mSf == nullptr) {
            ALOGE("SfDisplay: SurfaceFlingerAIDL not found");
            return false;
        }
        AIBinder_Class* clazz = sfClass();
        if (clazz == nullptr) {
            ALOGE("SfDisplay: AIBinder_Class_define failed");
            resetLocked();
            return false;
        }
        if (!AIBinder_associateClass(mSf, clazz)) {
            // Not fatal: the binder may already carry an equivalent class.
            ALOGW("SfDisplay: associateClass returned false");
        }
    }

    if (mToken != nullptr) return true;

    std::vector<int64_t> ids;
    if (!getIdsLocked(&ids) || ids.empty()) {
        ALOGE("SfDisplay: getPhysicalDisplayIds failed");
        resetLocked();
        return false;
    }

    mToken = getTokenLocked(ids.front());
    if (mToken == nullptr) {
        ALOGE("SfDisplay: getPhysicalDisplayToken failed for id=%lld",
              (long long)ids.front());
        resetLocked();
        return false;
    }
    return true;
}

bool SfDisplay::getIdsLocked(std::vector<int64_t>* ids) {
    AParcel* in = beginTransaction(mSf);
    if (in == nullptr) return false;

    AParcel* out = nullptr;
    binder_status_t rc = AIBinder_transact(mSf, kGetPhysicalDisplayIds, &in, &out, 0);
    if (rc != STATUS_OK || out == nullptr) {
        ALOGE("SfDisplay: getPhysicalDisplayIds transact rc=%d", rc);
        if (out != nullptr) AParcel_delete(out);
        return false;
    }

    bool ok = readStatus(out);
    if (ok) {
        int32_t count = 0;
        ok = AParcel_readInt32(out, &count) == STATUS_OK && count >= 0 && count <= 16;
        if (ok) {
            ids->resize(count);
            for (int32_t i = 0; i < count && ok; ++i) {
                ok = AParcel_readInt64(out, &(*ids)[i]) == STATUS_OK;
            }
        }
    }
    AParcel_delete(out);
    return ok;
}

AIBinder* SfDisplay::getTokenLocked(int64_t displayId) {
    AParcel* in = beginTransaction(mSf);
    if (in == nullptr) return nullptr;
    if (AParcel_writeInt64(in, displayId) != STATUS_OK) {
        AParcel_delete(in);
        return nullptr;
    }

    AParcel* out = nullptr;
    binder_status_t rc = AIBinder_transact(mSf, kGetPhysicalDisplayToken, &in, &out, 0);
    if (rc != STATUS_OK || out == nullptr) {
        ALOGE("SfDisplay: getPhysicalDisplayToken transact rc=%d", rc);
        if (out != nullptr) AParcel_delete(out);
        return nullptr;
    }

    AIBinder* token = nullptr;
    bool ok = readStatus(out);
    if (ok) {
        ok = AParcel_readStrongBinder(out, &token) == STATUS_OK && token != nullptr;
    }
    AParcel_delete(out);
    if (!ok && token != nullptr) {
        AIBinder_decStrong(token);
        token = nullptr;
    }
    return token;
}

bool SfDisplay::setPowerLocked(AIBinder* token, int mode) {
    AParcel* in = beginTransaction(mSf);
    if (in == nullptr) return false;
    if (AParcel_writeStrongBinder(in, token) != STATUS_OK ||
        AParcel_writeInt32(in, mode) != STATUS_OK) {
        AParcel_delete(in);
        return false;
    }

    AParcel* out = nullptr;
    binder_status_t rc = AIBinder_transact(mSf, kSetPowerMode, &in, &out, 0);
    if (rc != STATUS_OK || out == nullptr) {
        ALOGE("SfDisplay: setPowerMode(%d) transact rc=%d", mode, rc);
        if (out != nullptr) AParcel_delete(out);
        return false;
    }

    bool ok = readStatus(out);
    AParcel_delete(out);
    return ok;
}

bool SfDisplay::setPowerMode(int mode) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!resolveLocked()) return false;

    if (setPowerLocked(mToken, mode)) return true;

    // A dead/replaced SurfaceFlinger invalidates the cached token.
    resetLocked();
    if (!resolveLocked()) return false;
    return setPowerLocked(mToken, mode);
}

}  // namespace jiiov
}  // namespace fingerprint
}  // namespace biometrics
}  // namespace hardware
}  // namespace android
}  // namespace aidl
