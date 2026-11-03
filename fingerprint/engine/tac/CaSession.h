/*
 * anc.hal.so rebuild — TEE/CA transport.
 *
 * RE: AncCaOpenSession @0x12dcd0, AncCaTransmit @0x12e010,
 *     AncCaSendCommand @0x12e1b0 (spec §4).
 */
#pragma once

#include <android/log.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "../EngLog.h"
#include "../AncTypes.h"

// libMcClient (Trustonic) TEEC client API — linked, stays prebuilt.
extern "C" {
typedef struct {
    uint32_t started;    // +0x00
    uint32_t paramTypes;  // +0x04 (=0x767)
    struct {
        uint64_t buffer;    // +0x00: req/resp/share pointer
        uint64_t size;      // +0x08: 0x234/0x154/0x15e000
        uint64_t reserved;  // +0x10: always 0 (stride 0x18, verified in
                            // AncCaTransmit @0x12e010 disasm)
    } params[4];  // @ +0x08, +0x20, +0x38, +0x50
} TEEC_Operation;
typedef void* TEEC_Context;
typedef void* TEEC_Session;
typedef struct {
    uint8_t timeLow[16];
} TEEC_UUID;
int TEEC_TT_SetLocalRegistryPath(const char* path);
int TEEC_InitializeContext(const char* name, void* ctx);
int TEEC_OpenSession(void* ctx, void* sess, const TEEC_UUID* uuid,
                     uint32_t login, void* conn, void* op, uint32_t* origin);
int TEEC_InvokeCommand(void* sess, uint32_t cmd, TEEC_Operation* op,
                       uint32_t* origin);
void TEEC_CloseSession(void* sess);
void TEEC_FinalizeContext(void* ctx);
}  // extern "C"

namespace anc {

inline uint64_t NowMs() {
    // AncGetCurrentCATimeMs @0x12d680: wall-clock based press time
    // (year/mon/day/hr/min/sec recombined + 0x9fa52400 bias), NOT monotonic.
    struct timeval tv{};
    gettimeofday(&tv, nullptr);
    struct tm lt{};
    localtime_r(&tv.tv_sec, &lt);
    return ((uint64_t)(lt.tm_year + 0x76c)) * 0x77bbdb000ull +
           (uint64_t)lt.tm_mon * 0x9fa52400ull +
           (uint64_t)lt.tm_mday * 86400000ull +
           (uint64_t)lt.tm_hour * 3600000ull + (uint64_t)lt.tm_min * 60000ull +
           (uint64_t)lt.tm_sec * 1000ull + (uint64_t)tv.tv_usec / 1000ull +
           0x9fa52400ull;
}

class CaSession {
  public:
    static CaSession& get() {
        static CaSession s;
        return s;
    }

    // AncCaOpenSession: registry + ctx(4B) + sess(0x40) + OpenSession.
    // Returns 0 ok, 2 malloc fail, 0xf open fail (stock codes).
    int open() {
        if (sess_ != nullptr) return 0;
        ANCENG_LOGV("CaSession::open");
        TEEC_TT_SetLocalRegistryPath(kTaRegistry);
        ctx_ = malloc(4);
        if (ctx_ == nullptr) return 2;
        memset(ctx_, 0, 4);
        if (TEEC_InitializeContext(nullptr, ctx_) != 0) {
            free(ctx_);
            ctx_ = nullptr;
            return 2;
        }
        sess_ = malloc(0x40);
        if (sess_ == nullptr) {
            TEEC_FinalizeContext(ctx_);
            free(ctx_);
            ctx_ = nullptr;
            return 2;
        }
        memset(sess_, 0, 0x40);
        TEEC_UUID uuid;
        memcpy(&uuid, kTaUuid, 16);
        uint32_t origin = 0;
        // TEEC_OpenSession(ctx, sess, &UUID@0x151d8, login=0, conn=0, op=0)
        if (TEEC_OpenSession(ctx_, sess_, &uuid, 0, nullptr, nullptr,
                             &origin) != 0) {
            free(sess_);
            sess_ = nullptr;
            TEEC_FinalizeContext(ctx_);
            free(ctx_);
            ctx_ = nullptr;
            return 0xf;
        }
        return 0;
    }

