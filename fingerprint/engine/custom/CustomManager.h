/*
 * anc.hal.so rebuild — custom/optical manager: touch + HBM event workers,
 * factory-test dispatch, extension command routing.
 *
 * RE: InitCustomManager @0x39f80, InitTouchEventDetectWorker @0x3b880,
 * DoTouchEventDetectTask @0x3bc40 (code 195 -> event mgr),
 * InitHbmEventDetectWorker @0x3c040, DoHbmEventDetectTask @0x3c220
 * (TRAN_FULL_HBM_EVENT=FULL_HBM_SET -> event mgr), AncHandleNetlinkMessage
 * @0x2cca0, CustomExtensionCommand @0x3ad60, ExtensionFactoryTest @0x35820,
 * FactoryTestGroupDoTest @0x25180 (spec §4f-§4g).
 */
#pragma once

#include <android/log.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/netlink.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "manager/Manager.h"

namespace anc {
namespace custom {

// Touch device open (stock InitTouchEventDetectWorker @0x3b880). Evidence:
//   * the only device-path literals in the stock binary are "/dev/input" (the
//     directory) and the hardcoded default "/dev/input/event5";
//   * stock ships "get touch event device path: %s, name: %s",
//     "get touch event fd failed, fd: %d, use default path: %s" and
//     "open default touch event device fail, fd:%d";
//   * stock calls EVIOCGNAME (0x807f4506) and the DT trace shows it consuming
//     only EV_KEY code 195 (16 downs / 15 ups == 16 touches).
// /dev/input enumeration order is not stable across boots, so prefer the
// MediaTek touch node ("mtk-tpd": `irq/26-mtk-tpd` in the stock dmesg and
// strncpy(info->type,"mtk-tpd") in the MTK TPD driver), then the stock default
// path, then the first openable node — which is the previously working
// behaviour, so this can only be at least as robust as before.
inline int openTouchDevice(char* pathOut, size_t pathLen, char* nameOut,
                           size_t nameLen) {
    int chosen = -1;
    DIR* d = opendir("/dev/input");
    if (d) {
        struct dirent* e;
        while ((e = readdir(d))) {
            if (e->d_name[0] != 'e') continue;  // eventN
            char path[64];
            snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
            int fd = ::open(path, O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            char name[128] = {0};
            if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) < 0) name[0] = 0;
            // The FOD gesture is an EV_KEY 195 event. Device names and event
            // numbering vary after suspend/reprobe, so capability is the
            // authoritative selector. The old name-first selection could
            // bind the HAL to a touchscreen node that exposed coordinates but
            // not the off-screen FOD key.
            unsigned long keyBits[(KEY_MAX + 1 + (sizeof(unsigned long) * 8) - 1) /
                                  (sizeof(unsigned long) * 8)] = {};
            const bool hasFodKey =
                    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0 &&
                    ((keyBits[195 / (sizeof(unsigned long) * 8)] >>
                      (195 % (sizeof(unsigned long) * 8))) & 1UL);
            if (hasFodKey) {
                chosen = fd;
                snprintf(pathOut, pathLen, "%s", path);
                snprintf(nameOut, nameLen, "%s", name);
                break;
            }
            ::close(fd);
        }
        closedir(d);
    }
    if (chosen < 0) {
        // Keep the stock fallback, but use the current CM6 node when event
        // enumeration races the touch driver's probe.
        const char* fallbacks[] = {"/dev/input/event4", "/dev/input/event5"};
        for (const char* fallbackPathName : fallbacks) {
            chosen = ::open(fallbackPathName, O_RDONLY | O_NONBLOCK);
            if (chosen < 0) continue;
            unsigned long keyBits[(KEY_MAX + 1 + (sizeof(unsigned long) * 8) - 1) /
                                  (sizeof(unsigned long) * 8)] = {};
            if (ioctl(chosen, EVIOCGNAME(127), nameOut) >= 0 &&
                ioctl(chosen, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0 &&
                ((keyBits[195 / (sizeof(unsigned long) * 8)] >>
                  (195 % (sizeof(unsigned long) * 8))) & 1UL)) {
                snprintf(pathOut, pathLen, "%s", fallbackPathName);
                break;
            }
            ::close(chosen);
            chosen = -1;
        }
        if (chosen < 0) {
            ANCENG_LOGE(
                "[ANC_HAL][TouchEventDetect][E]: open default touch event device fail, fd:%d",
                chosen);
        }
    }
    if (chosen >= 0)
        ANCENG_LOGI(
            "[ANC_HAL][TouchEventDetect][I]: get touch event device path: %s, name: %s",
            pathOut, nameOut);
    return chosen;
}

// Touch worker: poll the tp input node; EV_KEY code 195 -> finger event.
inline void* touchTask(void* arg) {
    Manager* m = static_cast<Manager*>(arg);
    char path[64] = {0}, name[128] = {0};
    int fd = openTouchDevice(path, sizeof(path), name, sizeof(name));
    if (fd < 0) return nullptr;
    ANCENG_LOGI("[ANC_HAL][TouchEventDetect][I]: AncHandleTouchEventDetect");
    struct pollfd pfd{fd, POLLIN, 0};
    for (;;) {
        if (poll(&pfd, 1, -1) <= 0) continue;
        struct input_event ev[32];
        ssize_t n = read(fd, ev, sizeof(ev));
        for (ssize_t i = 0; i < n / (ssize_t)sizeof(ev[0]); i++) {
            if (ev[i].type == EV_KEY && ev[i].code == 195) {
                ANCENG_LOGI(
                    "[ANC_HAL][TouchEventDetect][I]: get touch event, code: %d, value: %d",
                    ev[i].code, ev[i].value);
                m->events.update(Manager::kEvFinger, ev[i].value ? 1 : 0);
            }
        }
    }
    return nullptr;
}

// HBM worker: netlink uevent socket; TRAN_FULL_HBM_EVENT=FULL_HBM_SET
// -> hbm event state 1. A real display-side event may satisfy this wait;
// the AOSP onUiReady() path also releases it through AncOnUiReady().
//
// Thread-ownership proof (DT trace): t7616 emits "get hbm event" x47,
// "UpdateEventState: event = 2" x32 and "HBM is Ready" x32 — i.e. the uevent
// is consumed, the event state is updated and the downstream dispatch all run
// on this worker's own thread.
inline void* hbmTask(void* arg) {
    Manager* m = static_cast<Manager*>(arg);
    int sock =
        socket(PF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK, NETLINK_KOBJECT_UEVENT);
    if (sock < 0) return nullptr;
    struct sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = getpid();
    addr.nl_groups = 0xffffffff;
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    ANCENG_LOGI("[ANC_HAL][HbmEventDetect][I]: AncHandleHbmEventDetect");
    char buf[4096];
    for (;;) {
        ssize_t n = recv(sock, buf, sizeof(buf) - 2, 0);
        if (n <= 0) {
            usleep(10000);
            continue;
        }
        buf[n] = 0;
        buf[n + 1] = 0;  // double-NUL terminator for field walk
        // Walk NUL-separated uevent fields like stock DoHbmEventDetectTask
        // (strstr on buf only sees the first segment before the first NUL).
        for (char* f = buf; *f != 0; f += strlen(f) + 1) {
            if (strncmp(f, "TRAN_FULL_HBM_EVENT=", 20) == 0) {
                const char* v = f + 20;
                ANCENG_LOGI(
                    "[ANC_HAL][HbmEventDetect][I]: get hbm event, TRAN_FULL_HBM_EVENT=%s",
                    v);
                if (strcmp(v, "FULL_HBM_SET") == 0)
                    m->events.update(Manager::kEvHbm, 1);
                else
                    // Stock clears event 2 SILENTLY: the DT capture has 30
                    // "FULL_HBM_CLEAR" uevents but ZERO
                    // "UpdateEventState: event = 2, state = 0" lines, and the
                    // blocking HBM wait really does block on every touch.
                    m->events.clearSilent(Manager::kEvHbm);
                break;
            }
        }
    }
    return nullptr;
}

// Stock AncNetlinkEventManager uses AF_NETLINK/SOCK_RAW with protocol 30,
// binds the process PID, and receives a screen event from tran_fp. The stock
// disassembly is explicit: socket(16, 3, 30). This is not NETLINK_USERSOCK;
// using protocol 3 silently misses every SCRN Netlink_unicast notification.
// tran_fp sends event 2 for screen-off and event 3 for screen-on. This is a
// separate channel from the NETLINK_KOBJECT_UEVENT HBM listener above.
inline void* screenTask(void* arg);

inline void startWorkers(Manager* m) {
    static bool started = false;
    if (started) return;
    started = true;
    pthread_t t;
    pthread_create(&t, nullptr, touchTask, m);
    pthread_detach(t);
    pthread_create(&t, nullptr, hbmTask, m);
    pthread_detach(t);
    pthread_create(&t, nullptr, screenTask, m);
    pthread_detach(t);
}

// Netlink driver message -> SCREEN state (AncHandleNetlinkMessage @0x2cca0).
// Verbatim DT trace (t7617, PID 7572):
//   "receive message from driver: screen off, 2"
//       -> "UpdateEventState: event = 1, state = 0"
//   "receive message from driver: screen on, 3"
//       -> "UpdateEventState: event = 1, state = 1"
// The earlier reconstruction mapped msgs 2/3 onto the FINGER event, which
// would inject phantom finger-downs (and therefore phantom 1002 captures) on
// every screen on/off — the DT evidence rules that out.
inline void handleNetlink(Manager* m, const char* what, int msg) {
    ANCENG_LOGI(
        "[ANC_HAL][NetlinkManager][I]: receive message from driver: %s, %d",
        what, msg);
    if (msg == 2) {
        gIsScreenOff = 1;
        m->events.update(Manager::kEvScreen, 0);
    } else if (msg == 3) {
        gIsScreenOff = 0;
        m->events.update(Manager::kEvScreen, 1);
    }
}

inline void* screenTask(void* arg) {
    Manager* m = static_cast<Manager*>(arg);
    int sock = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK, 30);
    if (sock < 0) {
        ANCENG_LOGE("[ANC_HAL][NetlinkManager][E]: fail to create screen netlink socket: %s",
                    strerror(errno));
        return nullptr;
    }
    struct sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = static_cast<uint32_t>(getpid());
    addr.nl_groups = 0;
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        ANCENG_LOGE("[ANC_HAL][NetlinkManager][E]: fail to bind screen netlink socket: %s",
                    strerror(errno));
        close(sock);
        return nullptr;
    }

