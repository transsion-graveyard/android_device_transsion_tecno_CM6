/*
 * anc.hal.so rebuild — event manager + work consumer + Fpm flows.
 *
 * RE: Em* @0x1215xx (event slots, 0x78 stride, cond timedwait),
 * Fpwc* @0x1201xx (task stack, DoCancelTask preempt, priority pick),
 * Fpm* @0x127xxx-0x128xxx, DoEnroll @0x22630, DoAuthenticate @0x22960,
 * AncHalEnroll @0x26a10, AncEnrollAuthWaitFingerDown @0x2b960,
 * WaitHbm @0x2bc80, result mappers Custom-Optical (spec §2, §4g).
 */
#pragma once

#include <android/log.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "../algo/AlgoEnvelope.h"
#include "../sensor/SensorDevice.h"

namespace anc {

// ---- Stock log-tag parity ----
// Macros (tag "JV_FP", stock priorities) live in ../EngLog.h so the sensor /
// TA layers can use them without including Manager.h (Manager.h already
// includes SensorDevice.h).
#include "../EngLog.h"

struct Manager;  // forward: EventManager dispatches through a Manager*

// Stock event-state handler (AncEventStateHandler). Runs on any value change
// of any event and owns the legacy finger reports. Defined after Manager.
inline void dispatchEventState(Manager* m, uint32_t event, int state);

// ---- Event manager (Em*): N events, stride 0x78, cond+mutex per event ----
static constexpr int kMaxEvents = 8;
struct EventSlot {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int state = 0;
    int waitFor = -1;  // -1 = nobody waiting
    bool cancelled = false;
    uint64_t timeMs = 0;
    EventSlot() {
        pthread_mutex_init(&mutex, nullptr);
        // Stock uses pthread_condattr_setclock (MONOTONIC) for timed waits;
        // default conds use REALTIME and would instantly expire with our
        // MONOTONIC abstime.
        pthread_condattr_t attr;
        pthread_condattr_init(&attr);
        pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        pthread_cond_init(&cond, &attr);
        pthread_condattr_destroy(&attr);
    }
};

class EventManager {
  public:
    // Set once at init (InitEventManager) so update() can reach the device.
    Manager* owner = nullptr;

