/* audio.c - procedurally synthesized chiptune SFX and looping BGM (no audio files).
 *
 * SFX: each effect is rendered once into a float scratch buffer, normalised to a
 * per-effect peak and converted to 16-bit mono 44.1 kHz before raylib copies it
 * into a Sound. Each Sound has a small round-robin of aliases so overlapping
 * plays (hover, placing stones) do not cut each other off.
 *
 * BGM: two seamless loops (MENU, GAME) are rendered at init. Music is streamed
 * through one AudioStream; audio_update() mixes the two active loop slots in
 * software so a track change is a 0.5 s crossfade.
 *
 * Everything is a silent no-op when the audio device is unavailable. */
#include "audio.h"

#include "raylib.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define AUDIO_SR 44100
#define MIX_FRAMES 1024          /* frames per stream refill */
#define SFX_VOICES 4             /* round-robin aliases per effect */
#define MUSIC_PEAK 0.35f         /* loop peak before the user's bgm volume */
#define MUSIC_FADE_S 0.5f        /* crossfade length */
#define TAU 6.283185307179586

typedef enum { WAVE_SQUARE, WAVE_TRI, WAVE_SAW, WAVE_SINE, WAVE_NOISE } WaveKind;

/* One synthesised note. dur is the gate length, r is the release tail after it.
 * f0 -> f1 is a geometric pitch slide across dur; lp0 -> lp1 a sweeping one-pole
 * low-pass cutoff (0 disables the filter). */
typedef struct {
    double start, dur;
    double f0, f1;
    WaveKind wave;
    float duty, vol;
    float a, d, s, r;
    float lp0, lp1;
} Tone;

typedef struct {
    BgmId track;
    double pos;      /* read position in the loop, in samples */
    float gain;
    float target;
} MusicSlot;

static bool s_inited;
static bool s_ready;
static float s_sfx_vol = 0.8f;
static float s_music_vol = 0.7f;

static Sound s_sfx_src[SFX_COUNT];
static Sound s_sfx_alias[SFX_COUNT][SFX_VOICES];
static int s_sfx_rr[SFX_COUNT];

static float *s_loop[BGM_COUNT];
static int s_loop_len[BGM_COUNT];
static MusicSlot s_slot[2];
static BgmId s_cur = BGM_NONE;
static AudioStream s_stream;
static short s_mix[MIX_FRAMES];

static float clampf(float x, float lo, float hi) {
    return x < lo ? lo : (x > hi ? hi : x);
}

static double mhz(int midi) {
    return 440.0 * pow(2.0, (midi - 69) / 12.0);
}

/* 15-bit Fibonacci LFSR (taps 15,14). Seed must be non-zero. */
static unsigned lfsr_step(unsigned lf) {
    unsigned bit = (lf ^ (lf >> 1)) & 1u;
    return (lf >> 1) | (bit << 14);
}

/* ADSR level at time t (seconds from note start). Sustain holds until dur, then
 * a linear release over r. */
static float env_at(double t, const Tone *k) {
    if (t >= k->dur + k->r) return 0.0f;
    double tt = t < k->dur ? t : k->dur;
    float l;
    if (k->a > 0.0f && tt < k->a) {
        l = (float)(tt / k->a);
    } else {
        double t2 = tt - k->a;
        if (k->d > 0.0f && t2 < k->d) l = 1.0f - (1.0f - k->s) * (float)(t2 / k->d);
        else l = k->s;
    }
    if (t > k->dur) {
        double rel = k->r > 0.0f ? 1.0 - (t - k->dur) / k->r : 0.0;
        l *= (float)(rel < 0.0 ? 0.0 : rel);
    }
    return clampf(l, 0.0f, 1.0f);
}

