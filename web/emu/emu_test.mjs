// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Hiro Ikezawa
// The browser emulator's wasm, driven as worklet.js drives it: boot, a key, the ARP, the screen, the LEDs, a
// preset saved and found again by a second instance given the sectors the page would keep.
//   node web/emu/emu_test.mjs build/emu/brutal.wasm
import { readFileSync } from "node:fs";
import { storeSectors, restoreSectors } from "./worklet.js";
const wasm = readFileSync(process.argv[2] || "build/emu/brutal.wasm");
let fails = 0, count = 0;
const check = (c, msg) => { count++; if (!c) fails++; console.log((c ? "  ok    " : "  FAIL  ") + msg); };
async function boot(sectors) {
  const { instance } = await WebAssembly.instantiate(wasm, {});
  const ex = instance.exports;
  if (ex._initialize) ex._initialize();
  ex.sim_nor_erase();
  restoreSectors(new Uint8Array(ex.memory.buffer, ex.sim_nor_ptr(), ex.sim_nor_size()), sectors);
  ex.sim_master(26000);
  ex.sim_boot();
  return ex;
}
function render(ex, ms) {
  let peak = 0;
  for (let t = 0; t < ms * 44.1; t += 128) {
    ex.sim_render_f(128);
    const l = new Float32Array(ex.memory.buffer, ex.sim_out_l(), 128);
    for (const v of l) peak = Math.max(peak, Math.abs(v));
  }
  return peak;
}
console.log("== browser emulator (wasm)");
const ex = await boot({});
render(ex, 200);
const fb = new Uint16Array(ex.memory.buffer, ex.sim_screen(), 240 * 240);
check(new Set(fb).size > 10, `boots and draws the screen (${new Set(fb).size} colours)`);
check(render(ex, 300) === 0, "silent with no key");
ex.sim_key(7, 1);
const p = render(ex, 400);
check(p > 0.05 && p <= 1.0, `a key plays (peak ${p.toFixed(3)})`);
ex.sim_key(7, 0);
render(ex, 3000);
ex.sim_button(10, 1); render(ex, 30); ex.sim_button(10, 0); render(ex, 30);   // PLAY: ARP on
const leds = () => Array.from(new Uint8Array(ex.memory.buffer, ex.sim_leds(), 41));
check(leds()[10] >= 1, "PLAY: ARP on (its LED)");
ex.sim_key(7, 1); ex.sim_key(10, 1); ex.sim_key(14, 1);
render(ex, 2000);
check(leds()[14 + 7] >= 1 && leds()[14 + 10] >= 1 && leds()[14 + 14] >= 1, "the ARP's keys lit");
ex.sim_key(7, 0); ex.sim_key(10, 0); ex.sim_key(14, 0);
render(ex, 300);
// SAVE to U01 and a second instance with the page's sectors
ex.sim_button(7, 1); render(ex, 30); ex.sim_button(7, 0); render(ex, 30);
ex.sim_button(7, 1); render(ex, 30); ex.sim_button(7, 0); render(ex, 300);
const sectors = storeSectors(new Uint8Array(ex.memory.buffer, ex.sim_nor_ptr(), ex.sim_nor_size()));
check(Object.keys(sectors).length >= 2 && ex.sim_flash_writes_count() > 0, `SAVE twice: written (${Object.keys(sectors).length} sectors to keep)`);
const ex2 = await boot(JSON.parse(JSON.stringify(sectors)));
render(ex2, 200);
const fb2 = new Uint16Array(ex2.memory.buffer, ex2.sim_screen(), 240 * 240);
check(new Set(fb2).size > 10, "a second instance boots from the kept sectors (the last session)");
{
  const t0 = Date.now();
  ex2.sim_key(0, 1); ex2.sim_key(4, 1); ex2.sim_key(7, 1); ex2.sim_key(11, 1);
  render(ex2, 10000);
  const ms = Date.now() - t0;
  check(ms < 10000, `10 s of 4 voices in ${ms} ms here (${(ms / 100).toFixed(1)} % of real time)`);
}
console.log(`emu: ${count} checks, ${fails} failed`);
process.exit(fails ? 1 : 0);
