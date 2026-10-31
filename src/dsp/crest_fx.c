/* Crest FX: Crest's effects chain (fx.c, a copy of Crest's) as a Schwung audio FX.
 *
 * The chain host opens modules/audio_fx/crest-fx/crest-fx.so and calls move_audio_fx_init_v2. Instances
 * come from a static pool: nothing here allocates, reads files or logs. With every effect off the block
 * is left untouched, so loading the module changes nothing until an effect is turned up. */
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef CREST_FX_PROFILE
#include <time.h>
#endif

#include "audio_fx_api.h"
#include "dsp_util.h"
#include "fx.h"

#define FX_EXPORT __attribute__((visibility("default")))
#define FX_MAX_INSTANCES 12 /* 4 tracks x 2 slots + 4 master; untouched instances cost no memory */
#define FX_BLOCK 128
#define FX_OFF 0.0005f /* the level below which fx.c leaves an effect off */

typedef struct {
    atomic_int in_use;
    int ever_used;
    /* create_instance clears from here up to fx */
    float v[P_COUNT];
    char name[32];
    int preset_idx;
#ifdef CREST_FX_PROFILE
    float cpu_us;
#endif
    fx_t fx; /* last: its big arena is cleared lazily, 64 KB per block */
} inst_t;

static const host_api_v1_t *g_host;
static inst_t g_pool[FX_MAX_INSTANCES];

/* ------------------------------------------------------------------ params */

static int find_param(const char *key) {
    int lo = 0, hi = P_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        int id = FX_PARAMS_SORTED[mid];
        int c = strcmp(key, FX_PARAMS[id].key);
        if (c == 0) return id;
        if (c < 0) hi = mid - 1;
        else lo = mid + 1;
    }
    return -1;
}

static int ci_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return *a == *b;
}

/* Enums accept an index or an option name. Returns 0 if the text is not understood. */
static int parse_value(int id, const char *s, float *out) {
    const param_desc_t *d = &FX_PARAMS[id];
    if (d->kind == PK_ENUM) {
        /* Names first: some options ("1/8", "1 bar") start with a digit. */
        for (int i = 0; i < d->nopts; i++)
            if (ci_eq(s, d->opts[i])) {
                *out = (float)i;
                return 1;
            }
        if (!((s[0] >= '0' && s[0] <= '9') || s[0] == '-' || s[0] == '+')) return 0;
    }
    char *end;
    float f = strtof(s, &end);
    if (end == s || !isfinite(f)) return 0;
    if (d->kind != PK_FLOAT) f = roundf(f);
    *out = clampf(f, d->mn, d->mx);
    return 1;
}

static int format_value(int id, float val, char *buf, int cap) {
    const param_desc_t *d = &FX_PARAMS[id];
    int n = d->kind == PK_FLOAT ? snprintf(buf, (size_t)cap, "%.4f", (double)val)
                                : snprintf(buf, (size_t)cap, "%d", (int)lrintf(val));
    return n >= cap ? -1 : n;
}

static int copy_out(const char *s, int len, char *buf, int cap) {
    if (len + 1 > cap) return -1;
    memcpy(buf, s, (size_t)len);
    buf[len] = 0;
    return len;
}

static void set_name(inst_t *I, const char *s) {
    strncpy(I->name, s, sizeof I->name - 1);
    I->name[sizeof I->name - 1] = 0;
}

static void preset_load(inst_t *I, int idx) {
    if (idx < 0 || idx >= FX_PRESET_COUNT) return;
    memcpy(I->v, FX_PRESET_VALUES[idx], sizeof I->v);
    set_name(I, FX_PRESET_NAMES[idx]);
    I->preset_idx = idx;
    fx_reset(&I->fx);
}

/* ------------------------------------------------------------------ state blob */

typedef struct {
    char *b;
    int cap, n, ok;
} wr_t;

static void w_str(wr_t *w, const char *s) {
    int l = (int)strlen(s);
    if (!w->ok || w->n + l + 1 > w->cap) {
        w->ok = 0;
        return;
    }
    memcpy(w->b + w->n, s, (size_t)l);
    w->n += l;
    w->b[w->n] = 0;
}

