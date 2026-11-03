/*
 * anc.hal.so rebuild — exported C interface.
 *
 * RE: Anc* @0x127xxx thin forwarders (spec §1), Fpm* bodies, challenge/auth
 * flows, InitFingerprintManager @0x1f6b0 ordering, pointer/detect entries.
 * This TU compiles to `anc_hal` (SONAME anc.hal.so), replacing the prebuilt
 * odm/lib64/anc.hal.so. Engine.h dlopen/dlsym surface unchanged.
 */
#include <android/log.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#include "AncTypes.h"
#include "custom/CustomManager.h"
#include "manager/EnrollAuth.h"
#include "manager/Manager.h"

#define ANC_LOGI(...) __android_log_print(ANDROID_LOG_INFO, "JV_FP", __VA_ARGS__)
#define ANC_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "JV_FP", __VA_ARGS__)

namespace {

anc::Device g_device{};
anc::Manager g_manager{};
bool g_inited = false;
pthread_mutex_t g_apiLock = PTHREAD_MUTEX_INITIALIZER;

anc::Manager* mgrOf(void* h) {
    if (!h || !g_inited) return nullptr;
    return &g_manager;
}

void queueTask(const char* name, void (*fn)(void*), void* arg, uint32_t prio) {
    anc::Task t;
    t.name = name;
    t.fn = fn;
    t.arg = arg;
    t.prio = prio;
    g_manager.consumer.push(t);
}

void doEnrollTask(void* m) { anc::doEnroll(static_cast<anc::Manager*>(m)); }
void doAuthTask(void* m) { anc::doAuthenticate(static_cast<anc::Manager*>(m)); }

}  // namespace

