# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

LOCAL_PATH := device/transsion/tecno/CM6

# Enable updating of APEXes
$(call inherit-product, $(SRC_TARGET_DIR)/product/updatable_apex.mk)

# A/B
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/launch_with_vendor_ramdisk.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/virtual_ab_ota/vabc_features.mk)

# Enforce generic ramdisk allow list
$(call inherit-product, $(SRC_TARGET_DIR)/product/generic_ramdisk.mk)

# A/B
PRODUCT_PACKAGES += \
    com.android.hardware.boot \
    android.hardware.boot-service.default_recovery

PRODUCT_PACKAGES += \
    android.hardware.boot-V1-ndk \
    android.hardware.boot@1.0 \
    android.hardware.boot@1.1

PRODUCT_PACKAGES += \
    update_engine \
    update_engine_sideload \
    update_verifier

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_system=true \
    POSTINSTALL_PATH_system=system/bin/otapreopt_script \
    FILESYSTEM_TYPE_system=$(BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE) \
    POSTINSTALL_OPTIONAL_system=true

AB_OTA_POSTINSTALL_CONFIG += \
    RUN_POSTINSTALL_vendor=true \
    POSTINSTALL_PATH_vendor=bin/checkpoint_gc \
    FILESYSTEM_TYPE_vendor=$(BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE) \
    POSTINSTALL_OPTIONAL_vendor=true

PRODUCT_PACKAGES += \
    checkpoint_gc \
    otapreopt_script

# Aconfig
PRODUCT_PACKAGES += \
    libaconfig_storage_read_api_cc

# Audio
PRODUCT_PACKAGES += \
    android.hardware.audio.common-util \
    android.hardware.audio.common@5.0 \
    android.hardware.audio.common@6.0 \
    android.hardware.audio.common@6.0-util \
    android.hardware.audio.common@7.0 \
    android.hardware.audio.effect@6.0 \
    android.hardware.audio.effect@7.0 \
    android.hardware.audio@6.0 \
    android.hardware.audio@7.0 \
    libaudio_aidl_conversion_common_ndk \
    libaudioaidlcommon \
    libaudioutils \
    vendor.mediatek.hardware.audio-V1-ndk

# Some MTK prebuilts are only 4K-page aligned
PRODUCT_CHECK_PREBUILT_MAX_PAGE_SIZE := false

# Audio effects config (ODM)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/audio/audio_effects_config.xml:$(TARGET_COPY_OUT_ODM)/etc/vconfig/audio/audio_effects_config.xml

# Audio Alsa
PRODUCT_PACKAGES += \
    libalsautilsv2 \
    libtinyalsa

# API levels
BOARD_SHIPPING_API_LEVEL := 31
PRODUCT_SHIPPING_API_LEVEL := 36

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth-service.mediatek \
    android.hardware.bluetooth.audio@2.0 \
    android.hardware.bluetooth.audio@2.1 \
    libbluetooth_audio_session_aidl \
    vendor.mediatek.hardware.bluetooth.audio-V5-ndk \
    vendor.mediatek.hardware.bluetooth.audio@2.1 \
    vendor.mediatek.hardware.bluetooth.audio@2.2

# Camera
PRODUCT_PACKAGES += \
    android.frameworks.cameraservice.common-V1-ndk \
    android.frameworks.cameraservice.device-V3-ndk \
    android.frameworks.cameraservice.service-V3-ndk \
    android.hardware.camera.common-V1-ndk \
    android.hardware.camera.common@1.0 \
    android.hardware.camera.device-V3-ndk \
    android.hardware.camera.device@1.0 \
    android.hardware.camera.device@3.2 \
    android.hardware.camera.device@3.3 \
    android.hardware.camera.device@3.4 \
    android.hardware.camera.device@3.5 \
    android.hardware.camera.device@3.6 \
    android.hardware.camera.metadata-V3-ndk \
    android.hardware.camera.provider-V3-ndk \
    android.hardware.camera.provider@2.4 \
    android.hardware.camera.provider@2.5 \
    android.hardware.camera.provider@2.6 \
    libcamera_metadata

# Chipinfo
PRODUCT_PACKAGES += \
    chipinfo

# Common
PRODUCT_PACKAGES += \
    android.hardware.common-V2-ndk \
    android.hardware.common.fmq-V1-ndk

# Contexthub
PRODUCT_PACKAGES += \
    android.hardware.contexthub-service.tinysys \
    chre_atoms_log \
    chremetrics-cpp

# CPU
PRODUCT_PACKAGES += \
    libRSCpuRef

