/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* A spectrogram of a 16-bit stereo WAV as a PPM (log frequency 40 Hz .. 20 kHz, 80 dB range):
 *   cc -O2 -o build/host/spectro tools/spectro.c -lm && build/host/spectro IN.wav OUT.ppm [width] */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 4096
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    int W = argc > 3 ? atoi(argv[3]) : 800, H = 256, x, y, i;
    long n;
    int16_t *s;
    static double re[N], im[N];
    uint8_t *img;
    if (!f) return 1;
    fseek(f, 0, SEEK_END); n = (ftell(f) - 44) / 4; fseek(f, 44, SEEK_SET);
    s = malloc(n * 4); fread(s, 4, n, f); fclose(f);
    img = calloc(W * H * 3, 1);
    for (x = 0; x < W; x++) {
        long c = (long)x * (n - N) / W;
        int j = 0, len, k;
        for (i = 0; i < N; i++) { re[i] = (s[2 * (c + i)] + s[2 * (c + i) + 1]) / 65536.0 * (0.5 - 0.5 * cos(2 * M_PI * i / (N - 1))); im[i] = 0; }
        for (i = 1; i < N; i++) { int bit = N >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; } }
        for (len = 2; len <= N; len <<= 1) { double a = -2 * M_PI / len, wr = cos(a), wi = sin(a);
            for (i = 0; i < N; i += len) { double cr = 1, ci = 0; for (k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k], vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } } }
        for (y = 0; y < H; y++) {
            double fr = 40 * pow(500.0, (double)(H - 1 - y) / (H - 1)), b = fr * N / 44100.0;
            int bi = (int)b; double p = re[bi] * re[bi] + im[bi] * im[bi], db = 10 * log10(p + 1e-12) - 25;
            double v = (db + 80) / 80; v = v < 0 ? 0 : v > 1 ? 1 : v;
            uint8_t *px = img + 3 * (y * W + x);
            px[0] = (uint8_t)(255 * fmin(1, v * 1.6)); px[1] = (uint8_t)(255 * fmax(0, v * 1.6 - 0.6) ); px[2] = (uint8_t)(255 * fmax(0, v - 0.75) * 3);
        }
    }
    f = fopen(argv[2], "wb"); fprintf(f, "P6\n%d %d\n255\n", W, H); fwrite(img, 1, W * H * 3, f); fclose(f);
    return 0;
}
