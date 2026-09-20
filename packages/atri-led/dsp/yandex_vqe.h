/* SPDX-License-Identifier: MIT */
/*
 * yandex_vqe.h -- Header for Yandex Voice Quality Enhancement (VQE) Engine
 *
 * Reverse Engineered from libyandex_vqe.so (Yandex Station Max)
 * Copyright (C) Yandex LLC
 */

#ifndef _YANDEX_VQE_H
#define _YANDEX_VQE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* YandexVqeC_Handle;
typedef void* YandexVqeC_ConfigHandle;
typedef void* YandexVqeC_SmartEqualizerHandle;
typedef void* YandexVqeC_EqualizerConfigHandle;
typedef void* YandexVqeC_EnergyDetectorHandle;

/* VQE Configuration API */
YandexVqeC_ConfigHandle YandexVqeC_Config_CreateFromPreset(const char *preset_name);
YandexVqeC_ConfigHandle YandexVqeC_Config_CreateFromJsonPreset(const char *json_preset);
int32_t                 YandexVqeC_Config_GetInputChunkSize(YandexVqeC_ConfigHandle config);
int32_t                 YandexVqeC_Config_GetProcessingFrame(YandexVqeC_ConfigHandle config);
int32_t                 YandexVqeC_Config_GetInputSamplingRate(YandexVqeC_ConfigHandle config);
int32_t                 YandexVqeC_Config_GetOutputSamplingRate(YandexVqeC_ConfigHandle config);
int32_t                 YandexVqeC_Config_GetMicsCount(YandexVqeC_ConfigHandle config);
int32_t                 YandexVqeC_Config_GetSpeakersCount(YandexVqeC_ConfigHandle config);
int32_t                 YandexVqeC_Config_GetOutputChannelCount(YandexVqeC_ConfigHandle config);
const char*             YandexVqeC_Config_ToString(YandexVqeC_ConfigHandle config);
uint64_t                YandexVqeC_Config_ToHash(YandexVqeC_ConfigHandle config);
void                    YandexVqeC_Config_Free(YandexVqeC_ConfigHandle config);

/* VQE Processing Pipeline */
YandexVqeC_Handle       YandexVqeC_Create(YandexVqeC_ConfigHandle config);
int32_t                 YandexVqeC_Process(YandexVqeC_Handle handle,
					   const int16_t *const *mic_channels,
					   const int16_t *const *speaker_loopback,
					   int32_t num_samples);
const float*            YandexVqeC_GetOutputChannelData(YandexVqeC_Handle handle, int32_t channel_index);
float                   YandexVqeC_GetDoa(YandexVqeC_Handle handle); /* Direction of Arrival in degrees [0..360] */
int32_t                 YandexVqeC_GetOmniMicIndex(YandexVqeC_Handle handle);
void                    YandexVqeC_GetMicHealthConfidences(YandexVqeC_Handle handle, float *confidences, int32_t count);
int32_t                 YandexVqeC_GetMicSpeakShift(YandexVqeC_Handle handle);
bool                    YandexVqeC_IsDegradationModeEnabled(YandexVqeC_Handle handle);
const char*             YandexVqeC_GetDegradationModeSwitchHistory(YandexVqeC_Handle handle);

void                    YandexVqeC_SetSpeakerVolume(YandexVqeC_Handle handle, float volume);
void                    YandexVqeC_SetCommunicationMode(YandexVqeC_Handle handle);
void                    YandexVqeC_SetASRMode(YandexVqeC_Handle handle);
void                    YandexVqeC_HardwareSyncTarget(YandexVqeC_Handle handle, int32_t target);
void                    YandexVqeC_SetLoggerHandler(void (*logger)(int level, const char *msg));
void                    YandexVqeC_Free(YandexVqeC_Handle handle);

/* Smart Equalizer (Room Acoustic Calibration) */
YandexVqeC_SmartEqualizerHandle YandexVqeC_SmartEqualizer_Create(const char *preset);
void    YandexVqeC_SmartEqualizer_OnMicReceived(YandexVqeC_SmartEqualizerHandle handle, const float *mic_data, int32_t len);
void    YandexVqeC_SmartEqualizer_OnSpeakerReceived(YandexVqeC_SmartEqualizerHandle handle, const float *spk_data, int32_t len);
void    YandexVqeC_SmartEqualizer_OnOmniMicChange(YandexVqeC_SmartEqualizerHandle handle, int32_t omni_mic);
void    YandexVqeC_SmartEqualizer_Process(YandexVqeC_SmartEqualizerHandle handle);
YandexVqeC_EqualizerConfigHandle YandexVqeC_SmartEqualizer_GetEqualizerConfig(YandexVqeC_SmartEqualizerHandle handle);
void    YandexVqeC_SmartEqualizer_GetMicAndFeedbackRms(YandexVqeC_SmartEqualizerHandle handle, float *mic_rms, float *fb_rms);
void    YandexVqeC_SmartEqualizer_SetUserEqualizerState(YandexVqeC_SmartEqualizerHandle handle, bool enabled);
const char* YandexVqeC_SmartEqualizer_ToString(YandexVqeC_SmartEqualizerHandle handle);
void    YandexVqeC_SmartEqualizer_Free(YandexVqeC_SmartEqualizerHandle handle);

/* Equalizer Config */
YandexVqeC_EqualizerConfigHandle YandexVqeC_EqualizerConfig_Create(void);
void    YandexVqeC_EqualizerConfig_SetNBands(YandexVqeC_EqualizerConfigHandle handle, int32_t nbands);
int32_t YandexVqeC_EqualizerConfig_GetNBands(YandexVqeC_EqualizerConfigHandle handle);
void    YandexVqeC_EqualizerConfig_SetBandConfig(YandexVqeC_EqualizerConfigHandle handle, int32_t band, float freq, float gain, float q);
void    YandexVqeC_EqualizerConfig_GetBandConfig(YandexVqeC_EqualizerConfigHandle handle, int32_t band, float *freq, float *gain, float *q);
void    YandexVqeC_EqualizerConfig_Free(YandexVqeC_EqualizerConfigHandle handle);

/* Error handling */
const char* YandexVqeC_Error_GetMessage(void);
void        YandexVqeC_Error_Free(void);

#ifdef __cplusplus
}
#endif

#endif /* _YANDEX_VQE_H */