static int state_save(const inst_t *I, char *buf, int cap) {
    wr_t w = {buf, cap, 0, 1};
    if (cap > 0) buf[0] = 0;
    w_str(&w, "{\"fmt\":\"crest-fx\",\"v\":\"1\",\"name\":\"");
    for (const char *p = I->name; *p; p++) {
        char c[3] = {0, 0, 0};
        if (*p == '"' || *p == '\\') {
            c[0] = '\\';
            c[1] = *p;
        } else if ((unsigned char)*p >= 32 && (unsigned char)*p < 127) {
            c[0] = *p;
        }
        w_str(&w, c);
    }
    w_str(&w, "\"");
    for (int i = 0; i < P_COUNT; i++) {
        char val[24];
        w_str(&w, ",\"");
        w_str(&w, FX_PARAMS[i].key);
        w_str(&w, "\":\"");
        if (format_value(i, I->v[i], val, sizeof val) < 0) return -1;
        w_str(&w, val);
        w_str(&w, "\"");
    }
    w_str(&w, "}");
    return w.ok ? w.n : -1;
}

/* Tolerant single-pass parser: unknown keys are ignored, missing keys take their default, and the blob
 * must start with "fmt":"crest-fx". Never reads past the terminating NUL. */
static int state_load(inst_t *I, const char *s) {
    float nv[P_COUNT];
    char name[sizeof I->name];
    for (int i = 0; i < P_COUNT; i++) nv[i] = FX_PARAMS[i].def;
    strcpy(name, "Init");
    int first = 1, fmt_ok = 0;
    const char *p = s;
    while (*p) {
        if (*p != '"') {
            p++;
            continue;
        }
        const char *k0 = ++p;
        while (*p && *p != '"') p++;
        if (!*p) break;
        char key[40];
        int klen = (int)(p - k0);
        if (klen > 39) klen = 39;
        memcpy(key, k0, (size_t)klen);
        key[klen] = 0;
        p++;
        while (*p == ' ') p++;
        if (*p != ':') continue;
        p++;
        while (*p == ' ') p++;
        char val[64];
        int vl = 0;
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) p++;
                if (vl < 63) val[vl++] = *p;
                p++;
            }
            if (*p) p++;
        } else {
            while (*p && *p != ',' && *p != '}') {
                if (vl < 63) val[vl++] = *p;
                p++;
            }
        }
        val[vl] = 0;
        if (first) {
            first = 0;
            if (strcmp(key, "fmt") != 0 || strcmp(val, "crest-fx") != 0) return -1;
            fmt_ok = 1;
            continue;
        }
        if (strcmp(key, "name") == 0) {
            strncpy(name, val, sizeof name - 1);
            name[sizeof name - 1] = 0;
        } else {
            int id = find_param(key);
            float f;
            if (id >= 0 && parse_value(id, val, &f)) nv[id] = f;
        }
    }
    if (!fmt_ok) return -1;
    memcpy(I->v, nv, sizeof I->v);
    memcpy(I->name, name, sizeof name);
    I->preset_idx = -1;
    fx_reset(&I->fx);
    return 0;
}

/* ------------------------------------------------------------------ plugin API */

static void *fx_create_instance(const char *module_dir, const char *config_json) {
    (void)module_dir;
    (void)config_json;
    for (int i = 0; i < FX_MAX_INSTANCES; i++) {
        inst_t *p = &g_pool[i];
        int expect = 0;
        if (!atomic_compare_exchange_strong(&p->in_use, &expect, 1)) continue;
        int was_used = p->ever_used;
        /* Everything except the arena, which keeps old audio until an effect is next switched on. */
        memset((char *)p + offsetof(inst_t, v), 0, offsetof(inst_t, fx) - offsetof(inst_t, v));
        memset(&p->fx, 0, offsetof(fx_t, mem));
        p->ever_used = 1;
        fx_init(&p->fx, was_used);
        preset_load(p, 0);
        return p;
    }
    return NULL;
}

static void fx_destroy_instance(void *instance) {
    inst_t *I = instance;
    if (I >= g_pool && I < g_pool + FX_MAX_INSTANCES) atomic_store(&I->in_use, 0);
}

static void fx_set_param(void *instance, const char *key, const char *val) {
    inst_t *I = instance;
    if (!I || !key || !val) return;
    if (strcmp(key, "state") == 0) {
        state_load(I, val);
    } else if (strcmp(key, "preset") == 0) {
        preset_load(I, atoi(val));
    } else if (strcmp(key, "name") == 0) {
        set_name(I, val);
    } else {
        int id = find_param(key);
        float f;
        if (id >= 0 && parse_value(id, val, &f)) I->v[id] = f;
    }
}

