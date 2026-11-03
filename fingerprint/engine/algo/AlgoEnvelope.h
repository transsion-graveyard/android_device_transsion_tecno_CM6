/*
 * anc.hal.so rebuild — ALGORITHM + TOKEN + TEMPLATE envelope builders.
 *
 * RE: Algo* @0x2exxx (spec §4b), Token* @0x30xxx-0x32xxx (spec §4b2).
 * All builders: memset cmd block, CONCAT type id, AncCaSendCommand
 * (category 3 = ALGORITHM, 4 = AUTH_TOKEN).
 */
#pragma once

#include <stdint.h>
#include <string.h>

#include "../tac/CaSession.h"

namespace anc {

// Generic category-3/4 sender: cmd block 0x130, id in low word.
inline int tacCmd(CmdType cat, uint32_t id, const void* extra = nullptr,
                  uint32_t extraLen = 0, void* respOut = nullptr,
                  uint32_t respLen = 0) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)cat;
    req.cmd[0] = (uint8_t)id;
    req.cmd[1] = (uint8_t)(id >> 8);
    req.cmd[2] = (uint8_t)(id >> 16);
    req.cmd[3] = (uint8_t)(id >> 24);
    if (extra && extraLen) memcpy(req.cmd + 8, extra, extraLen);
    int rc = CaSession::get().send(&req, &resp, nullptr);
    // send() returns resp->rc on TEEC success; TA success codes are
    // nonzero (300/400/401/500). Copy payload whenever TEEC delivered
    // a response (rc != 0x1f transmit-fail), NOT only when rc == 0.
    if (rc != 0x1f && respOut && respLen) { uint32_t n = respLen < sizeof(resp.payload) ? respLen : sizeof(resp.payload); memcpy(respOut, resp.payload, n); }
    return rc;
}