/* Add one note into buf. wrap = treat buf as a circular loop (for BGM seams). */
static void render_tone(float *buf, int len, bool wrap, const Tone *t) {
    if (!buf || len <= 0 || t->f0 <= 0.0) return;
    const double sr = AUDIO_SR;
    int n = (int)((t->dur + t->r) * sr);
    int start = (int)(t->start * sr + 0.5);
    double ph = 0.0;
    unsigned lf = 0x7FFFu;
    float lp = 0.0f;

    for (int i = 0; i < n; i++) {
        double tt = i / sr;
        double prog = t->dur > 0.0 ? tt / t->dur : 1.0;
        if (prog > 1.0) prog = 1.0;

        double f = t->f0;
        if (t->f1 > 0.0 && t->f1 != t->f0) f = t->f0 * pow(t->f1 / t->f0, prog);
        ph += f / sr;
        if (ph >= 1.0) {
            ph -= floor(ph);
            if (t->wave == WAVE_NOISE) lf = lfsr_step(lf);
        }

        float x;
        switch (t->wave) {
            case WAVE_SQUARE: x = ph < t->duty ? 1.0f : -1.0f; break;
            case WAVE_TRI:    x = (float)(4.0 * fabs(ph - 0.5) - 1.0); break;
            case WAVE_SAW:    x = (float)(2.0 * ph - 1.0); break;
            case WAVE_SINE:   x = (float)sin(TAU * ph); break;
            default:          x = (lf & 1u) ? 1.0f : -1.0f; break;
        }

        if (t->lp0 > 0.0f) {
            double fc = t->lp0;
            if (t->lp1 > 0.0f && t->lp1 != t->lp0) fc = t->lp0 * pow(t->lp1 / t->lp0, prog);
            float alpha = (float)(1.0 - exp(-TAU * fc / sr));
            lp += alpha * (x - lp);
            x = lp;
        }

        int idx = start + i;
        if (wrap) idx %= len;
        else if (idx >= len) break;
        buf[idx] += x * env_at(tt, t) * t->vol;
    }
}

/* Normalise to peak and convert to 16-bit mono PCM, then hand it to raylib (which
 * copies it). Returns an invalid Sound for silent input or allocation failure. */
static Sound sfx_finish(float *buf, int len, float peak) {
    Sound out = { 0 };
    float mx = 0.0f;
    for (int i = 0; i < len; i++) {
        float a = fabsf(buf[i]);
        if (a > mx) mx = a;
    }
    if (mx < 1e-5f) return out;

    short *pcm = (short *)malloc((size_t)len * sizeof(short));
    if (!pcm) return out;
    float k = peak / mx;
    for (int i = 0; i < len; i++) {
        float v = clampf(buf[i] * k, -1.0f, 1.0f);
        pcm[i] = (short)lrintf(v * 32767.0f);
    }
    Wave w = { .frameCount = (unsigned int)len, .sampleRate = AUDIO_SR,
               .sampleSize = 16, .channels = 1, .data = pcm };
    out = LoadSoundFromWave(w);
    free(pcm);
    return out;
}

static float *sfx_scratch(double secs, int *len) {
    *len = (int)(secs * AUDIO_SR);
    return (float *)calloc((size_t)*len, sizeof(float));
}

