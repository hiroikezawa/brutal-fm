/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* The UI on the simulated FM-1 (web/emu/bfm_sim.c): every page drawn (PNGs in build/ui/ for a look), the page
 * buttons, the knobs on their parameters, the A / B target, ALGORITHM, PRESETS, SAVE to flash and back, the ARP
 * shortcuts, the octave keys, the LEDs. */
#include <stdio.h>
#include <stdlib.h>
#include "../web/emu/bfm_sim.c"

static int t_fail, t_count;
#define CHECK(cond, ...) do { t_count++; if (cond) { printf("  ok    "); } else { t_fail++; printf("  FAIL  "); } \
    printf(__VA_ARGS__); printf("\n"); } while (0)

static void run_ms(uint32_t ms) { while (ms) { uint32_t k = ms > 50u ? 50u : ms; sim_render(k * 441u / 10u); ms -= k; } }
static void tap(uint32_t b) { sim_button(b, 1); run_ms(30); sim_button(b, 0); run_ms(30); }

/* the screen as a PPM (converted to PNG by the runner when Pillow is there) */
static void shot(const char *dir, const char *name)
{
    char path[256];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = sim_fb[i];
        uint8_t px[3] = {(uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63u) << 2 | ((c >> 9) & 3u)),
                         (uint8_t)((c & 31u) << 3 | ((c >> 2) & 7u))};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static uint32_t colours(void)
{
    static uint8_t seen[65536 / 8];
    uint32_t i, n = 0;
    memset(seen, 0, sizeof seen);
    for (i = 0; i < 240u * 240u; i++)
        if (!(seen[sim_fb[i] >> 3] & (1u << (sim_fb[i] & 7u)))) {
            seen[sim_fb[i] >> 3] |= (uint8_t)(1u << (sim_fb[i] & 7u));
            n++;
        }
    return n;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/ui";
    uint32_t p, i, ok;
    char name[64];
    sim_nor_erase();
    sim_boot();
    run_ms(100);
    printf("== boot and every page\n");
    CHECK(ui.page == 0 && colours() > 10, "boots on PERFORM, drawn (%u colours)", colours());
    for (p = 0; p < NPAGES; p++) {
        ui_page_to(p);
        run_ms(40);
        snprintf(name, sizeof name, "%02u_%s", p, PAGES[p].title);
        for (char *c = name; *c; c++)
            if (*c == ' ') *c = '_';
        shot(dir, name);
    }
    CHECK(1, "%u pages drawn to %s", (unsigned)NPAGES, dir);
    printf("== page buttons\n");
    memset(ui.sel_page, 0, sizeof ui.sel_page);           /* (the page walk above visited every page) */
    ui_page_to(0);
    tap(B_ARP);
    CHECK(PAGES[ui.page].group == G_ARP && ui.page == 1, "ARP: ARP GENERATE");
    tap(B_ARP);
    CHECK(ui.page == 2, "ARP again: the next ARP page (ARP TIME)");
    tap(B_EDIT);
    CHECK(PAGES[ui.page].group == G_EDIT, "EDIT: the operator pages");
    for (i = 0, ok = 1; i < 10u; i++) { uint32_t was = ui.page; tap(B_EDIT); ok &= ui.page != was; }
    CHECK(ok && PAGES[ui.page].group == G_EDIT, "EDIT cycles its 10 pages and comes round");
    tap(B_ARP);
    CHECK(ui.page == 2, "ARP returns to the ARP page it was on");
    tap(B_HOME);
    CHECK(ui.page == 0, "HOME: PERFORM");
    printf("== knobs\n");
    {
        uint32_t m = bfm_p.morph;
        for (i = 0; i < 5u; i++)
            sim_encoder(EN_K1, 1);
        CHECK(bfm_p.morph == m + 5, "KNOB 1 on PERFORM: five detents, MORPH +5");
        sim_encoder(EN_K1, 3);
        CHECK(bfm_p.morph == m + 11, "a fast turn (3 detents in a frame) moves twice as far: +6");
        sim_encoder(EN_K2, -200);
        CHECK(bfm_p.intensity == 0, "KNOB 2 down past the end: INTENSITY stops at 0");
        sim_encoder(EN_K2, 300);
        CHECK(bfm_p.intensity == 127, "..and at 127");
    }
    ui_page_to(7);                                         /* OP2 */
    {
        uint32_t a = bfm_p.s[0].ratio[1], b = bfm_p.s[1].ratio[1];
        sim_encoder(EN_K1, 1);
        CHECK(bfm_p.s[0].ratio[1] == a + 1 && bfm_p.s[1].ratio[1] == b, "OP2 RATIO edits sound A only");
        tap(B_SCL);
        sim_encoder(EN_K1, 1);
        CHECK(ui.target == 1 && bfm_p.s[1].ratio[1] == b + 1 && bfm_p.s[0].ratio[1] == a + 1, "SCL: sound B");
        tap(B_SCL);
        sim_encoder(EN_K3, -3);
        CHECK(ui.target == 2 && bfm_p.s[0].level[1] == bfm_p.s[1].level[1], "SCL again: A + B together");
        tap(B_SCL);
    }
    ui_page_to(10);                                        /* OP4 MODULATES */
    {
        uint32_t i0, m0 = bfm_p.s[0].mat[3 * 4 + 2];
        for (i0 = 0; i0 < m0 + 5u; i0++)
            sim_encoder(EN_K3, -1);                         /* OP4 > OP3 down through zero */
        CHECK(bfm_p.s[0].mat[14] == 5 && (bfm_p.s[0].neg[1] & (1u << 6)) && prm_get(P_MAT + 2) == -5,
              "a matrix knob turned down through zero: -5 (polarity bit set)");
        sim_encoder(EN_ALGO, 1);
        sim_encoder(EN_ALGO, -1);
        CHECK(!bfm_p.s[0].neg[0] && !bfm_p.s[0].neg[1], "ALGORITHM writes a positive template");
    }
    sim_encoder(EN_ALGO, 3);
    CHECK(bfm_p.algo == (BFM_FACTORY[0].algo + 3) % BFM_NALGO &&
          !memcmp(bfm_p.s[0].mat, BFM_ALGO_MAT[bfm_p.algo], 16), "ALGORITHM writes its template into the matrix");
    printf("== presets and flash\n");
    sim_encoder(EN_PRESET, 4);
    run_ms(300);
    CHECK(ui.preset_cur == 4 && !strcmp(bfm_p.name, BFM_FACTORY[4].name), "PRESETS +4, then a rest: F05 %s loads",
          BFM_FACTORY[4].name);
    bfm_p.morph = 77;
    tap(B_SAVE);
    CHECK(PAGES[ui.page].group == G_SAVE, "SAVE: the SAVE page");
    sim_encoder(EN_K1, 2);                                  /* slot U03 */
    ui.save_name[0] = 'X';
    tap(B_SAVE);
    run_ms(100);
    CHECK(ui.preset_cur == BFM_NFACTORY + 2, "SAVE again: written to U03");
    sim_encoder(EN_PRESET, -10);
    run_ms(300);
    CHECK(bfm_p.morph != 77, "another preset loaded");
    ui.preset_cur = 0;
    ui_preset_load(BFM_NFACTORY + 2);
    run_ms(50);
    CHECK(bfm_p.morph == 77 && bfm_p.name[0] == 'X', "U03 back from flash: MORPH 77, its name");
    {   /* the same flash after a "power cycle" */
        sim_boot();
        run_ms(50);
        CHECK(bfm_p.morph == 77 && ui.preset_cur == BFM_NFACTORY + 2, "a reboot comes back to U03 (the last session)");
    }
    {   /* a torn write: the newer copy damaged, the older one still loads */
        uint32_t off = 0xC0000u, k;
        for (k = 0; k < 2u; k++) {
            st_hdr_t h;
            st_read(off + k * 0x1000u, &h, sizeof h);
        }
        sim_nor[0xC0000u + 300u] ^= 0xFFu;                 /* corrupt copy A's payload */
        sim_nor[0xC1000u + 300u] ^= 0xFFu;                 /* and copy B's */
        CHECK(ui_preset_fetch(BFM_NFACTORY + 2, &bfm_stage) != 0, "a bank whose CRC fails is never loaded (EMPTY)");
    }
    printf("== ARP from the panel\n");
    sim_boot();
    ui_preset_load(0);
    run_ms(50);
    tap(B_PLAY);
    CHECK(bfm_p.arp.on, "PLAY: ARP on");
    sim_key(7, 1);                                         /* C4 */
    sim_key(11, 1);                                        /* E4 */
    sim_key(14, 1);                                        /* G4 */
    run_ms(500);
    CHECK(bfm_p.pat.npool == 3 && bfm_p.pat.pool[0] == 60 && sq.playing, "three keys: the ARP plays C4 E4 G4");
    CHECK(ui_led[NB + 7] >= LED_LIT && ui_led[NB + 11] >= LED_LIT && ui_led[NB + 14] >= LED_LIT && ui_led[NB + 8] == LED_GLOW,
          "their keys lit or mid, the others glow");
    {
        int all = 1;
        for (int i = 0; i < NB + NKEYS; i++)
            all &= ui_led[i] != LED_OFF;
        CHECK(all, "every button and key glows at least (findable on a dark stage)");
    }
    sim_button(B_ARP, 1);
    run_ms(30);
    tap(B_FX);
    sim_button(B_ARP, 0);
    run_ms(30);
    CHECK(bfm_p.arp.hold && PAGES[ui.page].group != G_ARP, "ARP held + FX: HOLD on (no page change)");
    sim_key(7, 0); sim_key(11, 0); sim_key(14, 0);
    run_ms(300);
    CHECK(sq.playing, "HOLD: the keys let go, it plays on");
    {   /* a preset change while the ARP runs changes the sound only */
        bfm_pattern_t a = bfm_p.pat;
        bfm_arpset_t s = bfm_p.arp;
        uint32_t t0 = sq.tick;
        ui_preset_load(12);
        run_ms(1000);
        CHECK(!strcmp(bfm_p.name, BFM_FACTORY[12].name) && sq.playing && !memcmp(&a, &bfm_p.pat, sizeof a) &&
              !memcmp(&s, &bfm_p.arp, sizeof s) && sq.tick > t0,
              "preset change while the ARP plays: the new sound, the same pattern and settings, it plays on");
        ui_preset_load(0);
        run_ms(300);
    }
    {
        bfm_pattern_t a = bfm_p.pat;
        tap(B_REC);
        run_ms(4000);
        CHECK(memcmp(&a, &bfm_p.pat, sizeof a) && bfm_p.pat.npool == 3, "REC: REGENERATE (at the loop)");
        sim_button(B_REC, 1);
        run_ms(700);
        sim_button(B_REC, 0);
        run_ms(50);
        CHECK(!memcmp(&a, &bfm_p.pat, sizeof a), "REC held: UNDO");
    }
    ui_page_to(5);                                         /* SEQ */
    {
        uint32_t k, bad = 0;
        for (k = 0; k < 40u; k++) {
            sim_encoder(EN_K2, (k & 1u) ? 3 : -2);
            sim_encoder(EN_K1, 1);
        }
        for (k = 0; k < 16u; k++)
            if ((bfm_p.pat.st[k].flags & ST_ON) && bfm_p.pat.st[k].note != 60 && bfm_p.pat.st[k].note != 64 &&
                bfm_p.pat.st[k].note != 67)
                bad++;
        CHECK(!bad && bfm_pat_valid(&bfm_p.pat), "SEQ: editing step notes only ever picks C4 E4 G4 (or a rest)");
        shot(dir, "zz_seq_edited");
    }
    tap(B_PLAY);
    run_ms(100);
    CHECK(!bfm_p.arp.on && !sq.playing, "PLAY again: ARP off, silent");
    printf("== octave\n");
    tap(B_OCTUP);
    sim_key(0, 1);
    run_ms(20);
    {
        uint32_t k, f4 = 0;
        for (k = 0; k < BFM_MAXV; k++)
            f4 |= bv[k].active && bv[k].gate && bv[k].note == 65;
        CHECK(ui.oct == 1 && ui_led[B_OCTUP] == 2 && f4, "OCT+: F3 plays F4");
    }
    sim_key(0, 0);
    sim_button(B_OCTDN, 1); sim_button(B_OCTUP, 1);
    run_ms(20);
    sim_button(B_OCTDN, 0); sim_button(B_OCTUP, 0);
    run_ms(20);
    CHECK(ui.oct == 0, "OCT- + OCT+: octave reset");
    printf("== MIDI Program Change\n");
    sim_midi(0xC0, 9, 0);
    run_ms(50);
    CHECK(ui.preset_cur == 9 && !strcmp(bfm_p.name, BFM_FACTORY[9].name), "Program Change 9: F10 %s",
          BFM_FACTORY[9].name);
    printf("ui: %d checks, %d failed\n", t_count, t_fail);
    return t_fail != 0;
}