namespace algo {

// Init sequence (InitFingerprintManager @0x1f6b0 + stock TRANSMIT order):
// EXT_SET_TIME(2) -> SENSOR_INIT(1) -> POWER(7) -> VENDOR_PROJ(1) ->
// GET_VERSION(0) -> POWER(7) -> GET_PARAM(4) -> ALGO_INIT(1) ->
// ALGO_VERSION(17) -> EXT_CALIB(26) -> POWER(7) -> EXT_DCS(19) ->
// VENDOR_QR(2) -> EXT_UPDATE(34) -> TMPL_GROUP(12) -> TMPL_LOAD(11) ->
// TMPL_AUTHID(13) -> TOKEN_CHALLENGE(2).
inline int extensionSetTime() {
    uint64_t ms = 0;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    return tacCmd(CmdType::EXTENSION, 2, &ms, 8);
}
inline int sensorInit() { return tacCmd(CmdType::SENSOR, 1); }
inline int vendorSetProject() {
    // VENDOR_SET_PROJECT_INFO(1): "CM6" + build id (from init log
    // "project meta info:4015_3feea1993_05").
    const char info[] = "4015_3feea1993_05";
    return tacCmd(CmdType::VENDOR, 1, info, sizeof(info));
}
inline int algoGetVersion(char* out, uint32_t len) {
    return tacCmd(CmdType::ALGORITHM, 17, nullptr, 0, out, len);
}
inline int extensionLoadCalibration() {
    return tacCmd(CmdType::EXTENSION, 26);
}
inline int extensionInitDataCollect() {
    return tacCmd(CmdType::EXTENSION, 19);
}
inline int vendorGetQr(char* out, uint32_t len) {
    return tacCmd(CmdType::VENDOR, 2, nullptr, 0, out, len);
}
// VENDOR_CHECK_SENSOR_QRCODE: category 6 (VENDOR), cmd 3 — confirmed from the
// HAL's own command-name table (@0x40a80, {u64 id, u64 name_ptr} stride 0x10:
// `3 VENDOR_CHECK_SENSOR_QRCODE`).
// RE: CustomCheckSensorQRCode @0x39e90 builds a 0xC-byte cmd block {3,0,0}
// and calls VendorTransmit @0x35c20, which sets req+0x10 = 6, memcpys the
// 0xC-byte block to req+0x1C, transmits, then copies resp+4 -> out (0x108).
// Stock calls this wedge at the head of EVERY enroll session
// (OpticalEnrollInit @0x3a2b0) and EVERY auth (OpticalAuthInit @0x3a4a0),
// sandwiched between setPowerMode(1) (WAKEUP) and setPowerMode(2) (SLEEP).
// Omitting it is the single structural difference left in our auth path:
// the TA then answers ALGORITHM_COMPARE_FEATURE with rc 0 instead of 500.
inline int vendorCheckSensorQrCode() {
    return tacCmd(CmdType::VENDOR, 3, nullptr, 0, nullptr, 0);
}
inline int extensionUpdateFile() { return tacCmd(CmdType::EXTENSION, 34); }
inline int templateSetActiveGroup(uint32_t gid, const char* path) {
    // TEMPLATE_SET_ACTIVE_GROUP(12, category 3): gid inline at cmd+8, path
    // staged in the TEEC shared buffer (stock reads path from shared buf).
    uint8_t* shared = CaSession::get().shared();
    size_t n = strlen(path) + 1;
    if (n > 0x1000) n = 0x1000;
    memcpy(shared, path, n);
    uint8_t buf[8] = {};
    memcpy(buf, &gid, 4);
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    req.cmd[0] = 12;
    memcpy(req.cmd + 8, buf, 8);
    return CaSession::get().send(&req, &resp, nullptr);
}
inline int templateLoadDatabase() {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    req.cmd[0] = 11;
    return CaSession::get().send(&req, &resp, nullptr);
}
inline int templateGetAuthId(uint64_t* out) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    req.cmd[0] = 13;
    int rc = CaSession::get().send(&req, &resp, nullptr);
    // RE GetAuthenticatorId @0x30190: after AncCaSendCommand the response
    // payload (resp+4) is memcpy'd to a local buffer, and the authId is read
    // with `ldur x8, [sp, 4]` -> *out = *(u64*)(payload + 4), NOT payload + 0.
    // The payload is {u32 pad, u64 authId}. Reading payload+0 (the old code)
    // returned bswap32(authId_lo) << 32 — e.g. 0x29BB8D0100000000 instead of
    // 0xEB5E2D8329BB8D01 — which makes the framework reject every HAT.
    if (rc == 0 && out) memcpy(out, resp.payload + 4, 8);
    return rc;
}
// UpdateAuthenticatorId @0x30370 (cat 3 cmd 22 == TEMPLATE_UPDATE_AUTHENTICATOR_ID,
// block+0 = 0x16, NO in-param): the TA mints a fresh authenticator id and
// returns it. RE: the request block is memset then only +0 is set, and the
// response is read with `ldur x8, [sp, 4]; str x8, [x19]` -> payload + 4.
inline int templateUpdateAuthId(uint64_t* out) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    req.cmd[0] = 22;
    int rc = CaSession::get().send(&req, &resp, nullptr);
    if (rc == 0 && out) memcpy(out, resp.payload + 4, 8);
    return rc;
}

