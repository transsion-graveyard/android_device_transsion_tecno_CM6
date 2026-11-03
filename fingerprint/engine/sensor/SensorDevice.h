/*
 * anc.hal.so rebuild — sensor device layer (/dev/jiiov_fp).
 *
 * RE: OpenSensorDevice @0x129b70, SensorDevice{Write,WriteRead} @0x129cf0/50,
 * SensorSetPowerMode @0x136a60, SensorTacCaptureImage @0x137520,
 * SensorGetParam @0x36670, SensorGetModuleId @0x371d0,
 * ioctl map §4e, CaSession for TA-backed ops.
 */
#pragma once

#include <fcntl.h>
#include <linux/ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../EngLog.h"
#include "../tac/CaSession.h"

namespace anc {

// POWER_MODE @0x40f98 (logbook/aidl/ANC_ENUM_TABLES.md) — the exact strings
// stock prints in "[ANC_HAL][SensorManager][D]: current power mode: %d (%s)".
inline const char* powerModeName(uint32_t mode) {
    switch (mode) {
        case 0: return "SENSOR_NORMAL";
        case 1: return "SENSOR_WAKEUP";
        case 2: return "SENSOR_SLEEP";
        case 3: return "SENSOR_POWER_ON";
        case 4: return "SENSOR_POWER_ON_INIT";
        case 5: return "SENSOR_POWER_OFF";
        case 6: return "SENSOR_HW_RESET";
        case 7: return "SENSOR_INIT";
        case 8: return "SENSOR_WAKEUP_RESET";
        case 9: return "SENSOR_HW_RESET_INIT";
        case 10: return "SENSOR_WAKEUP_SCAN";
        default: return "SENSOR_UNKNOWN";
    }
}

class SensorDevice {
  public:
    static SensorDevice& get() {
        static SensorDevice s;
        return s;
    }

    // OpenSensorDevice: stat + O_RDWR open; 0 ok, 0x2a fail.
    int open() {
        if (fd_ >= 0) return 0;
        struct stat st;
        if (stat(kFpDevice, &st) != 0) return 0x2a;
        fd_ = ::open(kFpDevice, O_RDWR);
        if (fd_ < 0) return 0x2a;
        return 0;
    }

    // Plain read/write data path (SensorDeviceWrite/WriteRead).
    int write(const void* buf, uint32_t len) {
        if (::write(fd_, buf, len) < 0) return 1;
        return 0;
    }
    // WriteRead: prefill out with in[0..wlen], then read rlen.
    int writeRead(const void* in, uint32_t wlen, void* out, uint32_t rlen) {
        memcpy(out, in, wlen);
        ssize_t n = ::read(fd_, out, rlen);
        if ((uint32_t)n != rlen) return 1;
        return 0;
    }

    // reset() cycles the sensor (and its clock domain), so CaSession's
    // "clock is open" state is no longer valid - drop it and let the next
    // send() re-open via the gate. Without this, doEnroll's initEnroll opened
    // the clock (keepSpi), then halSetPowerMode(1)'s reset() physically gated
    // it while spiHeld_ stayed true, so SENSOR_SET_POWER_MODE went out with
    // the clock off and timed out (rc 12).
    int reset() {
        int rc = ioctl0(FP_IO_RESET);
        CaSession::get().releaseSpi();
        return rc;
    }
    int powerOn() {
        int rc = ioctl0(FP_IO_POWER_ON);
        CaSession::get().releaseSpi();
        return rc;
    }
    int powerOff() {
        int rc = ioctl0(FP_IO_POWER_OFF);
        CaSession::get().releaseSpi();
        return rc;
    }
    int spiClkOn() { return ioctl0(FP_IO_SPI_CLK_ON); }
    int spiClkOff() {
        int rc = ioctl0(FP_IO_SPI_CLK_OFF);
        // The clock is physically off now; clear CaSession's held flag so the
        // next command opens it again (stock closes the clock after every
        // transmit unless EnableSensorSpiClkOnce(1) is holding it).
        CaSession::get().releaseSpi();
        return rc;
    }
    int irqInit() { return ioctl0(FP_IO_IRQ_INIT); }
    int setSpiSpeed(uint32_t hz) {
        return ::ioctl(fd_, FP_IO_SET_SPI_SPEED, &hz);
    }
    int reportKey(uint32_t key, uint32_t value) {
        struct {
            uint32_t k, v, pad;
        } ev{key, value, 0};
        return ::ioctl(fd_, FP_IO_REPORT_KEY, &ev);
    }

    // SensorInit TA command (sub 1, cat 2). Used by mode 4/7/9 paths.
    int sensorInitTa() {
        TacReq req{};
        TacResp resp{};
        req.category = (uint32_t)CmdType::SENSOR;
        uint32_t sub = 1;
        memcpy(req.cmd, &sub, 4);
        return CaSession::get().send(&req, &resp, spiGate());
    }

