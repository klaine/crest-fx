/* Native tests: drive Crest FX through the audio FX API with a fake host. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_fx_api.h"
#include "params_gen.h"

audio_fx_api_v2_t *move_audio_fx_init_v2(const host_api_v1_t *host);

static int g_pass, g_fail;
#define CHECK(cond, ...)                                  \
    do {                                                  \
        if (cond) {                                       \
            g_pass++;                                     \
        } else {                                          \
            g_fail++;                                     \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__);                          \
            printf("\n");                                 \
        }                                                 \
    } while (0)

#define FRAMES 128
static audio_fx_api_v2_t *api;
static host_api_v1_t host;
static float g_bpm = 120.0f;
static float fake_bpm(void) { return g_bpm; }

static void sp(void *h, const char *k, const char *v) { api->set_param(h, k, v); }

static uint32_t rng_state = 0x12345u;
static int16_t noise(int amp) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return (int16_t)((int)(rng_state % (uint32_t)(2 * amp + 1)) - amp);
}

static void fill_noise(int16_t *b, int amp) {
    for (int i = 0; i < 2 * FRAMES; i++) b[i] = noise(amp);
}

/* Run n silent blocks and return the peak absolute sample. */
static int run_silence(void *h, int n) {
    int peak = 0;
    int16_t b[2 * FRAMES];
    for (int k = 0; k < n; k++) {
        memset(b, 0, sizeof b);
        api->process_block(h, b, FRAMES);
        for (int i = 0; i < 2 * FRAMES; i++) {
            int a = abs(b[i]);
            if (a > peak) peak = a;
        }
    }
    return peak;
}

/* Feed one impulse in frame 0, then silence, and return the frame of the largest later output in the left
 * channel, or -1. */
static int echo_frame(void *h, int total_blocks) {
    int best = -1, bestv = 0;
    for (int k = 0; k < total_blocks; k++) {
        int16_t b[2 * FRAMES];
        memset(b, 0, sizeof b);
        if (k == 0) b[0] = b[1] = 20000;
        api->process_block(h, b, FRAMES);
        for (int i = 0; i < FRAMES; i++) {
            if (k == 0 && i == 0) continue;
            int a = abs(b[2 * i]);
            if (a > bestv) {
                bestv = a;
                best = k * FRAMES + i;
            }
        }
    }
    return best;
}

static void test_abi(void) {
    CHECK(api != NULL, "init returned NULL");
    CHECK(api->api_version == 2, "api_version %u", api->api_version);
    CHECK(api->create_instance && api->destroy_instance && api->process_block && api->set_param && api->get_param,
          "missing entry point");
    /* The documented layout: the host reads these by offset. */
    CHECK(offsetof(audio_fx_api_v2_t, process_block) == 3 * sizeof(void *), "process_block offset");
    CHECK(offsetof(audio_fx_api_v2_t, on_midi) == 6 * sizeof(void *), "on_midi offset");
}

static void test_pool(void) {
    void *h[13];
    int n = 0;
    for (int i = 0; i < 13; i++) {
        h[i] = api->create_instance(".", NULL);
        if (h[i]) n++;
    }
    CHECK(n == 12, "pool holds %d", n);
    CHECK(h[12] == NULL, "a 13th instance must fail, not overrun");
    for (int i = 0; i < 12; i++) api->destroy_instance(h[i]);
    void *again = api->create_instance(".", NULL);
    CHECK(again != NULL, "a freed instance is reusable");
    api->destroy_instance(again);
}

static void test_passthrough(void) {
    void *h = api->create_instance(".", NULL);
    int16_t in[2 * FRAMES], out[2 * FRAMES];
    for (int k = 0; k < 200; k++) {
        fill_noise(in, 32767);
        in[0] = 32767;
        in[1] = -32768;
        memcpy(out, in, sizeof in);
        api->process_block(h, out, FRAMES);
        if (memcmp(in, out, sizeof in) != 0) {
            CHECK(0, "Init must be bit-exact (block %d)", k);
            break;
        }
    }
    g_pass++;
    /* The active path is bit-exact too, right up to full scale: a running reverb with wet 0 and dry at unity. */
    sp(h, "rev_lvl", "0.5");
    sp(h, "fx_wet", "0");
    int bad = 0;
    for (int k = 0; k < 40; k++) {
        fill_noise(in, 32767);
        in[0] = 32767;
        in[1] = -32768;
        memcpy(out, in, sizeof in);
        api->process_block(h, out, FRAMES);
        for (int i = 0; i < 2 * FRAMES; i++)
            if (out[i] != in[i]) bad++;
    }
    CHECK(bad == 0, "wet 0, dry 1 must leave the signal bit-exact: %d samples off", bad);
    api->destroy_instance(h);
}