static int fx_get_param(void *instance, const char *key, char *buf, int buf_len) {
    inst_t *I = instance;
    if (!I || !key || !buf || buf_len < 1) return -1;
    if (strcmp(key, "chain_params") == 0) return copy_out(FX_CHAIN_PARAMS, FX_CHAIN_PARAMS_LEN, buf, buf_len);
    if (strcmp(key, "ui_hierarchy") == 0) return copy_out(FX_UI_HIERARCHY, FX_UI_HIERARCHY_LEN, buf, buf_len);
    if (strcmp(key, "state") == 0) return state_save(I, buf, buf_len);
    if (strcmp(key, "preset") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%d", I->preset_idx < 0 ? 0 : I->preset_idx);
        return n >= buf_len ? -1 : n;
    }
    if (strcmp(key, "preset_count") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%d", FX_PRESET_COUNT);
        return n >= buf_len ? -1 : n;
    }
    if (strcmp(key, "preset_name") == 0 || strcmp(key, "name") == 0)
        return copy_out(I->name, (int)strlen(I->name), buf, buf_len);
#ifdef CREST_FX_PROFILE
    if (strcmp(key, "cpu_us") == 0) {
        int n = snprintf(buf, (size_t)buf_len, "%.1f", (double)I->cpu_us);
        return n >= buf_len ? -1 : n;
    }
#endif
    int id = find_param(key);
    if (id < 0) return -1;
    return format_value(id, I->v[id], buf, buf_len);
}

/* Nothing to do: every effect is off and has finished its tail, and dry and wet are at unity. */
static int all_off(const inst_t *I) {
    const float *v = I->v;
    if (v[P_DST_MIX] * fminf(v[P_DST_LVL] * 20.0f, 1.0f) > FX_OFF) return 0; /* as fx_dist works it out */
    if (v[P_CHO_LVL] > FX_OFF || v[P_DLY_LVL] > FX_OFF || v[P_REV_LVL] > FX_OFF) return 0;
    if (v[P_FX_DRY] < 0.999f || v[P_FX_WET] < 0.999f) return 0;
    for (int i = 0; i < 4; i++)
        if (I->fx.wet_last[i] > FX_OFF) return 0;
    for (int z = 0; z < 3; z++)
        if (I->fx.z[z].active) return 0;
    return 1;
}

/* Back to int16 at unity, clamped at full scale: a dry signal that fits leaves bit for bit. */
static inline int16_t to_i16(float x) {
    return (int16_t)lrintf(clampf(x * 32768.0f, -32768.0f, 32767.0f));
}

static void fx_process_block(void *instance, int16_t *audio, int frames) {
    inst_t *I = instance;
    if (!I || !audio || frames <= 0 || all_off(I)) return;
#ifdef CREST_FX_PROFILE
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
#endif
    uint64_t fp = ftz_enter();
    float bpm = 120.0f;
    if (g_host && g_host->get_bpm) {
        float b = g_host->get_bpm();
        if (b > 20.0f && b < 400.0f) bpm = b;
    }
    float l[FX_BLOCK], r[FX_BLOCK];
    for (int done = 0; done < frames; done += FX_BLOCK) {
        int cnt = frames - done < FX_BLOCK ? frames - done : FX_BLOCK;
        int16_t *io = audio + 2 * done;
        for (int i = 0; i < cnt; i++) {
            l[i] = (float)io[2 * i] / 32768.0f;
            r[i] = (float)io[2 * i + 1] / 32768.0f;
        }
        fx_process(&I->fx, I->v, bpm, l, r, cnt);
        float chk = 0.0f;
        for (int i = 0; i < cnt; i++) chk += l[i] * l[i] + r[i] * r[i];
        if (!isfinite(chk) || chk > 1e12f) { /* runaway or NaN: the dry signal stands in and the tails go */
            fx_reset(&I->fx);
            continue;
        }
        for (int i = 0; i < cnt; i++) {
            io[2 * i] = to_i16(l[i]);
            io[2 * i + 1] = to_i16(r[i]);
        }
    }
    ftz_exit(fp);
#ifdef CREST_FX_PROFILE
    clock_gettime(CLOCK_MONOTONIC, &t1);
    I->cpu_us += 0.05f * (((float)(t1.tv_sec - t0.tv_sec)) * 1e6f + ((float)(t1.tv_nsec - t0.tv_nsec)) * 1e-3f - I->cpu_us);
#endif
}

static audio_fx_api_v2_t g_api = {
    .api_version = AUDIO_FX_API_VERSION_2,
    .create_instance = fx_create_instance,
    .destroy_instance = fx_destroy_instance,
    .process_block = fx_process_block,
    .set_param = fx_set_param,
    .get_param = fx_get_param,
    .on_midi = NULL,
};

FX_EXPORT audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host) {
    g_host = host;
    return &g_api;
}
