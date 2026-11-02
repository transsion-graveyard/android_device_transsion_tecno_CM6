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


CLEAR_AHARDWAREBUFFER_SYMBOLS = (
    blob_fixup()
    .clear_symbol_version('AHardwareBuffer_allocate')
    .clear_symbol_version('AHardwareBuffer_createFromHandle')
    .clear_symbol_version('AHardwareBuffer_describe')
    .clear_symbol_version('AHardwareBuffer_getNativeHandle')
    .clear_symbol_version('AHardwareBuffer_lock')
    .clear_symbol_version('AHardwareBuffer_lockPlanes')
    .clear_symbol_version('AHardwareBuffer_release')
    .clear_symbol_version('AHardwareBuffer_unlock')
)


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
        .replace_needed('av-audio-types-aidl-ndk.so', 'av-audio-types-aidl-ndk_prebuilt.so'),
    'vendor/lib64/mt6789/libmtkcam_hal_aidl_common.so': blob_fixup()
        .replace_needed('android.hardware.camera.common-V2-ndk.so', 'android.hardware.camera.common-V1-ndk.so'),
    'vendor/bin/mnld': blob_fixup()
        .replace_needed('libmnl.so', 'libmnl_mtk.so')
        .replace_needed('android.hardware.sensors-V2-ndk.so', 'android.hardware.sensors-V3-ndk.so'),
    'vendor/lib64/mt6789/libcam.utils.sensorprovider.so': blob_fixup()
        .replace_needed('android.hardware.sensors-V2-ndk.so', 'android.hardware.sensors-V3-ndk.so'),
    'vendor/lib64/hw/mt6789/vendor.mediatek.hardware.pq_aidl-impl.so': blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so')
        .replace_needed('android.hardware.sensors-V2-ndk.so', 'android.hardware.sensors-V3-ndk.so'),
    ('vendor/bin/hw/mt6789/android.hardware.graphics.allocator-V2-service-mediatek.mt6789',
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
    'vendor/lib64/hw/hwcomposer.mtk_common.so': blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so')
        .replace_needed('android.hardware.graphics.common-V6-ndk.so', 'android.hardware.graphics.common-V7-ndk.so'),
    'vendor/lib64/mt6789/libmtkcam_grallocutils.so': blob_fixup()
        .replace_needed('android.hardware.graphics.common-V5-ndk.so', 'android.hardware.graphics.common-V7-ndk.so'),
    'vendor/bin/hw/android.hardware.audio.service-aidl.mediatek': blob_fixup()
        .replace_needed('libaudio_aidl_conversion_common_ndk.so', 'libaudio_aidl_conversion_common_ndk_prebuilt.so'),
    'vendor/lib64/android.hardware.audio.core-impl-mediatek.so': blob_fixup()
        .add_needed('libaudioutils_shim.so')
        .replace_needed('libaudio_aidl_conversion_common_ndk.so', 'libaudio_aidl_conversion_common_ndk_prebuilt.so'),
    ('vendor/lib64/hw/android.hardware.audio.effect.aidl-impl-mediatek.so',
     'vendor/lib64/libsilkybrightnesscore.so',
     'vendor/lib64/libmtkgpuserv.so',
     'vendor/lib64/librt_extamp_intf.so',
     'vendor/lib64/libpqxmlparser.so',
     'vendor/lib64/libpqxmlflagparser.so'): blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so')
        .replace_needed('libaudio_aidl_conversion_common_ndk.so', 'libaudio_aidl_conversion_common_ndk_prebuilt.so'),
    'vendor/lib64/hw/android.hardware.soundtrigger3-impl.so': blob_fixup()
        .replace_needed('libaudio_aidl_conversion_common_ndk.so', 'libaudio_aidl_conversion_common_ndk_prebuilt.so'),
    'vendor/lib64/android.hardware.audio.effect-V2-ndk_prebuilt.so': blob_fixup()
        .replace_needed('android.hardware.audio.common-V3-ndk.so', 'android.hardware.audio.common-V3-ndk_prebuilt.so')
        .replace_needed('android.media.audio.common.types-V3-ndk.so', 'android.media.audio.common.types-V3-ndk_prebuilt.so'),
    'vendor/lib64/android.hardware.bluetooth.audio-V4-ndk_prebuilt.so': blob_fixup()
        .replace_needed('android.hardware.audio.common-V3-ndk.so', 'android.hardware.audio.common-V3-ndk_prebuilt.so'),
    'vendor/lib64/android.hardware.audio.common-V3-ndk_prebuilt.so': blob_fixup()
        .replace_needed('android.media.audio.common.types-V3-ndk.so', 'android.media.audio.common.types-V3-ndk_prebuilt.so'),
    'vendor/lib64/hw/audio.primary.mediatek.so': blob_fixup()
        .replace_needed('libtinyxml2.so', 'libtinyxml2-v34.so')
        .replace_needed('android.hardware.audio.effect-V2-ndk.so', 'android.hardware.audio.effect-V2-ndk_prebuilt.so')
        .replace_needed('android.hardware.bluetooth.audio-V4-ndk.so', 'android.hardware.bluetooth.audio-V4-ndk_prebuilt.so')
        .replace_needed('vendor.mediatek.hardware.bluetooth.audio-V1-ndk.so', 'vendor.mediatek.hardware.bluetooth.audio-V1-ndk_prebuilt.so'),
    ('vendor/lib64/hw/vendor.mediatek.hardware.bluetooth.audio@2.1-impl.so',
     'vendor/lib64/hw/vendor.mediatek.hardware.bluetooth.audio@2.2-impl.so',
     'vendor/lib64/libbluetooth_audio_session_aidl_mtk.so',
     'vendor/lib64/android.hardware.bluetooth.audio-impl-mediatek.so'): blob_fixup()
        .replace_needed('vendor.mediatek.hardware.bluetooth.audio-V1-ndk.so', 'vendor.mediatek.hardware.bluetooth.audio-V1-ndk_prebuilt.so'),
    'vendor/lib64/libbluetooth_audio_session_prebuilt.so': blob_fixup()
        .replace_needed('libbluetooth_audio_session_aidl.so', 'libbluetooth_audio_session_aidl_prebuilt.so'),
    'vendor/lib64/hw/android.hardware.bluetooth.audio@2.0-impl.so': blob_fixup()
        .replace_needed('libbluetooth_audio_session.so', 'libbluetooth_audio_session_prebuilt.so'),
    'vendor/lib64/hw/android.hardware.bluetooth.audio@2.1-impl.so': blob_fixup()
        .replace_needed('libbluetooth_audio_session.so', 'libbluetooth_audio_session_prebuilt.so'),
    'vendor/lib64/libpowerhal.so': blob_fixup()
        .replace_needed('android.hardware.bluetooth.audio-V4-ndk.so', 'android.hardware.bluetooth.audio-V6-ndk.so'),
    'vendor/lib64/libnotifyaudiohal.so': blob_fixup()
        .fix_soname(),
    ('vendor/lib64/hw/sensors.elliptic.so',
     'vendor/lib64/libspeech_enh_lib.so',
     'vendor/lib64/libnir_neon_driver_ndk.mtk.vndk.so',
     'odm/lib64/mt6789/libFy-3A-Param.so'): blob_fixup()
        .fix_soname(),
    ('odm/lib64/libeffect_hal.so',
     'odm/lib64/libanc_single_rt_bokeh.so',
     'odm/lib64/libVideoSpotEngine_gl.so',
     'odm/lib64/libtranssion_bodybeauty.so',
     'vendor/lib64/libmcve.so',
     'vendor/lib64/mt6789/libneuron_adapter_mgvi.so',
     'vendor/lib64/libneuralnetworks_sl_driver_mtk_prebuilt.so',
     'vendor/lib64/libtflite_mtk.so'): CLEAR_AHARDWAREBUFFER_SYMBOLS,
    'odm/lib64/libTranssionTone.so': blob_fixup()
        .add_needed('libTranColorEffectEngine.so'),
    'odm/lib64/libsegmention.so': blob_fixup()
        .add_needed('libtflite_native_gainference.so'),
    'vendor/lib64/mt6789/libmtkcam_hwnode.so': blob_fixup()
        .add_needed('libultrahdr_CM6.so'),
    ('vendor/lib64/libcodec2_mtk_vdec.so', 'vendor/lib64/libcodec2_mtk_venc.so'): blob_fixup()
        .replace_needed('libformatter.so', 'libformatter_mtk.so'),
    'vendor/etc/init/nfc2-service-nxp_w.rc': blob_fixup()
        .regex_replace(r'(?m)^([ \t]*)#interface aidl', r'\1interface aidl'),

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
