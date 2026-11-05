/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "CM6AudioPolicy"

#include <AudioPolicyConfig.h>
#include <AudioPolicyManager.h>
#include <EngineLibrary.h>
#include <media/AudioDeviceTypeAddr.h>
#include <system/audio.h>
#include <utils/Log.h>

namespace android {

namespace {

bool isManagedVoiceType(audio_devices_t type) {
    return type == AUDIO_DEVICE_OUT_EARPIECE
            || type == AUDIO_DEVICE_OUT_SPEAKER
            || type == AUDIO_DEVICE_OUT_SPEAKER_SAFE;
}

audio_devices_t resolveManagedVoiceDevice(AudioPolicyManager* apm, audio_devices_t device) {
    if (isManagedVoiceType(device)) {
        return device;
    }
    if (device != AUDIO_DEVICE_OUT_DEFAULT_FOR_VOLUME) {
        return AUDIO_DEVICE_NONE;
    }

    audio_attributes_t attributes = {};
    attributes.usage = AUDIO_USAGE_VOICE_COMMUNICATION;
    attributes.content_type = AUDIO_CONTENT_TYPE_SPEECH;

    AudioDeviceTypeAddrVector devices;
    if (apm->getDevicesForAttributes(attributes, 0, true, &devices) != NO_ERROR
            || devices.empty()) {
        return AUDIO_DEVICE_NONE;
    }

    audio_devices_t resolved = AUDIO_DEVICE_NONE;
    for (const auto& dev : devices) {
        if (!isManagedVoiceType(dev.mType)) {
            return AUDIO_DEVICE_NONE;
        }
        if (resolved == AUDIO_DEVICE_NONE) {
            resolved = dev.mType;
        }
    }
    return resolved;
}

} // namespace

class CM6AudioPolicyManager final : public AudioPolicyManager {
  public:
    CM6AudioPolicyManager(const sp<const AudioPolicyConfig>& config,
                          EngineInstance&& engine,
                          AudioPolicyClientInterface* clientInterface)
        : AudioPolicyManager(config, std::move(engine), clientInterface) {}

  protected:
    status_t setStreamVolumeIndex(audio_stream_type_t stream, int index, bool muted,
                                  audio_devices_t device) override {
        if (stream == AUDIO_STREAM_VOICE_CALL) {
            audio_devices_t managedDevice = resolveManagedVoiceDevice(this, device);
            if (managedDevice != AUDIO_DEVICE_NONE) {
                // Keep the APM volume curves current, but do not call the legacy
                // AudioPolicyManager voice volume path. The incallservice is the
                // sole writer of the MTK earpiece/speaker voice gain.
                IVolumeCurves& curves = getVolumeCurves(stream);
                return setVolumeCurveIndex(index, muted, managedDevice, curves);
            }
        }
        return AudioPolicyManager::setStreamVolumeIndex(stream, index, muted, device);
    }

    status_t checkAndSetVolume(IVolumeCurves& curves, VolumeSource volumeSource, int index,
                               const sp<AudioOutputDescriptor>& outputDesc,
                               DeviceTypeSet deviceTypes, bool adjustAttenuation,
                               int delayMs = 0, bool force = false) override {
        if (volumeSource == toVolumeSource(AUDIO_STREAM_VOICE_CALL, false)
                && isManagedVoiceRoute(deviceTypes)) {
            // The incallservice owns earpiece/speaker call gain. Do not send
            // the framework's 0..1 voice volume to the legacy HAL.
            return NO_ERROR;
        }
        return AudioPolicyManager::checkAndSetVolume(curves, volumeSource, index, outputDesc,
                                                     deviceTypes, adjustAttenuation, delayMs,
                                                     force);
    }

  private:
    static bool isManagedVoiceRoute(const DeviceTypeSet& deviceTypes) {
        if (deviceTypes.empty()) {
            return false;
        }
        for (audio_devices_t type : deviceTypes) {
            if (!isManagedVoiceType(type)) {
                return false;
            }
        }
        return true;
    }
};

extern "C" AudioPolicyInterface* createAudioPolicyManager(
        AudioPolicyClientInterface* clientInterface) {
    AudioPolicyManager* apm = nullptr;
    media::AudioPolicyConfig apmConfig;
    if (status_t status = clientInterface->getAudioPolicyConfig(&apmConfig); status == OK) {
        auto config = AudioPolicyConfig::loadFromApmAidlConfigWithFallback(apmConfig);
        ALOGD("%s loading APM engine %s", __func__,
              config->getEngineLibraryNameSuffix().c_str());
        apm = new CM6AudioPolicyManager(
                config,
                loadApmEngineLibraryAndCreateEngine(
                        config->getEngineLibraryNameSuffix(), apmConfig.engineConfig),
                clientInterface);
    } else {
        auto config = AudioPolicyConfig::loadFromApmXmlConfigWithFallback();
        apm = new CM6AudioPolicyManager(
                config,
                loadApmEngineLibraryAndCreateEngine(config->getEngineLibraryNameSuffix()),
                clientInterface);
    }
    status_t status = apm->initialize();
    if (status != NO_ERROR) {
        delete apm;
        apm = nullptr;
    }
    return apm;
}

extern "C" void destroyAudioPolicyManager(AudioPolicyInterface* interface) {
    delete interface;
}

} // namespace android
