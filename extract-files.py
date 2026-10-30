#!/usr/bin/env -S PYTHONPATH=../../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

from extract_utils.file import File
from extract_utils.fixups_blob import (
    BlobFixupCtx,
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.fixups_lib import (
    lib_fixup_remove,
    lib_fixups,
    lib_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)
from extract_utils.tools import (
    llvm_objdump_path,
)
from extract_utils.utils import (
    run_cmd,
)

namespace_imports = [
    'device/transsion/tecno/CM6',
    'hardware/mediatek',
]


def lib_fixup_vendor_suffix(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_{partition}' if partition == 'vendor' else None


lib_fixups: lib_fixups_user_type = {
    **lib_fixups,
    (
        'vendor.twopac.hardware.xoo@1.0',
        'vendor.twopac.hardware.oxo@1.0',
        'vendor.twopac.hardware.oox@1.0',
    ): lib_fixup_vendor_suffix,
    'libwpa_client': lib_fixup_remove,
}


def blob_fixup_return_1(
    ctx: BlobFixupCtx,
    file: File,
    file_path: str,
    symbol: str,
    *args,
    **kwargs,
):
    for line in run_cmd(
        [
            llvm_objdump_path,
            '--dynamic-syms',
            file_path,
        ]
    ).splitlines():
        if line.endswith(f' {symbol}'):
            offset, _ = line.split(maxsplit=1)

            with open(file_path, 'rb+') as f:
                f.seek(int(offset, 16))
                f.write(b'\x01\x00\xa0\xe3')  # mov r0, #1
                f.write(b'\x1e\xff\x2f\xe1')  # bx lr

            break


blob_fixups: blob_fixups_user_type = {
    'vendor/app/Test.apk': blob_fixup()
        .apktool_patch('blob-patches/TestApk.patch', '-s'),
    'vendor/etc/test.conf': blob_fixup()
        .patch_file('blob-patches/TestConf.patch')
        .regex_replace('(LOG_.*_ENABLED)=1', '\\1=0')
        .add_line_if_missing('DEBUG=0'),
    ('vendor/etc/test.0.xml', 'vendor/etc/test.1.xml'): blob_fixup()
        .fix_xml(),
    'vendor/lib/test.so': blob_fixup()
        .patchelf_version('0_17_2')
        .fix_soname()
        .add_needed('to_add.so')
        .remove_needed('to_remove.so')
        .replace_needed('from.so', 'to.so')
        .clear_symbol_version('rpc_call_invoke')
        .strip_debug_sections()
        .binary_regex_replace(b'\xFF\x00\x00\x94', b'\xFE\x00\x00\x94')
        .sig_replace('C0 03 5F D6 ?? ?? ?? ?? C0 03 5F D6', '1F 20 03 D5')
        .call(blob_fixup_return_1, 'license_check'),

    'system_ext/bin/hw/android.hardware.audio.parameter_parser.service': blob_fixup()
        .replace_needed('android.hardware.audio.core-V3-ndk.so', 'android.hardware.audio.core-V4-ndk.so'),
    'vendor/lib64/mt6789/libmtkcam_hal_aidl_common.so': blob_fixup()
        .replace_needed('android.hardware.camera.common-V2-ndk.so', 'android.hardware.camera.common-V1-ndk.so'),
    'vendor/bin/mnld': blob_fixup()
        .replace_needed('libmnl.so', 'libmnl_mtk.so')
        .replace_needed('android.hardware.sensors-V2-ndk.so', 'android.hardware.sensors-V3-ndk.so'),
    ('vendor/lib64/mt6789/libcam.utils.sensorprovider.so',
     'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.pq_aidl-impl.so'): blob_fixup()
        .replace_needed('android.hardware.sensors-V2-ndk.so', 'android.hardware.sensors-V3-ndk.so'),
    ('vendor/bin/hw/mt6789/android.hardware.graphics.allocator-V2-service-mediatek.mt6789',
     'vendor/lib64/hw/hwcomposer.mtk_common.so',
     'vendor/lib64/hw/mt6789/android.hardware.graphics.allocator-V2-mediatek.so',
     'vendor/lib64/hw/mt6789/mapper.mediatek.so',
     'vendor/lib64/libaimemc.so',
     'vendor/lib64/libcodec2_fsr.so',
     'vendor/lib64/libcodec2_vpp_AIMEMC_plugin.so',
     'vendor/lib64/libcodec2_vpp_AISR_plugin.so',
     'vendor/lib64/libgpud.so',
     'vendor/lib64/vendor.mediatek.hardware.camera.isphal-V1-ndk.so',
     'vendor/lib64/vendor.mediatek.hardware.pq_aidl-V8-ndk.so'): blob_fixup()
        .replace_needed('android.hardware.graphics.common-V6-ndk.so', 'android.hardware.graphics.common-V7-ndk.so'),
    'vendor/lib64/mt6789/libmtkcam_grallocutils.so': blob_fixup()
        .replace_needed('android.hardware.graphics.common-V5-ndk.so', 'android.hardware.graphics.common-V7-ndk.so'),
    ('vendor/bin/hw/android.hardware.audio.service-aidl.mediatek',
     'vendor/lib64/android.hardware.audio.core-impl-mediatek.so'): blob_fixup()
        .replace_needed('android.hardware.audio.core-V3-ndk.so', 'android.hardware.audio.core-V4-ndk.so')
        .replace_needed('android.hardware.audio.core.sounddose-V3-ndk.so', 'android.hardware.audio.core.sounddose-V4-ndk.so')
        .replace_needed('android.hardware.bluetooth.audio-V5-ndk.so', 'android.hardware.bluetooth.audio-V6-ndk.so')
        .replace_needed('android.hardware.soundtrigger3-V3-ndk.so', 'android.hardware.soundtrigger3-V4-ndk.so')
        .replace_needed('android.media.audio.common.types-V4-ndk.so', 'android.media.audio.common.types-V5-ndk.so'),
    'vendor/lib64/hw/android.hardware.audio.effect.aidl-impl-mediatek.so': blob_fixup()
        .replace_needed('android.hardware.audio.effect-V3-ndk.so', 'android.hardware.audio.effect-V4-ndk.so')
        .replace_needed('android.media.audio.common.types-V4-ndk.so', 'android.media.audio.common.types-V5-ndk.so'),
    'vendor/lib64/hw/android.hardware.soundtrigger3-impl.so': blob_fixup()
        .replace_needed('android.hardware.soundtrigger3-V3-ndk.so', 'android.hardware.soundtrigger3-V4-ndk.so'),
    'vendor/lib64/hw/audio.primary.mediatek.so': blob_fixup()
        .replace_needed('android.hardware.audio.effect-V2-ndk.so', 'android.hardware.audio.effect-V4-ndk.so')
        .replace_needed('android.hardware.bluetooth.audio-V4-ndk.so', 'android.hardware.bluetooth.audio-V6-ndk.so')
        .replace_needed('vendor.mediatek.hardware.bluetooth.audio-V1-ndk.so', 'vendor.mediatek.hardware.bluetooth.audio-V5-ndk.so'),
    ('vendor/lib64/hw/vendor.mediatek.hardware.bluetooth.audio@2.1-impl.so',
     'vendor/lib64/hw/vendor.mediatek.hardware.bluetooth.audio@2.2-impl.so',
     'vendor/lib64/libbluetooth_audio_session_aidl_mtk.so',
     'vendor/lib64/android.hardware.bluetooth.audio-impl-mediatek.so'): blob_fixup()
        .replace_needed('android.hardware.bluetooth.audio-V5-ndk.so', 'android.hardware.bluetooth.audio-V6-ndk.so')
        .replace_needed('vendor.mediatek.hardware.bluetooth.audio-V1-ndk.so', 'vendor.mediatek.hardware.bluetooth.audio-V5-ndk.so'),
    'vendor/lib64/libpowerhal.so': blob_fixup()
        .replace_needed('android.hardware.bluetooth.audio-V4-ndk.so', 'android.hardware.bluetooth.audio-V6-ndk.so'),
    ('vendor/lib64/libaudioprimarydevicehalifclient.so',
     'vendor/lib64/libnotifyaudiohal.so'): blob_fixup()
        .replace_needed('android.hardware.audio.core-V3-ndk.so', 'android.hardware.audio.core-V4-ndk.so'),
    ('vendor/lib64/libcodec2_mtk_vdec.so', 'vendor/lib64/libcodec2_mtk_venc.so'): blob_fixup()
        .replace_needed('libformatter.so', 'libformatter_mtk.so'),

}  # fmt: skip

module = ExtractUtilsModule(
    'CM6',
    'transsion/tecno',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