# DeviceConfig
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/device-config-defaults:$(TARGET_COPY_OUT_SYSTEM)/etc/device-config-defaults

# Display | SurfaceFlinger
SOONG_CONFIG_NAMESPACES += surfaceflinger
SOONG_CONFIG_surfaceflinger += frame_rate_category_high
SOONG_CONFIG_surfaceflinger_frame_rate_category_high := 120

# DRM
PRODUCT_PACKAGES += \
    android.hardware.drm-V1-ndk \
    android.hardware.drm.common-V1-ndk \
    libdrm

# External
PRODUCT_PACKAGES += \
    libblas \
    libcap \
    libcompiler_rt \
    libcrypto \
    libcurl \
    libexif \
    libexpat \
    libflatbuffers-cpp \
    libimage_io \
    libjpeg \
    libjpegdecoder \
    libjpegencoder \
    libjsoncpp \
    liblzma \
    libminijail \
    libmnl \
    libnl \
    libpcap \
    libpng \
    libruy \
    libspeexresampler \
    libsqlite \
    libssl \
    libtextclassifier_hash \
    libtinyalsav2 \
    libtinyxml2 \
    libtinyxml2-v34 \
    libultrahdr \
    libxml2 \
    libz

# Fastbootd
PRODUCT_PACKAGES += \
    android.hardware.fastboot-service.example_recovery \
    fastbootd

# Fingerprint - Biometrics
PRODUCT_PACKAGES += \
    android.hardware.biometrics.common-V3-ndk \
    android.hardware.biometrics.common.thread \
    android.hardware.biometrics.fingerprint-V3-ndk \
    android.hardware.biometrics.fingerprint-service.jiiov \
    vendor.anc.fingerprint.extension-V1-ndk \
    anc_hal

# GNS
PRODUCT_PACKAGES += \
    android.hardware.gnss-V5-ndk

# Graphics
PRODUCT_PACKAGES += \
    android.hardware.graphics.allocator-V2-ndk \
    android.hardware.graphics.allocator@2.0 \
    android.hardware.graphics.allocator@3.0 \
    android.hardware.graphics.allocator@4.0 \
    android.hardware.graphics.bufferqueue@1.0 \
    android.hardware.graphics.bufferqueue@2.0 \
    android.hardware.graphics.common-V5-ndk \
    android.hardware.graphics.common-V6-ndk \
    android.hardware.graphics.common@1.0 \
    android.hardware.graphics.common@1.1 \
    android.hardware.graphics.common@1.2 \
    android.hardware.graphics.composer3-V2-ndk \
    android.hardware.graphics.composer3-V3-ndk \
    android.hardware.graphics.composer3-V4-ndk \
    android.hardware.graphics.mapper@2.0 \
    android.hardware.graphics.mapper@2.1 \
    android.hardware.graphics.mapper@3.0 \
    android.hardware.graphics.mapper@4.0 \
    libgralloctypes \
    libgrallocusage \
    android.hardware.memtrack-service.mediatek

# Hardware
PRODUCT_PACKAGES += \
    android.system.suspend-V1-ndk \
    libbinderdebug \
    libhardware \
    libhardware_legacy \
    libhwbinder

# Health
PRODUCT_PACKAGES += \
    android.hardware.health-service.cm6 \
    android.hardware.health-service.example_recovery \
    android.hardware.health-V1-ndk \
    android.hardware.health-V4-ndk

# Infrared
PRODUCT_PACKAGES += \
    android.hardware.ir-service.cm6 \
    android.hardware.ir-V1-ndk

# Kernel
PRODUCT_ENABLE_UFFD_GC := true

# Keylayout
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/keylayout/mtk-tpd.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/mtk-tpd.kl \
    $(LOCAL_PATH)/configs/keylayout/mtk-pmic-keys.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/mtk-pmic-keys.kl

# Lights
PRODUCT_PACKAGES += \
    android.hardware.light-service.cm6 \
    android.hardware.light-V1-ndk \
    android.hardware.light-V2-ndk

