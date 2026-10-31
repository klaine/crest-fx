// The knob cards: real units from the same curves as src/dsp/fx.c, and no drawing from a missing value.
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import assert from "node:assert/strict";
import vm from "node:vm";

const src = readFileSync(fileURLToPath(new URL("../../src/cards.js", import.meta.url)), "utf8");
vm.runInThisContext(src);

function draw(name, raw, values) {
  const prints = [];
  let rects = 0;
  const ctx = {
    print: (x, y, s) => { prints.push(s); assert.ok(x >= 0 && y >= 0); },
    fillRect: (x, y, w, h) => { rects++; assert.ok(x >= 0 && y >= 0 && w > 0 && h > 0, `rect ${x},${y},${w},${h}`); },
    textWidth: (s) => s.length * 5,
  };
  globalThis.crfx(ctx, { name, raw, w: 120, h: 34, values });
  return { prints, rects };
}

assert.ok(draw("Delay Time", "0").prints.includes("10 ms"));
assert.ok(draw("Delay Time", "1").prints.includes("2 s"));
assert.ok(draw("Reverb Decay", "1").prints.includes("20 s"));
assert.ok(draw("Reverb Pre-delay", "0.5").prints.includes("250 ms"));
assert.ok(draw("Distortion Tone", "1").prints.includes("20 kHz"));
assert.ok(draw("Reverb Level", "0.25").prints.includes("25%"));
assert.ok(draw("Delay Time", "0.5", { dly_sync: "8" }).prints.includes("synced"));
assert.ok(draw("Delay Time", "0.5", { "fx1:dly_sync": "8" }).prints.includes("synced"));
assert.ok(!draw("Delay Time", "0.5", { dly_sync: "0" }).prints.includes("synced"));
assert.ok(draw("Chorus Feedback", "-1").rects > 4);
assert.ok(draw("Chorus Feedback", "1").rects > 4);
assert.equal(draw("Delay Time", null).rects, 0);
assert.equal(draw("Delay Time", "").rects, 0);
assert.equal(draw("Something Else", "0.5").rects, 0);
console.log("cards ok");
