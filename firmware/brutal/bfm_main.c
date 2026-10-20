/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * BRUTAL FM changes (C) 2026 Hiro Ikezawa */
/* BRUTAL FM boot, interrupts and main loop on the FM-1. The structure is Felucca's (src/main.c, src/audio.c):
 * the same boot order (watchdog, boot-loop guard, fault vectors, guards; LCD, input on TIMER5 at 10 kHz,
 * audio on ALNK0), the same TIMER5 nesting into the audio render, the same UBOOT and update entries, so the
 * ways back to another firmware are Felucca's, unchanged:
 *   - OCT- + OCT+ held 5 s: UBOOT (the chip's own update mode)
 *   - SysEx F0 22 24 35 7D F7: UBOOT
 *   - the M-UPGRADE update protocol (ota.c): the web installer, tools/fm1_install.py, M-VAVE's updater
 * What runs is BRUTAL FM's: bfm_block renders the audio, ui_* reads the panel and draws. */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];


/* panel: Felucca's measured default mapping of the printed labels to the matrix (src/panel.c PANEL_DEFAULT) */
static const uint8_t PANEL_BTN[NB] = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1};
static const uint8_t PANEL_ENC[NE] = {0, 1, 6, 2, 3, 4, 5};

/* diagnostics kept across resets (read with fm1t memr, as Felucca's) */
#define DBG_MAGIC 0x31474442u                 /* "BDG1" */
struct bfm_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames, last_us, cpu_q8, boots, sheds;
} bfm_dbg __attribute__((section(".noinit")));
static volatile uint32_t t5_nested_ticks;

/* ------------------------------------------------------------ TIMER5 --- */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed, last, acc;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    bfm_dbg.timer_irqs++;
    fm1_input_tick();
    acc += t0 - last;
    last = t0;
    while (acc >= 1000u * FM1_TICKS_PER_US) {
        acc -= 1000u * FM1_TICKS_PER_US;
        fm1_ms++;
    }
    if (usb_due)
        owed |= 1u;
    if (sub % 5u == 2u)
        owed |= 2u;
    if (++sub == 10u)
        sub = 0;
    if (bfm_dbg.in_audio) {                   /* nested in the render: only the scan (and USB audio) */
        bfm_dbg.nested++;
#if FELUCCA_UAC
        if (usb_due)
            uac_service();
#endif
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
    if (owed & 2u)
        uart_midi_poll();
    owed = 0;
}
extern void isr_timer5(void);
static void timer5_start(void) { fm1_timer5_start(isr_timer5, 4); }   /* above ALNK0 (3): the scan nests */

/* ------------------------------------------------------------- audio --- */
#define OUT_SHIFT 7                           /* Q15 -> 24 bit, -6 dBFS ceiling (as Felucca) */
#define CPU_AVG (4096u / HALF_FRAMES)
#define SHED_TICKS ((HALF_FRAMES * 1000000u / BFM_FS) * 85u / 100u * FM1_TICKS_PER_US)
static uint8_t shed_over;
static uint32_t audio_cpu_rem;

/* overload: two halves in a row above 85 %: the quietest released voice goes, else the oldest */
static void bfm_shed(void)
{
    uint32_t i;
    bfm_voice_t *best = 0;
    for (i = 0; i < BFM_MAXV; i++)
        if (bv[i].active && !bv[i].gate && (!best || bv[i].age < best->age))
            best = &bv[i];
    if (!best)
        for (i = 0; i < BFM_MAXV; i++)
            if (bv[i].active && (!best || bv[i].age < best->age))
                best = &bv[i];
    if (best) {
        best->kill = 1;
        bfm_dbg.sheds++;
    }
}

static void midi_drain(void)                  /* USB and TRS MIDI in: one ring (usb.c, midi_uart.c) */
{
    if (midi_in_overflow) {                   /* a broken stream: drop it and release every note */
        mi_r = mi_w;
        sq_stop();
        bfm_all_off();
        set_clear(sq.phys);
        midi_in_overflow = 0;
    }
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ];
        mi_r++;
        sq_midi(pkt);
    }
}