static Sound build_sfx(SfxId id) {
    int n = 0;
    float *b = NULL;
    float peak = 0.5f;
    Sound snd = { 0 };

    switch (id) {
        case SFX_HOVER:
            b = sfx_scratch(0.06, &n); peak = 0.2f;
            if (b) render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.03, .f0 = 1400, .f1 = 1700,
                .wave = WAVE_TRI, .vol = 0.5f, .a = 0.002f, .d = 0.012f, .s = 0.3f, .r = 0.012f });
            break;
        case SFX_CLICK:
            b = sfx_scratch(0.16, &n);
            if (b) {
                render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.05, .f0 = 660, .f1 = 660,
                    .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.6f, .a = 0.002f, .d = 0.03f, .s = 0.4f, .r = 0.015f });
                render_tone(b, n, false, &(Tone){ .start = 0.045, .dur = 0.08, .f0 = 990, .f1 = 990,
                    .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.6f, .a = 0.002f, .d = 0.05f, .s = 0.35f, .r = 0.02f });
            }
            break;
        case SFX_BACK:
            b = sfx_scratch(0.2, &n);
            if (b) render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.12, .f0 = 720, .f1 = 360,
                .wave = WAVE_SQUARE, .duty = 0.25f, .vol = 0.6f, .a = 0.002f, .d = 0.05f, .s = 0.5f, .r = 0.04f });
            break;
        case SFX_PLACE_FIRE:
            /* crackle, then a rising square whoosh */
            b = sfx_scratch(0.26, &n); peak = 0.55f;
            if (b) {
                render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.06, .f0 = 12000, .f1 = 12000,
                    .wave = WAVE_NOISE, .vol = 0.45f, .a = 0.001f, .d = 0.04f, .s = 0.0f, .r = 0.02f,
                    .lp0 = 5000, .lp1 = 5000 });
                render_tone(b, n, false, &(Tone){ .start = 0.01, .dur = 0.16, .f0 = 200, .f1 = 900,
                    .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.35f, .a = 0.03f, .d = 0.08f, .s = 0.5f, .r = 0.04f,
                    .lp0 = 1500, .lp1 = 6000 });
            }
            break;
        case SFX_PLACE_WATER:
            /* downward bloop with two tiny droplets */
            b = sfx_scratch(0.3, &n);
            if (b) {
                render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.16, .f0 = 600, .f1 = 220,
                    .wave = WAVE_SINE, .vol = 0.6f, .a = 0.003f, .d = 0.1f, .s = 0.3f, .r = 0.05f });
                render_tone(b, n, false, &(Tone){ .start = 0.05, .dur = 0.025, .f0 = 1500, .f1 = 2100,
                    .wave = WAVE_SINE, .vol = 0.3f, .a = 0.002f, .d = 0.015f, .s = 0.0f, .r = 0.005f });
                render_tone(b, n, false, &(Tone){ .start = 0.11, .dur = 0.025, .f0 = 1800, .f1 = 2400,
                    .wave = WAVE_SINE, .vol = 0.25f, .a = 0.002f, .d = 0.015f, .s = 0.0f, .r = 0.005f });
            }
            break;
        case SFX_UNDO:
            /* PLACE_FIRE in reverse: a descending sweep that swells then cuts */
            b = sfx_scratch(0.3, &n);
            if (b) render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.2, .f0 = 1000, .f1 = 250,
                .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.4f, .a = 0.12f, .d = 0.0f, .s = 1.0f, .r = 0.01f,
                .lp0 = 2500, .lp1 = 2500 });
            break;
        case SFX_WIN:
            /* C major arpeggio into a held C6-E6-G6 chord */
            b = sfx_scratch(1.5, &n); peak = 0.55f;
            if (b) {
                static const int arp[4] = { 72, 76, 79, 84 };
                for (int i = 0; i < 4; i++)
                    render_tone(b, n, false, &(Tone){ .start = 0.14 * i, .dur = 0.13, .f0 = mhz(arp[i]), .f1 = mhz(arp[i]),
                        .wave = WAVE_SQUARE, .duty = 0.25f, .vol = 0.4f, .a = 0.004f, .d = 0.05f, .s = 0.5f, .r = 0.03f });
                static const int chord[3] = { 84, 88, 91 };
                for (int i = 0; i < 3; i++)
                    render_tone(b, n, false, &(Tone){ .start = 0.56, .dur = 0.6, .f0 = mhz(chord[i]), .f1 = mhz(chord[i]),
                        .wave = WAVE_SQUARE, .duty = 0.25f, .vol = 0.22f, .a = 0.005f, .d = 0.2f, .s = 0.6f, .r = 0.3f });
                render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.56, .f0 = mhz(48), .f1 = mhz(48),
                    .wave = WAVE_TRI, .vol = 0.4f, .a = 0.005f, .d = 0.1f, .s = 0.6f, .r = 0.05f });
                render_tone(b, n, false, &(Tone){ .start = 0.56, .dur = 0.6, .f0 = mhz(43), .f1 = mhz(43),
                    .wave = WAVE_TRI, .vol = 0.3f, .a = 0.005f, .d = 0.1f, .s = 0.6f, .r = 0.3f });
            }
            break;
        case SFX_LOSE:
            /* A minor descent A-G-F-E, last note sliding down */
            b = sfx_scratch(1.1, &n);
            if (b) {
                static const int phrase[3] = { 69, 67, 65 };
                for (int i = 0; i < 3; i++)
                    render_tone(b, n, false, &(Tone){ .start = 0.18 * i, .dur = 0.16, .f0 = mhz(phrase[i]), .f1 = mhz(phrase[i]),
                        .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.4f, .a = 0.005f, .d = 0.05f, .s = 0.5f, .r = 0.1f,
                        .lp0 = 2500, .lp1 = 2500 });
                render_tone(b, n, false, &(Tone){ .start = 0.54, .dur = 0.45, .f0 = mhz(64), .f1 = mhz(56),
                    .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.4f, .a = 0.005f, .d = 0.05f, .s = 0.5f, .r = 0.1f,
                    .lp0 = 2500, .lp1 = 2500 });
            }
            break;
        case SFX_TICK:
            b = sfx_scratch(0.03, &n); peak = 0.35f;
            if (b) render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.01, .f0 = 2400, .f1 = 2400,
                .wave = WAVE_SINE, .vol = 0.6f, .a = 0.0f, .d = 0.008f, .s = 0.0f, .r = 0.004f });
            break;
        case SFX_POPUP:
            /* two soft sine chimes, E6 then A6 */
            b = sfx_scratch(0.5, &n); peak = 0.4f;
            if (b) {
                render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.16, .f0 = mhz(88), .f1 = mhz(88),
                    .wave = WAVE_SINE, .vol = 0.5f, .a = 0.003f, .d = 0.12f, .s = 0.25f, .r = 0.15f });
                render_tone(b, n, false, &(Tone){ .start = 0.09, .dur = 0.3, .f0 = mhz(93), .f1 = mhz(93),
                    .wave = WAVE_SINE, .vol = 0.45f, .a = 0.003f, .d = 0.2f, .s = 0.2f, .r = 0.2f });
            }
            break;
        case SFX_ERROR:
            /* two short low square notes, C3 then A2 */
            b = sfx_scratch(0.35, &n);
            if (b) {
                render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.12, .f0 = 130.8, .f1 = 130.8,
                    .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.5f, .a = 0.003f, .d = 0.02f, .s = 0.8f, .r = 0.03f,
                    .lp0 = 1200, .lp1 = 1200 });
                render_tone(b, n, false, &(Tone){ .start = 0.14, .dur = 0.14, .f0 = 110.0, .f1 = 110.0,
                    .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.5f, .a = 0.003f, .d = 0.02f, .s = 0.8f, .r = 0.03f,
                    .lp0 = 1200, .lp1 = 1200 });
            }
            break;
        case SFX_TRANSITION:
            /* airy filtered-noise sweep, quiet */
            b = sfx_scratch(0.6, &n); peak = 0.25f;
            if (b) render_tone(b, n, false, &(Tone){ .start = 0, .dur = 0.45, .f0 = 9000, .f1 = 9000,
                .wave = WAVE_NOISE, .vol = 0.25f, .a = 0.15f, .d = 0.1f, .s = 0.6f, .r = 0.2f,
                .lp0 = 300, .lp1 = 3000 });
            break;
        default:
            break;
    }

    if (b) {
        snd = sfx_finish(b, n, peak);
        free(b);
    }
    return snd;
}

