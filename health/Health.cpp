/*
 * Copyright (C) 2026 The Infinity-X Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <cstdint>
#include <memory>
#include <string_view>

#include <android-base/logging.h>
#include <android/binder_interface_utils.h>
#include <health-impl/ChargerUtils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::Health;
using aidl::android::hardware::health::HealthInfo;
using aidl::android::hardware::health::charger::ChargerCallback;
using aidl::android::hardware::health::charger::ChargerModeMain;
using ::android::hardware::health::InitHealthdConfig;
using ::ndk::ScopedAStatus;
using ::ndk::SharedRefBase;

namespace aidl::android::hardware::health {

/*
 * MTK's fuel-gauge sysfs interface reports charge_full, charge_full_design
 * and charge_counter in 0.01 mAh units on this platform, while the health AIDL
 * contract requires microamp-hours (uAh).  The raw value 520000 therefore
 * means 5200 mAh, not 520 mAh.
 *
 * Scale only values that are still in the small raw range.  This keeps the
 * conversion idempotent if a future kernel/DT change starts reporting uAh.
 */
class HealthImpl : public Health {
  public:
    using Health::Health;
    ~HealthImpl() override = default;

    ScopedAStatus getChargeCounterUah(int32_t* out) override {
        ScopedAStatus status = Health::getChargeCounterUah(out);
        if (status.isOk()) {
            ScaleLegacyChargeValue(out);
        }
        return status;
    }

  protected:
    void UpdateHealthInfo(HealthInfo* health_info) override {
        ScaleLegacyChargeValue(&health_info->batteryFullChargeUah);
        ScaleLegacyChargeValue(&health_info->batteryFullChargeDesignCapacityUah);
        ScaleLegacyChargeValue(&health_info->batteryChargeCounterUah);
    }

  private:
    static void ScaleLegacyChargeValue(int32_t* value) {
        if (*value > 0 && *value < 1000000) {
            *value *= 10;
        }
    }
};

}  // namespace aidl::android::hardware::health

static constexpr const char* kInstanceName = "default";
static constexpr std::string_view kChargerArg{"--charger"};

int main(int argc, char** argv) {
#ifdef __ANDROID_RECOVERY__
    android::base::InitLogging(argv, android::base::KernelLogger);
#endif

    using aidl::android::hardware::health::HealthImpl;

    auto config = std::make_unique<healthd_config>();
    InitHealthdConfig(config.get());

    auto binder = SharedRefBase::make<HealthImpl>(kInstanceName, std::move(config));

    if (argc >= 2 && argv[1] == kChargerArg) {
        return ChargerModeMain(binder, std::make_shared<ChargerCallback>(binder));
    }

    LOG(INFO) << "Starting CM6 health HAL";

    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
