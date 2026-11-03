/*
 * Copyright (C) 2026 The Project Infinity-X
 *
 * ANC fingerprint extension service — source rebuild of
 * odm/lib64/libanc_extension_service.so.
 *
 * RE reference: logbook/aidl/ANC_EXTENSION_SERVICE_SPEC.md
 *  - AncFingerprintExtension::AncFingerprintExtension(void*) @0x104820:
 *      vtable, this+0x60 = OnExcuteCommand, this+0x68 = anc handle,
 *      AncSetExtensionCallback(handle, this+0x60)
 *  - AncRegisterExtensionService @0x104150: make<> + asBinder +
 *      AServiceManager_addService(IFingerprintExtension/default)
 *  - excuteCommand @0x104c90: AncExcuteCommand(handle, cmd, in, len,
 *      &out, &outlen) -> status = (ret != 0)
 *  - OnExcuteCommand @0x1048e0: copy buf -> vec, invoke client cb @+0x30
 *  - setNotify @0x104c00: store client cb at this+0x50/0x58
 */
#pragma once

#include <aidl/vendor/anc/fingerprint/extension/BnFingerprintExtension.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <dlfcn.h>
#include <mutex>
#include <vector>

namespace aidl {
namespace vendor {
namespace anc {
namespace fingerprint {
namespace extension {

using ::ndk::ScopedAStatus;
using ::ndk::SharedRefBase;
using ::ndk::SpAIBinder;

class AncFingerprintExtension : public BnFingerprintExtension {
  public:
    explicit AncFingerprintExtension(void* ancHandle) : mAncHandle(ancHandle) {
        // Mirror stock ctor: publish {thunk, handle} pair to the engine.
        mCbPair.fn = &AncFingerprintExtension::OnExcuteThunk;
        mCbPair.handle = ancHandle;
        auto setExtCb = reinterpret_cast<void (*)(void*, void*)>(
                dlsym(RTLD_DEFAULT, "AncSetExtensionCallback"));
        if (setExtCb != nullptr) {
            setExtCb(ancHandle, &mCbPair);
        } else {
            LOG(ERROR) << "AncSetExtensionCallback not found";
        }
    }

    ScopedAStatus excuteCommand(int32_t cmd, const std::vector<uint8_t>& in,
                                std::vector<AncStatus>* status,
                                std::vector<uint8_t>* out) override {
        // Mirror stock excuteCommand @0x104c90.
        auto ancExec = reinterpret_cast<int (*)(void*, int32_t, const uint8_t*,
                                                uint32_t, void**, uint32_t*)>(
                dlsym(RTLD_DEFAULT, "AncExcuteCommand"));
        int ret = -1;
        void* obuf = nullptr;
        uint32_t olen = 0;
        if (ancExec != nullptr) {
            ret = ancExec(mAncHandle, cmd, in.data(),
                          static_cast<uint32_t>(in.size()), &obuf, &olen);
        } else {
            LOG(ERROR) << "AncExcuteCommand not found";
        }
        if (obuf != nullptr && olen > 0) {
            const uint8_t* p = static_cast<const uint8_t*>(obuf);
            out->assign(p, p + olen);
        }
        AncStatus s;
        s.status = (ret != 0) ? 1 : 0;
        status->push_back(s);
        return ScopedAStatus::ok();
    }

    ScopedAStatus setNotify(
            const std::shared_ptr<IFingerprintExtensionClientCallback>& cb) override {
        // Mirror stock setNotify @0x104c00: stash client callback.
        std::lock_guard<std::mutex> l(mMutex);
        mClientCb = cb;
        return ScopedAStatus::ok();
    }

    // Invoked by the engine through the device+0x08 callback pair
    // (stock OnExcuteCommand @0x1048e0): forward to the AIDL client.
    static void OnExcuteThunk(void* handle, int32_t cmd, int32_t arg,
                              const uint8_t* buf, uint32_t len) {
        AncFingerprintExtension* self =
                static_cast<AncFingerprintExtension*>(handle);
        if (self == nullptr) return;
        std::shared_ptr<IFingerprintExtensionClientCallback> cb;
        {
            std::lock_guard<std::mutex> l(self->mMutex);
            cb = self->mClientCb;
        }
        if (!cb) {
            LOG(WARNING) << "fingerprint extension callback is nullptr";
            return;
        }
        std::vector<uint8_t> v;
        if (buf != nullptr && len > 0) v.assign(buf, buf + len);
        // Client callback: onExcuteCommand(long, int, int, byte[])
        // (parcel order from Bp stub disasm).
        cb->onExcuteCommand(0, cmd, arg, v);
    }

  private:
    struct CbPair {
        void (*fn)(void*, int32_t, int32_t, const uint8_t*, uint32_t);
        void* handle;
    };
    CbPair mCbPair{};
    void* mAncHandle = nullptr;
    std::mutex mMutex;
    std::shared_ptr<IFingerprintExtensionClientCallback> mClientCb;
};

// Mirrors stock AncRegisterExtensionService @0x104150 (libanc_extension_service.so):
// construct against the anc.hal.so device handle, publish
// vendor.anc.fingerprint.extension.IFingerprintExtension/default.
inline int AncExtensionRegister(void* ancHandle) {
    auto svc = ndk::SharedRefBase::make<AncFingerprintExtension>(ancHandle);
    binder_status_t status = AServiceManager_addService(
            svc->asBinder().get(),
            "vendor.anc.fingerprint.extension.IFingerprintExtension/default");
    if (status != STATUS_OK) {
        LOG(ERROR) << "register extension service failed: " << status;
        return -1;
    }
    LOG(INFO) << "register extension service success";
    return 0;
}

}  // namespace extension
}  // namespace fingerprint
}  // namespace anc
}  // namespace vendor
}  // namespace aidl
