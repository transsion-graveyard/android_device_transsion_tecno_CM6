/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * CM6 lights HAL.
 *
 * Replaces the proprietary MTK lights HAL (android.hardware.lights-service.mediatek)
 * and the Transsion tranled HAL (vendor.hardware.tranled-service).
 *
 * Devices:
 *   - BACKLIGHT : /sys/class/leds/lcd-backlight/brightness
 *   - notification/battery/camera LED : AW2023 red channel driven through the
 *     Transsion "tran_led_cmd" sysfs interface.
 *
 * tran_led_cmd protocol (6 integers, space separated):
 *   <mode> <color> <brightness> <t1> <t2> <current>
 *     mode : 0 = off, 1 = solid, 2 = blink, 3 = breathe
 *     color: channel bit index (0 = red on CM6)
 *     brightness: 0..255
 *     mode 2 -> t1 = on(hold) ms,  t2 = off ms
 *     mode 3 -> t1 = rise ms,      t2 = fall ms
 *     current: per-channel drive level 0..15 (I2C reg 0x39 + 3*color)
 */

#include <aidl/android/hardware/light/BnLights.h>
#include <aidl/android/hardware/light/FlashMode.h>
#include <aidl/android/hardware/light/HwLight.h>
#include <aidl/android/hardware/light/HwLightState.h>
#include <aidl/android/hardware/light/LightType.h>
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <array>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using aidl::android::hardware::light::BnLights;
using aidl::android::hardware::light::FlashMode;
using aidl::android::hardware::light::HwLight;
using aidl::android::hardware::light::HwLightState;
using aidl::android::hardware::light::LightType;

namespace {

constexpr const char* kBacklightPath = "/sys/class/leds/lcd-backlight/brightness";
constexpr const char* kBacklightMaxPath = "/sys/class/leds/lcd-backlight/max_brightness";
constexpr uint32_t kBacklightDefaultMax = 5119;

// The platform device is named "odm:tran_led_core"; the bus symlink is the most
// stable path, the /sys/devices path is the fallback.
constexpr const char* kTranLedPaths[] = {
        "/sys/bus/platform/devices/odm:tran_led_core/tran_led_cmd",
        "/sys/devices/platform/odm/odm:tran_led_core/tran_led_cmd",
};

// Notification LED channel on CM6 (red).
constexpr int kLedColorRed = 0;

// Max drive level for the AW2023 per-channel current register.
constexpr int kLedMaxCurrent = 15;

constexpr uint32_t kDefaultNotificationOnMs = 500;
constexpr uint32_t kDefaultNotificationOffMs = 2000;
constexpr uint32_t kChargeBreatheRiseMs = 1000;
constexpr uint32_t kChargeBreatheFallMs = 1000;

// Brightness for the charge indicator. The framework does not drive
// LightType::BATTERY unless its battery-light feature is switched on, so the
// charger is polled directly and the breathe pattern is emitted from here.
constexpr uint8_t kChargeBreatheBrightness = 255;

constexpr const char* kBatteryStatusPath = "/sys/class/power_supply/battery/status";
constexpr const char* kBatteryStatusCharging = "Charging";
// On this MTK platform battery/status keeps reporting "Charging" after the charge
// terminates; capacity_level flips to "Full" (charge_counter == charge_full, and
// the charger driver logs is_done/is_full). That is the only reliable "fully
// charged" signal here, so the breathe must stop on it, not on battery/status.
constexpr const char* kBatteryCapacityLevelPath =
        "/sys/class/power_supply/battery/capacity_level";
constexpr const char* kBatteryCapacityFull = "Full";
constexpr uint32_t kChargingPollMs = 1000;

bool writeToFile(const std::string& path, const std::string& value) {
    std::ofstream file(path);
    if (!file) {
        return false;
    }
    file << value;
    return file.good();
}

bool readUintFromFile(const std::string& path, uint32_t* value) {
    std::ifstream file(path);
    if (!file) {
        return false;
    }
    file >> *value;
    return true;
}

std::string readSysfsString(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return std::string();
    }

    std::string value;
    file >> value;
    return value;
}