void fm1_alnk0_irq(void)
{
    uint8_t p;
    uint32_t t0;
    bfm_dbg.in_audio = 1;
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us, all, i;
        int32_t *o = &abuf[half * HALF_WORDS];
        if ((shed_over & 3u) == 3u) {
            shed_over = 0;
            bfm_shed();
        }
#if FELUCCA_UAC
        uac_render_start();
#endif
        for (b = 0; b < HALF_FRAMES; b += BFM_CTL) {
            int32_t *x = o + 2u * b;
            midi_drain();
            bfm_block(x, BFM_CTL);
#if FELUCCA_UAC
            uac_tap(x, BFM_CTL);
#endif
            ui_scope[ui_scope_w++ & 127u] = (uint16_t)x[0];
            for (i = 0; i < 2u * BFM_CTL; i++)
                x[i] *= 1 << OUT_SHIFT;
        }
        fm1_audio_ack_half();
        all = fm1_ticks() - t0;
        shed_over = (uint8_t)(shed_over << 1 | (all > SHED_TICKS));
        us = (all - t5_nested_ticks) / FM1_TICKS_PER_US;
        {
            uint32_t load = bfm_dbg.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / BFM_FS) +
                            audio_cpu_rem;
            bfm_dbg.cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half)
            bfm_dbg.late++;
        bfm_dbg.halves++;
        bfm_dbg.last_us = us;
        if (us > bfm_dbg.max_us)
            bfm_dbg.max_us = us;
    }
    bfm_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}

/* ------------------------------------------------------------- crash --- */
static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}
static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, 0xF800u);
    draw_text_box(0, 20, 240, &AF_M, "BRUTAL FM CRASH", 0xFFFFu, 1);
    hexs(b, c->vec);
    draw_text_box(10, 60, 220, &AF_M, b, 0xFFFFu, 0);
    hexs(b, c->pc);
    draw_text_box(10, 84, 220, &AF_M, b, 0xFFFFu, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* ------------------------------------------------------------ platform --- */
static uint32_t ui_now(void) { return fm1_ms; }

static void plat_leds(void)                  /* ui_led[] -> the matrix LEDs: lit, the dim glow, off */
{
    uint32_t i, p, r;
    uint8_t lit[FM1_NCOL] = {0}, dim[FM1_NCOL] = {0};
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? PANEL_BTN[i] : 14u + (i - NB);
        if (!ui_led[i])
            continue;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id) {
                    if (ui_led[i] == 2u)
                        lit[p] |= (uint8_t)(1u << r);
                    else
                        dim[p] |= (uint8_t)(1u << r);
                }
    }
    for (p = 0; p < FM1_NCOL; p++) {
        fm1_led[p] = lit[p];
        fm1_led_dim[p] = dim[p];
    }
}

static void plat_input(void)                 /* the panel -> the UI */
{
    static uint32_t notes_prev;
    uint32_t rel, pr = fm1_input_edges(&rel), i, notes = fm1_in.notes;
    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << PANEL_BTN[i];
        if (pr & bit)
            ui_button(i, 1);
        if (rel & bit)
            ui_button(i, 0);
    }
    for (i = 0; i < NE; i++)
        ui_encoder(i, fm1_enc_take(PANEL_ENC[i]));
    for (i = 0; i < NKEYS; i++) {
        uint32_t bit = 1u << i;
        if ((notes ^ notes_prev) & bit)
            ui_note_key(i, (notes & bit) != 0u);
    }
    notes_prev = notes;
}

static int32_t master_knob = 512 * 16;
static void master_poll(void)
{
    int32_t a = fm1_adc_read(FM1_ADC_MASTER);
    if (a >= 0) {
        uint32_t k;
        master_knob += (a * 16 - master_knob) / 8;
        k = (uint32_t)master_knob / 16u;
        bfm_master_q15 = (k * k) >> 5;       /* square law, 1023 -> ~32704 */
    }
}