#  Media
PRODUCT_PACKAGES += \
    android.hardware.media.bufferpool@2.0 \
    android.hardware.media.bufferpool2-V2-ndk \
    android.hardware.media.c2-V1-ndk \
    android.media.audio.common.types-V2-ndk \
    android.media.audio.common.types-V3-ndk \
    android.media.audio.common.types-V4-ndk \
    android.media.audio.eraser.types-V1-ndk \
    android.media.soundtrigger.types-V1-ndk \
    android.media.soundtrigger.types-V3-ndk \
    libaudio_aidl_conversion_common_ndk \
    libavservices_minijail \
    libcodec2 \
    libcodec2_aidl \
    libcodec2_hal_common \
    libcodec2_hidl@1.0 \
    libcodec2_hidl@1.1 \
    libcodec2_hidl@1.2 \
    libcodec2_soft_common \
    libcodec2_vndk \
    libmedia_helper \
    libmediautils_vendor \
    libsfplugin_ccodec_utils \
    libstagefright_aidl_bufferpool2 \
    libstagefright_bufferpool@2.0.1 \
    libstagefright_bufferqueue_helper \
    libstagefright_foundation

# Media - hidltransport
PRODUCT_PACKAGES += \
    android.hardware.drm-service.clearkey \
    android.hardware.media@1.0 \
    android.hidl.memory.token@1.0 \
    android.hidl.memory@1.0 \
    android.hidl.safe_union@1.0 \
    android.hidl.token@1.0 \
    android.hidl.token@1.0-utils \
    libhidlbase \
    libhidlmemory \
    libhidltransport

# Media - C2
PRODUCT_PACKAGES += \
    android.hardware.media.c2@1.0 \
    android.hardware.media.c2@1.1 \
    android.hardware.media.c2@1.2

# Media - OMX
PRODUCT_PACKAGES += \
    android.hardware.media.omx@1.0

# Memtrack
PRODUCT_PACKAGES += \
    android.hardware.memtrack-V1-ndk

# Memory
PRODUCT_PACKAGES += \
    dmabuf_dump \
    libdmabufheap \
    libion \
    libmeminfo \
    libmemunreachable

# Native
PRODUCT_PACKAGES += \
    libdumpstateutil

# NFC
PRODUCT_PACKAGES += \
    android.hardware.nfc-V1-ndk \
    android.hardware.nfc-V2-ndk \
    android.hardware.nfc@1.0 \
    vendor.nxp.nxpnfc_aidl-V1-ndk

# Overlays
PRODUCT_ENFORCE_RRO_TARGETS := *
PRODUCT_PACKAGES += \
    FrameworksResTarget \
    NetworkStackResTarget \
    SettingsResTarget \
    SystemUIResTarget \
    TetheringResTarget \
    WifiResTarget

# Partitions
PRODUCT_USE_DYNAMIC_PARTITIONS := true

# Power
PRODUCT_PACKAGES += \
    vendor.mediatek.hardware.mtkpower@1.0 \
    vendor.mediatek.hardware.mtkpower@1.1 \
    vendor.mediatek.hardware.mtkpower@1.2

PRODUCT_PACKAGES += \
    vendor.mediatek.hardware.mtkpower-V1-ndk \
    vendor.mediatek.hardware.mtkpower-V2-ndk

PRODUCT_PACKAGES += \
    android.hardware.power-service.pixel-libperfmgr

# Power configurations
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/power/powerhint.json:$(TARGET_COPY_OUT_VENDOR)/etc/powerhint.json

# Thermal
PRODUCT_PACKAGES += \
    android.hardware.thermal-service.pixel \
    thermal_symlinks

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/thermal/thermal_info_config.json:$(TARGET_COPY_OUT_VENDOR)/etc/thermal_info_config.json

# Permissions (features identical to AOSP source)
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.audio.low_latency.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.audio.low_latency.xml \
    frameworks/native/data/etc/android.hardware.biometrics.face.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.biometrics.face.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml \
    frameworks/native/data/etc/android.hardware.faketouch.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.faketouch.xml \
    frameworks/native/data/etc/android.hardware.location.gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.location.gps.xml \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml \
    frameworks/native/data/etc/android.hardware.telephony.ims.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.ims.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.distinct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.distinct.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.hardware.vulkan.compute-0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.compute-0.xml \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level-1.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_3.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version-1_3.xml \
    frameworks/native/data/etc/android.hardware.wifi.direct.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.direct.xml \
    frameworks/native/data/etc/android.hardware.wifi.passpoint.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.passpoint.xml \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    frameworks/native/data/etc/android.software.ipsec_tunnels.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.ipsec_tunnels.xml \
    frameworks/native/data/etc/android.software.midi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.midi.xml \
    frameworks/native/data/etc/android.software.opengles.deqp.level-2025-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.opengles.deqp.level-2025-03-01.xml \
    frameworks/native/data/etc/android.software.verified_boot.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.verified_boot.xml \
    frameworks/native/data/etc/android.software.vulkan.deqp.level-2025-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.vulkan.deqp.level-2025-03-01.xml