std::string readBatteryStatus() {
    return readSysfsString(kBatteryStatusPath);
}

// "Charging" means the charger is attached AND the pack is still taking charge.
// The second condition is essential on CM6: battery/status keeps reporting
// "Charging" even at 100%, so without the capacity_level check the breathe runs
// forever on a full battery.
bool readChargingFromSysfs() {
    if (readBatteryStatus() != kBatteryStatusCharging) {
        return false;
    }
    return readSysfsString(kBatteryCapacityLevelPath) != kBatteryCapacityFull;
}

struct Rgb {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

// Mirrors LineageOS Color(uint32_t): extract AARRGGBB and alpha-scale the RGB.
Rgb colorToRgb(int32_t color) {
    uint8_t alpha = (color >> 24) & 0xFF;
    uint8_t red = (color >> 16) & 0xFF;
    uint8_t green = (color >> 8) & 0xFF;
    uint8_t blue = color & 0xFF;

    if (alpha > 0 && alpha < 0xFF) {
        red = red * alpha / 0xFF;
        green = green * alpha / 0xFF;
        blue = blue * alpha / 0xFF;
    }

    return {red, green, blue};
}

uint8_t colorToBrightness(int32_t color) {
    Rgb rgb = colorToRgb(color);
    return static_cast<uint8_t>((77 * rgb.red + 150 * rgb.green + 29 * rgb.blue) >> 8);
}

bool colorIsLit(int32_t color) {
    Rgb rgb = colorToRgb(color);
    return rgb.red != 0 || rgb.green != 0 || rgb.blue != 0;
}

// tran_led_cmd modes. Modes 2 (blink) and 3 (breathe) repeat in hardware until the
// node is written again.
constexpr int kLedModeOff = 0;
constexpr int kLedModeSolid = 1;
constexpr int kLedModeBlink = 2;
constexpr int kLedModeBreathe = 3;

// One resolved tran_led_cmd effect. t1/t2 are the mode-specific timing fields:
//   mode 2 -> t1 = on/hold ms, t2 = off ms
//   mode 3 -> t1 = rise ms,    t2 = fall ms
// Modes 2 (blink) and 3 (breathe) program the AW2023 once and never re-arm, so they only
// stay visible while something rewrites tran_led_cmd each cycle. Mode 0/1 are steady.
struct LedEffect {
    int mode;
    uint8_t brightness;
    uint32_t t1;
    uint32_t t2;

    bool operator==(const LedEffect& rhs) const {
        return mode == rhs.mode && brightness == rhs.brightness && t1 == rhs.t1 && t2 == rhs.t2;
    }
    bool operator!=(const LedEffect& rhs) const { return !(*this == rhs); }
};

}  // namespace

class Lights : public BnLights {
  public:
    Lights() {
        for (const char* path : kTranLedPaths) {
            if (std::ifstream(path).good()) {
                mTranLedPath = path;
                break;
            }
        }
        if (mTranLedPath.empty()) {
            LOG(WARNING) << "tran_led_cmd node not found, notification LED disabled";
        }

        if (!readUintFromFile(kBacklightMaxPath, &mBacklightMax) || mBacklightMax == 0) {
            mBacklightMax = kBacklightDefaultMax;
        }

        for (auto& state : mLedStates) {
            state = HwLightState{};
        }

        if (!mTranLedPath.empty()) {
            mCharging = readChargingFromSysfs();
            mMonitorThread = std::thread(&Lights::monitorChargingLoop, this);
        }
    }

    ~Lights() override {
        mStopMonitor = true;
        if (mMonitorThread.joinable()) {
            mMonitorThread.join();
        }
    }

    ndk::ScopedAStatus getLights(std::vector<HwLight>* out) override {
        std::lock_guard<std::mutex> lock(mMutex);

        if (std::ifstream(kBacklightPath).good()) {
            addLight(out, LightType::BACKLIGHT);
        }

        if (!mTranLedPath.empty()) {
            addLight(out, LightType::BATTERY);
            addLight(out, LightType::NOTIFICATIONS);
            addLight(out, LightType::ATTENTION);
            addLight(out, LightType::MICROPHONE);
            addLight(out, LightType::CAMERA);
        }

        return ndk::ScopedAStatus::ok();
    }