// AlgoInitEnroll @0x2edf0 (0x1a = no-calibration/already-init passthrough).
inline int algoInit() { return tacCmd(CmdType::ALGORITHM, 1); }
inline int initEnroll() { return tacCmd(CmdType::ALGORITHM, 3); }
// AlgoDeinitEnroll @0x2f3d0. Disasm (block base sp+0x150 == req+0x1C == cmd):
//   0x2f480 cmp  w21, 1        ; w21 = param_2 & 0xff   (the "done" arg)
//   0x2f484 mov  w8, 4
//   0x2f488 cset w9, eq        ; w9 = (done == 1)
//   0x2f498 str  w8, [sp,0x150]  -> cmd+0x00 = 4
//   0x2f4a0 str  w9, [sp,0x154]  -> cmd+0x04 = done     <-- HERE
//   0x2f504 add x0, req, 0x1c ; 0x2f508 add x1, sp,0x150 ; w2=0x130 ; memcpy
// So the done flag lives at cmd+4. tacCmd() stages `extra` at cmd+8, hence
// the old code always transmitted cmd+4 == 0 == "ABORT": the TA discarded the
// session template. Observed in /tmp/opencode/auth_test2.logcat:
//   12:22:37.852 DEINIT_ENROLL -> 12:22:37.880 = 28 ms (stock 277-303 ms)
//   response all-zero -> fid 0
//   12:22:37.889 TEMPLATE_GET_ALL_FINGERPRINTS_IDS -> count=0
//   then every ALGORITHM_COMPARE_FEATURE returned 0 instead of 500.
// fid out: payload+0x0C (`0x2f53c ldr w8,[sp,0xc]`, sp = resp+4 copy).
inline int deinitEnroll(bool done, void* idOut = nullptr) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    uint32_t hdr = 4;
    uint32_t flag = done ? 1 : 0;
    memcpy(req.cmd + 0x00, &hdr, 4);   // cmd+0 = id 4
    memcpy(req.cmd + 0x04, &flag, 4);  // cmd+4 = done (NOT cmd+8)
    int rc = CaSession::get().send(&req, &resp, nullptr);
    if (rc == 0 && idOut) memcpy(idOut, resp.payload + 0x0C, 4);
    return rc;
}
// AlgoInitVerify @0x12f760: local_4f0[0] = CONCAT14(param_1==1, 5) ->
// cmd+0 = id 5, cmd+4 (byte) = (study==1). _local_4e0 at local_4f0+0x10 ->
// cmd+16 = param_2. Stock DoAuthenticate calls AlgoInitVerify(study_flag, 0)
// where study_flag is the manager's adaptive-learning setting (log
// "study flag = %d"/"need_study:1"). The old tacCmd() put p2 at cmd+8 and
// always sent cmd+4 = 0 (study off).
inline int initVerify(bool study, uint32_t p2 = 0) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    uint32_t five = 5;
    uint32_t flag = study ? 1 : 0;
    memcpy(req.cmd + 0, &five, 4);  // cmd+0 = id 5
    memcpy(req.cmd + 4, &flag, 4);  // cmd+4 = (study==1)
    memcpy(req.cmd + 0x14, &p2, 4);  // cmd+0x14 = param_2 (RE: str w1,[block+0x14])
    return CaSession::get().send(&req, &resp, nullptr);
}
// ExtractFeature @0x12f580: enroll uses ExtractFeature(0,0,0,0,0) (bare
// id 6). Restored after the verify-param rewrite below.
inline int extractFeature(uint32_t* score = nullptr) {
    uint8_t resp[0x14d] = {};
    int rc = tacCmd(CmdType::ALGORITHM, 6, nullptr, 0, resp, sizeof(resp));
    if (rc == 300 && score) memcpy(score, resp, 4);
    return rc;
}
// ExtractFeature @0x12f580 window (source local_510, memcpy 0x130):
//   local_510 = CONCAT44(param_3, 6)     -> cmd+0 = 6,  cmd+4  = param_3
//   local_500 = CONCAT44(param_1, param_2)-> cmd+16= param_2, cmd+20= param_1
//   uStack_4f8= CONCAT44(hi, param_4)    -> cmd+24= param_4
// Enroll (AlgoEnroll @0x12efc8): ExtractFeature(0,0,0,0,0) — bare id.
// Verify (AncAuthVerifyFeature @0x12ab50 -> AlgoVerifyExtractFeature
// @0x12f948): AlgoVerifyExtractFeature(param_2[1], 0, dcs, 0) expands to
// ExtractFeature(1, idx, 0, dcs, 0). So verify needs:
//   cmd+0=6, cmd+4=p3=0, cmd+16=p2=idx, cmd+20=p1=1, cmd+24=p4=dcs.
// The earlier tacCmd() staging wrote cmd+8 only; the previous manual build
// put dcs at cmd+4 and idx at cmd+20 (both wrong) -> TA rc=1.
inline int extractFeatureVerify(uint32_t idx, uint32_t dcs,
                                uint32_t* score = nullptr) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    uint32_t six = 6, zero = 0, one = 1;
    memcpy(req.cmd + 0, &six, 4);    // cmd+0  = id 6
    memcpy(req.cmd + 4, &zero, 4);   // cmd+4  = p3 (0)
    memcpy(req.cmd + 16, &idx, 4);   // cmd+16 = p2 (auth slot index)
    memcpy(req.cmd + 20, &one, 4);   // cmd+20 = p1 (1 = verify mode)
    memcpy(req.cmd + 24, &dcs, 4);   // cmd+24 = p4 (DCS word)
    int rc = CaSession::get().send(&req, &resp, nullptr);
    if (rc == 300 && score) memcpy(score, resp.payload, 4);
    return rc;
}
// AlgoEnrollFeature @0x2f040: stock writes resp+4[0..3] to *fid_out,
// resp+4[4..7] to *remaining_out. Returns TA rc (401=continue, 400=done).
inline int enrollFeature(const void* data, uint32_t len, uint32_t* fidOut,
                         uint32_t* remOut) {
    uint8_t resp[0x14d] = {};
    int rc = tacCmd(CmdType::ALGORITHM, 7, data, len, resp, sizeof(resp));
    ANCENG_LOGV("enrollFeat rc=%d out=%02x%02x%02x%02x%02x%02x%02x%02x",
                        rc, resp[0], resp[1], resp[2], resp[3], resp[4],
                        resp[5], resp[6], resp[7]);
    // Disasm AlgoEnrollFeature @0x12f040: *param_1 = payload[0..4],
    // *param_2 = payload[12..16] (uStack_648 high). Stock-log evidence:
    // 0x13 at payload+0 with displayed remaining=19 -> param_1 = remaining
    // for 401. For 400 the fid rides at payload+12 (EnrollFinish prints it
    // BEFORE deinit). Caller passes (fidOut, remOut) = (param_2-slot,
    // param_1-slot): fid <- +12, remaining <- +0.
    if (rc == 401 || rc == 400 || rc == 0x191 || rc == 0) {
        if (remOut) memcpy(remOut, resp, 4);
        if (fidOut) memcpy(fidOut, resp + 12, 4);
    }
    return rc;
}
struct CompareOut {
    uint32_t fid;
    uint32_t status;
    uint8_t hat[0x45];
    uint8_t extra[0x100];
};
// AlgoCompareFeature @0x12f950: local_500 = CONCAT44(hi,8) -> cmd+0 = 8;
// uStack_4d8 = CONCAT44(*(u32*)(param_2+0x100),...) at local_500+0x28 ->
// cmd+0x28 = data length; local_4d0 (local_500+0x30) = memscpy(param_2,len)
// -> cmd+0x30 = data. Stock passes param_3 = the zeroed {data, len@0x100}
// struct (GetHmacKeyFromKeymasterTa is a stub), so len == 0 in practice.
// Response (local_650 = resp payload): *param_1 = payload[0x0C] (fid),
// param_1[2] = payload[0x28] (status), HAT = payload[0x30],
// HAT length = payload[0x130].
inline int compareFeature(const void* feat, uint32_t len, CompareOut* out) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    uint32_t eight = 8;
    memcpy(req.cmd + 0, &eight, 4);   // cmd+0 = id 8
    memcpy(req.cmd + 0x2C, &len, 4);  // cmd+0x2C = feature length
                                      // (RE AlgoCompareFeature @0x2f950:
                                      //  str w6, [sp, 0x17c] == block+0x2C,
                                      //  NOT block+0x28)
    if (feat && len) {
        uint32_t n = len < 0x100 ? len : 0x100;
        memcpy(req.cmd + 0x30, feat, n);  // cmd+0x30 = feature data
    }
    int rc = CaSession::get().send(&req, &resp, nullptr);
    if (out) {
        memset(out, 0, sizeof(*out));
        if (rc != 0x1f) {
            memcpy(&out->fid, resp.payload + 0x0C, 4);
            memcpy(&out->status, resp.payload + 0x28, 4);
            memcpy(out->hat, resp.payload + 0x30, 0x45);
        }
    }
    ANCENG_LOGV(        "compare rc=%d fid=%u status=%u p0=%02x%02x%02x%02x p28=%02x%02x%02x%02x p30=%02x%02x%02x%02x",
        rc, out ? out->fid : 0, out ? out->status : 0,
        resp.payload[0], resp.payload[1], resp.payload[2], resp.payload[3],
        resp.payload[0x28], resp.payload[0x29], resp.payload[0x2a], resp.payload[0x2b],
        resp.payload[0x30], resp.payload[0x31], resp.payload[0x32], resp.payload[0x33]);
    return rc;
}
// AlgoFeatureStudy @0x2fc30. Disasm (block base sp+0x150):
//   0x2fcd4 mov  w8, 9
//   0x2fce4 str  w19, [sp,0x15c]   -> cmd+0x0C = param_1 (fid)   <-- HERE
//   0x2fcec str  w8,  [sp,0x150]   -> cmd+0x00 = 9
// tacCmd() staged fid at cmd+8, so the TA studied fid 0.
inline int featureStudy(uint32_t fid) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    uint32_t hdr = 9;
    memcpy(req.cmd + 0x00, &hdr, 4);  // cmd+0 = id 9
    memcpy(req.cmd + 0x0C, &fid, 4);  // cmd+0x0C = fid
    return CaSession::get().send(&req, &resp, nullptr);
}
inline int deinitVerify() { return tacCmd(CmdType::ALGORITHM, 10); }
// Image / slide / dump helpers.
// AlgoTransferImage @0x31430. Disasm (block base sp+0x160):
//   0x31464 mov  w22, w0           ; param_1 = kind
//   0x31xxx str  w8,  [sp,0x160]   -> cmd+0x00 = 0x15
//   0x31xxx str  w22, [sp,0x17c]   -> cmd+0x1C = kind
// tacCmd() staged kind at cmd+8. Benign while kind==0 (all call sites pass
// 0), but aligned so the byte layout matches stock exactly.
inline int transferImage(uint32_t kind, void* out = nullptr) {
    (void)out;
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    uint32_t hdr = 0x15;
    memcpy(req.cmd + 0x00, &hdr, 4);   // cmd+0 = id 0x15
    memcpy(req.cmd + 0x1C, &kind, 4);  // cmd+0x1C = kind
    return CaSession::get().send(&req, &resp, nullptr);
}
// EXTENSION_AUTH_RESULT_COLLOCT: category 5 (EXTENSION), cmd 20 — confirmed
// from the HAL command-name table (@0x40a80: `20 EXTENSION_AUTH_RESULT_COLLOCT`).
// Stock sends it as the FIRST step of the post-match auth tail
// (DoAuthenticate @0x22fec -> AncAuthFinish @0x2ae90), i.e.
//   compare(8)=500 -> AUTH_RESULT_COLLOCT(20) -> TRANSFER_IMAGE(21)
//   -> FEATURE_STUDY(9) -> DEINIT_VERIFY(10) -> power(2)
// We previously sent none of these.
inline int extensionAuthResultCollect() {
    return tacCmd(CmdType::EXTENSION, 20);
}
// EXTENSION_SINGLE_ENROLL_COLLOCT @stock: cat 5 (EXTENSION), id 21,
// sent after every enroll-feature (401 and 400), rc 0.
inline int extensionSingleEnrollCollect() {
    return tacCmd(CmdType::EXTENSION, 21);
}
// EXTENSION_ENROLL_END_COLLOCT @stock: cat 5, id 22, after DEINIT, rc 0.
inline int extensionEnrollEndCollect() {
    return tacCmd(CmdType::EXTENSION, 22);
}
inline int initSlideDetect() { return tacCmd(CmdType::ALGORITHM, 0x19); }
inline int deinitSlideDetect() { return tacCmd(CmdType::ALGORITHM, 0x1b); }

}  // namespace algo