    // EmUpdateEventState(event, state): store + signal waiter on match, then
    // run the stock event-state handler on a value change.
    //
    // Ground truth (real stock ROM, stock-rom-logs/fingerprint-logcat.txt
    // 10-04 15:43:48.876 and the DT trace) proves the handler owns the legacy
    // finger reports, on whichever thread performs the update:
    //     [ANC_HAL][TouchEventDetect][I]: get touch event, code: 195, value: 1
    //     [ANC_HAL][EventManager][D]: UpdateEventState: event = 0, state = 1
    //     ... onAcquired byte acquiredInfo =6 vendorCode=2      (=1002)
    // and, on the service thread:
    //     [ANC_HAL][FPManager][I]: [FPM] OnPointerDown
    //     [ANC_HAL][EventManager][D]: UpdateEventState: event = 0, state = 1
    //     fingerprint.jiiov: acquired legacy=1002
    // Counts are exactly 1:1 (27 touch-downs -> 27 x vendorCode=2, 27 x
    // vendorCode=20; 27 touch-ups -> 24 x vendorCode=3), so 1002/1003 MUST be
    // emitted from here and NOT from the enroll/auth worker.
    void update(uint32_t event, int state) {
        EventSlot& s = slots_[event % kMaxEvents];
        pthread_mutex_lock(&s.mutex);
        s.state = state;
        s.timeMs = NowMs();
        // Stock g_previous_finger_up_time: track last finger-UP edge so
        // waits can require a fresh lift-and-touch (AncEnrollAuthWait @0x2b960).
        if ((event % kMaxEvents) == 0 && state == 0) fingerUpMs_ = s.timeMs;
        ANCENG_LOGD("[ANC_HAL][EventManager][D]: UpdateEventState: event = %d, state = %d",
                    (int)event, state);
        if (s.waitFor >= 0 && s.waitFor == state)
            pthread_cond_signal(&s.cond);
        pthread_mutex_unlock(&s.mutex);
        // Dispatch UNCONDITIONALLY (not only on a value change) and OUTSIDE
        // the slot lock. The DT trace proves both properties: the stock build
        // produced exactly 32 "UpdateEventState: event = 0, state = 1" lines
        // and exactly 32 "acquired legacy=1002" lines, and 32
        // "UpdateEventState: event = 2, state = 1" but 0 occurrences of
        // "event = 2, state = 0" (event 2 latches), yet 32 "HBM is Ready" —
        // i.e. the handler fires per call, not per transition.
        dispatchEventState(owner, event % kMaxEvents, state);
    }
    // Read-only state peek (used for the "has been ready" readyCheck log).
    int peek(uint32_t event) {
        EventSlot& s = slots_[event % kMaxEvents];
        pthread_mutex_lock(&s.mutex);
        int st = s.state;
        pthread_mutex_unlock(&s.mutex);
        return st;
    }
    // Silent clear — the stock HBM-off path. Two independent DT facts force
    // this to bypass the logged update():
    //   * 30 "get hbm event ... FULL_HBM_CLEAR" lines exist but ZERO
    //     "UpdateEventState: event = 2, state = 0" lines, and
    //   * the blocking HBM wait must genuinely block on every touch (the 16
    //     touches only ever yield 16 "has been ready" short-circuits, all from
    //     the post-SENSOR_SET_POWER_MODE ready-check), which is impossible if
    //     event 2 latched at 1 forever.
    void clearSilent(uint32_t event) {
        EventSlot& s = slots_[event % kMaxEvents];
        pthread_mutex_lock(&s.mutex);
        s.state = 0;
        s.timeMs = NowMs();
        pthread_mutex_unlock(&s.mutex);
    }
    // Last finger-UP edge timestamp (ms). 0 = never.
    uint64_t fingerUpMs() const { return fingerUpMs_; }
    // EmWaitEventState(event, state, timeoutMs): 0 ok, 1 busy, 0x15 timeout.
    // quiet=true keeps the safe readyCheck short-circuit but suppresses the
    // readyCheck log. Needed because stock emits exactly ONE
    // "waiting event 2, state 1, has been ready" per touch (16 of 16), i.e.
    // only from the post-SENSOR_SET_POWER_MODE ready-check — the earlier
    // blocking HBM wait inside WaitFingerDown is silent.
    int wait(uint32_t event, int state, int timeoutMs, bool readyCheck,
             bool quiet = false) {
        EventSlot& s = slots_[event % kMaxEvents];
        pthread_mutex_lock(&s.mutex);
        if (s.waitFor != -1) {
            pthread_mutex_unlock(&s.mutex);
            return 1;
        }
        if (readyCheck && s.state == state) {
            pthread_mutex_unlock(&s.mutex);
            // Stock: "[ANC_HAL][EventManager][I]: waiting event 2, state 1,
            // has been ready" — emitted only on the readyCheck short-circuit.
            if (!quiet)
                ANCENG_LOGI("[ANC_HAL][EventManager][I]: waiting event %u, state %d, has been ready",
                            event % kMaxEvents, state);
            return 0;
        }
        if (s.cancelled) {
            // Stock: cancelled wait logs "has been canceled" and returns 0
            // (proceed), NOT busy. Consume the flag.
            s.cancelled = false;
            s.waitFor = -1;
            pthread_mutex_unlock(&s.mutex);
            return 0;
        }
        s.waitFor = state;
        ANCENG_LOGV("evWait ev=%u st=%d to=%d",
                            event, state, timeoutMs);
        int rc;
        if (timeoutMs < 0) {
            rc = pthread_cond_wait(&s.cond, &s.mutex);
            rc = (rc == 0) ? 0 : 1;
        } else {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            ts.tv_sec += timeoutMs / 1000;
            ts.tv_nsec += (timeoutMs % 1000) * 1000000LL;
            if (ts.tv_nsec >= 1000000000) {
                ts.tv_sec++;
                ts.tv_nsec -= 1000000000;
            }
            rc = pthread_cond_timedwait(&s.cond, &s.mutex, &ts);
            rc = (rc == 0) ? 0 : (rc == 110 /*ETIMEDOUT*/ ? 0x15 : 1);
        }
        // Stock checks the cancel flag on wake (EmCancelWaitEventState sets
        // a separate flag + signals; waiter consumes it here).
        if (s.cancelled) {
            s.cancelled = false;
            s.waitFor = -1;
            pthread_mutex_unlock(&s.mutex);
            return 0x16;  // cancelled (stock: "has been canceled" path)
        }
        s.cancelled = false;
        s.waitFor = -1;
        pthread_mutex_unlock(&s.mutex);
        return rc;
    }
    void cancel(uint32_t event) {
        EventSlot& s = slots_[event % kMaxEvents];
        pthread_mutex_lock(&s.mutex);
        s.cancelled = true;
        s.waitFor = -1;  // clear stale waiter so next wait blocks fresh
        pthread_cond_signal(&s.cond);
        pthread_mutex_unlock(&s.mutex);
    }
    // Op-start hygiene. AncCancel @AncHal.cpp:216 -> EmCancelWaitEventState
    // latches s.cancelled unconditionally. If no worker is blocked in wait()
    // at that instant (the common case when the framework cancels an op the
    // HAL already finished), the flag has NO consumer and is left set. The
    // NEXT operation's first wait() then hits the "cancelled -> return 0"
    // branch above and PROCEEDS WITHOUT A FINGER: the TA captures an empty
    // frame, extract/compare fail, and the service reports an instant
    // "auth result fid=0" -> onAuthenticationFailed -> the user feels a
    // vibration for a touch they never made.
    //   Live proof (CM6 eng, 2026-10-18, no touch/event-0 line exists in the
    //   whole interval between these timestamps):
    //     00:14:28.482 fingerprint.jiiov: cancellation requested
    //     00:14:29.538 fingerprint.jiiov: authenticate op=0
    //     00:14:29.618 acquired legacy=1020  (capture with ZERO touch events)
    //     00:14:29.680 evWait ev=2 st=1 to=600
    //     00:14:30.677 auth result fid=0      <- spurious failure / vibration
    // Same shape again 00:14:35.616 cancel -> 00:14:40.187 op ->
    // 00:14:40.269 phantom 1020 -> 00:14:41.480 fid=0.
    // A cancel aimed at a LIVE wait is unaffected: it is delivered by wait()
    // itself (blocked waiter -> 0x16 post-cond check).
    void clearLatchedCancel() {
        for (int i = 0; i < kMaxEvents; i++) {
            EventSlot& s = slots_[i];
            pthread_mutex_lock(&s.mutex);
            s.cancelled = false;
            pthread_mutex_unlock(&s.mutex);
        }
    }

