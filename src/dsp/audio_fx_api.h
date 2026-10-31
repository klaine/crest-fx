/* Schwung's audio FX plugin API, as documented in Schwung's modules guide ("Audio FX Plugin API").
 * Schwung (https://github.com/charlesvestal/schwung), MIT License, Copyright (c) 2025-2026 Charles Vestal.
 * See THIRD_PARTY_NOTICES.md. Every entry point runs under the threading contract in plugin_api.h. */
#ifndef AUDIO_FX_API_H
#define AUDIO_FX_API_H

#include <stdint.h>

#include "plugin_api.h"

#define AUDIO_FX_API_VERSION_2 2

typedef struct audio_fx_api_v2 {
    uint32_t api_version;
    void *(*create_instance)(const char *module_dir, const char *config_json);
    void (*destroy_instance)(void *instance);
    /* Stereo interleaved, processed in place. */
    void (*process_block)(void *instance, int16_t *audio_inout, int frames);
    void (*set_param)(void *instance, const char *key, const char *val);
    int (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    void (*on_midi)(void *instance, const uint8_t *msg, int len, int source); /* optional, may be NULL */
} audio_fx_api_v2_t;

#endif