namespace token {

// TOKEN payload layout (verified in AuthorizeEnroll @0x1321c0 disasm):
// cmd+0x00 u32 sub-cmd, cmd+0x10 u32 payload length, cmd+0x14 payload bytes.
inline int tokenCmd(uint32_t sub, const void* payload, uint32_t len,
                    void* respOut = nullptr, uint32_t respLen = 0) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::AUTH_TOKEN;
    req.cmd[0] = (uint8_t)sub;
    req.cmd[1] = (uint8_t)(sub >> 8);
    req.cmd[2] = (uint8_t)(sub >> 16);
    req.cmd[3] = (uint8_t)(sub >> 24);
    memcpy(req.cmd + 0x10, &len, 4);
    if (payload && len) memcpy(req.cmd + 0x14, payload, len);
    int rc = CaSession::get().send(&req, &resp, nullptr);
    if (rc == 0 && respOut && respLen) {
        uint32_t copy = respLen;
        if (copy > sizeof(resp.payload)) copy = sizeof(resp.payload);
        memcpy(respOut, resp.payload, copy);
    }
    return rc;
}

// GetEnrollChallenge @0x32020: sub 2 -> u64 challenge.
// CONCAT44(uStack_6f8, uStack_6fc) = bytes resp+4..resp+12 in order
// (6fc=low @+4, 6f8=high @+8). Copy 8B from resp+4.
inline int getEnrollChallenge(uint64_t* out) {
    uint8_t resp[0x150] = {};
    int rc = tokenCmd(2, nullptr, 0, resp, sizeof(resp));
    if (rc == 0 && out) memcpy(out, resp + 4, 8);
    return rc;
}
// AuthorizeEnroll @0x321c0: sub 3, HAT in, 0x45 len.
inline int authorizeEnroll(const uint8_t hat[0x45]) {
    // Belt-and-braces: stage HAT both inline (cmd+0x14, verified in disasm)
    // and at shared-buffer start (in case TA reads it from there).
    memcpy(CaSession::get().shared(), hat, 0x45);
    return tokenCmd(3, hat, 0x45);
}
// SetAuthenticateChallenge @0x323b0 (396 bytes) disasm:
//   memset(sp+0x118, 0, 0x218)   ; block base = sp+0x118
//   0x32448  str w21(4), [sp, 0x118]   -> block+0x00 = sub-cmd 4
//   0x32440  str x19,     [sp, 0x120]  -> block+0x08 = the u64 challenge
// No length at +0x10 and nothing at +0x14 (block stays zero there).
// NOTE: AuthorizeEnroll (sub 3) uses a DIFFERENT layout (len@+0x10,
// data@+0x14 — see tokenCmd), which is why sub 4 needs its own builder.
// stock auth sends challenge == 0; keystore2 logged
// "add_auth_token(challenge=0, ...)" for the matching HAT.
inline int setAuthChallenge(uint64_t challenge) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::AUTH_TOKEN;
    req.cmd[0] = 4;
    memcpy(req.cmd + 8, &challenge, 8);  // RE: block+0x08
    return CaSession::get().send(&req, &resp, nullptr);
}
// CheckToken @0x32540: sub 6, token 0x45 in.
inline int checkToken(const uint8_t tok[0x45]) {
    return tokenCmd(6, tok, 0x45);
}
// GetHmacKeyFromKeymasterTa @0x2e0e0: STOCK STUB (returns 0, no TA call).
inline int getHmacKey(void* /*out*/, uint32_t* zero) {
    if (zero) *zero = 0;
    return 0;
}
// Template store (TA-side, category 3 = ALGORITHM per stock disasm
// TemplateSetActiveGroup @0x12ff40 / LoadDatabase @0x130550 /
// DeleteFingerprint @0x1306f0 / GetAllFingerprintsId @0x130890:
// `mov w8,#0x3` stored at req+0x10):
// sub 0xe delete, 0xf list, 0xc set-group, 0xb load-db.
inline int templateCmd(uint32_t sub, const void* payload, uint32_t len) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    req.cmd[0] = (uint8_t)sub;
    req.cmd[1] = (uint8_t)(sub >> 8);
    req.cmd[2] = (uint8_t)(sub >> 16);
    req.cmd[3] = (uint8_t)(sub >> 24);
    if (payload && len) memcpy(req.cmd + 8, payload, len);
    return CaSession::get().send(&req, &resp, nullptr);
}
// DeleteFingerprint @0x306f0. Disasm (block base sp+0x150):
//   0x30714 mov  w20, w0            ; param_1 = fid
//   0x3079c mov  w8, 0xe
//   0x307ac str  w20, [sp,0x15c]    -> cmd+0x0C = fid      <-- HERE
//   0x307b4 str  w8,  [sp,0x150]    -> cmd+0x00 = 0xe
// templateCmd() staged the fid at cmd+8, so our REMOVE_ENROLLMENT deleted fid
// 0 and the TA reported success without deleting anything.
inline int deleteFingerprint(uint32_t fid) {
    TacReq req{};
    TacResp resp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    uint32_t hdr = 0xe;
    memcpy(req.cmd + 0x00, &hdr, 4);  // cmd+0 = id 0xe
    memcpy(req.cmd + 0x0C, &fid, 4);  // cmd+0x0C = fid
    return CaSession::get().send(&req, &resp, nullptr);
}
inline int getAllIds(uint32_t* ids, uint32_t max, uint32_t* count) {
    // GetAllFingerprintsId @0x130890: resp+4 copied to local_660; ids[]
    // memcpy'd from uStack_650 (= payload+16), count = local_640 high word
    // (= payload+36). Old code read +0/+4 -> fid 0 -> framework pruned the
    // template as dangling ("Null identifier").
    uint8_t resp[64] = {};
    TacReq req{};
    TacResp tresp{};
    req.category = (uint32_t)CmdType::ALGORITHM;
    req.cmd[0] = 0xf;
    int rc = CaSession::get().send(&req, &tresp, nullptr);
    if (rc == 0) memcpy(resp, tresp.payload, sizeof(resp));
    if (rc == 0 && count) {
        uint32_t n = 0;
        memcpy(&n, resp + 36, 4);
        if (n > max) n = max;
        *count = n;
        for (uint32_t i = 0; i < n; i++)
            memcpy(&ids[i], resp + 16 + i * 4, 4);
    }
    return rc;
}

}  // namespace token
}  // namespace anc
