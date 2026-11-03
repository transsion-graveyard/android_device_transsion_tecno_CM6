/*
 * anc.hal.so rebuild — stock log-tag parity helpers.
 *
 * The shipped anc.hal.so logs under tag "JV_FP"; every [ANC_HAL] / [ANC_TAC]
 * line in the real stock capture carries "I JV_FP", and the DT capture shows
 * the same messages at their true priorities, e.g.
 *   "D JV_FP : [ANC_HAL][EventManager][D]: UpdateEventState: ..."
 *   "I JV_FP : [ANC_HAL][TouchEventDetect][I]: get touch event, ..."
 * Emitting the identical tag, priority and message text lets our build be
 * diff'ed directly against the stock captures.
 *
 * android log priorities: 2=VERBOSE 3=DEBUG 4=INFO 5=WARN 6=ERROR
 *
 * This header exists (rather than keeping the macros in Manager.h) because
 * SensorDevice.h must log too, and Manager.h already includes SensorDevice.h,
 * so SensorDevice.h cannot include Manager.h.
 */
#pragma once

#include <android/log.h>

#ifndef ANCENG_LOGV
#define ANCENG_LOGV(...) __android_log_print(2, "JV_FP", __VA_ARGS__)
#endif
#ifndef ANCENG_LOGD
#define ANCENG_LOGD(...) __android_log_print(3, "JV_FP", __VA_ARGS__)
#endif
#ifndef ANCENG_LOGI
#define ANCENG_LOGI(...) __android_log_print(4, "JV_FP", __VA_ARGS__)
#endif
#ifndef ANCENG_LOGW
#define ANCENG_LOGW(...) __android_log_print(5, "JV_FP", __VA_ARGS__)
#endif
#ifndef ANCENG_LOGE
#define ANCENG_LOGE(...) __android_log_print(6, "JV_FP", __VA_ARGS__)
#endif
