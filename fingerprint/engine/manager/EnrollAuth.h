/*
 * anc.hal.so rebuild — enroll / authenticate worker flows.
 *
 * RE: DoEnroll @0x22630, DoAuthenticate @0x22960, AncHalEnroll @0x26a10,
 * AncAuthCaptureImage @0x2aa10, AncAuthVerifyFeature @0x2ab50,
 * EnrollFinish @0x264e0, EnrollContinue @0x26670.
 * Notify emission via device->notify[slot] (spec §3).
 */
#pragma once

#include <stdint.h>

#include "manager/Manager.h"

namespace anc {

// Emit helper: device notify slot call.
inline void notify(Device* dev, NotifySlot slot, uint64_t a1 = 0,
                   uint64_t a2 = 0, uint64_t a3 = 0, uint64_t a4 = 0) {
    if (dev && dev->notify && dev->notify[slot]) dev->notify[slot](0, a1, a2, a3, a4);
}

// Map algo enroll return -> notify. Faithful to stock CustomEnrollResult
// @0x37cd0 (disassembled, block base w19 = the return code). Every ACQUIRED
// arm ends at 0x37ddc -> `ldr x8,[x8,#0x28]; blr x8` with w1 = the code
// below, i.e. the onEnrollAcquired path; NOT onEnrollmentProgress:
//   0x192 (402) -> 0x3ed = 1005        (0x37d70 mov w1,0x3ed -> b 0x37ddc)
//   0x193 (403) -> 0x3ee = 1006        (0x37dd8) VENDOR_DUPLICATE
//   0x12f (303) -> 1                   (0x37da8) PARTIAL
//   0x72  (114) -> 1                   (0x37da8) PARTIAL
//   0x26  (38)  -> 5                   (0x37d98)
//   0x69  (105) -> 5                   (0x37d98)
//   0x6e  (110) -> 3                   (0x37d8c)
//   0x6f/0x70   -> 3 then query fn 9   (0x37d0c / 0x37d20)
//   0x15  (21)  -> log only            (0x37db8 .. 0x37de4)
//   default     -> log only            (0x37df8)
// Only 401 (EnrollContinue) and 400 (EnrollFinish) carry an ENROLL_RESULT.
// Mapping 402 to ENROLL_RESULT(fid=0,remaining=0) was a live bug: the TA
// answered 402 on a re-entered enroll, the framework read the (0,0) pair as
// "enrollment completed" and added a second fingerprint, so dumpsys reported
// prints count=2 while TEMPLATE_GET_ALL_FINGERPRINTS_IDS still showed 1.
//
// RETURN VALUE (RE, 0x37de4/0x37de8): w19 is initialised to the code and is
// zeroed only by the arms that notify, so every *mapped* code returns 0 while
// an UNMAPPED code falls to 0x37df8 ("[ANC_HAL][VendorCommon] ...") and
// returns rc itself. The enroll loop treats non-zero as "stop", so an
// unmapped code aborts the enrollment. (The earlier note here claimed the
// default "returns 0" — that was wrong: 0x37df8 branches to 0x37de8 with
// w19 still holding rc.)
inline int emitEnrollResult(Device* dev, int algoRet, uint32_t fid,
                            uint32_t remaining) {
    switch (algoRet) {
        case 0x192: notify(dev, ACQUIRED, 1005, 0, 0, 0); return 0;
        case 0x193: notify(dev, ACQUIRED, 1006, 0, 0, 0); return 0;
        case 0x12f:
        case 0x72:  notify(dev, ACQUIRED, 1, 0, 0, 0); return 0;
        case 0x26:
        case 0x69:  notify(dev, ACQUIRED, 5, 0, 0, 0); return 0;
        // 0x6f/0x70 additionally re-query fn 9 and return q==0 ? 0 : rc.
        case 0x6e:
        case 0x6f:
        case 0x70:  notify(dev, ACQUIRED, 3, 0, 0, 0); return 0;
        case 0x15:  return 0;  // stock logs and emits nothing (0x37db4->0x37dd0)
        case 401:  // == 0x191, EnrollContinue: progress, fid 0
        case kEnrollFinish400:  // EnrollFinish: completion
            notify(dev, ENROLL_RESULT, fid, 0, remaining, 0);
            return 0;
        default:
            // 0x37df8: log "[ANC_HAL][VendorCommon] ..." then return rc.
            ANCENG_LOGV(                "enrollResult unmapped rc=%d fid=%u rem=%u (stock: log, ret=rc)",
                algoRet, fid, remaining);
            return algoRet;
    }
}

// OpticalEnrollResult @0x3a350 (stock-anc.hal.so). This is what the enroll
// loop actually reaches for an attempt result: the loop calls
// AncEnrollResult @0x2b2f0 -> m->cb[0x18], and InitCustomManager @0x39f80
// installs m->cb = 0x45010 (adr x9,0x45010; str x9,[x0,0x20]) whose slot
// +0x18 is 0x3a350. Returns 0 => "keep the enroll alive", non-zero => stop.
//   cmp w2, 0x12c ; b.le 0x3a398
//   rc > 300:
//     d = rc-301; d <= 6 && (0x4b >> d) & 1 -> acquired(3)      [301,302,304,307]
//     d == 4                                -> acquired(1)      [305]
//     rc == 0x196 || rc == 0x194            -> acquired(3)      [406,404]
//     else                                  -> CustomEnrollResult(m, rc)
//   rc <= 300:
//     d = rc-107; d < 3                     -> acquired(1)      [107,108,109]
//     rc == 0x25 (37)                       -> log only; return 0  <<<< HBM
//     else                                  -> CustomEnrollResult(m, rc)
// Every notify arm ends at 0x3a3e4 `mov w0,wzr; ret`, so only the tail-call
// into CustomEnrollResult can produce a non-zero result.
inline int opticalEnrollResult(Manager* m, int rc) {
    Device* dev = m->device;
    if (rc > 300) {
        uint32_t d = (uint32_t)(rc - 301);
        if (d <= 6 && ((0x4bu >> d) & 1u)) { notify(dev, ACQUIRED, 3, 0, 0, 0); return 0; }
        if (d == 4) { notify(dev, ACQUIRED, 1, 0, 0, 0); return 0; }
        if (rc == 0x196 || rc == 0x194) { notify(dev, ACQUIRED, 3, 0, 0, 0); return 0; }
        return emitEnrollResult(dev, rc, 0, 0);
    }
    uint32_t d = (uint32_t)(rc - 107);
    if (d < 3) { notify(dev, ACQUIRED, 1, 0, 0, 0); return 0; }
    if (rc == 0x25) {
        ANCENG_LOGI(            "[ANC_HAL][CustomManager] enroll wait hbm timeout, ret value: %d", rc);
        return 0;
    }
    return emitEnrollResult(dev, rc, 0, 0);
}

// Stock AncHalEnroll @0x26a10 attempt classifier (0x26bd0 .. 0x26d00).
// Both the wait-failure path (0x26b64) and the capture path (0x26dc0 ->
// 0x26b80) funnel into it, which is why it also knows 400/401/402.
//   rc == 0x191 (401): reset counter; EnrollContinue; CONTINUE loop (0x26bf0)
//   rc == 0x192 (402): reset counter; goto recover (0x26be0 -> 0x26c80)
//   rc <= 0x68 and rc in {0x15,0x25,0x26}: goto recover   (timeouts, NO counter)
//   rc == 0x69 (105): goto recover
//   rc == 0x190 (400): EnrollFinish; STOP (0x26aa0 -> 0x26ab0)
//   anything else: ++counter; if counter < 10 goto recover, else STOP @0x199
// recover: r = AncEnrollResult(m, ctx, rc) (= opticalEnrollResult); if r == 0
//          the loop CONTINUES, else it logs "anc enroll result failed, ret: %d"
//          and stops with r.
// Returns true when the enroll loop must retry.
inline bool enrollAttemptRetry(Manager* m, int rc, uint32_t& retryCnt,
                               int& reportOut) {
    reportOut = rc;
    if (rc == 0x191) {  // 401: EnrollContinue
        retryCnt = 0;
        return true;
    }
    if (rc == 0x192) {  // 402
        retryCnt = 0;
        return opticalEnrollResult(m, rc) == 0;
    }
    if (rc <= 0x68) {
        // The mask 0x6000200000 covers bits 21 (0x15), 37 (0x25), 38 (0x26) —
        // the wait timeouts. A late FULL_HBM_SET lands here, so the session
        // survives instead of being torn down by ALGORITHM_DEINIT_ENROLL.
        if (rc >= 0 && rc <= 0x26 && ((1ull << rc) & 0x6000200000ull))
            return opticalEnrollResult(m, rc) == 0;
    } else if (rc <= 0x190) {
        if (rc == 0x69) return opticalEnrollResult(m, rc) == 0;
        if (rc == 0x190) return false;  // 400: EnrollFinish
    }
    // 0x26c34: retry counter at [x28,0x3c4] (0x1a7000+0x3c4).
    uint32_t n = ++retryCnt;
    ANCENG_LOGI(        "[ANC_HAL][PressEnroll] Enroll continue fail cnt = %d", n & 0xff);
    if (n < 10) return opticalEnrollResult(m, rc) == 0;
    ANCENG_LOGI(        "[ANC_HAL][PressEnroll] check enroll fail count: %d", 0x199);
    reportOut = 0x199;
    return false;
}

// Map algo auth return -> notify. (CustomAuthResult @0x37f10 /
// OpticalAuthResult @0x3a540 condensed.)
inline void emitAuthResult(Device* dev, int algoRet, uint32_t fid,
                           const uint8_t* hat69) {
    if (algoRet == kAlgoMatchOk) {  // 500
        notify(dev, AUTHENTICATED, fid, 0, (uint64_t)hat69, 69);
    } else if (algoRet == 0x6e || algoRet == 0x72 || algoRet == 0x12d ||
               algoRet == 0x130 || algoRet == 0x131 || algoRet == 0x133) {
        notify(dev, ACQUIRED, (uint32_t)algoRet, 0, 0, 0);
    } else if (algoRet == 0x15 || algoRet == 0x1a || algoRet == 0x25) {
        // swallowed (timeout / no-calibration / hbm-timeout): no notify
    } else {
        // fail path: fid 0 + failed notify (adapter builds empty msg)
        notify(dev, AUTHENTICATED, 0, 0, 0, 0);
    }
}

// Debug probe: re-read the TA's template id list
// (TEMPLATE_GET_ALL_FINGERPRINTS_IDS = category 3 ALGORITHM, cmd 15) and log
// it. Distinguishes "the TA never committed a template" (list empty -> the
// compare can never return 500 -> the framework shows no enrolled
// fingerprints) from "committed but the compare was rejected".
// Parsing (stock GetAllFingerprintsId @0x30890): count = payload+36,
// ids = payload+16, count clamped to max.
inline void dbgLogAllIds(const char* where) {
    uint32_t ids[8] = {};
    uint32_t count = 0;
    int rc = token::getAllIds(ids, 8, &count);
    ANCENG_LOGV(                        "enumerate[%s] rc=%d count=%u ids=%08x,%08x,%08x,%08x",
                        where, rc, count, ids[0], ids[1], ids[2], ids[3]);
}

// DoEnroll body (queued by FpmEnroll; runs on consumer thread).
// Returns engine rc; notifies progress via slots.
inline int doEnroll(Manager* m) {
    Producer& p = m->prod;
    Device* dev = m->device;
    // Drop any cancel flag that no waiter consumed (see
    // EventManager::clearLatchedCancel): a cancel that lands while no worker is
    // blocked must not make THIS op's first wait report as if a finger had
    // arrived.
    m->events.clearLatchedCancel();
    // Probed first, before any TA/sensor state is touched, so the enroll
    // sequence itself stays byte-identical to stock.
    dbgLogAllIds("enroll-start");
    int rc = token::authorizeEnroll(p.hat);
    if (rc != 0) {
        notify(dev, ERROR, (uint32_t)rc, 0, 0, 0);
        return rc;
    }
    // Stock holds SPI open across the enroll (EnableSensorSpiClkOnce(1)).
    CaSession::get().setKeepSpi(true);
    rc = algo::initEnroll();
    ANCENG_LOGV("doEnroll initEnroll rc=%d", rc);
    if (rc != 0 && rc != 0x1a) {
        notify(dev, ERROR, (uint32_t)rc, 0, 0, 0);
        return rc;
    }
    int dupCount = 0;  // 403 duplicates (diagnostic only)
    // Stock retry counter at [x28,0x3c4] (0x1a7000+0x3c4), logged as
    // "Enroll continue fail cnt = %d" (0x146eb). Reset on 401/402; the
    // enrollment is abandoned with 0x199 once it reaches 10.
    uint32_t retryCnt = 0;
    bool committed = false;  // 400 path already ran DEINIT(commit=1)
    // ---- OpticalEnrollInit @0x3a2b0 (stock runs it once per enroll session,
    // immediately after ALGORITHM_INIT_ENROLL). Registration log @09:13:
    //   ALGORITHM_INIT_ENROLL -> power(1) WAKEUP -> VENDOR_CHECK_SENSOR_QRCODE
    //   -> power(2) SLEEP -> (per-touch power(8) capture...)
    // The touch/HBM detect enables are already satisfied by
    // custom::startWorkers. Non-fatal: log the rc and keep going.
    SensorDevice::get().halSetPowerMode(1);  // SENSOR_WAKEUP
    int qrRc = algo::vendorCheckSensorQrCode();
    SensorDevice::get().halSetPowerMode(2);  // SENSOR_SLEEP
    ANCENG_LOGV("OpticalEnrollInit qrcode rc=%d", qrRc);
    for (;;) {
        ANCENG_LOGV("doEnroll waitFinger");
        rc = waitFingerDown(m, false);
        ANCENG_LOGV("doEnroll waitFinger rc=%d", rc);
        if (rc != 0) {
            // Stock logs "enroll wait finger down failed, ret: %d" (0xa8d3)
            // and then classifies (0x26bd0). This is the fix for the
            // "last touch does not complete" abort: a late FULL_HBM_SET makes
            // WaitHbm time out with 0x25 (37), which is in the classifier's
            // recoverable mask {0x15,0x25,0x26} -> AncEnrollResult ->
            // OpticalEnrollResult logs "enroll wait hbm timeout, ret value: %d"
            // and returns 0, so stock RE-ARMS and waits for the next touch.
            // We used to `break` here, which discarded all completed samples
            // via ALGORITHM_DEINIT_ENROLL and left the enroll client hung
            // (Fps state 1, count 0).
            ANCENG_LOGI(                "[ANC_HAL][PressEnroll] enroll wait finger down failed, ret: %d", rc);
            int report = rc;
            if (enrollAttemptRetry(m, rc, retryCnt, report)) {
                // 0x26ac8: power mode 2 (SLEEP) before re-arming.
                SensorDevice::get().halSetPowerMode(2);
                rc = 0;
                continue;
            }
            rc = report;
            break;
        }
        // Stock per-touch ordering, proven by the instrumented DT trace of
        // this exact binary (00:01:12.693 -> .824):
        //   .693 t7615 [TouchEventDetect][I] get touch event, code: 195, value: 1
        //   .693 t7615 [EventManager][D] UpdateEventState: event = 0, state = 1
        //   .693 t7615 fingerprint.jiiov: acquired legacy=1002   <- event handler
        //   .788 t7616 [HbmEventDetect][I] get hbm event ... FULL_HBM_SET
        //   .788 t7616 [EventManager][D] UpdateEventState: event = 2, state = 1
        //   .788 t7616 [EventHandler][D] HBM is Ready
        //   .789 t7613 fingerprint.jiiov: acquired legacy=1020   <- THIS worker
        //   .789 t7613 [SensorManager][D] current power mode: 2 (SENSOR_SLEEP), set sensor power mode: 10 (SENSOR_WAKEUP_SCAN)
        //   .824 t7613 [SensorManager][D] sensor current power mode: 10 (SENSOR_WAKEUP_SCAN)
        //   .824 t7613 [FingerStatus][D] is_screen_off = 0
        //   .824 t7613 [EventManager][I] waiting event 2, state 1, has been ready
        //   .824 t7613 [FingerStatus][D] wait hbm spent 35 ms
        //   .824 t7613 [ANC_TAC][TacCommand][I] enable sensor spi clk once: 1
        //   .824 t7613 TRANSMIT >>>> command id = 5 (SENSOR_CAPTURE_IMAGE)
        //
        // The 1002 is NOT emitted here — the event-state handler owns it, and
        // the counts prove it: 32 x "UpdateEventState: event = 0, state = 1"
        // vs 32 x "acquired legacy=1002" against only 16 touch events (two
        // ev0 raises per touch: the touch worker AND [FPM] OnPointerDown).
        // Duplicate 1002s are harmless because the adapter's startCaptureLight
        // dedups on gIlluminating (exactly 16 illuminations for 16 touches).
        uint64_t thbm = NowMs();
        notify(dev, ACQUIRED, 1020, 0, 0, 0);  // LockCpuPerformance
        // Stock ENROLL per-touch power (registration log @09:13:32.6+):
        //   "current power mode: 2 (SLEEP), set: 8 (WAKEUP_RESET)"
        //   -> capture(5) -> extract(6) -> enroll(7)
        //   -> "current power mode: 8 (WAKEUP_RESET), set: 2 (SLEEP)"
        // i.e. mode 8 (WAKEUP_RESET) before capture and mode 2 (SLEEP)
        // after every touch. We previously used mode 10 (WAKEUP_SCAN,
        // which is the AUTH pre-capture mode) and never slept between
        // touches, so the next capture could reuse the previous frame and
        // the TA flagged it 403 (duplicate) — every other tap.
        SensorDevice::get().halSetPowerMode(8);  // WAKEUP_RESET (stock enroll)
        ANCENG_LOGD("[ANC_HAL][FingerStatus][D]: is_screen_off = %d", gIsScreenOff);
        // The HBM edge was already consumed by WaitFingerDown (the adapter
        // illuminates on the event handler's 1002), so this is stock's
        // post-power-mode ready-check and it logs the pair
        // "waiting event 2, state 1, has been ready" + "wait hbm spent %d ms".
        // thbm is sampled BEFORE the power-mode set so the spent value brackets
        // the SENSOR_SET_POWER_MODE transmit, exactly as stock does.
        waitHbmReady(m, thbm);
        SensorDevice::get().spiClkOn();  // "enable sensor spi clk once: 1"
        uint8_t img[0x75] = {};
        rc = SensorDevice::get().captureImage(0, img);
        if (rc != 0) {
            // Stock 0x26d04: a capture failure also reaches the classifier
            // (0x26d58 -> 0x26db0 -> 0x26b80) with the capture code.
            SensorDevice::get().halSetPowerMode(2);  // SLEEP
            int report = rc;
            if (enrollAttemptRetry(m, rc, retryCnt, report)) continue;
            rc = report;
            break;
        }
        rc = algo::extractFeature();
        if (rc != kAlgoExtractOk) {
            // Stock's capture path funnels into the SAME classifier
            // (0x26e4c -> 0x26d58 -> 0x26db0 -> 0x26b80). OpticalEnrollResult
            // maps the stale-frame code 305 to acquired(1) and returns 0, so
            // the touch is retried and counted by "Enroll continue fail cnt".
            SensorDevice::get().halSetPowerMode(2);  // SLEEP
            int report = rc;
            if (enrollAttemptRetry(m, rc, retryCnt, report)) continue;
            rc = report;
            break;
        }
        uint32_t fid = 0, remaining = 0;
        // AlgoEnrollFeature @0x12f040 takes NO data (signature is
        // (u32* remOut, u32* fidOut)); its command block is all-zero except
        // cmd+0 = 7. Stock therefore sends zero payload. We used to stage the
        // 0x75-byte image at cmd+8 — non-stock. Send none.
        rc = algo::enrollFeature(nullptr, 0, &fid, &remaining);
        // 305 (extract stale-frame) / 303 (enroll stale-frame) / 114: finger was
        // not freshly down. 403 = duplicate (same area twice). 402 = the TA's
        // "reposition" acquire (CustomEnrollResult maps it to ACQUIRED 1005).
        // Stock @09:13:46: on 403 it still runs the per-touch tail
        // (EXTENSION_SINGLE_ENROLL_COLLOCT 21 + ALGORITHM_TRANSFER_IMAGE 21),
        // emits acquired legacy=1006 (VENDOR 6 = DUPLICATE), power-sleeps (2)
        // and continues the loop. It NEVER aborts the enrollment because of
        // duplicates (registration log: 7 x 403 interleaved with 401s, still
        // finished with 400). The earlier `dupCount >= 5` guard was a
        // regression that killed enrollments after a few taps.
        //
        // None of these is an ENROLL_RESULT — emitEnrollResult now maps them
        // to ACQUIRED, and the loop keeps going. Reporting 402 as
        // ENROLL_RESULT(fid=0, remaining=0) is what made the framework add a
        // phantom enrollment (count=2 with 1 real template).
        if (rc == 305 || rc == 303 || rc == 403 || rc == 402 || rc == 114) {
            if (rc == 403) {
                algo::extensionSingleEnrollCollect();
                algo::transferImage(0);
                ++dupCount;
            }
            SensorDevice::get().halSetPowerMode(2);  // SLEEP (stock post-touch)
            // Same classifier as the wait failure (0x26bd0): 402 resets the
            // counter and maps to acquired(1005); 403 increments it and maps
            // to acquired(1006); 305 maps to acquired(1). All of them return 0
            // from OpticalEnrollResult, so the loop continues — stock stops
            // only after 10 consecutive unproductive taps (0x199).
            int report = rc;
            if (enrollAttemptRetry(m, rc, retryCnt, report)) continue;
            rc = report;
            break;
        }
        // 401 = continue (emit progress, loop back for next touch).
        // 400 = done (deinit, emit acquired-good, break).
        // 0x191 = alternate continue code.
        if (rc == 401 || rc == 0x191) {
            retryCnt = 0;  // stock 0x26bf8: reset "Enroll continue fail cnt"
            // Stock per-touch tail: EXTENSION_SINGLE_ENROLL_COLLOCT (cat 5,
            // id 21) then ALGORITHM_TRANSFER_IMAGE (cat 3, id 21), both rc 0,
            // then SENSOR_SET_POWER_MODE(2) (SLEEP) before the next wait.
            algo::extensionSingleEnrollCollect();
            algo::transferImage(0);
            SensorDevice::get().halSetPowerMode(2);  // SLEEP (stock post-touch)
            notify(dev, ACQUIRED, 0, 0, 0, 0);  // GOOD -> light off
            emitEnrollResult(dev, rc, 0, remaining);  // stock: fid=0 until 400
            ANCENG_LOGV("enroll 401 remaining=%u dup=%d",
                                remaining, dupCount);
            continue;
        }
        if (rc == kEnrollFinish400) {
            // Stock finish @09:18:37: SINGLE_ENROLL_COLLOCT -> TRANSFER_IMAGE
            // -> DEINIT(commit=1, 303ms template write) -> GET_AUTH_ID ->
            // SLEEP -> ENROLL_END_COLLOCT -> ENROLL_RESULT(fid,0).
            // fid is already known from the 400 resp (payload+12).
            algo::extensionSingleEnrollCollect();
            algo::transferImage(0);
            uint32_t finishFid = 0;
            algo::deinitEnroll(true, &finishFid);
            committed = true;  // do not deinit again below (would re-commit)
            if (finishFid == 0) finishFid = fid;
            { uint64_t aid = 0; algo::templateGetAuthId(&aid); }
            SensorDevice::get().halSetPowerMode(2);  // SLEEP
            algo::extensionEnrollEndCollect();
            // Post-commit probe: does the TA now list the template? If this
            // logs count=0 the enrollment never reached the TA database, and
            // every later ALGORITHM_COMPARE_FEATURE is doomed to return 0.
            dbgLogAllIds("enroll-committed");
            emitEnrollResult(dev, kEnrollFinish400, finishFid, 0);
            rc = 0;
            break;
        }
        // Anything else reaches the classifier (stock 0x26bd0). Unmapped codes
        // fall through CustomEnrollResult's default (0x37df8 -> 0x37de8 with
        // w19 == rc) and therefore abort the enrollment.
        int report = rc;
        if (enrollAttemptRetry(m, rc, retryCnt, report)) continue;
        rc = report;
        break;
    }
    // Stock aborts a non-committed enrollment (deinit done=0 discards the
    // session template). The 400 path already committed (done=1); calling
    // deinitEnroll(true) a second time was redundant.
    if (!committed) algo::deinitEnroll(false);
    CaSession::get().setKeepSpi(false);
    SensorDevice::get().spiClkOff();
    if (rc != 0) notify(dev, ERROR, (uint32_t)rc, 0, 0, 0);
    return rc;
}

// DoAuthenticate body (queued by FpmAuthenticate).
inline int doAuthenticate(Manager* m) {
    Producer& p = m->prod;
    Device* dev = m->device;
    // Drop a cancel flag that no waiter consumed (see
    // EventManager::clearLatchedCancel). AncCancel latches it even when the
    // previous op's worker already returned, which made this op's first
    // waitFinger() return "proceed" and capture a frame with no finger on it.
    m->events.clearLatchedCancel();
    token::getHmacKey(nullptr, nullptr);  // stock stub
    // Probed BEFORE the prelude so the stock TA sequence (power(1) ->
    // QR check -> power(2) -> setChallenge -> initVerify) stays contiguous.
    dbgLogAllIds("auth-start");
    // ---- OpticalAuthInit @0x3a4a0 (runs at the head of EVERY auth) ----
    //   AncEnableDetectTouchEvent(1); AncEnableDetectHbmEvent(1);
    //   setPowerMode(1) WAKEUP;
    //   CustomCheckSensorQRCode();       // VENDOR cat 6 cmd 3
    //   setPowerMode(2) SLEEP;
    // custom::startWorkers keeps the touch/HBM detectors permanently enabled,
    // so the two enable calls are already satisfied. The
    // power(1) -> VENDOR_CHECK_SENSOR_QRCODE -> power(2) wedge is NOT
    // optional: the stock unlock log shows it ahead of both auths
    // ("current power mode: 2 (SENSOR_SLEEP), set: 1 (SENSOR_WAKEUP)" ...
    // "TRANSMIT >>>> command id = 3 (VENDOR_CHECK_SENSOR_QRCODE)" ...
    // "set sensor power mode: 2 (SENSOR_SLEEP)") and it is the only
    // structural call still missing from our auth path.
    SensorDevice::get().halSetPowerMode(1);  // SENSOR_WAKEUP
    int qrRc = algo::vendorCheckSensorQrCode();
    SensorDevice::get().halSetPowerMode(2);  // SENSOR_SLEEP
    ANCENG_LOGV("OpticalAuthInit qrcode rc=%d", qrRc);
    int rc = token::setAuthChallenge(p.challenge);
    if (rc != 0) {
        notify(dev, ERROR, (uint32_t)rc, 0, 0, 0);
        return rc;
    }
    // Stock DoAuthenticate @0x122d98: AlgoInitVerify(study_flag, 0) where
    // study_flag is the manager's adaptive-learning bit (log
    // "study flag = %d", "need_study:1"). Sent as cmd+4.
    rc = algo::initVerify(true);
    if (rc != 0 && rc != 0x1a) {
        notify(dev, ERROR, (uint32_t)rc, 0, 0, 0);
        return rc;
    }
    uint8_t hat[0x45] = {};
    bool matched = false;
    bool deinitDone = false;  // set when the match path already sent cmd 10
    // Outer loop: wait for a finger-down, then run up to 2 capture+verify
    // attempts on the SAME press (stock "authenticate retry :0/:1",
    // capture mode = retry+1). Loops until a match or cancel (rc 0x16).
    for (;;) {
        rc = waitFingerDown(m, true);
        if (rc != 0) break;
        // Stock AUTH per-touch ordering — identical shape to enroll; the 1002
        // belongs to the event handler, not this worker (see doEnroll):
        //   .234 1002 (event handler) -> .241 1020 (here) -> .242 cmd7 --*
        //   .564 enable sensor spi clk once: 1 -> .565 authenticate retry :0
        //   .565 cmd5 SENSOR_CAPTURE_IMAGE
        uint64_t thbm = NowMs();
        notify(dev, ACQUIRED, 1020, 0, 0, 0);  // LockCpuPerformance
        // Stock auth @09:17:22: POWER_MODE(31ms wake) before capture —
        // sensor sits in SLEEP after enroll finish. Without this the TA's
        // SPI xfer finds no image -> capture hangs 8s -> rc 13.
        SensorDevice::get().halSetPowerMode(10);  // WAKEUP_SCAN
        ANCENG_LOGD("[ANC_HAL][FingerStatus][D]: is_screen_off = %d", gIsScreenOff);
        waitHbmReady(m, thbm);  // "waiting event 2 ... has been ready" + spent
        SensorDevice::get().spiClkOn();  // "enable sensor spi clk once: 1"
        int lastRc = 0;
        for (int retry = 0; retry < 2; retry++) {
            ANCENG_LOGI("[ANC_HAL][Auth][I]: authenticate retry :%d", retry);
            // SensorCaptureImageWithMode: mode = (retry<3) ? retry+1 : 0.
            uint8_t img[0x75] = {};
            rc = SensorDevice::get().captureImage((uint32_t)(retry + 1), img);
            lastRc = rc;
            ANCENG_LOGV("auth retry=%d capture rc=%d img=%02x%02x%02x%02x",
                                retry, rc, img[0], img[1], img[2], img[3]);
            if (rc != 0) break;
            // AncAuthVerifyFeature -> ExtractFeature(1, idx, 0, dcs, 0):
            // p1=1 verify flag, p2=idx (retry), p4=dcs word.
            rc = algo::extractFeatureVerify((uint32_t)retry, 0);
            lastRc = rc;
            ANCENG_LOGV("auth retry=%d extract rc=%d", retry, rc);
            if (rc != kAlgoExtractOk) break;
            // AlgoCompareFeature(&out,&local_240): local_240 is the zeroed
            // {data,len} struct (GetHmacKeyFromKeymasterTa is a stub), so
            // the feature payload length is 0 — TA compares its stored
            // verify feature against the loaded template.
            algo::CompareOut out{};
            rc = algo::compareFeature(nullptr, 0, &out);
            lastRc = rc;
            if (rc == kAlgoMatchOk) {
                memcpy(hat, out.hat, 0x45);
                // ---- Stock post-match tail, in exact TA-transmit order ----
                //   DoAuthenticate @0x22fec  AncAuthFinish  @0x2ae90
                //      -> EXTENSION_AUTH_RESULT_COLLOCT  (cat 5, cmd 20)
                //      -> ALGORITHM_TRANSFER_IMAGE       (cat 3, cmd 21)
                //   DoAuthenticate @0x2302c  AlgoFeatureStudy @0x2fc30
                //      -> ALGORITHM_FEATURE_STUDY        (cat 3, cmd 9)
                //   DoAuthenticate @0x23050  AncAuthResult @0x2af70
                //      -> the AUTHENTICATED notify ("auth result fid=N")
                //   DoAuthenticate           AlgoDeinitVerify @0x2fdc0
                //      -> ALGORITHM_DEINIT_VERIFY        (cat 3, cmd 10)
                // then "current power mode: 10 (WAKEUP_SCAN), set: 2
                // (SENSOR_SLEEP)". We previously sent NONE of the first three.
                algo::extensionAuthResultCollect();      // cat 5 cmd 20
                // cat 3 cmd 21, kind = AlgoSaveAuthImage's bundle.
                //
                // RE (stock-anc.hal.so): AlgoTransferImage(kind,...) @0x31430
                // writes kind at envelope+0x1C (cmd+0x1C). The only caller is
                // TransferThenSaveAlgoImage(kind,...) @0x23e90, which is fed by
                // four wrappers:
                //   AlgoSaveEnrollImage    @0x23e20 -> kind 0  (enroll bundle)
                //   AlgoSaveAuthImage      @0x23f60 -> kind 1  (verify bundle)
                //   AlgoSaveHeartBeatImage @0x24020 -> kind 2
                //                               @0x240e8 -> kind 3
                // The verify path is the ONLY one that reaches kind 1:
                // AlgoSaveAuthImage's thunk (0x3d030) is called only from
                // DoAuthenticate @0x22f00 / @0x23160, and AlgoSaveEnrollImage's
                // thunk (0x3d1d0) is called from CollectAndUpdateEnrollAlgoData
                // (enroll only).
                //
                // Sending kind 0 here asked the TA for the *enroll* image
                // bundle while the TA was in verify state, so it answered
                // rc=1 (no such bundle) - the residual "cat=3 cmd=21 rc=1"
                // seen after every successful match. The enroll-path
                // transferImage(0) above still returns 0, which is what
                // confirms kind 0 is the enroll bundle and kind 1 the
                // verify bundle.
                algo::transferImage(1);                  // cat 3 cmd 21 (auth bundle)
                notify(dev, ACQUIRED, 0, 0, 0, 0);  // GOOD -> light off (HBM clear)
                emitAuthResult(dev, rc, out.fid, hat);
                // "algo verify, match ok, need_study:1" -> adapt the stored
                // template ("verify study spent 185 ms"). Stock gates this on
                // the manager's need_study bit; always asking is harmless
                // when nothing changed.
                algo::featureStudy(out.fid);             // cat 3 cmd 9
                algo::deinitVerify();                    // cat 3 cmd 10
                SensorDevice::get().halSetPowerMode(2);  // SLEEP
                deinitDone = true;
                dbgLogAllIds("auth-match");
                matched = true;
                rc = 0;
                break;
            }
        }
        if (matched) break;
        SensorDevice::get().halSetPowerMode(2);  // SLEEP (stock post-touch)
        notify(dev, ACQUIRED, 0, 0, 0, 0);  // light off
        emitAuthResult(dev, lastRc, 0, nullptr);
        // Stock DoAuthenticate @0x122d98 loops on the SAME authenticate()
        // operation until a match or a cancel: after a non-match it returns to
        // AncEnrollAuthWaitFingerDown @0x2b960 and re-arms for the NEXT press.
        // The framework does NOT re-issue authenticate() after
        // onAuthenticationFailed — the FingerprintAuthenticationClient stays
        // alive in BiometricScheduler (State: 2) until the prompt ends — so
        // breaking here left the engine idle for the rest of the op and a
        // correct-finger retry could never be captured.
        //   Live proof (CM6 eng, keyguard, 2026-10-18):
        //     00:14:29.538 authenticate op=0
        //     00:14:30.677 auth result fid=0 -> onAuthenticationFailed
        //     00:14:32.421 acquired legacy=1002  <- CORRECT finger down
        //     00:14:33.828 acquired legacy=1003  <- up; no 1020/evWait/compare
        //    (engine idle; framework op stayed live until cancel 00:14:35.616)
        // exactly the reported "one wrong touch, then a correct finger never
        // unlocks". waitFinger()'s g_previous_finger_up_time rule (fresh LIFT
        // before the next DOWN) keeps this loop from re-capturing the same
        // press, and waitFinger()'s cancel path (0x16) exits on cancel.
        continue;  // re-arm for the next press within the same op
    }
    if (!deinitDone) algo::deinitVerify();
    SensorDevice::get().spiClkOff();
    if (rc != 0) notify(dev, ERROR, (uint32_t)rc, 0, 0, 0);
    return rc;
}

}  // namespace anc