    // Stock AncNetlinkEventManager sends a registration frame after bind;
    // tran_fp uses that frame to remember this socket's netlink PID before it
    // emits SCRN Netlink_unicast notifications. Without registration the
    // socket is valid but receives no screen events.
    struct {
        nlmsghdr header;
    } registration{};
    registration.header.nlmsg_len = sizeof(registration);
    registration.header.nlmsg_type = 0x90;
    registration.header.nlmsg_pid = addr.nl_pid;
    struct sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    ssize_t registered = sendto(sock, &registration, sizeof(registration), 0,
                                reinterpret_cast<struct sockaddr*>(&kernel),
                                sizeof(kernel));
    if (registered < 0) {
        ANCENG_LOGE("[ANC_HAL][NetlinkManager][E]: screen netlink registration failed: %s",
                    strerror(errno));
    } else {
        ANCENG_LOGI("[ANC_HAL][NetlinkManager][I]: screen netlink registered pid=%u",
                    addr.nl_pid);
    }
    ANCENG_LOGI("[ANC_HAL][NetlinkManager][I]: screen netlink listener ready pid=%u",
                addr.nl_pid);

    alignas(nlmsghdr) char buf[4096];
    for (;;) {
        ssize_t n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) {
            usleep(10000);
            continue;
        }
        int remaining = static_cast<int>(n);
        for (nlmsghdr* nh = reinterpret_cast<nlmsghdr*>(buf);
             NLMSG_OK(nh, remaining);
             nh = NLMSG_NEXT(nh, remaining)) {
            if (NLMSG_PAYLOAD(nh, 0) < sizeof(int32_t)) continue;
            int32_t event = 0;
            memcpy(&event, NLMSG_DATA(nh), sizeof(event));
            handleNetlink(m, "screen", event);
        }
    }
    return nullptr;
}

// Extension command ids of interest (CustomExtensionCommand @0x3ad60).
enum : int { kExtTranUi = 10, kExtQrInfo = 0xc, kExtUi2 = 0xd };

// Factory-test external op table (g_factory_test_external_operation,
// stride {name, id, fn}; ids 1,3,4,5,6,7,8,9,0xf per spec).
struct FactoryOp {
    const char* name;
    int id;
    int (*fn)(void* mgr, void* arg);
};

}  // namespace custom
}  // namespace anc