    ndk::ScopedAStatus setLightState(int32_t id, const HwLightState& state) override {
        std::lock_guard<std::mutex> lock(mMutex);

        switch (static_cast<LightType>(id)) {
            case LightType::BACKLIGHT:
                setBacklightLocked(state);
                break;
            case LightType::BATTERY:
            case LightType::NOTIFICATIONS:
            case LightType::ATTENTION:
            case LightType::MICROPHONE:
            case LightType::CAMERA: {
                size_t index = static_cast<size_t>(id);
                if (index >= mLedStates.size()) {
                    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
                }
                mLedStates[index] = state;
                mLedValid[index] = true;
                updateLedLocked();
                break;
            }
            default:
                return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        }

        return ndk::ScopedAStatus::ok();
    }

    binder_status_t dump(int fd, const char** /*args*/, uint32_t /*numArgs*/) override {
        std::lock_guard<std::mutex> lock(mMutex);
        dprintf(fd, "android.hardware.light-service.cm6\n");
        dprintf(fd, "backlight: %s (max %u)\n", kBacklightPath, mBacklightMax);
        dprintf(fd, "tran_led: %s\n", mTranLedPath.empty() ? "(none)" : mTranLedPath.c_str());
        return STATUS_OK;
    }

  private:
    static void addLight(std::vector<HwLight>* out, LightType type) {
        HwLight light;
        light.id = static_cast<int32_t>(type);
        light.type = type;
        light.ordinal = 0;
        out->push_back(light);
    }

    void setBacklightLocked(const HwLightState& state) {
        uint8_t brightness = colorToBrightness(state.color);
        uint32_t scaled = static_cast<uint32_t>(brightness) * mBacklightMax / 0xFF;
        writeToFile(kBacklightPath, std::to_string(scaled));
    }

    static bool stateIsLit(const HwLightState& state) {
        return colorIsLit(state.color);
    }

    static uint32_t notificationOnMs(const HwLightState& state) {
        return (state.flashMode == FlashMode::TIMED && state.flashOnMs > 0)
                       ? static_cast<uint32_t>(state.flashOnMs)
                       : kDefaultNotificationOnMs;
    }

    static uint32_t notificationOffMs(const HwLightState& state) {
        return (state.flashMode == FlashMode::TIMED && state.flashOffMs > 0)
                       ? static_cast<uint32_t>(state.flashOffMs)
                       : kDefaultNotificationOffMs;
    }

    // Resolve which effect the current light states ask for. The highest-priority lit
    // light wins, matching LineageOS' lights HAL; nothing lit means "off".
    LedEffect resolveEffectLocked() const {
        mKeepalive = false;

        static const LightType kPriority[] = {
                LightType::CAMERA,    LightType::MICROPHONE,  LightType::NOTIFICATIONS,
                LightType::ATTENTION, LightType::BATTERY,
        };

        for (LightType type : kPriority) {
            size_t index = static_cast<size_t>(static_cast<int32_t>(type));
            if (index >= mLedStates.size() || !mLedValid[index] ||
                !stateIsLit(mLedStates[index])) {
                continue;
            }

            const HwLightState& state = mLedStates[index];
            uint8_t brightness = colorToBrightness(state.color);
            if (brightness == 0) {
                break;
            }

            switch (type) {
                case LightType::CAMERA:
                case LightType::MICROPHONE:
                    // Privacy indicator: steady while the camera/mic is in use.
                    return {kLedModeSolid, brightness, 0, 0};
                case LightType::NOTIFICATIONS:
                case LightType::ATTENTION:
                    // Mode 2 is one-shot in the kernel just like mode 3: aw2023_led_work()
                    // maps t1/t2 onto hold_time_ms/off_time_ms, calls breathe_set once and
                    // returns without queueing the combo work. Re-issue every on+off.
                    mKeepalive = true;
                    return {kLedModeBlink, brightness, notificationOnMs(state),
                            notificationOffMs(state)};
                case LightType::BATTERY:
                    // Charging: smooth hardware breathe.
                    mKeepalive = true;
                    return {kLedModeBreathe, brightness, kChargeBreatheRiseMs,
                            kChargeBreatheFallMs};
            default:
                return {kLedModeSolid, brightness, 0, 0};
        }
        }

        // Charge indicator. Nothing from the framework is lit here, so fall back to the
        // hardware breathe pattern whenever the charger is attached. Priority still keeps
        // notifications and the privacy indicator above this.
        if (mCharging) {
            // aw2023_led_work() routes mode 3 straight into aw2023_led_breathe_set() and
            // returns without queueing the combo work, so a single tran_led_cmd store buys
            // exactly one rise+fall and the chip then sits idle. Only modes 4-6 re-arm via
            // aw2023_combo_delayed_work_handler. The stock vendor.hardware.tranled-service
            // loops frames in userspace for the same reason (auto_loop / "cmd loop"), so
            // re-issue on every rise+fall instead of writing once.
            mKeepalive = true;
            return {kLedModeBreathe, kChargeBreatheBrightness, kChargeBreatheRiseMs,
                    kChargeBreatheFallMs};
        }

        return {kLedModeOff, 0, 0, 0};
    }

