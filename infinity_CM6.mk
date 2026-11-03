#
# SPDX-FileCopyrightText: The InfinityX Project
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit from CM6 device
$(call inherit-product, device/transsion/tecno/CM6/device.mk)

# Inherit some common Infinity stuff.
$(call inherit-product, vendor/infinity/config/common_full_phone.mk)

# Inherit Lineage compat stubs (fixes IMS NoClassDefFoundError: TelephonyMetrics)
$(call inherit-product, hardware/lineage/compat/frameworks/compat.mk)

PRODUCT_DEVICE := CM6
PRODUCT_NAME := infinity_CM6
PRODUCT_BRAND := TECNO
PRODUCT_MODEL := TECNO CM6
PRODUCT_MANUFACTURER := TECNO

PRODUCT_GMS_CLIENTID_BASE := android-transsion

# Whether Including Google Apps (Default: true)
WITH_GAPPS := false

# Infinity
TARGET_HAS_UDFPS := true
INFINITY_MAINTAINER := "qiratdahaf"
PERF_ANIM_OVERRIDE := true
