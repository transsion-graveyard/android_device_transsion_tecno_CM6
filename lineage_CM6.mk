# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit from CM6 device
$(call inherit-product, device/transsion/tecno/CM6/device.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

PRODUCT_DEVICE := CM6
PRODUCT_NAME := lineage_CM6
PRODUCT_BRAND := TECNO
PRODUCT_MODEL := TECNO CM6
PRODUCT_MANUFACTURER := tecno

PRODUCT_GMS_CLIENTID_BASE := android-transsion

PRODUCT_BUILD_PROP_OVERRIDES += \
    BuildDesc="sys_mssi_64_64only_cn_armv82-user 16 BP2A.250605.031.A3 185643 release-keys" \
    BuildFingerprint=TECNO/CM6-OP/TECNO-CM6:16/BP2A.250605.031.A3/301550007:user/release-keys
