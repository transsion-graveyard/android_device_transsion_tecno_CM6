# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

DEVICE_PATH := device/transsion/tecno/CM6
CONFIGS_PATH := $(DEVICE_PATH)/configs

# A/B
AB_OTA_UPDATER := true
AB_OTA_PARTITIONS += \
    boot \
    system \
    system_dlkm \
    system_ext \
    product \
    vendor \
    vendor_dlkm \
    odm \
    odm_dlkm \
    vbmeta_system \
    vbmeta_vendor

BOARD_USES_RECOVERY_AS_BOOT := true

# Architecture
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 :=
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := cortex-a55
# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := TECNO-CM6
TARGET_NO_BOOTLOADER := true

# Display
TARGET_SCREEN_DENSITY := 480
TARGET_USES_VULKAN := true

# Kernel
BOARD_BOOT_HEADER_VERSION := 4
BOARD_KERNEL_BASE := 0x3fff8000
BOARD_KERNEL_CMDLINE := bootopt=64S3,32N2,64N2 bootconfig
BOARD_KERNEL_PAGESIZE := 4096
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOT_HEADER_VERSION)
BOARD_KERNEL_IMAGE_NAME := Image
BOARD_INCLUDE_DTB_IN_BOOTIMG := true
BOARD_KERNEL_SEPARATED_DTBO := true
TARGET_KERNEL_CONFIG := CM6_defconfig
TARGET_KERNEL_SOURCE := kernel/transsion/tecno/CM6

# Kernel - prebuilt
TARGET_FORCE_PREBUILT_KERNEL := true
ifeq ($(TARGET_FORCE_PREBUILT_KERNEL),true)
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilts/kernel
TARGET_PREBUILT_DTB := $(DEVICE_PATH)/prebuilts/dtb.img
BOARD_MKBOOTIMG_ARGS += --dtb $(TARGET_PREBUILT_DTB)
BOARD_INCLUDE_DTB_IN_BOOTIMG :=
BOARD_PREBUILT_DTBOIMAGE := $(DEVICE_PATH)/prebuilts/dtbo.img
BOARD_KERNEL_SEPARATED_DTBO :=
endif

# Partitions
BOARD_FLASH_BLOCK_SIZE := 262144 # (BOARD_KERNEL_PAGESIZE * 64)
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_VENDOR_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_DTBOIMG_PARTITION_SIZE := 8388608
BOARD_SUPER_PARTITION_SIZE := 13335613440
# partition-size:super: 31add5000 (from liblp metadata)
BOARD_SUPER_PARTITION_GROUPS := mtk_dynamic_partitions
BOARD_MTK_DYNAMIC_PARTITIONS_PARTITION_LIST := $(ALL_PARTITIONS)
# $(ALL_PARTITIONS) currently expands to: odm odm_dlkm product system system_dlkm system_ext tr_carrier tr_company tr_manifest tr_misc tr_overlayfs tr_preload tr_product tr_region vendor vendor_dlkm
BOARD_MTK_DYNAMIC_PARTITIONS_SIZE := 13331419136
# 13335613440 - 4194304 (4 MiB) = 13331419136

# Platform
TARGET_BOARD_PLATFORM := mt6789

# Properties
PROPS_PATH := $(CONFIGS_PATH)/properties
TARGET_SYSTEM_PROP += $(PROPS_PATH)/system.prop
TARGET_SYSTEM_EXT_PROP += $(PROPS_PATH)/system_ext.prop
TARGET_VENDOR_PROP += $(PROPS_PATH)/vendor.prop
TARGET_ODM_PROP += $(PROPS_PATH)/odm.prop
TARGET_PRODUCT_PROP += $(PROPS_PATH)/product.prop
TARGET_VENDOR_DLKM_PROP += $(PROPS_PATH)/vendor_dlkm.prop
TARGET_ODM_DLKM_PROP += $(PROPS_PATH)/odm_dlkm.prop
TARGET_SYSTEM_DLKM_PROP += $(PROPS_PATH)/system_dlkm.prop

# VINTF
VINTF_PATH := $(CONFIGS_PATH)/vintf
DEVICE_MANIFEST_FILE := $(VINTF_PATH)/manifest.xml
DEVICE_MATRIX_FILE := $(VINTF_PATH)/compatibility_matrix.xml
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE := $(VINTF_PATH)/compatibility_matrix.device.xml

# Recovery
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.mt6789
TARGET_RECOVERY_PIXEL_FORMAT := BGRA_8888
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# Security patch level
VENDOR_SECURITY_PATCH := 2026-09-05

# Verified Boot
BOARD_AVB_ENABLE := true
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3
BOARD_AVB_VENDOR_BOOT_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_VENDOR_BOOT_ALGORITHM := SHA256_RSA4096
BOARD_AVB_VENDOR_BOOT_ROLLBACK_INDEX := 1
BOARD_AVB_VENDOR_BOOT_ROLLBACK_INDEX_LOCATION := 1


# Inherit the proprietary files
include vendor/transsion/tecno/CM6/BoardConfigVendor.mk