/* Circular non-recursive echo: out[i] += g1*x[i-d1] + g2*x[i-d2], wrapping so the
 * loop seam stays continuous. */
static void add_echo(float *out, const float *dry, float *tmp, int n, int d1) {
    int d2 = 2 * d1;
    for (int i = 0; i < n; i++) {
        int i1 = (i + n - d1) % n;
        int i2 = (i + n - d2 % n) % n;
        tmp[i] = 0.4f * dry[i1] + 0.16f * dry[i2];
    }
    for (int i = 0; i < n; i++) out[i] += tmp[i];
}

/* MENU: A dorian-ish, 84 BPM, 8 bars. Triangle bass, square melody with echo,
 * noise hats, occasional water drips. */
static float *build_menu_loop(int *out_len) {
    const double beat = 60.0 / 84.0;
    const int n = (int)lround(32.0 * beat * AUDIO_SR);
    float *out = (float *)calloc((size_t)n, sizeof(float));
    float *mel = (float *)calloc((size_t)n, sizeof(float));
    float *tmp = (float *)calloc((size_t)n, sizeof(float));
    if (!out || !mel || !tmp) {
        free(out); free(mel); free(tmp);
        return NULL;
    }

    static const int roots[8] = { 33, 31, 29, 31, 33, 38, 28, 33 };
    for (int b = 0; b < 8; b++) {
        for (int k = 0; k < 4; k++) {
            int p = roots[b] + ((k & 1) ? 7 : 0);
            render_tone(out, n, true, &(Tone){ .start = (b * 4 + k) * beat, .dur = beat * 0.9, .f0 = mhz(p), .f1 = mhz(p),
                .wave = WAVE_TRI, .vol = 0.4f, .a = 0.005f, .d = 0.1f, .s = 0.6f, .r = 0.05f });
        }
    }

    static const struct { int m; float beats; } lead[] = {
        {76, 1.0f}, {72, 0.5f}, {74, 0.5f}, {69, 1.0f}, {71, 1.0f},
        {72, 1.0f}, {76, 1.0f}, {74, 1.0f}, {71, 1.0f},
        {67, 1.0f}, {71, 1.0f}, {74, 1.0f}, {71, 1.0f},
        {69, 0.5f}, {71, 0.5f}, {72, 1.0f}, {74, 1.0f}, {76, 1.0f},
        {77, 1.0f}, {76, 1.0f}, {74, 1.0f}, {72, 1.0f},
        {69, 1.0f}, {72, 1.0f}, {77, 0.5f}, {76, 0.5f}, {74, 1.0f},
        {76, 1.5f}, {74, 0.5f}, {71, 1.0f}, {68, 1.0f},
        {69, 2.0f}, {64, 1.0f}, {69, 1.0f},
    };
    double cur = 0.0;
    for (size_t i = 0; i < sizeof lead / sizeof lead[0]; i++) {
        double len = lead[i].beats * beat;
        render_tone(mel, n, true, &(Tone){ .start = cur * beat, .dur = len * 0.85, .f0 = mhz(lead[i].m), .f1 = mhz(lead[i].m),
            .wave = WAVE_SQUARE, .duty = 0.25f, .vol = 0.22f, .a = 0.01f, .d = 0.12f, .s = 0.5f, .r = 0.08f,
            .lp0 = 3500, .lp1 = 3500 });
        cur += lead[i].beats;
    }
    add_echo(out, mel, tmp, n, (int)(0.75 * beat * AUDIO_SR));

    for (int e = 0; e < 64; e++) {
        render_tone(out, n, true, &(Tone){ .start = e * beat * 0.5, .dur = 0.03, .f0 = 9000, .f1 = 9000,
            .wave = WAVE_NOISE, .vol = (e & 1) ? 0.09f : 0.04f, .a = 0.0f, .d = 0.02f, .s = 0.0f, .r = 0.01f });
    }

    static const double drips[4] = { 6.5, 18.5, 26.5, 30.5 };
    for (int i = 0; i < 4; i++) {
        render_tone(out, n, true, &(Tone){ .start = drips[i] * beat, .dur = 0.1, .f0 = 1900, .f1 = 1100,
            .wave = WAVE_SINE, .vol = 0.16f, .a = 0.002f, .d = 0.05f, .s = 0.1f, .r = 0.04f });
    }

    free(mel);
    free(tmp);

    float mx = 0.0f;
    for (int i = 0; i < n; i++) if (fabsf(out[i]) > mx) mx = fabsf(out[i]);
    if (mx > 1e-5f) for (int i = 0; i < n; i++) out[i] *= MUSIC_PEAK / mx;

    *out_len = n;
    return out;
}

