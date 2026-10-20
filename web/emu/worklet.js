// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Hiro Ikezawa
// (the design after Felucca's web/emu/worklet.js, (C) 2026 Leo Kuroshita, GPL-3.0, and X0X's)
// BRUTAL FM in an AudioWorklet: brutal.wasm is the firmware against a simulated FM-1 (web/emu/bfm_sim.c).
// Each render quantum asks it for 128 frames, which runs the device's clock forward (its main loop every 15 ms
// of audio); ~30 times a second the screen, the LEDs and any flash that changed go to the page.

// the flash the page keeps: BRUTAL FM's store (settings 0xFC000.., presets 0xC0000..0xC9FFF), sectors in use only
export const STORE_AREAS = [[0xC0000, 0xCA000], [0xFC000, 0xFE000]];
export function storeSectors(nor) {
  const out = {};
  for (const [a, b] of STORE_AREAS)
    for (let off = a; off < b; off += 4096) {
      const s = nor.subarray(off, off + 4096);
      if (s.some((v) => v !== 0xFF)) out[off] = Array.from(s);
    }
  return out;
}
export function restoreSectors(nor, sectors) {
  for (const [k, v] of Object.entries(sectors || {})) {
    const off = +k;
    if (STORE_AREAS.some(([a, e]) => off >= a && off + 4096 <= e && off % 4096 === 0) && v.length === 4096)
      nor.set(Uint8Array.from(v), off);
  }
}

if (typeof registerProcessor === "function") {
  class Brutal extends AudioWorkletProcessor {
    constructor() {
      super();
      this.ex = null;
      this.sentDraws = -1;
      this.sentWrites = -1;
      this.sentLeds = "";
      this.last = 0;
      this.period = 1 / 30;
      this.busy = 0;
      this.frames = 0;
      this.port.onmessage = (e) => this.onMessage(e.data);
    }

    async onMessage(m) {
      if (m.type === "load") {
        try {
          const { instance } = await WebAssembly.instantiate(m.wasm, {});
          const ex = instance.exports;
          if (ex._initialize) ex._initialize();
          ex.sim_nor_erase();
          restoreSectors(new Uint8Array(ex.memory.buffer, ex.sim_nor_ptr(), ex.sim_nor_size()), m.sectors);
          ex.sim_master(m.master ?? 26000);
          ex.sim_boot();
          this.ex = ex;
          this.sentWrites = ex.sim_flash_writes_count();
          this.port.postMessage({ type: "ready", rate: ex.sim_sample_rate() });
        } catch (err) {
          this.port.postMessage({ type: "error", message: String((err && err.message) || err) });
        }
        return;
      }
      const ex = this.ex;
      if (!ex) return;
      if (m.type === "btn") ex.sim_button(m.b, m.down ? 1 : 0);
      else if (m.type === "enc") ex.sim_encoder(m.e, m.n | 0);
      else if (m.type === "key") ex.sim_key(m.k, m.down ? 1 : 0);
      else if (m.type === "master") ex.sim_master(m.value | 0);
      else if (m.type === "midi") ex.sim_midi(m.data[0] | 0, m.data[1] | 0, m.data[2] | 0);
      else if (m.type === "visible") this.period = m.on ? 1 / 30 : 1 / 4;
    }

    publish() {
      const ex = this.ex, mem = ex.memory.buffer, msg = { type: "frame" }, transfer = [];
      const leds = Array.from(new Uint8Array(mem, ex.sim_leds(), 41)).join("");
      if (leds !== this.sentLeds) {
        this.sentLeds = leds;
        msg.leds = leds;
      }
      const draws = ex.sim_screen_draws();
      if (draws !== this.sentDraws) {
        this.sentDraws = draws;
        msg.fb = new Uint16Array(mem, ex.sim_screen(), 240 * 240).slice();
        transfer.push(msg.fb.buffer);
      }
      const writes = ex.sim_flash_writes_count();
      if (writes !== this.sentWrites) {
        this.sentWrites = writes;
        msg.sectors = storeSectors(new Uint8Array(mem, ex.sim_nor_ptr(), ex.sim_nor_size()));
      }
      if (this.frames >= 44100) {                       // the share of real time the device's work takes here
        msg.load = this.busy / (this.frames / 44.1);
        this.busy = this.frames = 0;
      }
      if (msg.leds || msg.fb || msg.sectors || msg.load !== undefined) this.port.postMessage(msg, transfer);
    }

    process(inputs, outputs) {
      const ex = this.ex;
      if (!ex) return true;
      const out = outputs[0], n = out[0].length, t0 = Date.now();
      ex.sim_render_f(n);
      out[0].set(new Float32Array(ex.memory.buffer, ex.sim_out_l(), n));
      if (out[1]) out[1].set(new Float32Array(ex.memory.buffer, ex.sim_out_r(), n));
      this.busy += Date.now() - t0;
      this.frames += n;
      if (currentTime - this.last >= this.period) {
        this.last = currentTime;
        this.publish();
      }
      return true;
    }
  }
  registerProcessor("brutal", Brutal);
}
