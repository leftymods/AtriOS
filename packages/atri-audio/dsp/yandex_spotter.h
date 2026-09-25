/* SPDX-License-Identifier: MIT */
/*
 * yandex_spotter.h -- Header for Yandex Neural Keyword Spotter (Wake-Word) Engine
 *
 * Reverse Engineered from libyandex_spotter.so (Yandex Station Max)
 * Copyright (C) Yandex LLC
 */

#ifndef _YANDEX_SPOTTER_H
#define _YANDEX_SPOTTER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* SpotterHandle;
typedef void* SpotterConfHandle;

typedef enum {
	SPOTTER_EVENT_NONE             = 0,
	SPOTTER_EVENT_SPOT             = 1,
	SPOTTER_EVENT_SUBTHRESHOLD     = 2,
	SPOTTER_EVENT_SPEECH_BEGIN     = 3,
	SPOTTER_EVENT_SPEECH_END       = 4
} SpotterEvent;

/* Spotter Configuration API */
SpotterConfHandle spotter_conf_read(const char *model_dir);
void              spotter_conf_free(SpotterConfHandle conf);
int32_t           spotter_conf_version(SpotterConfHandle conf);
int32_t           spotter_conf_native_sample_rate(SpotterConfHandle conf);
int32_t           spotter_conf_get_required_channel_count(SpotterConfHandle conf);

int32_t           spotter_version_from_dirname(const char *dirname);
int32_t           spotter_required_channel_count_from_dirname(const char *dirname);
int32_t           spotter_required_channel_count_to_submit_to_server_from_dirname(const char *dirname);

/* Spotter Lifecycle */
SpotterHandle     spotter_create(const char *model_dir);
SpotterHandle     spotter_create_multichannel(const char *model_dir, int32_t num_channels);
void              spotter_destroy(SpotterHandle spotter);
void              spotter_finish(SpotterHandle spotter);

/* Audio Data Ingestion */
int32_t           spotter_feed_data(SpotterHandle spotter, const int16_t *pcm_data, size_t num_samples);
int32_t           spotter_submit_raw_data(SpotterHandle spotter, const int16_t *pcm_data, size_t num_samples);
int32_t           spotter_submit_raw_data_seamless(SpotterHandle spotter, const int16_t *pcm_data, size_t num_samples);

/* Detection and Events */
SpotterEvent      spotter_get_next_event(SpotterHandle spotter);
SpotterEvent      spotter_get_next_event_seamless(SpotterHandle spotter);
bool              is_hit_or_subhit_event(SpotterEvent event);
const char*       event_id_to_str(SpotterEvent event);

/* Phrase & Channel Metadata */
const char*       spotter_get_current_phrase(SpotterHandle spotter);
int32_t           spotter_get_current_phrase_id(SpotterHandle spotter);
const char*       spotter_get_phrase(SpotterHandle spotter, int32_t phrase_id);
int32_t           spotter_get_current_channel_id(SpotterHandle spotter);

/* Verification and Diagnostics */
bool              spotter_is_subthreshold_activation(SpotterHandle spotter);
float             spotter_get_current_subthreshold_activation_priority(SpotterHandle spotter);
const char*       spotter_get_current_activation_metainfo(SpotterHandle spotter);
const char*       spotter_get_current_activation_info_for_online_validation(SpotterHandle spotter);

size_t            spotter_get_unhandled_nbytes(SpotterHandle spotter);
void              spotter_reset_timings(SpotterHandle spotter);
const char*       spotter_get_logs(SpotterHandle spotter);
void              spotter_reset_logs(SpotterHandle spotter);

#ifdef __cplusplus
}
#endif

#endif /* _YANDEX_SPOTTER_H */