/* GAME: calm and focused, 96 BPM, 8 bars. Steady eighth-note bass pulse with a
 * kick on each beat, pentatonic sixteenth arpeggio, light offbeat hats. */
static float *build_game_loop(int *out_len) {
    const double beat = 60.0 / 96.0;
    const int n = (int)lround(32.0 * beat * AUDIO_SR);
    float *out = (float *)calloc((size_t)n, sizeof(float));
    if (!out) return NULL;

    static const int roots[8] = { 33, 31, 29, 31, 33, 38, 28, 33 };
    static const int arp[8] = { 69, 72, 76, 79, 81, 79, 76, 72 };

    for (int e = 0; e < 64; e++) {
        int bar = e / 8;
        int p = roots[bar] + ((e & 1) ? 12 : 0);
        render_tone(out, n, true, &(Tone){ .start = e * beat * 0.5, .dur = beat * 0.375, .f0 = mhz(p), .f1 = mhz(p),
            .wave = WAVE_TRI, .vol = 0.3f, .a = 0.003f, .d = 0.05f, .s = 0.7f, .r = 0.03f });
    }

    for (int sx = 0; sx < 128; sx++) {
        int bar = sx / 16;
        int p = arp[sx % 8] + (bar >= 4 ? -12 : 0);
        render_tone(out, n, true, &(Tone){ .start = sx * beat * 0.25, .dur = beat * 0.175, .f0 = mhz(p), .f1 = mhz(p),
            .wave = WAVE_SQUARE, .duty = 0.5f, .vol = 0.12f, .a = 0.003f, .d = 0.03f, .s = 0.3f, .r = 0.02f,
            .lp0 = 3000, .lp1 = 3000 });
    }

    for (int q = 0; q < 32; q++) {
        render_tone(out, n, true, &(Tone){ .start = q * beat, .dur = 0.07, .f0 = 140, .f1 = 50,
            .wave = WAVE_SINE, .vol = 0.3f, .a = 0.001f, .d = 0.05f, .s = 0.0f, .r = 0.01f });
        if (q & 1)
            render_tone(out, n, true, &(Tone){ .start = q * beat, .dur = 0.04, .f0 = 8000, .f1 = 8000,
                .wave = WAVE_NOISE, .vol = 0.07f, .a = 0.0f, .d = 0.03f, .s = 0.0f, .r = 0.01f });
    }

    float mx = 0.0f;
    for (int i = 0; i < n; i++) if (fabsf(out[i]) > mx) mx = fabsf(out[i]);
    if (mx > 1e-5f) for (int i = 0; i < n; i++) out[i] *= MUSIC_PEAK / mx;

    *out_len = n;
    return out;
}

