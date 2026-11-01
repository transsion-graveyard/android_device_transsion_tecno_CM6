//
// SPDX-FileCopyrightText: The LineageOS Project
// SPDX-License-Identifier: Apache-2.0
//

// No-op shim for the prebuilt vendor libTran_Log.so.
//
// The stock library only exports the two symbols below (verified with
// `readelf -W --dyn-syms libTran_Log.so`):
//   _Z14tran_log_levelv          -> tran_log_level()
//   _Z22Is_tran_force_open_logi  -> Is_tran_force_open_log(int)
// None of the vendor blobs that link against libTran_Log.so import either of
// them, so the bodies are irrelevant; they are only kept so the exported
// symbol names/ABI stay identical to stock.

int Is_tran_force_open_log(int) {
    return 1;
}

void tran_log_level() {
}