static void draw_splash(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 84, 240, &AF_L, "BRUTAL FM", T_THEME, 1);
    draw_text_box(0, 124, 240, &AF_S, BFM_VERSION, T_MID, 1);
    draw_text_box(0, 200, 240, &AF_S, "ON FELUCCA'S FM-1 LAYER", T_DIM, 1);
}

static void bfm_boot_patch(void)             /* the last saved session, else F01 */
{
    uint32_t pr = 0;
    bfm_patch_t p;
    if (!store_last_load(&p, &pr)) {
        bfm_p = p;
        ui.preset_cur = (int16_t)(pr < NPRESETS ? pr : 0u);
    } else {
        bfm_p = BFM_FACTORY[0];
    }
    str_cpy(ui.save_name, bfm_p.name, BFM_NAME);
}

/* ------------------------------------------------------------- main --- */
static void fm1_main(void)
{
    uint32_t ms;
    {
        uint32_t f = irq_save();
        flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;   /* the expected 1 MiB part, else RAM only */
        irq_restore(f);
    }
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();
#endif
    bfm_settings_default(&bfm_set);
    if (flash_ok && !store_settings_load(&bfm_set))
        bfm_settings_sanitize(&bfm_set);
    bfm_dsp_init();
    bfm_seq_init();
    ui_init();
    bfm_boot_patch();
    lcd_init();
    draw_splash();
    if (bfm_dbg.magic != DBG_MAGIC) {
        memset(&bfm_dbg, 0, sizeof bfm_dbg);
        bfm_dbg.magic = DBG_MAGIC;
    }
    bfm_dbg.boots++;
    bfm_dbg.in_audio = 0;
    fm1_input_init();
    fm1_adc_init();
    (void)fm1_adc_read(FM1_ADC_MASTER);
    master_poll();
    master_knob = fm1_adc_read(FM1_ADC_MASTER) * 16;
    audio_init();
    usb_start();
    uart_midi_init();
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    for (ms = 0; ms < 600u; ms += 10u) {
        fm1_delay_ms(10);
        master_poll();
    }
    ui.dirty = 8;
    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {   /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        master_poll();
        {   /* OCT- + OCT+ held 5 s: UBOOT (a countdown from 2 s; letting go cancels it) */
            static uint32_t t0;
            uint32_t both = (1u << PANEL_BTN[B_OCTDN]) | (1u << PANEL_BTN[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (ui.uboot) {
                    ui.uboot = 0;
                    ui_popup("UPDATE MODE", "CANCELLED");
                }
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != ui.uboot) {
                    char t[4] = {(char)('0' + left), 0};
                    ui.uboot = (uint8_t)left;
                    ui_popup("UPDATE MODE IN", t);
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
                lcd_fill(0, 0, 240, 240, T_BG);
                draw_text_box(0, 110, 240, &AF_M, "UBOOT", T_THEME, 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;
                fm1_enter_uboot();
            }
        }
#if FELUCCA_OTA
        ota_service();
        if (usb.ota_req) {
            usb.ota_req = 0;
            rq_panic = 1;
            if (flash_ok)
                ota_session();                 /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, T_BG);
            ui.dirty = 8;
        }
#endif
        if (usb.uboot_req) {
            fm1_audio_stop();
            lcd_fill(0, 0, 240, 240, T_BG);
            draw_text_box(0, 110, 240, &AF_M, "UBOOT (USB)", T_THEME, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
        bfm_dbg.ui_frames++;
        ui.cpu_q8 = (uint16_t)bfm_dbg.cpu_q8;
        plat_input();
        ui_frame();
        plat_leds();
        while (fm1_ms - m < 15u)               /* ~60 frames a second; the keys on every millisecond */
            plat_input();
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {             /* two boots that died in their first 30 s: UBOOT, to be rescued */
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