/* Fill out[] with the next frames of the mixed loop slots (mono 16-bit). */
static void music_fill(short *out, int frames) {
    const float step = 1.0f / (MUSIC_FADE_S * AUDIO_SR);
    for (int i = 0; i < frames; i++) {
        float v = 0.0f;
        for (int k = 0; k < 2; k++) {
            MusicSlot *s = &s_slot[k];
            if (s->gain < s->target) {
                s->gain += step;
                if (s->gain > s->target) s->gain = s->target;
            } else if (s->gain > s->target) {
                s->gain -= step;
                if (s->gain < s->target) s->gain = s->target;
            }
            if (s->gain <= 0.0f && s->target <= 0.0f) {
                s->gain = 0.0f;
                s->track = BGM_NONE;
                continue;
            }
            if (s->track <= BGM_NONE || s->track >= BGM_COUNT) continue;
            const float *loop = s_loop[s->track];
            int len = s_loop_len[s->track];
            if (!loop || len <= 0) continue;
            int idx = (int)s->pos;
            if (idx >= len) idx = len - 1;
            v += loop[idx] * s->gain;
            s->pos += 1.0;
            if (s->pos >= len) s->pos -= len;
        }
        v = clampf(v * s_music_vol, -1.0f, 1.0f);
        out[i] = (short)lrintf(v * 32767.0f);
    }
}

/* At most one refill per sub-buffer per frame: a device that drains instantly
 * (e.g. a null backend) must not be able to keep this loop spinning. */
static void music_refill(void) {
    for (int i = 0; i < 2 && IsAudioStreamProcessed(s_stream); i++) {
        music_fill(s_mix, MIX_FRAMES);
        UpdateAudioStream(s_stream, s_mix, MIX_FRAMES);
    }
}