    // AncHalSensorPowerMode @0x121f50: full state machine, not just TA cmd.
    // Tracks current mode; mode transitions perform device/ioctl steps.
    int halSetPowerMode(uint32_t mode) {
        // Stock logs the attempt BEFORE the no-op early return: the DT capture
        // has 70 "current power mode: ..." lines but only 35 "sensor current
        // power mode: ..." lines, so the first is unconditional and the second
        // only follows a command that actually ran.
        ANCENG_LOGD(
            "[ANC_HAL][SensorManager][D]: current power mode: %d (%s), set sensor power mode: %d (%s)",
            curMode_, powerModeName(curMode_), mode, powerModeName(mode));
        if (mode == curMode_) return 0;
        int rc = 0;
        if (mode == 1 || mode == 8 || mode == 10) {
            // WAKEUP / WAKEUP_RESET / WAKEUP_SCAN: power-on reset + TA wake.
            rc = reset();
            if (rc == 0) rc = setPowerMode(mode);
        } else if (mode == 2) {
            rc = setPowerMode(2);  // SLEEP
        } else if (mode == 3) {
            rc = powerOn();
        } else if (mode == 4) {
            rc = reset();
            if (rc == 0) {
                // InitSensor + sleep (stock mode-4 path).
                rc = sensorInitTa();
                setPowerMode(2);
            }
        } else if (mode == 5) {
            rc = powerOff();
        } else if (mode == 6) {
            rc = reset();
        } else if (mode == 7) {
            rc = sensorInitTa();
            setPowerMode(2);
        } else if (mode == 9) {
            powerOn();
            rc = reset();
            if (rc == 0) {
                rc = sensorInitTa();
                if (rc != 0) powerOff();
            }
        } else {
            return 0x17;  // unsupported
        }
        if (rc == 0) {
            curMode_ = mode;
            ANCENG_LOGD(
                "[ANC_HAL][SensorManager][D]: sensor current power mode: %d (%s)",
                mode, powerModeName(mode));
        }
        return rc;
    }
    uint32_t curMode() const { return curMode_; }

    // SensorSetPowerMode @0x36a60: raw TA cmd (sub 7).
    int setPowerMode(uint32_t mode) {
        TacReq req{};
        TacResp resp{};
        req.category = (uint32_t)CmdType::SENSOR;
        uint32_t sub = 7;
        memcpy(req.cmd, &sub, 4);
        memcpy(req.cmd + 4, &mode, 4);
        return CaSession::get().send(&req, &resp, spiGate());
    }

    // SensorGetParam @0x36670: cat-2 passthrough, cmd id 4, payload at +8.
    int getParam(const void* in108, void* out, uint32_t outLen) {
        TacReq req{};
        TacResp resp{};
        req.category = (uint32_t)CmdType::SENSOR;
        req.cmd[0] = 4;
        memcpy(req.cmd + 8, in108, 0x108);
        int rc = CaSession::get().send(&req, &resp, spiGate());
        if (rc == 0) memcpy(out, resp.payload, outLen);
        return rc;
    }

    // SensorGetModuleId @0x371d0: param id 0x65, 0x13 bytes.
    int getModuleId(char* out, uint32_t outLen) {
        if (outLen < 0x14) return 3;
        uint8_t param[0x14] = {};
        param[0] = 0x65;
        uint8_t resp[0x14] = {};
        int rc = getParam(param, resp, sizeof(resp));
        if (rc == 0) {
            memcpy(out, resp, 0x13);
            out[0x13] = 0;
        }
        return rc;
    }

    // SensorTacCaptureImage @0x37520: cat 2, {mode, 5}; copies 0x75 img info.
    int captureImage(uint32_t mode, void* imgInfo /*0x75*/) {
        TacReq req{};
        TacResp resp{};
        req.category = (uint32_t)CmdType::SENSOR;
        uint32_t sub = 5;  // CONCAT44(mode,5) = (mode<<32)|5
        memcpy(req.cmd, &sub, 4);
        memcpy(req.cmd + 4, &mode, 4);
        int rc = CaSession::get().send(&req, &resp, spiGate());
        if (rc == 0 && imgInfo)
            memcpy(imgInfo, resp.payload + 8, 0x75);
        return rc;
    }

    CaSession::SpiGate* spiGate() { return &gate_; }

  private:
    SensorDevice() {
        gate_.open = []() -> int { return SensorDevice::get().spiClkOn(); };
        gate_.close = []() -> int {
            return SensorDevice::get().spiClkOff();
        };
        // Stock brackets EVERY TA transmit with this pair (AncCaSendCommand
        // @0x2e1b0); register it so even gate-less algo/token/vendor commands
        // open the clock. Without this, VENDOR_CHECK_SENSOR_QRCODE (always
        // sent gate-less by stock, VendorTransmit @0x35c20 passes 0) ran with
        // the SPI clock off and answered rc 12.
        CaSession::get().setSpiGate(&gate_);
    }
    int ioctl0(unsigned req) { return ::ioctl(fd_, req, 0); }
    int fd_ = -1;
    uint32_t curMode_ = 5;  // stock initial: SENSOR_POWER_OFF
    CaSession::SpiGate gate_{};
};

}  // namespace anc
