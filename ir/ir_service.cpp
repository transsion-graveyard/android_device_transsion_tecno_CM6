/*
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "IRTX"

#include <aidl/android/hardware/ir/BnConsumerIr.h>
#include <aidl/android/hardware/ir/ConsumerIrFreqRange.h>
#include <android/binder_interface_utils.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <android/binder_status.h>
#include <fcntl.h>
#include <linux/lirc.h>
#include <log/log.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace aidl::android::hardware::ir {

namespace {

constexpr int32_t kCarrierFreqMinHz = 5000;
constexpr int32_t kCarrierFreqMaxHz = 80000;
constexpr uint32_t kDutyCycle = 33;
constexpr char kIrTxDevice[] = "/dev/lirc0";

}  // namespace

class ConsumerIr : public BnConsumerIr {
  public:
    ::ndk::ScopedAStatus getCarrierFreqs(
            std::vector<ConsumerIrFreqRange>* _aidl_return) override;
    ::ndk::ScopedAStatus transmit(int32_t in_carrierFreqHz,
                                  const std::vector<int32_t>& in_pattern) override;
};

::ndk::ScopedAStatus ConsumerIr::getCarrierFreqs(
        std::vector<ConsumerIrFreqRange>* _aidl_return) {
    _aidl_return->clear();
    ConsumerIrFreqRange range;
    range.minHz = kCarrierFreqMinHz;
    range.maxHz = kCarrierFreqMaxHz;
    _aidl_return->push_back(range);
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus ConsumerIr::transmit(int32_t in_carrierFreqHz,
                                          const std::vector<int32_t>& in_pattern) {
    if (in_carrierFreqHz < kCarrierFreqMinHz || in_carrierFreqHz > kCarrierFreqMaxHz) {
        ALOGD("IRTX: unsupported carrierFreqHz:%d", in_carrierFreqHz);
        return ::ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    uint32_t carrierFreqHz = static_cast<uint32_t>(in_carrierFreqHz);
    uint32_t dutyCycle = kDutyCycle;

    int fd = open(kIrTxDevice, O_RDWR);
    if (fd < 0) {
        ALOGE("file open fail, fd=%d\n", fd);
        return ::ndk::ScopedAStatus::ok();
    }

    int32_t n = static_cast<int32_t>(in_pattern.size());
    std::vector<int32_t> buffer(static_cast<size_t>(n) + 1);
    if (n > 0) {
        memcpy(buffer.data(), in_pattern.data(), static_cast<size_t>(n) * sizeof(int32_t));
    }
    buffer[n] = 1;

    int ret = ioctl(fd, LIRC_SET_SEND_CARRIER, &carrierFreqHz);
    if (ret < 0) {
        ALOGE("LIRC_SET_SEND_CARRIER fail, err: %d\n", ret);
    }
    ret = ioctl(fd, LIRC_SET_SEND_DUTY_CYCLE, &dutyCycle);
    if (ret < 0) {
        ALOGD("LIRC_SET_SEND_DUTY_CYCLE fail, err: %d\n", ret);
    }
    ALOGD("IRTX: Send to driver");

    int32_t count = n + (1 & ~n);
    ssize_t written = write(fd, buffer.data(), static_cast<size_t>(count) * sizeof(int32_t));
    if (written < 0) {
        ALOGE("irtx write fail\n");
    }
    ALOGD("Done, Turn OFF IRTX\n");

    close(fd);
    return ::ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::ir

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<::aidl::android::hardware::ir::ConsumerIr> service =
            ::ndk::SharedRefBase::make<::aidl::android::hardware::ir::ConsumerIr>();
    const std::string instance =
            std::string() + ::aidl::android::hardware::ir::IConsumerIr::descriptor + "/default";
    binder_status_t status =
            AServiceManager_addService(service->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGD("Failed to register ConsumerIr service");
        return EXIT_FAILURE;
    }

    ALOGD("ConsumerIr service starts to join service pool");
    ABinderProcess_joinThreadPool();
    ALOGD("ConsumerIr service exception, end!");
    return EXIT_FAILURE;
}