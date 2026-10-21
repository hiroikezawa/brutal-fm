/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* BRUTAL FM firmware: one compilation unit, as Felucca builds itself (the HAL is header-only; each file uses
 * what the files above it define, so the order matters).
 *   firmware/hal/       Felucca's FM-1 hardware layer (GPL-3.0, unchanged)
 *   firmware/drivers/   Felucca's LCD, canvas, USB, TRS MIDI, flash store, update entry (GPL-3.0; storage.c
 *                       with its own magic and map)
 *   firmware/brutal/    BRUTAL FM: the engine, the COMPLEX ARP, presets, the UI, the main loop */

/* ---------------------------------------------------------- build options (tools/build.py may override) --- */
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1          /* settings and user presets in flash */
#endif
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1            /* the M-UPGRADE update entry (the way to another firmware without UBOOT) */
#endif
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_CDC 1            /* the USB layout Felucca ships and tested (audio + MIDI + a CDC port); BRUTAL FM
                                 * has no console: what the host types is dropped (bfm_main.c) */
#ifndef FELUCCA_UAC
#define FELUCCA_UAC 1            /* USB audio: the output, 16-bit stereo, to a computer */
#endif
#define FELUCCA_UART 1           /* TRS MIDI IN */
#define FELUCCA_CPU2 0           /* one core: the second core's start-up stays Felucca's beta, not used here */
#define FM1_CPU1_USED 0
#define FM1_CPU1_HALT 1          /* ..but every reset (and our own boot) holds core 1 (hal/fm1_sys.h fm1_cpu1_halt,
                                  * registers only): a firmware before this one may have left it running, and its
                                  * waiting loop lives in the RAM this boot clears */
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_700"    /* package identity (build.py): BRUTAL FM's own number range, 7XY */
#endif
#ifndef BFM_VERSION
#define BFM_VERSION "v0.1.1"
#endif
#define BFM_FLASH FELUCCA_FLASH
#if FELUCCA_OTA && !FELUCCA_FLASH
#error "FELUCCA_OTA needs FELUCCA_FLASH"
#endif

#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_audio.h"
#include "fm1_adc.h"
#include "fm1_lcd_hw.h"

#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "libc.c"
#include "lcd.c"
#include "gfx.c"

#include "bfm_core.c"            /* the engine, the ARP, the runtime (portable) */

/* the I2S half buffer (usb.c sizes its audio ring by it; storage_hw.c silences it around a flash erase) */
#define HALF_FRAMES 128u
#define HALF_WORDS (HALF_FRAMES * 2u)
#include "usb.c"
#include "midi_uart.c"

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
#define BOOTGUARD_MAGIC 0x47425442u           /* "BTBG" */
#if FELUCCA_FLASH
#include "fm1_flash.h"
#include "storage_hw.c"
#include "storage.c"
#else
static uint8_t flash_ok;
#endif
#include "bfm_store.c"
#include "bfm_ui.c"
#if FELUCCA_OTA
#include "ota.c"
#include "ota_hw.c"
#endif
#include "bfm_main.c"