  private:
    EventSlot slots_[kMaxEvents];
    // Last finger-UP edge (ms, monotonic). Mutable via update() — declare
    // mutable so the const fingerUpMs() reader stays lock-free (u64 atomic
    // on arm64 for this purpose).
    uint64_t fingerUpMs_ = 0;
};

// ---- Work consumer (Fpwc* + DoTask @0x20aa0) ----
struct Task {
    const char* name = nullptr;  // "DoEnroll"/"DoAuthenticate"/"DoCancelTask"
    void (*fn)(void*) = nullptr;
    void* arg = nullptr;
    uint32_t prio = 0;
};

class WorkConsumer {
  public:
    void push(const Task& t) {
        pthread_mutex_lock(&mutex_);
        if (count_ < kMaxTasks) tasks_[count_++] = t;
        if (status_ == IDLE) pthread_cond_signal(&cond_);
        pthread_mutex_unlock(&mutex_);
    }
    void pushCancel() {
        Task t;
        t.name = "DoCancelTask";
        push(t);
    }
    // Consumer thread: DoCancelTask preempts; else highest-prio task runs.
    static void* threadEntry(void* self) {
        return static_cast<WorkConsumer*>(self)->loop();
    }
    void* loop() {
        pthread_mutex_lock(&mutex_);
        status_ = IDLE;
        pthread_cond_signal(&cond_);
        for (;;) {
            // Find cancel first, else min prio.
            int idx = -1;
            for (int i = 0; i < count_; i++) {
                if (tasks_[i].name &&
                    strcmp(tasks_[i].name, "DoCancelTask") == 0) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                uint32_t best = 0xffffffff;
                for (int i = 0; i < count_; i++) {
                    if (tasks_[i].prio < best) {
                        best = tasks_[i].prio;
                        idx = i;
                    }
                }
            }
            if (idx < 0) {
                pthread_cond_wait(&cond_, &mutex_);
                continue;
            }
            Task t = tasks_[idx];
            memmove(&tasks_[idx], &tasks_[idx + 1],
                    (count_ - idx - 1) * sizeof(Task));
            count_--;
            status_ = BUSY;
            pthread_mutex_unlock(&mutex_);
            if (t.fn) t.fn(t.arg);
            pthread_mutex_lock(&mutex_);
            status_ = IDLE;
        }
        return nullptr;
    }
    void start() {
        pthread_create(&thread_, nullptr, threadEntry, this);
    }