static void test_effects_heard(void) {
    /* Each effect at full level changes the signal, and the time-based ones ring on into silence. */
    static const char *const LVL[4] = {"dst_lvl", "cho_lvl", "dly_lvl", "rev_lvl"};
    for (int e = 0; e < 4; e++) {
        void *h = api->create_instance(".", NULL);
        sp(h, LVL[e], "0.8");
        int16_t in[2 * FRAMES], out[2 * FRAMES];
        long diff = 0;
        /* Longer than the default delay (1/8D at 120 BPM, 375 ms = 130 blocks) plus a reused slot's clearing. */
        for (int k = 0; k < 200; k++) {
            fill_noise(in, 8000);
            memcpy(out, in, sizeof in);
            api->process_block(h, out, FRAMES);
            for (int i = 0; i < 2 * FRAMES; i++) diff += abs(out[i] - in[i]);
        }
        CHECK(diff > 10000, "%s did nothing (diff %ld)", LVL[e], diff);
        if (e >= 2) {
            int tail = run_silence(h, 140);
            CHECK(tail > 20, "%s left no tail (%d)", LVL[e], tail);
        }
        api->destroy_instance(h);
    }
}

static void test_delay_time(void) {
    /* Sync 1/4 at 100 BPM: 0.6 s = 26460 samples, stereo with no feedback, heard at the tap. */
    void *h = api->create_instance(".", NULL);
    sp(h, "dly_lvl", "1");
    sp(h, "dly_sync", "1/4");
    sp(h, "dly_fb", "0");
    sp(h, "dly_lp", "1");
    sp(h, "dly_hp", "0");
    g_bpm = 100.0f;
    /* A reused pool slot clears its 740 KB line first, 64 KB a block, staying dry: wait that out. */
    run_silence(h, 16);
    int at = echo_frame(h, 26460 / FRAMES + 4);
    CHECK(abs(at - 26460) <= 2, "the echo landed at %d, expected 26460", at);
    api->destroy_instance(h);
    g_bpm = 120.0f;
}

static void test_tail_ends_and_bypass_returns(void) {
    void *h = api->create_instance(".", NULL);
    sp(h, "rev_lvl", "0.5");
    int16_t b[2 * FRAMES];
    for (int k = 0; k < 20; k++) {
        fill_noise(b, 8000);
        api->process_block(h, b, FRAMES);
    }
    sp(h, "rev_lvl", "0");
    run_silence(h, 44100 * 3 / FRAMES); /* longer than the 2 s hold */
    int16_t in[2 * FRAMES], out[2 * FRAMES];
    fill_noise(in, 32767);
    memcpy(out, in, sizeof in);
    api->process_block(h, out, FRAMES);
    CHECK(memcmp(in, out, sizeof in) == 0, "bit-exact bypass must return once the effect is off");
    api->destroy_instance(h);
}

static void test_no_stale_audio(void) {
    void *h = api->create_instance(".", NULL);
    sp(h, "dly_lvl", "1");
    sp(h, "dly_sync", "Off");
    sp(h, "dly_time", "0.1");
    sp(h, "rev_lvl", "1");
    int16_t b[2 * FRAMES];
    for (int k = 0; k < 100; k++) {
        fill_noise(b, 20000);
        api->process_block(h, b, FRAMES);
    }
    api->destroy_instance(h);
    /* The next user of that pool slot, in silence, must not hear the last one's audio. */
    h = api->create_instance(".", NULL);
    sp(h, "dly_lvl", "1");
    sp(h, "dly_sync", "Off");
    sp(h, "dly_time", "0.1");
    sp(h, "rev_lvl", "1");
    int peak = run_silence(h, 200);
    CHECK(peak == 0, "stale audio reached a new instance (peak %d)", peak);
    api->destroy_instance(h);
}

static void test_instances_independent(void) {
    void *a = api->create_instance(".", NULL), *b = api->create_instance(".", NULL);
    sp(a, "rev_lvl", "1");
    int16_t x[2 * FRAMES];
    for (int k = 0; k < 40; k++) {
        fill_noise(x, 20000);
        api->process_block(a, x, FRAMES);
    }
    char buf[32];
    api->get_param(b, "rev_lvl", buf, sizeof buf);
    CHECK(atof(buf) == 0.0, "params leaked between instances: %s", buf);
    CHECK(run_silence(b, 10) == 0, "audio leaked between instances");
    api->destroy_instance(a);
    api->destroy_instance(b);
}

static void test_params(void) {
    void *h = api->create_instance(".", NULL);
    char buf[64];
    sp(h, "rev_type", "Plate");
    api->get_param(h, "rev_type", buf, sizeof buf);
    CHECK(strcmp(buf, "2") == 0, "enum by name: %s", buf);
    sp(h, "dly_sync", "1/8");
    api->get_param(h, "dly_sync", buf, sizeof buf);
    CHECK(strcmp(buf, "7") == 0, "an option starting with a digit: %s", buf);
    sp(h, "rev_lvl", "7");
    api->get_param(h, "rev_lvl", buf, sizeof buf);
    CHECK(atof(buf) == 1.0, "clamped: %s", buf);
    sp(h, "rev_lvl", "nan");
    api->get_param(h, "rev_lvl", buf, sizeof buf);
    CHECK(atof(buf) == 1.0, "nan is refused: %s", buf);
    CHECK(api->get_param(h, "nope", buf, sizeof buf) == -1, "unknown key");
    sp(h, "nope", "1");
    CHECK(api->get_param(h, "chain_params", buf, 4) == -1, "a short buffer is refused");
    api->destroy_instance(h);
}

