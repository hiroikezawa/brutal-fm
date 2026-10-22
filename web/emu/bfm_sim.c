/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa
 * The design (the device clock driven by the audio asked for, a NOR image the page keeps, exports read by an
 * AudioWorklet) follows Felucca's web/emu (Leo Kuroshita, GPL-3.0) and X0X's (charlesvestal/fm1-x0x, GPL-3.0). */
/* BRUTAL FM on a simulated FM-1: the firmware's own sources (the engine, the ARP, the presets and the flash
 * store, the UI and its drawing) with the hardware replaced:
 *   - the screen: lcd_fill / lcd_blit into a 240 x 240 RGB565 frame
 *   - the flash: a 1 MiB NOR image behind storage.c's hooks (the page keeps its sectors)
 *   - the clock: milliseconds advance with the audio rendered; the main loop's frame runs every 15 ms of it
 *   - the panel: buttons, encoders and keys by their printed labels, the LEDs as ui_led[] (0 off, 1 the glow, 3 mid, 2 lit)
 *   - MIDI: USB-MIDI packets straight into the engine (sim_midi)
 * Used by the browser emulator (web/emu/build.sh, emcc) and by the host UI tests (tests/ui_test.c, cc). */
#include <stdint.h>
#include <string.h>                      /* the host's (or Emscripten's) memset / memcpy / memcmp */
#define memset felucca_memset            /* libc.c's own (the firmware's freestanding copies): renamed away */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define BFM_POOL_SECTION
#define BFM_FLASH 1
#ifndef BFM_VERSION
#define BFM_VERSION "v0.1.3"
#endif

/* ------------------------------------------------------------- screen --- */
static uint16_t sim_fb[240 * 240];
static uint32_t sim_draws;
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            sim_fb[j * 240u + i] = c;
    sim_draws++;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            sim_fb[(y + j) * 240u + x + i] = (uint16_t)(p[j * w + i] >> 8 | p[j * w + i] << 8);   /* (the canvas
                                                                                                * holds the panel's byte order) */
    sim_draws++;
}
static void lcd_sync(void) {}
#include "gfx.c"

#include "bfm_core.c"

/* -------------------------------------------------------------- flash --- */
static uint8_t sim_nor[0x100000];
static uint32_t sim_flash_writes;
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off + n > sizeof sim_nor)
        return -1;
    memcpy(dst, sim_nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off & 0xFFFu || off + 0x1000u > sizeof sim_nor)
        return -8;
    memset(sim_nor + off, 0xFF, 0x1000);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    const uint8_t *s = src;
    if (off + n > sizeof sim_nor)
        return -8;
    for (i = 0; i < n; i++)
        sim_nor[off + i] &= s[i];                   /* NOR: programming only clears bits */
    sim_flash_writes++;
    return 0;
}
#include "storage.c"
#include "bfm_store.c"

/* -------------------------------------------------------------- the UI --- */
static uint32_t sim_ms;
static uint32_t ui_now(void) { return sim_ms; }
#include "bfm_ui.c"

/* ------------------------------------------------------------- the loop --- */
#define EXPORT __attribute__((used, visibility("default")))
static uint32_t sim_samples, sim_next_frame;
static uint32_t sim_master_q15 = 26000;
static int32_t sim_out[2 * BFM_CTL * 8];
static int16_t sim_pcm[2 * 4096];

EXPORT int sim_boot(void)
{
    uint32_t pr = 0;
    bfm_patch_t p;
    bfm_settings_default(&bfm_set);
    if (!store_settings_load(&bfm_set))
        bfm_settings_sanitize(&bfm_set);
    bfm_dsp_init();
    bfm_seq_init();
    ui_init();
    if (!store_last_load(&p, &pr)) {
        bfm_p = p;
        ui.preset_cur = (int16_t)(pr < NPRESETS ? pr : 0u);
    } else {
        bfm_p = BFM_FACTORY[0];
    }
    str_cpy(ui.save_name, bfm_p.name, BFM_NAME);
    bfm_master_q15 = sim_master_q15;
    sim_ms = 0;
    sim_samples = 0;
    sim_next_frame = 0;
    ui.dirty = 8;
    ui_frame();
    return 0;
}

/* n stereo frames (<= 4096) into sim_pcm (int16 interleaved); the main loop's frames as the clock passes */
EXPORT int16_t *sim_render(uint32_t n)
{
    uint32_t done = 0, i;
    if (n > 4096u)
        n = 4096u;
    while (done < n) {
        uint32_t k = n - done < BFM_CTL ? n - done : BFM_CTL;
        bfm_block(sim_out, k);
        for (i = 0; i < 2u * k; i++)
            sim_pcm[2u * done + i] = (int16_t)sim_out[i];
        ui_scope[ui_scope_w++ & 127u] = (uint16_t)sim_out[0];
        done += k;
        sim_samples += k;
        while (sim_samples >= 441u) {                 /* 10 ms */
            sim_samples -= 441u;
            sim_ms += 10u;
        }
        if ((int32_t)(sim_ms - sim_next_frame) >= 0) {
            sim_next_frame = sim_ms + 15u;
            ui_frame();
        }
    }
    return sim_pcm;
}

EXPORT void sim_frame(void) { ui_frame(); }
EXPORT void sim_button(uint32_t b, uint32_t down) { ui_button(b, down); }
EXPORT void sim_encoder(uint32_t e, int32_t steps) { ui_encoder(e, steps); }
EXPORT void sim_key(uint32_t k, uint32_t down) { ui_note_key(k, down); }
EXPORT void sim_master(uint32_t q15) { sim_master_q15 = bfm_master_q15 = q15 > 32767u ? 32767u : q15; }
EXPORT void sim_midi(uint32_t st, uint32_t d1, uint32_t d2)    /* a MIDI message from the page's Web MIDI input */
{
    uint32_t cin = st >= 0xF8u ? 0xFu : st >> 4;
    sq_midi(cin | st << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24);
}
EXPORT void sim_advance_ms(uint32_t ms) { sim_ms += ms; }
EXPORT uint16_t *sim_screen(void) { return sim_fb; }
EXPORT uint32_t sim_screen_draws(void) { return sim_draws; }
EXPORT uint8_t *sim_leds(void) { return ui_led; }
EXPORT uint8_t *sim_nor_ptr(void) { return sim_nor; }
EXPORT uint32_t sim_nor_size(void) { return sizeof sim_nor; }
EXPORT void sim_nor_erase(void) { memset(sim_nor, 0xFF, sizeof sim_nor); }
EXPORT uint32_t sim_flash_writes_count(void) { return sim_flash_writes; }
EXPORT uint32_t sim_sample_rate(void) { return BFM_FS; }
EXPORT uint32_t sim_now_ms(void) { return sim_ms; }

/* the browser's AudioWorklet wants two float channels */
static float sim_l[4096], sim_r[4096];
EXPORT void sim_render_f(uint32_t n)
{
    uint32_t i;
    const int16_t *p = sim_render(n);
    for (i = 0; i < n && i < 4096u; i++) {
        sim_l[i] = p[2u * i] * (1.0f / 32768.0f);
        sim_r[i] = p[2u * i + 1u] * (1.0f / 32768.0f);
    }
}
EXPORT float *sim_out_l(void) { return sim_l; }
EXPORT float *sim_out_r(void) { return sim_r; }
EXPORT uint32_t sim_cpu_meter(void) { return (uint32_t)bm.peak; }
EXPORT void sim_cpu_set(uint32_t q8) { ui.cpu_q8 = (uint16_t)q8; }