  private:
    enum { IDLE = 2, BUSY = 3 };
    static constexpr int kMaxTasks = 16;
    pthread_mutex_t mutex_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t cond_ = PTHREAD_COND_INITIALIZER;
    Task tasks_[kMaxTasks];
    int count_ = 0;
    int status_ = 0;
    pthread_t thread_{};
};

// ---- Manager aggregate (g_fp_manager + producer state) ----
// Screen state mirrored from the netlink screen-on/off messages. Stock logs it
// as "[ANC_HAL][FingerStatus][D]: is_screen_off = %d" immediately before the
// post-power-mode HBM ready-check.
inline int gIsScreenOff = 0;

struct Manager {
    Producer prod{};
    EventManager events;
    WorkConsumer consumer;
    Device* device = nullptr;
    bool customInit = false;
    // Finger-down/up event ids used with EventManager.
    static constexpr uint32_t kEvFinger = 0;
    // Screen on/off. Proof (DT trace, t7572):
    //   "receive message from driver: screen off, 2" ->
    //       "UpdateEventState: event = 1, state = 0"
    //   "receive message from driver: screen on, 3" ->
    //       "UpdateEventState: event = 1, state = 1"
    static constexpr uint32_t kEvScreen = 1;
    static constexpr uint32_t kEvHbm = 2;
    // InitEventManager @0x21580 order: stash the device, then let every
    // update() dispatch through us.
    void init(Device* dev) {
        device = dev;
        events.owner = this;
    }
};

// AncEventStateHandler — the stock event-state callback dispatch. This is the
// single source of the legacy finger reports; both the touch worker and the
// service's [FPM] OnPointerDown reach it through EventManager::update().
inline void dispatchEventState(Manager* m, uint32_t event, int state) {
    if (!m || !m->device) return;
    if (event == Manager::kEvFinger) {
        // AncFingerDownReport / AncFingerUpReport -> legacy 1002 / 1003.
        // The adapter maps >=1000 to AcquiredInfo::VENDOR(code-1000), which is
        // exactly stock's "onAcquired byte acquiredInfo =6 vendorCode=2/3".
        uint64_t legacy = (state != 0) ? 1002 : 1003;
        if (m->device->notify && m->device->notify[ACQUIRED])
            m->device->notify[ACQUIRED](0, legacy, 0, 0, 0);
    } else if (event == Manager::kEvHbm && state != 0) {
        ANCENG_LOGD("[ANC_HAL][EventHandler][D]: HBM is Ready");
    }
}

// Finger wait (AncEnrollAuthWaitFingerDown @0x2b960, AncWaitFingerDown):
// enroll -> remaining-timeout cap 60000ms; auth -> infinite.
// Stock g_previous_finger_up_time: after a capture with the finger still
// down, the next wait blocks until a FRESH lift-and-touch (otherwise the TA
// captures an empty frame -> extract 305 / enroll 303).
//
// IMPORTANT (corrected against the real stock capture): this waits for the
// FINGER event ONLY. The HBM wait is a SEPARATE, LATER step — see waitHbm().
// Real stock auth, 10-04 15:44:28:
//     .234 onAcquired vendorCode=2     (1002, finger down)
//     .241 onAcquired vendorCode=20    (1020)
//     .242 TRANSMIT >>>> cmd 7 (SENSOR_SET_POWER_MODE)
//     .281 TRANSMIT <<<< cmd 7, 39 ms
//     .543 [ANC_HAL][HbmEventDetect][I]: get hbm event ... FULL_HBM_SET
//     .564 enable sensor spi clk once: 1
//     .565 [ANC_HAL][Auth][I]: authenticate retry :0
//     .565 TRANSMIT >>>> cmd 5 (SENSOR_CAPTURE_IMAGE)
// i.e. power-mode is set BEFORE the HBM event is awaited.
inline int waitFinger(Manager* m, bool isAuth) {
    // Stock g_previous_finger_up_time (@0x2b960): after a wait has fired
    // (finger down), the next wait must see a LIFT (finger-up edge advance)
    // before accepting down-state — else the TA captures a stale/empty
    // frame -> extract 305 / enroll 303. A plain 0-sentinel never arms
    // (0 == 0 initially), so use an explicit waited_ flag.
    static __thread bool waited_ = false;
    static __thread uint64_t lastWaitUp_ = 0;
    uint32_t timeout = isAuth ? 0xffffffff : 60000;
    int rc;
    if (waited_ && m->events.fingerUpMs() == lastWaitUp_) {
        // No lift since the last wait: block for a FRESH down edge, no
        // readyCheck short-circuit on the stale down-state.
        rc = m->events.wait(Manager::kEvFinger, 1, (int)timeout, false);
        if (rc != 0) return (rc == 0x15 && !isAuth) ? 0x198 : rc;
    } else {
        rc = m->events.wait(Manager::kEvFinger, 1, (int)timeout, true);
        if (rc != 0) return (rc == 0x15 && !isAuth) ? 0x198 : rc;
    }
    waited_ = true;
    lastWaitUp_ = m->events.fingerUpMs();
    return 0;
}

// WaitHbm @0x2bc80 blocking half: event 2 state 1, 600ms; timeout -> 0x25.
// Deliberately SILENT: across the whole instrumented DT capture there are 16
// touches, 16 "wait hbm spent" lines and 16 "has been ready" lines, all from
// waitHbmReady() below (one per touch, after SENSOR_SET_POWER_MODE). If this
// blocking wait also logged, the counts would be 32.
inline int waitHbmBlocking(Manager* m) {
    // readyCheck=true + quiet=true: if event 2 is already set the call returns
    // immediately (no deadlock if the uevent beat us to the wait), but it never
    // logs — which keeps the "has been ready" count at exactly one per touch.
    //
    // Screen-off includes the HAL-driven DOZE transition before the mtkfb HBM
    // write. SurfaceFlinger's first power-mode commit can take longer than the
    // stock AOD 600 ms, so allow 2 s while the panel is off. On-screen keeps
    // stock's 600 ms.
    const int timeout = (gIsScreenOff != 0) ? 2000 : 600;
    int rc = m->events.wait(Manager::kEvHbm, 1, timeout, true, true);
    if (rc == 0x15) rc = 0x25;
    return rc;
}

// Post-SENSOR_SET_POWER_MODE HBM ready-check (stock WaitHbm's logging face).
// DT trace, verbatim ordering (00:01:12.789 -> .824):
//     .789 fingerprint.jiiov: acquired legacy=1020
//     .789 [SensorManager][D]: current power mode: 2 (SENSOR_SLEEP), set sensor power mode: 10 (SENSOR_WAKEUP_SCAN)
//     .789 TRANSMIT >>>> command id = 7 (SENSOR_SET_POWER_MODE)
//     .824 TRANSMIT <<<< command id = 7 ..., spent time = 35 ms
//     .824 [ANC_HAL][EventManager][I]: waiting event 2, state 1, has been ready
//     .824 [ANC_HAL][FingerStatus][D]: wait hbm spent 35 ms
//     .824 [ANC_TAC][TacCommand][I]: enable sensor spi clk once: 1
//     .824 [ANC_HAL][Auth][I]: authenticate retry :0
//     .824 TRANSMIT >>>> command id = 5 (SENSOR_CAPTURE_IMAGE)
// "wait hbm spent" therefore times the interval that CONTAINS the cmd-7
// transmit, so t0 must be sampled BEFORE the power-mode set.
inline void waitHbmReady(Manager* m, uint64_t t0) {
    // 600 ms cap (stock WaitHbm timeout) rather than 0: the normal case still
    // short-circuits because waitFingerDown already consumed the HBM edge, but
    // if the panel's FULL_HBM_CLEAR lands in the gap we still block for the
    // real SET instead of capturing an unlit frame.
    m->events.wait(Manager::kEvHbm, 1, 600, true);
    ANCENG_LOGD("[ANC_HAL][FingerStatus][D]: wait hbm spent %d ms",
                (int)(NowMs() - t0));
}

// AncEnrollAuthWaitFingerDown @0x2b960: finger event, then the 600 ms HBM wait.
// This is the stock combined helper and is what the enroll/auth workers call
// ahead of the 1020 report.
inline int waitFingerDown(Manager* m, bool isAuth) {
    int rc = waitFinger(m, isAuth);
    if (rc != 0) return rc;
    return waitHbmBlocking(m);
}

}  // namespace anc
