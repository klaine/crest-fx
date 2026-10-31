/* Crest FX: on-device parameter cards (Schwung `card_script`).
 *
 * One drawer, `crfx`, serves every float knob: the host raises the card while the knob is held or has
 * just turned, and it shows the value in real units with a bar. The curves are DISP in tools/gen/gen.py
 * and must match src/dsp/fx.c. Contract (Schwung docs, "The parameter card"): (0,0) is the inside of the
 * card, o.w x o.h its size, o.raw the wire value or null, o.values the page's other values.
 */
(function () {
  /* BEGIN GENERATED META */
  var META = {"keys":{"FX Dry Level":"fx_dry","FX Wet Level":"fx_wet","Distortion Drive":"dst_lvl","Distortion Tone":"dst_tone","Distortion Mix":"dst_mix","Chorus Level":"cho_lvl","Chorus Rate":"cho_rate","Chorus Depth":"cho_depth","Chorus Delay":"cho_dly","Chorus Feedback":"cho_fb","Chorus LoPass":"cho_lp","Chorus HiPass":"cho_hp","Delay Level":"dly_lvl","Delay Time":"dly_time","Delay Feedback":"dly_fb","Delay Low-pass":"dly_lp","Delay High-pass":"dly_hp","Delay Width":"dly_width","Delay Slew Rate":"dly_slew","Reverb Level":"rev_lvl","Reverb Size":"rev_size","Reverb Decay":"rev_decay","Reverb Damping":"rev_damp","Reverb Low Cut":"rev_hp","Reverb Pre-delay":"rev_pre","Reverb LoPass":"rev_lpf","Reverb HiPass":"rev_hpf","Reverb Mod Depth":"rev_mdep","Reverb Mod Rate":"rev_mrate"},"disp":{"dst_tone":["exp",1500,20000,"Hz"],"cho_rate":["exp",0.05,8,"Hz"],"cho_dly":["lin",3,30,"ms"],"cho_lp":["exp",500,20000,"Hz"],"cho_hp":["exp",20,1000,"Hz"],"dly_time":["exp",10,2000,"ms"],"dly_lp":["exp",1000,20000,"Hz"],"dly_hp":["exp",20,2000,"Hz"],"dly_slew":["exp",10,3000,"ms"],"rev_decay":["exp",0.2,20,"s"],"rev_hp":["exp",20,1000,"Hz"],"rev_pre":["lin",0,500,"ms"],"rev_lpf":["exp",500,20000,"Hz"],"rev_hpf":["exp",20,1000,"Hz"],"rev_mrate":["exp",1,32,"x"]}};
  /* END GENERATED META */

  function trim(x, d) {
    var s = x.toFixed(d);
    if (s.indexOf(".") >= 0) s = s.replace(/0+$/, "").replace(/\.$/, "");
    return s;
  }

  function unitText(v, u) {
    var a = Math.abs(v);
    if (u === "s") return a < 1 ? trim(v * 1000, 0) + " ms" : trim(v, a < 10 ? 2 : 1) + " s";
    if (u === "ms") return a >= 1000 ? trim(v / 1000, 2) + " s" : trim(v, a < 10 ? 1 : 0) + " ms";
    if (u === "Hz") return a >= 1000 ? trim(v / 1000, a >= 10000 ? 1 : 2) + " kHz" : trim(v, a < 1 ? 3 : a < 10 ? 2 : a < 100 ? 1 : 0) + " Hz";
    if (u === "x") return "x" + trim(v, 2);
    return trim(v, 1) + " " + u;
  }

  function reading(key, x) {
    var f = META.disp[key];
    if (!f) return Math.round(x * 100) + "%";
    if (f[0] === "exp") return unitText(f[1] * Math.pow(f[2] / f[1], x), f[3]);
    return unitText(f[1] + (f[2] - f[1]) * x, f[3]);
  }

  function sibling(values, key) {
    if (!values) return null;
    if (values[key] !== undefined) return values[key];
    var suffix = ":" + key;
    for (var k in values) if (k.length > suffix.length && k.slice(-suffix.length) === suffix) return values[k];
    return null;
  }

  function fit(ctx, s, w) {
    while (s.length > 1 && ctx.textWidth(s) > w) s = s.slice(0, -1);
    return s;
  }

  globalThis.crfx = function (ctx, o) {
    var w = o.w, h = o.h;
    var x = o.raw === null || o.raw === undefined || o.raw === "" ? NaN : Number(o.raw);
    var key = META.keys[o.name];
    if (!isFinite(x) || !key) {
      ctx.print(2, 1, fit(ctx, String(o.name || ""), w - 4), 1);
      return;
    }
    var text = reading(key, x);
    if (key === "dly_time") {
      var sync = sibling(o.values, "dly_sync");
      if (sync !== null && sync !== "Off" && Number(sync) !== 0) text = "synced"; /* the Sync knob sets the time */
    }
    ctx.print(2, 1, fit(ctx, String(o.name).toUpperCase(), w - 6 - ctx.textWidth(text)), 1);
    ctx.print(Math.max(0, w - 2 - ctx.textWidth(text)), 1, text, 1);
    var by = Math.min(17, h - 9), bw = w - 4;
    ctx.fillRect(2, by, bw, 1, 1);
    ctx.fillRect(2, by + 6, bw, 1, 1);
    ctx.fillRect(2, by, 1, 7, 1);
    ctx.fillRect(1 + bw, by, 1, 7, 1);
    if (key === "cho_fb") { /* the one bipolar knob */
      var mid = 2 + Math.floor(bw / 2), len = Math.round(x * (bw / 2 - 2));
      ctx.fillRect(mid, by - 1, 1, 9, 1);
      if (len > 0) ctx.fillRect(mid + 1, by + 2, len, 3, 1);
      else if (len < 0) ctx.fillRect(mid + len, by + 2, -len, 3, 1);
    } else {
      var fill = Math.round(Math.max(0, Math.min(1, x)) * (bw - 4));
      if (fill > 0) ctx.fillRect(4, by + 2, fill, 3, 1);
    }
  };
})();