    static int currentFor(uint8_t brightness) {
        int current = (static_cast<int>(brightness) * kLedMaxCurrent + 127) / 0xFF;
        if (brightness > 0 && current == 0) {
            current = 1;
        }
        return current;
    }

    void writeLedLocked(const LedEffect& effect) {
        const std::string command =
                std::to_string(effect.mode) + " " + std::to_string(kLedColorRed) + " " +
                std::to_string(effect.brightness) + " " + std::to_string(effect.t1) + " " +
                std::to_string(effect.t2) + " " + std::to_string(currentFor(effect.brightness)) +
                "\n";

        LOG(INFO) << "tran_led_cmd: " << command;

        if (!writeToFile(mTranLedPath, command)) {
            LOG(ERROR) << "Failed to write tran_led_cmd: " << command;
            return;
        }

        mApplied = effect;
        mLastWrite = std::chrono::steady_clock::now();
    }

    // Blink is timed here rather than in the chip. For mode 2 the driver stores t1/t2 at
    // struct offsets 0x260/0x268, but aw2023_led_breathe_set only reads rise (0x25c) and
    // fall (0x264) -- which mode 2 never writes, so they keep the 6ms default and the LED
    // only ever gets a ~6ms flash. Alternating solid/off reproduces the requested 500/2000
    // duty cycle exactly, and matches how the stock vendor.hardware.tranled-service worked
    // (its binary contains "auto_loop", "cmd loop" and "total frame").
    void applyBlinkLocked(const LedEffect& effect) {
        const auto now = std::chrono::steady_clock::now();

        if (!mApplied || *mApplied != effect) {
            // New notification (or the parameters changed): light immediately so the first
            // flash is not delayed by a poll interval.
            mBlinkOn = true;
            mBlinkFlipAt = now + std::chrono::milliseconds(effect.t1);
        } else if (now >= mBlinkFlipAt) {
            mBlinkOn = !mBlinkOn;
            mBlinkFlipAt = now + std::chrono::milliseconds(mBlinkOn ? effect.t1 : effect.t2);
        }

        if (mBlinkOn) {
            writeLedLocked({kLedModeSolid, effect.brightness, 0, 0});
        } else {
            writeLedLocked({kLedModeOff, 0, 0, 0});
        }

        // Track the logical request rather than the hardware write, so the next pass still
        // matches and can flip the phase.
        mApplied = effect;
    }