# Permissions provided by existing AOSP soong modules
PRODUCT_PACKAGES += \
    android.hardware.hardware_keystore_V3.xml \
    android.hardware.se.omapi.uicc.prebuilt.xml \
    android.hardware.sensor.accelerometer.prebuilt.xml \
    android.hardware.sensor.compass.prebuilt.xml \
    android.hardware.sensor.gyroscope.prebuilt.xml \
    android.hardware.sensor.light.prebuilt.xml \
    android.hardware.sensor.proximity.prebuilt.xml \
    android.hardware.sensor.stepcounter.prebuilt.xml \
    android.hardware.sensor.stepdetector.prebuilt.xml \
    features_com.android.virt.xml

# Permissions (odm, features identical to AOSP source)
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.consumerir.xml:$(TARGET_COPY_OUT_ODM)/etc/permissions/android.hardware.consumerir.xml \
    frameworks/native/data/etc/android.hardware.fingerprint.xml:$(TARGET_COPY_OUT_ODM)/etc/permissions/android.hardware.fingerprint.xml \
    frameworks/native/data/etc/android.hardware.nfc.xml:$(TARGET_COPY_OUT_ODM)/etc/permissions/android.hardware.nfc.xml \
    frameworks/native/data/etc/android.hardware.nfc.hce.xml:$(TARGET_COPY_OUT_ODM)/etc/permissions/android.hardware.nfc.hce.xml \
    frameworks/native/data/etc/com.android.nfc_extras.xml:$(TARGET_COPY_OUT_ODM)/etc/permissions/com.android.nfc_extras.xml \
    frameworks/native/data/etc/com.nxp.mifare.xml:$(TARGET_COPY_OUT_ODM)/etc/permissions/com.nxp.mifare.xml

# Permissions (system_ext)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/permissions/com.transsion.tranfacfwk.permission.xml:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/permissions/com.transsion.tranfacfwk.permission.xml

# Permissions (vendor-specific / divergent from AOSP source)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/permissions/android.hardware.camera.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.camera.xml \
    $(LOCAL_PATH)/configs/permissions/android.hardware.keystore.app_attest_key.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.keystore.app_attest_key.xml \
    $(LOCAL_PATH)/configs/permissions/android.hardware.microphone.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.microphone.xml \
    $(LOCAL_PATH)/configs/permissions/com.mediatek.hardware.vow.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.mediatek.hardware.vow.xml \
    $(LOCAL_PATH)/configs/permissions/com.mediatek.hardware.vow_dsp.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.mediatek.hardware.vow_dsp.xml \
    $(LOCAL_PATH)/configs/permissions/com.transsion.tranfacsdk.permission.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/com.transsion.tranfacsdk.permission.xml

# Permissions (product)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/permissions/tran_camera_extensions.xml:$(TARGET_COPY_OUT_PRODUCT)/etc/permissions/tran_camera_extensions.xml

# Product characteristics
PRODUCT_CHARACTERISTICS := default

# Radio
PRODUCT_PACKAGES += \
    android.hardware.radio-V4-ndk \
    android.hardware.radio.data-V4-ndk \
    android.hardware.radio.messaging-V4-ndk \
    android.hardware.radio.modem-V4-ndk \
    android.hardware.radio.network-V4-ndk \
    android.hardware.radio.sap-V1-ndk \
    android.hardware.radio.sim-V4-ndk \
    android.hardware.radio.voice-V4-ndk \
    android.hardware.radio@1.0 \
    android.hardware.radio@1.1 \
    android.hardware.radio@1.5 \
    android.hardware.radio@1.6

# Radio - IMS
PRODUCT_PACKAGES += \
    android.hardware.radio.ims-V3-ndk

# Radio - Configs
PRODUCT_PACKAGES += \
    android.hardware.radio.config-V4-ndk \
    android.hardware.radio.config@1.1 \
    android.hardware.radio.config@1.2 \
    android.hardware.radio.config@1.3

# RIL
ENABLE_VENDOR_RIL_SERVICE := true

# Renderscript
PRODUCT_PACKAGES += \
    android.hardware.renderscript@1.0

# Rootdir
PRODUCT_PACKAGES += \
    init.insmod.sh