void audio_init(void) {
    if (s_inited) return;
    s_inited = true;

    InitAudioDevice();
    if (!IsAudioDeviceReady()) {
        s_ready = false;
        return;
    }
    s_ready = true;

    for (int id = 0; id < SFX_COUNT; id++) {
        s_sfx_src[id] = build_sfx((SfxId)id);
        if (!IsSoundValid(s_sfx_src[id])) continue;
        for (int k = 0; k < SFX_VOICES; k++) {
            s_sfx_alias[id][k] = LoadSoundAlias(s_sfx_src[id]);
            if (IsSoundValid(s_sfx_alias[id][k])) SetSoundVolume(s_sfx_alias[id][k], s_sfx_vol);
        }
    }

    s_loop[BGM_MENU] = build_menu_loop(&s_loop_len[BGM_MENU]);
    s_loop[BGM_GAME] = build_game_loop(&s_loop_len[BGM_GAME]);

    SetAudioStreamBufferSizeDefault(MIX_FRAMES);
    s_stream = LoadAudioStream(AUDIO_SR, 16, 1);
    if (s_stream.buffer) {
        /* prime both sub-buffers so playback starts without an underrun */
        music_fill(s_mix, MIX_FRAMES);
        UpdateAudioStream(s_stream, s_mix, MIX_FRAMES);
        music_fill(s_mix, MIX_FRAMES);
        UpdateAudioStream(s_stream, s_mix, MIX_FRAMES);
        PlayAudioStream(s_stream);
    }
}

void audio_shutdown(void) {
    if (!s_inited) return;

    if (s_ready) {
        if (s_stream.buffer) {
            StopAudioStream(s_stream);
            UnloadAudioStream(s_stream);
        }
        for (int id = 0; id < SFX_COUNT; id++) {
            for (int k = 0; k < SFX_VOICES; k++) {
                if (IsSoundValid(s_sfx_alias[id][k])) UnloadSoundAlias(s_sfx_alias[id][k]);
            }
        }
        for (int id = 0; id < SFX_COUNT; id++) {
            if (IsSoundValid(s_sfx_src[id])) UnloadSound(s_sfx_src[id]);
        }
        CloseAudioDevice();
    }

    for (int i = 0; i < BGM_COUNT; i++) {
        free(s_loop[i]);
        s_loop[i] = NULL;
        s_loop_len[i] = 0;
    }
    memset(s_sfx_src, 0, sizeof s_sfx_src);
    memset(s_sfx_alias, 0, sizeof s_sfx_alias);
    memset(s_sfx_rr, 0, sizeof s_sfx_rr);
    memset(s_slot, 0, sizeof s_slot);
    memset(&s_stream, 0, sizeof s_stream);
    s_cur = BGM_NONE;
    s_inited = false;
    s_ready = false;
}

void audio_update(void) {
    if (!s_ready || !s_stream.buffer) return;
    music_refill();
}

void audio_apply_settings(const Settings *s) {
    if (!s) return;
    s_sfx_vol = clampf(s->sfx_muted ? 0.0f : s->sfx_volume, 0.0f, 1.0f);
    s_music_vol = clampf(s->bgm_muted ? 0.0f : s->bgm_volume, 0.0f, 1.0f);
    if (!s_ready) return;
    for (int id = 0; id < SFX_COUNT; id++) {
        for (int k = 0; k < SFX_VOICES; k++) {
            if (IsSoundValid(s_sfx_alias[id][k])) SetSoundVolume(s_sfx_alias[id][k], s_sfx_vol);
        }
    }
}

void audio_play(SfxId id) {
    if (!s_ready || (int)id < 0 || (int)id >= SFX_COUNT) return;
    if (s_sfx_vol <= 0.0f) return;
    int k = s_sfx_rr[id];
    s_sfx_rr[id] = (k + 1) % SFX_VOICES;
    if (IsSoundValid(s_sfx_alias[id][k])) PlaySound(s_sfx_alias[id][k]);
    else if (IsSoundValid(s_sfx_src[id])) PlaySound(s_sfx_src[id]);
}

void audio_music(BgmId id) {
    if (!s_ready || (int)id < 0 || (int)id >= BGM_COUNT) return;
    if (id == s_cur) return;
    s_cur = id;

    if (id == BGM_NONE) {
        s_slot[0].target = 0.0f;
        s_slot[1].target = 0.0f;
        return;
    }
    /* reuse the quieter slot for the new track; the louder one fades out */
    int pick = (s_slot[0].gain <= s_slot[1].gain) ? 0 : 1;
    s_slot[pick].track = id;
    s_slot[pick].pos = 0.0;
    s_slot[pick].target = 1.0f;
    s_slot[1 - pick].target = 0.0f;
}
