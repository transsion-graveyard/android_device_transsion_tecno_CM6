/*
 * Copyright (C) 2026 The Project Infinity-X
 *
 * anc.hal.so source rebuild — shared types.
 *
 * RE reference: logbook/aidl/ANC_HAL_REBUILD_SPEC.md (§2-§4),
 * logbook/aidl/ANC_ENUM_TABLES.md (all enum values).
 *
 * Verified against stock anc.hal.so
 * (sha256 3cc2f3ec63f9e3839b5fae43ebfb7c561dbb0406c420dc97d7143aeb5415673d)
 * via Ghidra 12.1.4 decompilation (function@address cited per item).
 */
#pragma once

#include <pthread.h>
#include <stdint.h>

namespace anc {

// ---- TA wire (AncCaTransmit @0x12e010, AncCaSendCommand @0x12e1b0) ----
static constexpr uint32_t kTacReqSize = 0x234;
static constexpr uint32_t kTacRespSize = 0x154;
static constexpr uint32_t kTacSharedSize = 0x15e000;
static constexpr uint32_t kTacParamTypes = 0x767;  // TEEC param types
static constexpr uint32_t kTacCommandId = 0;      // TEEC_InvokeCommand cmd 0

// TA UUID bytes at 0x151d8: 00 00 02 03 ... (00000203-0000-...).
static constexpr uint8_t kTaUuid[16] = {
        0x00, 0x00, 0x02, 0x03, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
static constexpr char kTaRegistry[] = "/odm/app/mcRegistry";

// Request envelope: +0x00 u32 size (=0x234), +0x10 u32 category,
// +0x14 u64 timestamp ms; command block follows (0x11c/0x130).
struct TacReq {
    uint32_t size;        // +0x00 = 0x234
    uint32_t pad04[3];    // +0x04
    uint32_t category;    // +0x10: 2=SENSOR 3=ALGORITHM 4=AUTH_TOKEN...
    uint64_t timestampMs;  // +0x14 UNALIGNED (stock uses stur; keep packed)
    uint8_t cmd[0x130];   // +0x1c command block (low word = command id;
                          // builders memset 0x11c-0x130 of it)
    uint8_t tail[0x234 - 0x1c - 0x130];  // = 0xE8 padding to 0x234
} __attribute__((packed));
static_assert(sizeof(TacReq) == 0x234, "TacReq must be 0x234");
// Response envelope 0x154: +0x00 s32 rc, payload at +0x04.
struct TacResp {
    int32_t rc;  // +0x00
    uint8_t payload[kTacRespSize - 4];
};

// Command categories (CMD_TYPE @0x409b8).
enum class CmdType : uint32_t {
    NONE = 0, SENSOR = 2, ALGORITHM = 3, AUTH_TOKEN = 4, EXTENSION = 5,
    VENDOR = 6, DO_TEST = 7, TEST_SHARE_BUFFER = 8,
};

// ALGORITHM command ids (ALGO_CMD @0x40ad8; verified via CONCAT constants).
enum class AlgoCmd : uint32_t {
    INIT_ENROLL = 3,          // AlgoInitEnroll @0x2edf0
    DEINIT_ENROLL = 4,        // AlgoDeinitEnroll @0x2f3d0
    INIT_VERIFY = 5,          // AlgoInitVerify @0x2f760
    EXTRACT_FEATURE = 6,      // ExtractFeature @0x2f580
    ENROLL_FEATURE = 7,       // AlgoEnrollFeature @0x2f040
    COMPARE_FEATURE = 8,       // AlgoCompareFeature @0x2f950
    FEATURE_STUDY = 9,         // AlgoFeatureStudy @0x2fc30
    DEINIT_VERIFY = 10,        // AlgoDeinitVerify @0x2fdc0
    TRANSFER_IMAGE = 0x15,     // AlgoTransferImage @0x31430
    INIT_SLIDE_DETECT = 0x19,
    DEINIT_SLIDE_DETECT = 0x1b,
    DUMP_FILE = 0x1c,
};

// TOKEN sub-commands (category 4; verified via mov w8 stores).
enum class TokenCmd : uint32_t {
    SET_HMAC_KEY = 1,
    GET_ENROLL_CHALLENGE = 2,  // GetEnrollChallenge @0x32020
    AUTHORIZE_ENROLL = 3,      // AuthorizeEnroll @0x321c0
    SET_AUTH_CHALLENGE = 4,    // SetAuthenticateChallenge @0x323b0
    GET_AUTH_RESULT = 5,
    CHECK_TOKEN = 6,           // CheckToken @0x32540
    DELETE_FP = 0xe,           // DeleteFingerprint @0x306f0
    GET_ALL_IDS = 0xf,         // GetAllFingerprintsId @0x30890
};

// SENSOR command ids (category 2).
enum class SensorCmd : uint32_t {
    SET_PARAM = 3,      // (via SensorSetPowerMode-style builders)
    GET_PARAM = 4,      // SensorGetParam @0x36670
    CAPTURE_IMAGE = 5,  // SensorTacCaptureImage @0x37520
    SET_POWER_MODE = 7,  // SensorSetPowerMode @0x36a60
};

// ---- Return codes of interest (RETURNTYPE @0x40008; full table in spec) ----
static constexpr int kAncOk = 0;
static constexpr int kAncErrInvalidParam = 0x1e;
static constexpr int kAncErrCancel = 0x16;
static constexpr int kAncErrTimeout = 0x15;
static constexpr int kAncErrNoCalibration = 0x1a;
static constexpr int kAlgoExtractOk = 300;
static constexpr int kAlgoMatchOk = 500;
static constexpr int kEnrollContinue401 = 401;
static constexpr int kEnrollFinish400 = 400;

// ---- /dev/jiiov_fp ioctl map (verified, §4e) ----
static constexpr char kFpDevice[] = "/dev/jiiov_fp";
enum : unsigned {
    FP_IO_RESET = 0x6100,
    FP_IO_REQ_RES = 0x6101,
    FP_IO_REL_RES = 0x6102,
    FP_IO_ENABLE_IRQ = 0x6103,
    FP_IO_DISABLE_IRQ = 0x6104,
    FP_IO_IRQ_INIT = 0x6105,
    FP_IO_IRQ_DEINIT = 0x6106,
    FP_IO_SET_IRQ_MASK = 0x6107,
    FP_IO_CLEAR_IRQ_MASK = 0x6108,
    FP_IO_SPI_CLK_ON = 0x6109,
    FP_IO_SPI_CLK_OFF = 0x610a,
    FP_IO_POWER_ON = 0x610b,
    FP_IO_POWER_OFF = 0x610c,
    FP_IO_SET_SPI_SPEED = 0x4004610d,
    FP_IO_CLEAR_TP_FLAG = 0x610e,
    FP_IO_REPORT_KEY = 0x40086111,
};

// ---- Notify slots (device+0x00 table; adapter-verified) ----
enum NotifySlot : int {
    CHALLENGE_GENERATED = 0,  // -> legacy type 7
    CHALLENGE_REVOKED = 1,    // -> 8
    AUTH_ID_RETRIEVED = 2,    // -> 9
    AUTH_ID_INVALIDATED = 3,  // -> 10
    ENROLL_RESULT = 4,        // -> 3
    ACQUIRED = 5,             // -> 1
    AUTHENTICATED = 6,        // -> 5
    ERROR = 7,                // -> -1
    REMOVED = 8,              // -> 4
    ENUMERATED = 9,           // -> 6
    SLOT_COUNT = 10,
};
using NotifyFn = void (*)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

// Device object layout (GetFingerprintDevice @0x26ff0 returns &g_device).
struct Device {
    const NotifyFn* notify;  // +0x00 (AncSetNotifyCallback stores table)
    void* extCbPair;         // +0x08 (AncSetExtensionCallback)
    void* customCb;          // +0x10 (AncSetCustomCallback)
    void* fpManager;         // +0x18 (InitFingerprintManager sets)
};

// Producer (manager) state offsets (FpmEnroll/Authenticate verified).
struct Producer {
    void* vtable;          // +0x00 (PushTaskToConsumer at +0x10)
    void* device;          // +0x40 (device back-pointer)
    uint8_t hat[0x45];     // +0x48 (enroll HAT copy)
    int32_t groupId;       // +0x90
    int32_t enrollTimeout;  // +0x94 (=600)
    char storePath[0x100];  // +0x98
    uint64_t challenge;    // +0x198 (auth opId)
    uint64_t authId;       // +0x1a0
    uint8_t cmdBuf[0x30];  // +0x1b0 (extension command staging)
    pthread_mutex_t lock;  // +0x1e0
};

}  // namespace anc