PRODUCT_PACKAGES += \
    fstab.mt6789 \
    fstab.mt6789.vendor_ramdisk \
    init.cgroup.rc \
    init.connectivity.common.rc \
    init.connectivity.rc \
    init.modem.rc \
    init.mt6789.rc \
    init.mt6789.aosp.rc \
    init.mt6789.usb.rc \
    init.mtkgki.rc \
    init.project.rc \
    init.sensor_2_0.rc \
    init_connectivity.rc \
    init.recovery.mt6789.rc \

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/etc/fstab.mt6789:$(TARGET_VENDOR_RAMDISK_OUT)/first_stage_ramdisk/fstab.mt6789

# Rootdir | use stock rc
$(call soong_config_set_bool,mediatek_gadget,use_custom_usb_gadget_rc,true)

# RS
PRODUCT_PACKAGES += \
    libRSDriver \
    libRS_internal

# Secure Element
PRODUCT_PACKAGES += \
    android.hardware.secure_element-V1-ndk \
    vendor.nxp.nxpese@1.0

# Shims
PRODUCT_PACKAGES += \
    libaedv \
    libtranlog \
    libTran_Log

# Sensors
PRODUCT_PACKAGES += \
    android.hardware.sensors-service.multihal

PRODUCT_PACKAGES += \
    android.frameworks.sensorservice-V1-ndk \
    android.frameworks.sensorservice@1.0 \
    android.hardware.sensors-V2-ndk \
    android.hardware.sensors-V3-ndk \
    android.hardware.sensors@1.0 \
    android.hardware.sensors@2.0 \
    android.hardware.sensors@2.0-ScopedWakelock \
    android.hardware.sensors@2.1

# Security
PRODUCT_PACKAGES += \
    android.hardware.security.keymint-V1-ndk \
    android.hardware.security.keymint-V3-ndk \
    android.hardware.security.keymint-V4-ndk \
    android.hardware.security.rkp-V1-ndk \
    android.hardware.security.secureclock-V1-ndk \
    android.hardware.security.sharedsecret-V1-ndk

# Security - GateKeeper
PRODUCT_PACKAGES += \
    android.hardware.gatekeeper-V1-ndk

# Security - Keymaster
PRODUCT_PACKAGES += \
    android.hardware.keymaster-V4-ndk

# Security - Keystore
PRODUCT_PACKAGES += \
    android.system.keystore2-V1-ndk

# Soundtrigger
PRODUCT_PACKAGES += \
    android.hardware.soundtrigger3-V1-ndk \
    android.hardware.soundtrigger3-V3-ndk

# Stats
PRODUCT_PACKAGES += \
    android.frameworks.stats-V1-ndk \
    android.frameworks.stats-V2-ndk

# System
PRODUCT_PACKAGES += \
    libbase \
    libbcinfo \
    libfmq \
    libnetutils \
    libprocessgroup \
    libprocinfo \
    libsysutils \
    libtrusty \
    libunwindstack \
    libutils \
    libutilscallstack \
    server_configurable_flags

# UI
PRODUCT_PACKAGES += \
    libui

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb-V3-ndk \
    android.hardware.usb.gadget-V1-ndk

# Vibrator
PRODUCT_PACKAGES += \
    android.hardware.vibrator-service.cm6 \
    android.hardware.vibrator-V2-ndk \
    libvibratorutils

# Wi-Fi (source stack; MTK libwifi-hal and vendor tools kept)
PRODUCT_PACKAGES += \
    android.hardware.wifi-service \
    hostapd \
    wpa_supplicant \
    wlan_assistant \
    libkeystore-engine-wifi-hidl \
    libkeystore-wifi-hidl

PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/wifi/wifi_hal.xml:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/vendor_hals/wifi_hal.xml

# Permissions provided by AOSP Soong modules
PRODUCT_PACKAGES += \
    android.hardware.hardware_keystore_V3.xml \
    android.hardware.se.omapi.uicc.prebuilt.xml \
    android.hardware.sensor.accelerometer.prebuilt.xml \
    android.hardware.sensor.compass.prebuilt.xml \
    android.hardware.sensor.gyroscope.prebuilt.xml \
    android.hardware.sensor.light.prebuilt.xml \
    android.hardware.sensor.proximity.prebuilt.xml \
    android.hardware.sensor.stepcounter.prebuilt.xml \
    android.hardware.sensor.stepdetector.prebuilt.xml \
    features_com.android.virt.xml

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH) \
    hardware/google/interfaces \
    hardware/google/pixel \
    hardware/google/pixel/pixelstats \
    hardware/google/pixel/power-libperfmgr \
    hardware/google/pixel/thermal \
    hardware/mediatek

# Inherit the proprietary files
$(call inherit-product, vendor/transsion/tecno/CM6/CM6-vendor.mk)