extern "C" {

// ---- lifecycle ----
void* GetFingerprintDevice() { return &g_device; }

int InitFingerprintDevice(void* dev) {
    if (!dev) return -1;
    pthread_mutex_lock(&g_apiLock);
    if (g_inited) {
        pthread_mutex_unlock(&g_apiLock);
        return 0;
    }
    // Init order mirrors InitFingerprintManager @0x1f6b0:
    // sensor dev -> CA -> sensor mgr -> producer -> events -> consumer ->
    // extension -> custom(workers) -> TA version -> module id -> algo.
    if (anc::SensorDevice::get().open() != 0) {
        ANC_LOGE("init sensor device failed");
        pthread_mutex_unlock(&g_apiLock);
        return -1;
    }
    if (anc::CaSession::get().open() != 0) {
        ANC_LOGE("fail to init anc ca");
        pthread_mutex_unlock(&g_apiLock);
        return -1;
    }
    // Stock InitEventManager @0x21580 stores the device AND installs the
    // manager as the EventManager dispatch owner. Assigning only .device left
    // events.owner == nullptr, so EventManager::update() -> dispatchEventState()
    // returned immediately for EVERY event: the engine never emitted the
    // legacy finger-down/up codes (AncFingerDownReport 1002 /
    // AncFingerUpReport 1003) and never logged "HBM is Ready".
    // Live proof (CM6, 2026-10-17 23:49, enroll):
    //   47x "UpdateEventState: event = 0, state = 1" and 45x "... state = 0"
    //   but 0x "HBM is Ready", 0x "acquired legacy=1002/1003".
    // => the adapter's startCaptureLight()/stopCaptureLight() gate never
    //    opened, endIllumination() never ran at enroll end, and the last
    //    tran_lcm_hbm_store stayed value = 1 (HBM stuck lit).
    g_manager.init(&g_device);
    g_manager.consumer.start();
    anc::custom::startWorkers(&g_manager);
    // Full stock init sequence (TRANSMIT order verified in stock log):
    anc::SensorDevice::get().powerOn();
    anc::algo::extensionSetTime();
    anc::SensorDevice::get().halSetPowerMode(4);  // POWER_ON_INIT
    anc::algo::sensorInit();
    anc::SensorDevice::get().halSetPowerMode(2);  // SLEEP
    anc::algo::vendorSetProject();
    anc::SensorDevice::get().halSetPowerMode(2);
    char modId[0x20] = {};
    anc::SensorDevice::get().getModuleId(modId, sizeof(modId));
    ANC_LOGI("[ANC_HAL][FPManager][I]: module id:%s", modId);
    // Stock init order (TRANSMIT order verified in stock log):
    //   powerOn, EXTENSION_SET_TIME, POWER_ON_INIT(4), SENSOR_INIT,
    //   SLEEP(2), VENDOR_SET_PROJECT, SLEEP, GET_MODULE_ID, ALGO_INIT(1),
    //   ALGO_VERSION(17), EXTENSION_LOAD_CALIBRATION, SLEEP,
    //   EXTENSION_INIT_DATA_COLLECT, VENDOR_GET_QR, EXTENSION_UPDATE_FILE,
    //   TEMPLATE_SET_ACTIVE_GROUP(12), TEMPLATE_LOAD_DB(11),
    //   TEMPLATE_GET_AUTH_ID(13).
    // There is NO ALGORITHM_INIT_VERIFY in stock's init; the earlier
    // initVerify(false) here put the TA into verify mode at boot and made the
    // later auth initVerify return 0x1a ("already initialised") instead of
    // stock's 0.
    anc::algo::algoInit();
    char algoVer[100] = {};
    anc::algo::algoGetVersion(algoVer, sizeof(algoVer));
    ANC_LOGI("[ANC_HAL][FPManager][I]: algo version:%s", algoVer);
    anc::algo::extensionLoadCalibration();
    anc::SensorDevice::get().halSetPowerMode(2);
    anc::algo::extensionInitDataCollect();
    char qr[0x30] = {};
    anc::algo::vendorGetQr(qr, sizeof(qr));
    anc::algo::extensionUpdateFile();
    anc::algo::templateSetActiveGroup(0, "/data/vendor_de/0/fpdata/");
    anc::algo::templateLoadDatabase();
    uint64_t authId = 0;
    anc::algo::templateGetAuthId(&authId);
    g_manager.prod.authId = authId;
    g_device.fpManager = &g_manager;
    g_inited = true;
    ANC_LOGI("init fingerprint device");
    pthread_mutex_unlock(&g_apiLock);
    return 0;
}

int DeinitFingerprintDevice(void* /*dev*/) {
    g_inited = false;
    g_device.fpManager = nullptr;
    return 0;
}

void AncSetNotifyCallback(void* dev, void* table) {
    // Stock @0x27100: *param_1 = param_2.
    *(void**)dev = table;
}
void AncSetExtensionCallback(void* dev, void* cb) {
    *(void**)((char*)dev + 8) = cb;
}
void AncSetCustomCallback(void* dev, void* cb) {
    *(void**)((char*)dev + 16) = cb;
}

// ---- challenge / auth id ----
int AncGenerateChallenge(void* h) {
    anc::Manager* m = mgrOf(h);
    if (!m) return -1;
    uint64_t ch = 0;
    int rc = anc::token::getEnrollChallenge(&ch);
    if (rc == 0 && g_device.notify)
        g_device.notify[anc::CHALLENGE_GENERATED](0, ch, 0, 0, 0);
    return rc;
}
int AncRevokeChallenge(void* h, uint64_t ch) {
    anc::Manager* m = mgrOf(h);
    if (!m) return -1;
    uint64_t cur = 0;
    if (anc::token::getEnrollChallenge(&cur) == 0) m->prod.challenge = cur;
    if (g_device.notify) g_device.notify[anc::CHALLENGE_REVOKED](0, ch, 0, 0, 0);
    return 0;
}
uint64_t AncGetAuthenticatorId(void* h) {
    anc::Manager* m = mgrOf(h);
    if (!m) return 0;
    // Stock AncGetAuthenticatorId @0x272e0 -> FpmGetAuthenticatorId @0x27d00:
    // it re-queries the TA on EVERY call (TEMPLATE_GET_AUTHENTICATOR_ID,
    // cat 3 cmd 13), stores the fresh id at mgr+0x1a0, then hands that id to
    // the callback (vtable[+0x10] == AuthIdRetrieved). The previous version
    // returned a value cached once at init, so after an enrollment committed a
    // new template the framework kept the STALE authenticator id and dropped
    // every HAT whose authenticatorId no longer matched.
    uint64_t id = 0;
    int rc = anc::algo::templateGetAuthId(&id);
    if (rc == 0) m->prod.authId = id;
    if (g_device.notify) g_device.notify[anc::AUTH_ID_RETRIEVED](0, id, 0, 0, 0);
    return id;
}
int AncInvalidateAuthenticatorId(void* h) {
    anc::Manager* m = mgrOf(h);
    if (!m) return -1;
    // Stock AncInvalidateAuthenticatorId @0x27360 -> FpmInvalidateAuthenticatorId
    // @0x27db0: ask the TA for a NEW authenticator id (stored at mgr+0x1a0) and
    // notify AUTH_ID_INVALIDATED with it. The TA side is UpdateAuthenticatorId
    // (cat 3 cmd 22, block+0 = 0x16).
    uint64_t newId = 0;
    if (anc::algo::templateUpdateAuthId(&newId) == 0) m->prod.authId = newId;
    if (g_device.notify) g_device.notify[anc::AUTH_ID_INVALIDATED](0, newId, 0, 0, 0);
    return 0;
}

// ---- operations (Fpm* semantics: lock, stage, queue) ----
int AncEnroll(void* h, const uint8_t* hat) {
    anc::Manager* m = mgrOf(h);
    if (!m || !hat) return -1;
    pthread_mutex_lock(&m->prod.lock);
    memcpy(m->prod.hat, hat, 0x45);
    m->prod.enrollTimeout = 600;
    queueTask("DoEnroll", doEnrollTask, m, 1);
    pthread_mutex_unlock(&m->prod.lock);
    return 0;
}
int AncAuthenticate(void* h, uint64_t opId) {
    anc::Manager* m = mgrOf(h);
    if (!m) return -1;
    pthread_mutex_lock(&m->prod.lock);
    m->prod.challenge = opId;
    queueTask("DoAuthenticate", doAuthTask, m, 1);
    pthread_mutex_unlock(&m->prod.lock);
    return 0;
}
int AncCancel(void* h) {
    anc::Manager* m = mgrOf(h);
    if (!m) return -1;
    pthread_mutex_lock(&m->prod.lock);
    m->consumer.pushCancel();
    m->events.cancel(anc::Manager::kEvFinger);
    m->events.cancel(anc::Manager::kEvHbm);
    pthread_mutex_unlock(&m->prod.lock);
    return 0;
}
int AncEnumerate(void* h) {
    anc::Manager* m = mgrOf(h);
    if (!m) return -1;
    uint32_t ids[5] = {};
    uint32_t count = 0;
    anc::token::getAllIds(ids, 5, &count);
    if (g_device.notify) {
        uint64_t packed = 0;
        // Adapter copies ids[]+count; pass via two u64 slots the DT Engine
        // re-splits (a1=ptr-ish/a2=count). Use stack array address.
        g_device.notify[anc::ENUMERATED](0, (uint64_t)ids, count, 0, 0);
        (void)packed;
    }
    return 0;
}
int AncRemove(void* h, const uint32_t* ids, uint32_t count) {
    anc::Manager* m = mgrOf(h);
    if (!m || !ids || !count) return -1;
    uint32_t removed[5] = {};
    uint32_t n = 0;
    for (uint32_t i = 0; i < count && n < 5; i++) {
        int rc = anc::token::deleteFingerprint(ids[i]);
        ANC_LOGI("[ANC_HAL][FPManager][I]: remove id=%u rc=%d", ids[i], rc);
        if (rc == 0) removed[n++] = ids[i];
    }
    if (g_device.notify && n)
        g_device.notify[anc::REMOVED](0, (uint64_t)removed, n, 0, 0);
    // Return the number of templates the TA actually deleted. The adapter uses
    // this to avoid telling the framework an enrollment is gone when the TEE
    // delete failed (which leaves an orphaned template that blocks re-enroll).
    return static_cast<int>(n);
}
int AncSetActiveGroup(void* h, int32_t gid, const char* path) {
    anc::Manager* m = mgrOf(h);
    if (!m || !path) return -1;
    if (path[0] == '/' && path[1] == 0) return -1;  // unwritable (stock check)
    pthread_mutex_lock(&m->prod.lock);
    m->prod.groupId = gid;
    strncpy(m->prod.storePath, path, sizeof(m->prod.storePath) - 1);
    anc::algo::templateSetActiveGroup((uint32_t)gid, path);
    pthread_mutex_unlock(&m->prod.lock);
    return 0;
}

// ---- pointer / UI / detect (FpmOnPointer* -> event mgr) ----
// AOSP parity: pointer callbacks only carry touch state to the engine. The
// framework owns optical illumination and calls onUiReady() after the display
// has been configured. No panel sysfs writes or synthetic HBM uevents belong
// on this path.
void AncOnPointerDown(void* h, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                      uintptr_t) {
    anc::Manager* m = mgrOf(h);
    if (!m) return;
    m->events.update(anc::Manager::kEvFinger, 1);
}
void AncOnPointerUp(void* h, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
                    uintptr_t) {
    anc::Manager* m = mgrOf(h);
    if (!m) return;
    m->events.update(anc::Manager::kEvFinger, 0);
    // Require a fresh framework readiness callback for the next capture.
    m->events.clearSilent(anc::Manager::kEvHbm);
}
void AncOnUiReady(void* h) {
    anc::Manager* m = mgrOf(h);
    if (!m) return;
    // ISession::onUiReady is the AIDL illumination-ready edge.
    m->events.update(anc::Manager::kEvHbm, 1);
}
void AncClearHbm(void* h) {
    anc::Manager* m = mgrOf(h);
    if (!m) return;
    // The proprietary touch worker does not pass through AOSP onPointerUp.
    // Clear silently so a later screen-off capture cannot consume a stale
    // HBM-ready latch from the previous on-screen operation.
    m->events.clearSilent(anc::Manager::kEvHbm);
}
bool AncIsScreenOff(void* h) {
    return mgrOf(h) != nullptr && anc::gIsScreenOff != 0;
}
void AncDetectInteraction(void* h) {
    anc::Manager* m = mgrOf(h);
    if (!m) return;
    // Detect = behavior-detect capture loop; simplified to a finger wait
    // that emits slot 1 progress via the touch worker path.
    queueTask("DoDetect", doAuthTask, m, 2);
}
int AncResetLockout(void* h, const void* /*hat*/) {
    return h ? 0 : -1;
}

// ---- Tran / factory / fido (stubs in stock FpmTran* — log + return 0) ----
int AncTranSendCommand(void* h, int, int) { return h ? 0 : -1; }
int AncTranSendFactoryTestRequire(void* h) { return h ? 0 : -1; }
int AncTranAuthenticateFido(void* h, int) { return h ? 0 : -1; }
int AncTranSendFingerEvent(void* h, int, int) { return h ? 0 : -1; }

// Extension dispatch entry (called by rebuilt extension service):
// routes cmd into ExtensionCommandWork/factory paths (spec §4g condensed).
int AncExcuteCommand(void* h, int32_t cmd, const uint8_t* in, uint32_t inLen,
                     void** out, uint32_t* outLen) {
    anc::Manager* m = mgrOf(h);
    if (!m) return -1;
    if (cmd == anc::custom::kExtQrInfo) {  // 0xc: QR info
        char modId[0x30] = {};
        int rc = anc::SensorDevice::get().getModuleId(modId, sizeof(modId));
        if (out && outLen) {
            *outLen = 0x68;
            *out = malloc(*outLen);
            if (*out) {
                memset(*out, 0, *outLen);
                *(int*)*out = rc;
            }
        }
        return rc;
    }
    if (cmd == anc::custom::kExtTranUi) return 0;  // tran UI query
    return 0x17;  // unknown (stock "invalid command id")
}

}  // extern "C"