    // The kernel reprograms the AW2023 from scratch on every store and blanks the LEDs
    // first, so writing an unchanged effect restarts a running pattern from dark. The
    // framework does push unchanged effects (LogicalLight.setModes() sets mModesUpdate,
    // defeating the changed-only check in LightImpl.setLightLocked, LightsService.java:394),
    // so only write when the resolved effect actually changes -- except breathe, which the
    // chip runs once and never re-arms.
    void applyLedLocked(const LedEffect& effect) {
        if (mTranLedPath.empty()) {
            return;
        }

        if (effect.mode == kLedModeBlink) {
            applyBlinkLocked(effect);
            return;
        }

        if (mApplied && *mApplied == effect) {
            if (!mKeepalive) {
                return;
            }

            const std::chrono::milliseconds period(effect.t1 + effect.t2);
            if (std::chrono::steady_clock::now() - mLastWrite < period) {
                return;
            }
        }

        writeLedLocked(effect);
    }

    // How long the monitor may sleep before it has to act again. A blink phase is honoured
    // exactly: the sleep is never capped at kChargingPollMs, because the off phase (2000ms)
    // is longer than the poll interval and capping it would write mode 0 repeatedly and
    // stretch the cycle. Otherwise wake for a breathe re-arm or the battery poll.
    uint32_t nextWakeDelayLocked() const {
        const auto now = std::chrono::steady_clock::now();
        uint32_t delay = kChargingPollMs;

        if (mApplied && mApplied->mode == kLedModeBlink) {
            if (mBlinkFlipAt <= now) {
                return 1;
            }
            const auto due =
                    std::chrono::duration_cast<std::chrono::milliseconds>(mBlinkFlipAt - now)
                            .count() +
                    1;
            return static_cast<uint32_t>(due < 1 ? 1 : due);
        }

        if (mApplied && mApplied->mode == kLedModeBreathe && mKeepalive) {
            const auto dueTime = mLastWrite + std::chrono::milliseconds(mApplied->t1 + mApplied->t2);
            if (dueTime <= now) {
                return 1;
            }
            const auto due =
                    std::chrono::duration_cast<std::chrono::milliseconds>(dueTime - now).count() +
                    1;
            return static_cast<uint32_t>(due < 1 ? 1 : (due > delay ? delay : due));
        }

        return delay;
    }

    void updateLedLocked() { applyLedLocked(resolveEffectLocked()); }

    void monitorChargingLoop() {
        bool first = true;
        while (!mStopMonitor.load()) {
            std::string status = readBatteryStatus();
            std::string capacityLevel = readSysfsString(kBatteryCapacityLevelPath);
            bool charging = readChargingFromSysfs();
            uint32_t sleepMs = kChargingPollMs;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                // The first pass always applies, so the indicator comes up straight away
                // when the charger is already attached at boot.
                if (first || charging != mCharging) {
                    first = false;
                    mCharging = charging;
                    LOG(INFO) << "battery status \"" << status << "\" capacity_level \""
                              << capacityLevel << "\" -> charging=" << charging;
                }
                // Runs every pass, not just on a status change: applyLedLocked() ignores
                // repeats unless the effect is self-rearming, which has to re-arm the chip
                // once per cycle, and it flips the blink phase when one is due.
                updateLedLocked();
                sleepMs = nextWakeDelayLocked();
            }

            for (uint32_t waited = 0; waited < sleepMs && !mStopMonitor.load(); waited += 20) {
                std::this_thread::sleep_for(std::chrono::milliseconds(
                        std::min<uint32_t>(20, sleepMs - waited)));
            }
        }
    }

    std::mutex mMutex;
    std::string mTranLedPath;
    std::optional<LedEffect> mApplied;
    std::chrono::steady_clock::time_point mLastWrite{};
    bool mBlinkOn = false;
    std::chrono::steady_clock::time_point mBlinkFlipAt{};
    mutable bool mKeepalive = false;
    uint32_t mBacklightMax = kBacklightDefaultMax;
    std::array<HwLightState, 10> mLedStates{};
    std::array<bool, 10> mLedValid{};
    bool mCharging = false;
    std::atomic<bool> mStopMonitor{false};
    std::thread mMonitorThread;
};

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<Lights> lights = ndk::SharedRefBase::make<Lights>();

    const std::string instance = std::string() + Lights::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(lights->asBinder().get(), instance.c_str());
    CHECK(status == STATUS_OK) << "Failed to register " << instance;

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not be reached
}
