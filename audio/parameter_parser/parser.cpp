/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <aidl/android/hardware/audio/core/VendorParameter.h>
#include <aidl/android/media/audio/BnHalAdapterVendorExtension.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>

#include <string>
#include <vector>

namespace {

using ::aidl::android::hardware::audio::core::VendorParameter;
using ::aidl::android::media::audio::BnHalAdapterVendorExtension;
using ::aidl::android::media::audio::IHalAdapterVendorExtension;

class MtkParameterParser final : public BnHalAdapterVendorExtension {
  public:
    ndk::ScopedAStatus parseVendorParameterIds(
            const IHalAdapterVendorExtension::ParameterScope& scope,
            const std::string& rawKeys,
            std::vector<std::string>* ids) override {
        LOG(INFO) << __func__ << ": " << scope.toString() << " " << rawKeys;
        ids->clear();
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus parseVendorParameters(
            const IHalAdapterVendorExtension::ParameterScope& scope,
            const std::string& rawKeysAndValues,
            std::vector<VendorParameter>* syncParameters,
            std::vector<VendorParameter>* asyncParameters) override {
        LOG(INFO) << __func__ << ": " << scope.toString() << " " << rawKeysAndValues;
        syncParameters->clear();
        asyncParameters->clear();

        // The MTK AIDL adapter passes VendorParameter.id straight to the
        // legacy AudioALSAHardware::setParameters() string parser. Preserve
        // the complete vendor parameter string, including the semicolons.
        // This matches the stock A16.3 parser behavior: every non-empty raw
        // key/value string is forwarded unchanged.
        if (!rawKeysAndValues.empty()) {
            VendorParameter parameter;
            parameter.id = rawKeysAndValues;
            syncParameters->push_back(std::move(parameter));
        }
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus parseBluetoothA2dpReconfigureOffload(
            const std::string& value, std::vector<VendorParameter>* parameters) override {
        LOG(INFO) << __func__ << ": " << value;
        parameters->clear();
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus parseBluetoothLeReconfigureOffload(
            const std::string& value, std::vector<VendorParameter>* parameters) override {
        LOG(INFO) << __func__ << ": " << value;
        parameters->clear();
        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus processVendorParameters(
            const IHalAdapterVendorExtension::ParameterScope& scope,
            const std::vector<VendorParameter>& parameters, std::string* values) override {
        LOG(INFO) << __func__ << ": " << scope.toString()
                  << " count=" << parameters.size();
        values->clear();
        return ndk::ScopedAStatus::ok();
    }
};

}  // namespace

extern "C" int createIHalAdapterVendorExtension() {
    auto parser = ndk::SharedRefBase::make<MtkParameterParser>();
    const std::string instance =
            std::string(IHalAdapterVendorExtension::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(parser->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "Failed to register " << instance << ": " << status;
        return -1;
    }
    LOG(INFO) << "Registered " << instance;
    return 0;
}