static void test_state(void) {
    void *a = api->create_instance(".", NULL), *b = api->create_instance(".", NULL);
    sp(a, "dly_lvl", "0.3125");
    sp(a, "dly_mode", "PingPong");
    sp(a, "name", "My \"FX\"");
    static char blob[8192], blob2[8192];
    int n = api->get_param(a, "state", blob, sizeof blob);
    CHECK(n > 0, "state save");
    sp(b, "state", blob);
    api->get_param(b, "state", blob2, sizeof blob2);
    CHECK(strcmp(blob, blob2) == 0, "state round trip:\n%s\n%s", blob, blob2);
    char buf[64];
    api->get_param(b, "name", buf, sizeof buf);
    CHECK(strcmp(buf, "My \"FX\"") == 0, "name round trip: %s", buf);
    sp(b, "state", "{\"fmt\":\"crest\",\"dly_lvl\":\"0.9\"}");
    api->get_param(b, "dly_lvl", buf, sizeof buf);
    CHECK(atof(buf) == 0.3125, "another module's blob is refused: %s", buf);
    CHECK(api->get_param(a, "state", blob, 8) == -1, "a short state buffer is refused");
    sp(b, "state", "{\"fmt\":\"crest-fx\",\"rev_lvl");
    sp(b, "state", "{\"fmt\":\"crest-fx\",\"unknown\":\"1\",\"rev_lvl\":\"0.5\"");
    api->get_param(b, "rev_lvl", buf, sizeof buf);
    CHECK(atof(buf) == 0.5, "tolerant parse: %s", buf);
    api->destroy_instance(a);
    api->destroy_instance(b);
}

static void test_presets(void) {
    char buf[64];
    void *h = api->create_instance(".", NULL);
    api->get_param(h, "preset_count", buf, sizeof buf);
    CHECK(atoi(buf) == FX_PRESET_COUNT, "preset_count %s", buf);
    for (int p = 0; p < FX_PRESET_COUNT; p++) {
        snprintf(buf, sizeof buf, "%d", p);
        sp(h, "preset", buf);
        api->get_param(h, "preset_name", buf, sizeof buf);
        CHECK(strcmp(buf, FX_PRESET_NAMES[p]) == 0, "preset %d name %s", p, buf);
        /* A loud burst then silence: the output stays bounded, and every preset but Init does something. */
        int16_t in[2 * FRAMES], out[2 * FRAMES];
        long diff = 0;
        for (int k = 0; k < 400; k++) {
            fill_noise(in, 32767);
            memcpy(out, in, sizeof in);
            api->process_block(h, out, FRAMES);
            for (int i = 0; i < 2 * FRAMES; i++) diff += abs(out[i] - in[i]);
        }
        if (p == 0) CHECK(diff == 0, "Init changed the signal");
        else CHECK(diff > 1000, "%s did nothing", FX_PRESET_NAMES[p]);
        run_silence(h, 400); /* UBSan and the NaN guard watch this */
    }
    sp(h, "preset", "999");
    api->get_param(h, "preset", buf, sizeof buf);
    CHECK(atoi(buf) == FX_PRESET_COUNT - 1, "an out-of-range preset is ignored: %s", buf);
    api->destroy_instance(h);
}

static void test_extreme_feedback(void) {
    /* Everything at maximum for a long time: bounded and finite (a NaN would stop the tail dead). */
    void *h = api->create_instance(".", NULL);
    sp(h, "dst_lvl", "1");
    sp(h, "cho_lvl", "1");
    sp(h, "cho_fb", "1");
    sp(h, "dly_lvl", "1");
    sp(h, "dly_fb", "1");
    sp(h, "rev_lvl", "1");
    sp(h, "rev_decay", "1");
    int16_t b[2 * FRAMES];
    for (int k = 0; k < 2000; k++) {
        fill_noise(b, 32767);
        api->process_block(h, b, FRAMES);
    }
    int tail = run_silence(h, 20);
    CHECK(tail > 0, "the tail died");
    api->destroy_instance(h);
}

int main(void) {
    host.api_version = 1;
    host.sample_rate = 44100;
    host.frames_per_block = FRAMES;
    host.get_bpm = fake_bpm;
    api = move_audio_fx_init_v2(&host);
    test_abi();
    test_pool();
    test_passthrough();
    test_effects_heard();
    test_delay_time();
    test_tail_ends_and_bypass_returns();
    test_no_stale_audio();
    test_instances_independent();
    test_params();
    test_state();
    test_presets();
    test_extreme_feedback();
    printf("%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
