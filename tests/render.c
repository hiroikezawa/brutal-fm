/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* Renders every factory preset to a WAV and prints what it sounds like in numbers:
 *   tests/render OUTDIR [preset]
 * Each preset plays a phrase (a low note, a high note, a chord, a held note while MORPH sweeps 0 -> 127);
 * ARP presets play a held chord through the COMPLEX ARP instead. Also writes demo WAVs of the ARP (8 / 16
 * steps, REGENERATE, the locks) and a MORPH sweep. The figures: peak and RMS (dBFS), DC, spectral centroid,
 * spectral flatness (0 tonal .. 1 noise), and the share of energy above 16 kHz (aliasing shows up there). */
#include "bfm_host.h"
#include <sys/stat.h>

#define SR BFM_FS
static int16_t W[2 * SR * 12];

typedef struct { double peak_db, rms_db, dc, centroid, flat, hf; } stats_t;

static void fft(double *re, double *im, int n)
{
    int i, j, k, len;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len, wr = cos(a), wi = sin(a);
        for (i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                double t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

static stats_t analyse(const int16_t *s, uint32_t frames)
{
    enum { N = 4096 };
    static double re[N], im[N], mag[N / 2];
    stats_t st = {0};
    double sum = 0, sq = 0, pk = 0, cnum = 0, cden = 0, lsum = 0, hf = 0;
    uint32_t i, w, nw = 0;
    for (i = 0; i < frames; i++) {
        double v = (s[2 * i] + s[2 * i + 1]) / 65536.0;
        sum += v;
        sq += v * v;
        if (fabs(v) > pk) pk = fabs(v);
    }
    for (i = 0; i < N / 2; i++)
        mag[i] = 0;
    for (w = 0; w + N <= frames; w += N / 2, nw++)
        for (i = 0; i < N; i++) {
            double h = 0.5 - 0.5 * cos(2 * M_PI * i / (N - 1));
            re[i] = (s[2 * (w + i)] + s[2 * (w + i) + 1]) / 65536.0 * h;
            im[i] = 0;
        }
    /* average the spectra of all windows */
    for (w = 0, nw = 0; w + N <= frames; w += N / 2, nw++) {
        for (i = 0; i < N; i++) {
            double h = 0.5 - 0.5 * cos(2 * M_PI * i / (N - 1));
            re[i] = (s[2 * (w + i)] + s[2 * (w + i) + 1]) / 65536.0 * h;
            im[i] = 0;
        }
        fft(re, im, N);
        for (i = 1; i < N / 2; i++)
            mag[i] += re[i] * re[i] + im[i] * im[i];
    }
    for (i = 1; i < N / 2; i++) {
        double f = (double)i * SR / N, p = mag[i] + 1e-20;
        cnum += f * p;
        cden += p;
        lsum += log(p);
        if (f > 16000)
            hf += p;
    }
    st.peak_db = 20 * log10(pk + 1e-12);
    st.rms_db = 10 * log10(sq / frames + 1e-20);
    st.dc = sum / frames;
    st.centroid = cnum / cden;
    st.flat = exp(lsum / (N / 2 - 1)) / (cden / (N / 2 - 1));
    st.hf = 10 * log10(hf / cden + 1e-20);
    return st;
}

static uint32_t phrase(uint32_t p)              /* the test phrase of a preset; returns frames */
{
    uint32_t f = 0;
    host_load(p);
    f += 64;
    host_render(W, 64);
    if (BFM_FACTORY[p].arp.on) {
        static const uint8_t ch[4] = {48, 51, 55, 58};
        uint32_t i;
        for (i = 0; i < 4; i++)
            bfm_key_push(ch[i], 100, 1);
        f += host_render(W + 2 * f, SR * 4), SR * 4;
        for (i = 0; i < 4; i++)
            bfm_key_push(ch[i], 0, 0);
        host_render(W + 2 * f, SR * 1);
        return f + SR;
    }
    bfm_key_push(36, 110, 1);
    host_render(W + 2 * f, SR); f += SR;
    bfm_key_push(36, 0, 0);
    host_render(W + 2 * f, SR / 2); f += SR / 2;
    bfm_key_push(60, 80, 1);
    host_render(W + 2 * f, SR); f += SR;
    bfm_key_push(60, 0, 0);
    host_render(W + 2 * f, SR / 2); f += SR / 2;
    bfm_key_push(48, 100, 1); bfm_key_push(55, 100, 1); bfm_key_push(63, 100, 1);
    host_render(W + 2 * f, SR * 3 / 2); f += SR * 3 / 2;
    bfm_key_push(48, 0, 0); bfm_key_push(55, 0, 0); bfm_key_push(63, 0, 0);
    host_render(W + 2 * f, SR); f += SR;
    {   /* MORPH sweep on a held note */
        uint32_t k;
        bfm_key_push(43, 100, 1);
        for (k = 0; k < 128; k++) {
            bfm_p.morph = (uint8_t)k;
            host_render(W + 2 * f, SR * 3 / 128); f += SR * 3 / 128;
        }
        bfm_key_push(43, 0, 0);
        host_render(W + 2 * f, SR); f += SR;
        bfm_p.morph = BFM_FACTORY[p].morph;
    }
    return f;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/wav";
    int only = argc > 2 ? atoi(argv[2]) : -1;
    uint32_t p;
    char path[512];
    mkdir(dir, 0755);
    host_init();
    printf("%-3s %-12s %-6s %7s %7s %8s %9s %6s %7s\n", "#", "NAME", "MODE", "PEAK", "RMS", "DC", "CENTROID", "FLAT",
           ">16k");
    for (p = 0; p < BFM_NFACTORY; p++) {
        uint32_t fr;
        stats_t st;
        if (only >= 0 && (int)p != only)
            continue;
        host_init();
        fr = phrase(p);
        st = analyse(W, fr);
        snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, p + 1, BFM_FACTORY[p].name);
        for (char *c = path + strlen(dir) + 1; *c; c++)
            if (*c == ' ')
                *c = '_';
        wav_write(path, W, fr);
        printf("%-3u %-12s %-6s %7.1f %7.1f %8.4f %9.0f %6.3f %7.1f\n", p + 1, BFM_FACTORY[p].name,
               (const char *[]){"METAL", "GROWL", "CHAOS", "NOISE"}[BFM_FACTORY[p].mode], st.peak_db, st.rms_db,
               st.dc, st.centroid, st.flat, st.hf);
    }
    return 0;
}
