//
// SPDX-FileCopyrightText: The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
//

// Source-built shim replacing the prebuilt vendor libaedv.so.
//
// The vendor blobs have 56 DT_NEEDED entries for libaedv.so. They import
// only the symbols below; the rest of the stock AEE implementation is not
// required by the image. All exception/log routing is a no-op.
//
// aee_fprintf, aee_load_is_customer_built, aee_load_is_user_built,
// aee_log_route, get_aee_log_status and the rtt_dump_* entry points are
// used by vendor/bin/rttv_v2. The remaining entry points are the common
// camera/audio/display/mbrain imports.

#include <stdint.h>

int aee_switch_ftrace(int status) {
    (void)status;
    return 0;
}

int aee_system_exception(const char *lib, const char *path,
                         unsigned int options, const char *log_msg) {
    (void)lib;
    (void)path;
    (void)options;
    (void)log_msg;
    return 0;
}

int aee_system_warning(const char *lib, const char *path,
                       unsigned int options, const char *log_msg) {
    (void)lib;
    (void)path;
    (void)options;
    (void)log_msg;
    return 0;
}

int aee_modem_warning(const char *lib, const char *path, unsigned int options,
                      const char *log_msg, const char *ver) {
    (void)lib;
    (void)path;
    (void)options;
    (void)log_msg;
    (void)ver;
    return 0;
}

int aee_log_msg(const char *lib, const char *path, unsigned int options,
                const char *log_msg, const char *ver) {
    (void)lib;
    (void)path;
    (void)options;
    (void)log_msg;
    (void)ver;
    return 0;
}

int aee_fprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

int aee_load_is_customer_built(void) {
    return 0;
}

int aee_load_is_user_built(void) {
    return 0;
}

int aee_log_route(const char *path) {
    (void)path;
    return 0;
}

int get_aee_log_status(void) {
    return 0;
}

int rtt_dump_backtrace(void) {
    return 0;
}

int rtt_dump_all_backtrace(void) {
    return 0;
}

int rtt_dump_all_backtrace_by_name(const char *name) {
    (void)name;
    return 0;
}
