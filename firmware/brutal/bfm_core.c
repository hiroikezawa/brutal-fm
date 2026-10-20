/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* The portable core of BRUTAL FM: data, generated tables, the ARP generator, the DSP, the runtime. No HAL:
 * the firmware (brutal.c), the browser emulator (web/emu/brutal_web.c) and the host tests include this one
 * file, in this order. The includer provides memset / memcpy and RING_PUBLISH. */
#ifndef BFM_POOL_SECTION
#define BFM_POOL_SECTION __attribute__((section(".pool")))
#endif
#include "bfm_types.h"
#include "bfm_tables.h"          /* build/gen: tools/gen_bfm_tables.py */
#include "bfm_factory.h"         /* build/gen: tools/gen_bfm_factory.py */
#include "bfm_arp.c"
#include "bfm_dsp.c"
#include "bfm_seq.c"