    // AncCaTransmit: stamp req+0x14, TEEC_InvokeCommand(sess, 0, op).
    // op: paramTypes 0x767, req 0x234 / resp 0x154 / shared 0x15e000.
    // Returns 0 ok, 0x1f transmit fail.
    int transmit(TacReq* req, TacResp* resp, uint8_t* shared) {
        resp->rc = 0;
        req->timestampMs = NowMs();
        ANCENG_LOGV("transmit cat=%u cmd=%u",
                            req->category, req->cmd[0]);
        if (req->category == 4) {
            char hex[0x234 * 2 + 1];
            const uint8_t* b = (const uint8_t*)req;
            for (int i = 0; i < 0x70; i++)
                snprintf(hex + i * 2, 3, "%02x", b[i]);
            hex[0x70 * 2] = 0;
            ANCENG_LOGV("req112=%s", hex);
        }
        TEEC_Operation op{};
        op.started = 0;
        op.paramTypes = kTacParamTypes;
        op.params[0].buffer = (uint64_t)req;
        op.params[0].size = kTacReqSize;
        op.params[1].buffer = (uint64_t)resp;
        op.params[1].size = kTacRespSize;
        op.params[2].buffer = (uint64_t)shared;
        op.params[2].size = kTacSharedSize;
        uint32_t origin = 0;
        ANCENG_LOGV("invoke...");
        int tRc = TEEC_InvokeCommand(sess_, kTacCommandId, &op, &origin);
        {
            char rhex[0x40 * 2 + 1];
            const uint8_t* rb = (const uint8_t*)resp;
            for (int i = 0; i < 0x40; i++)
                snprintf(rhex + i * 2, 3, "%02x", rb[i]);
            rhex[0x40 * 2] = 0;
            ANCENG_LOGV(                                "invoke rc=%d origin=%u respRc=%d resp64=%s",
                                tRc, origin, resp->rc, rhex);
        }
        if (tRc != 0) return 0x1f;
        return 0;
    }

    // AncCaSendCommand: size=0x234, SPI-clk gate, mutex, timing logs.
    // spiGate: {open(), close()} provided by sensor layer (may be null).
    struct SpiGate {
        int (*open)();
        int (*close)();
    };
    int send(TacReq* req, TacResp* resp, const SpiGate* gate) {
        pthread_mutex_lock(&mutex_);
        req->size = kTacReqSize;
        int rc;
        // Stock brackets EVERY TA transmit with the sensor SPI clock:
        // AncCaSendCommand @0x2e1b0 calls SensorDeviceOpenSpiClk (@0x2e240
        // thunk 0x3d6f0) before and SensorDeviceCloseSpiClk (@0x2e29c thunk
        // 0x3d700) after the invoke - not just for the cat-2 sensor
        // commands. SensorDevice registers its gate once (setSpiGate), so a
        // gate-less tacCmd()/tokenCmd() still gets the clock.
        const SpiGate* g = (gate && gate->open) ? gate : gate_;
        if (!spiHeld_) {
            if (g && g->open && (rc = g->open()) != 0) {
                pthread_mutex_unlock(&mutex_);
                return rc;
            }
            // Claim "held" ONLY when the clock was really switched on. The
            // old unconditional `spiHeld_ = true` let a gate-less command
            // (e.g. ALGORITHM_INIT_ENROLL during doEnroll) poison the flag,
            // so the next SENSOR_SET_POWER_MODE skipped gate->open(). With
            // the SPI clock off the TA cannot reach the sensor, waited the
            // hardware timeout (~10 s) and answered rc 12 - exactly what the
            // 12:24:34 capture shows for SENSOR_SET_POWER_MODE (12) and
            // VENDOR_CHECK_SENSOR_QRCODE (12); the SPI clock also stayed off
            // after spiClkOff(), so every later re-enroll returned 12/13/114.
            spiHeld_ = (g && g->open) ? true : false;
        }
        rc = transmit(req, resp, shared_);
        if (!keepSpi_ && spiHeld_) {
            if (g && g->close) g->close();
            spiHeld_ = false;
        }
        if (rc == 0) rc = resp->rc;
        pthread_mutex_unlock(&mutex_);
        return rc;
    }

    uint8_t* shared() { return shared_; }
    void setKeepSpi(bool k) { keepSpi_ = k; }
    // SensorDevice registers its open/close pair exactly once.
    void setSpiGate(SpiGate* g) { gate_ = g; }
    // The clock was physically switched off (SensorDevice::spiClkOff): drop
    // the held state so the next command opens it again.
    void releaseSpi() { spiHeld_ = false; }

  private:
    CaSession() {
        pthread_mutex_init(&mutex_, nullptr);
        // g_share_memory .bss 0x15e000 (NOT ION — spec correction).
        shared_ = new uint8_t[kTacSharedSize]();
    }
    void* ctx_ = nullptr;
    void* sess_ = nullptr;
    pthread_mutex_t mutex_;
    uint8_t* shared_ = nullptr;
    bool spiHeld_ = false;
    bool keepSpi_ = false;  // DAT_002a84c8
    SpiGate* gate_ = nullptr;  // registered by SensorDevice::get()
};

}  // namespace anc
