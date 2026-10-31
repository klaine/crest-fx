#!/usr/bin/env python3
"""Generate Crest FX's parameter tables and Move contract. The single source of truth.

  gen.py           write src/dsp/params_gen.{h,c}, the META block in src/cards.js and build/*.json
  gen.py --check   fail if any committed file is stale

Rules: enum option order is append-only and keys are never renamed (indices and keys are saved in Move
sets). The keys, ranges and option lists match Crest's effect parameters, because src/dsp/fx.c is a
copy of Crest's and reads them as they are.
"""
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

FX_ROUTES = ["Parallel", "Dly>Rev>Cho", "Dly>Cho>Rev", "Rev>Dly>Cho", "Rev>Cho>Dly", "Cho>Dly>Rev", "Cho>Rev>Dly"]
DIST_TYPES = ["Soft", "Hard", "Fold", "Crush"]
CHORUS_TYPES = ["Chorus 1", "Chorus 2", "Ensemble"]
DELAY_SYNC = ["Off", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D",
              "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/1T", "1/2D", "1 bar"]
DELAY_MODES = ["Stereo", "PingPong", "Mono"]
DELAY_RATIOS = ["1/1", "4/3", "3/4", "3/2", "2/3", "2/1", "1/2", "3/1", "1/3", "4/1", "1/4"]  # left : right time
REVERB_TYPES = ["Room", "Hall", "Plate"]

SHORT_OPT = {"Chorus 1": "Cho1", "Chorus 2": "Cho2", "Ensemble": "Ens", "PingPong": "PngP", "Stereo": "Ster",
             "Parallel": "Para", "Dly>Rev>Cho": "DRC", "Dly>Cho>Rev": "DCR", "Rev>Dly>Cho": "RDC",
             "Rev>Cho>Dly": "RCD", "Cho>Dly>Rev": "CDR", "Cho>Rev>Dly": "CRD", "Crush": "Crsh", "Plate": "Plat"}

PARAMS = []
BY_KEY = {}


def P(key, name, short, kind, mn, mx, default, opts=None):
    assert len(short) <= 5, (key, short)
    if kind == "enum":
        mn, mx = 0, len(opts) - 1
        default = opts.index(default)
    p = dict(key=key, name=name, short=short, kind=kind, mn=mn, mx=mx, default=default, opts=opts)
    PARAMS.append(p)
    BY_KEY[key] = p


P("fx_route", "FX Routing", "Route", "enum", 0, 0, "Cho>Dly>Rev", opts=FX_ROUTES)
P("fx_dry", "FX Dry Level", "Dry", "float", 0, 1, 1.0)
P("fx_wet", "FX Wet Level", "Wet", "float", 0, 1, 1.0)
P("dst_lvl", "Distortion Drive", "Level", "float", 0, 1, 0.0)
P("dst_type", "Distortion Type", "Type", "enum", 0, 0, "Soft", opts=DIST_TYPES)
P("dst_tone", "Distortion Tone", "Tone", "float", 0, 1, 0.7)
P("dst_mix", "Distortion Mix", "Mix", "float", 0, 1, 1.0)
P("cho_lvl", "Chorus Level", "Level", "float", 0, 1, 0.0)
P("cho_type", "Chorus Type", "Type", "enum", 0, 0, "Chorus 1", opts=CHORUS_TYPES)
P("cho_rate", "Chorus Rate", "Rate", "float", 0, 1, 0.3)
P("cho_depth", "Chorus Depth", "Depth", "float", 0, 1, 0.5)
P("cho_dly", "Chorus Delay", "Dly", "float", 0, 1, 0.3)
P("cho_fb", "Chorus Feedback", "Fb", "float", -1, 1, 0.0)
P("cho_lp", "Chorus LoPass", "LoPas", "float", 0, 1, 0.709)
P("cho_hp", "Chorus HiPass", "HiPas", "float", 0, 1, 0.016)
P("dly_lvl", "Delay Level", "Level", "float", 0, 1, 0.0)
P("dly_time", "Delay Time", "Time", "float", 0, 1, 0.5)
P("dly_sync", "Delay Sync", "Sync", "enum", 0, 0, "1/8D", opts=DELAY_SYNC)
P("dly_fb", "Delay Feedback", "Fb", "float", 0, 1, 0.35)
P("dly_mode", "Delay Mode", "Mode", "enum", 0, 0, "Stereo", opts=DELAY_MODES)
P("dly_lp", "Delay Low-pass", "LP", "float", 0, 1, 0.7)
P("dly_hp", "Delay High-pass", "HP", "float", 0, 1, 0.1)
P("dly_ratio", "Delay L/R Ratio", "Ratio", "enum", 0, 0, "1/1", opts=DELAY_RATIOS)
P("dly_width", "Delay Width", "Width", "float", 0, 1, 1.0)
P("dly_slew", "Delay Slew Rate", "Slew", "float", 0, 1, 0.252)
P("rev_lvl", "Reverb Level", "Level", "float", 0, 1, 0.0)
P("rev_type", "Reverb Type", "Type", "enum", 0, 0, "Hall", opts=REVERB_TYPES)
P("rev_size", "Reverb Size", "Size", "float", 0, 1, 0.6)
P("rev_decay", "Reverb Decay", "Decay", "float", 0, 1, 0.5)
P("rev_damp", "Reverb Damping", "Damp", "float", 0, 1, 0.4)
P("rev_hp", "Reverb Low Cut", "HP", "float", 0, 1, 0.15)
P("rev_pre", "Reverb Pre-delay", "Pre", "float", 0, 1, 0.04)
P("rev_lpf", "Reverb LoPass", "LoPas", "float", 0, 1, 0.583)
P("rev_hpf", "Reverb HiPass", "HiPas", "float", 0, 1, 0.0)
P("rev_mdep", "Reverb Mod Depth", "MDep", "float", 0, 1, 0.504)
P("rev_mrate", "Reverb Mod Rate", "MRate", "float", 0, 1, 0.031)

# Display curves for the knob cards (src/cards.js). They must match src/dsp/fx.c. Floats not listed show percent.
DISP = {"dst_tone": ["exp", 1500, 20000, "Hz"], "cho_rate": ["exp", 0.05, 8, "Hz"], "cho_dly": ["lin", 3, 30, "ms"],
        "cho_lp": ["exp", 500, 20000, "Hz"], "cho_hp": ["exp", 20, 1000, "Hz"], "dly_time": ["exp", 10, 2000, "ms"],
        "dly_lp": ["exp", 1000, 20000, "Hz"], "dly_hp": ["exp", 20, 2000, "Hz"], "dly_slew": ["exp", 10, 3000, "ms"],
        "rev_decay": ["exp", 0.2, 20, "s"], "rev_hp": ["exp", 20, 1000, "Hz"], "rev_pre": ["lin", 0, 500, "ms"],
        "rev_lpf": ["exp", 500, 20000, "Hz"], "rev_hpf": ["exp", 20, 1000, "Hz"], "rev_mrate": ["exp", 1, 32, "x"]}

# The root page in two groups of four. Knobs 1-4 are the mixer: each effect's level in signal order.
# Knobs 5-8 are what is played: delay feedback and sync (throws, tempo divisions), reverb decay, and
# one Wet knob that pulls everything the chain adds out at once.
ROOT_KNOBS = [("dst_lvl", "Dist"), ("cho_lvl", "Chor"), ("dly_lvl", "Delay"), ("rev_lvl", "Verb"),
              ("dly_fb", "DlyFb"), ("dly_sync", "Sync"), ("rev_decay", "Decay"), ("fx_wet", "Wet")]
# At most 8 knobs a page.
LEVELS = [
    ("fx_main", "Routing", ["fx_route", "fx_dry", "fx_wet"]),
    ("fx_dist", "Distortion", ["dst_lvl", "dst_type", "dst_tone", "dst_mix"]),
    ("fx_cho", "Chorus", ["cho_lvl", "cho_type", "cho_rate", "cho_depth", "cho_dly", "cho_fb", "cho_lp", "cho_hp"]),
    ("fx_dly", "Delay", ["dly_lvl", "dly_time", "dly_sync", "dly_fb", "dly_mode", "dly_ratio", "dly_width"]),
    ("fx_dly2", "Delay Tone", ["dly_lp", "dly_hp", "dly_slew"]),
    ("fx_rev", "Reverb", ["rev_lvl", "rev_type", "rev_size", "rev_decay", "rev_damp", "rev_hp", "rev_pre"]),
    ("fx_rev2", "Reverb Tone", ["rev_lpf", "rev_hpf", "rev_mdep", "rev_mrate"]),
]

# Eight starting points, one role each. A preset earns its place by setting what the root knobs cannot
# (reverb type and size, delay time, sync and mode, combinations); chorus and drive alone are one root
# knob away from Init. Index 0 must stay Init: everything off, the signal passes untouched.
PRESETS = [
    ("Init", {}),
    # ambience to leave on anything: 0.8 s room, 10 ms ahead
    ("Room", {"rev_lvl": 0.22, "rev_type": "Room", "rev_size": 0.35, "rev_decay": 0.3, "rev_damp": 0.5, "rev_pre": 0.02}),
    # 3.2 s hall, 30 ms pre-delay keeps the attack clear
    ("Hall", {"rev_lvl": 0.35, "rev_type": "Hall", "rev_size": 0.7, "rev_decay": 0.6, "rev_pre": 0.06}),
    # bright 1.3 s plate with the low end cut, for drums and percussive sources
    ("Plate", {"rev_lvl": 0.3, "rev_type": "Plate", "rev_size": 0.5, "rev_decay": 0.4, "rev_damp": 0.2, "rev_hp": 0.3}),
    # 110 ms mono single repeat and a touch of room
    ("Slapback", {"dly_lvl": 0.4, "dly_sync": "Off", "dly_time": 0.45, "dly_fb": 0.0, "dly_mode": "Mono",
                  "rev_lvl": 0.1, "rev_type": "Room", "rev_size": 0.3, "rev_decay": 0.25}),
    # the synced bread and butter: dotted eighths with a filtered feedback path and a hall tail
    ("Dotted Echo", {"dly_lvl": 0.35, "dly_sync": "1/8D", "dly_fb": 0.45, "dly_lp": 0.55, "dly_hp": 0.2,
                     "rev_lvl": 0.15, "rev_type": "Hall", "rev_decay": 0.5}),
    ("Ping Pong", {"dly_lvl": 0.35, "dly_sync": "1/8", "dly_mode": "PingPong", "dly_fb": 0.5, "dly_lp": 0.5, "dly_hp": 0.3}),
    # ambient: ensemble into long dotted-quarter echoes into a huge hall
    ("Wash", {"cho_lvl": 0.3, "cho_type": "Ensemble", "cho_rate": 0.25, "dly_lvl": 0.3, "dly_sync": "1/4D", "dly_fb": 0.6,
              "dly_lp": 0.4, "rev_lvl": 0.5, "rev_type": "Hall", "rev_size": 0.9, "rev_decay": 0.8, "rev_pre": 0.1}),
]


def fnum(x):
    x = float(x)
    return int(x) if x == int(x) else round(x, 6)


def short_opts(opts):
    return [SHORT_OPT.get(o, o) for o in opts]


def chain_params():
    out = []
    for p in PARAMS:
        e = {"key": p["key"], "name": p["name"], "short_name": p["short"], "type": p["kind"]}
        if p["kind"] == "enum":
            e["options"] = list(p["opts"])
            so = short_opts(p["opts"])
            if so != e["options"]:
                e["short_options"] = so
        else:
            e["min"], e["max"] = fnum(p["mn"]), fnum(p["mx"])
            e["card_script"] = "cards.js#crfx"
        e["default"] = fnum(p["default"])
        out.append(e)
    out.append({"key": "preset", "name": "Preset", "type": "int", "min": 0, "max": len(PRESETS) - 1, "step": 1})
    return out


def knob_entry(k):
    if isinstance(k, tuple):
        return {"key": k[0], "short_name": k[1], "label": BY_KEY[k[0]]["name"]}
    return k


def knob_key(k):
    return k[0] if isinstance(k, tuple) else k


def ui_hierarchy():
    root = [knob_entry(k) for k in ROOT_KNOBS]
    root.append({"level": "presets", "label": "Presets"})
    levels = {"root": {"label": "Crest FX", "knobs": [knob_key(k) for k in ROOT_KNOBS], "params": root}}
    levels["presets"] = {"label": "Presets", "list_param": "preset", "count_param": "preset_count",
                         "name_param": "preset_name", "navigate_to": "root", "params": []}
    for lid, label, knobs in LEVELS:
        assert len(knobs) <= 8, lid
        root.append({"level": lid, "label": label})
        levels[lid] = {"label": label, "knobs": list(knobs), "params": list(knobs)}
    return {"levels": levels}


def c_str(s):
    s = s.replace("\\", "\\\\").replace('"', '\\"')
    parts = [s[i:i + 110] for i in range(0, len(s), 110)]
    # A split must not separate a backslash from what it escapes.
    for i in range(len(parts) - 1):
        while parts[i].endswith("\\") and (len(parts[i]) - len(parts[i].rstrip("\\"))) % 2:
            parts[i + 1] = parts[i][-1] + parts[i + 1]
            parts[i] = parts[i][:-1]
    return "\n".join('    "%s"' % p for p in parts)


def preset_row(vals):
    row = []
    for p in PARAMS:
        v = vals.get(p["key"], p["default"])
        row.append(p["opts"].index(v) if isinstance(v, str) else v)
    return row


def gen_h():
    lines = ["/* GENERATED by tools/gen/gen.py. Do not edit. */", "#ifndef CREST_FX_PARAMS_GEN_H",
             "#define CREST_FX_PARAMS_GEN_H", "#include <stdint.h>", "", "enum {"]
    lines += ["    P_%s," % p["key"].upper() for p in PARAMS]
    lines += ["    P_COUNT", "};", "", "enum { PK_FLOAT, PK_ENUM };", "", "typedef struct {", "    const char *key;",
              "    uint8_t kind;", "    float mn, mx, def;", "    const char *const *opts;", "    int nopts;",
              "} param_desc_t;", "", "extern const param_desc_t FX_PARAMS[P_COUNT];",
              "extern const uint16_t FX_PARAMS_SORTED[P_COUNT]; /* ids ordered by strcmp(key) */", "",
              "extern const char FX_CHAIN_PARAMS[];", "extern const int FX_CHAIN_PARAMS_LEN;",
              "extern const char FX_UI_HIERARCHY[];", "extern const int FX_UI_HIERARCHY_LEN;", "",
              "#define FX_PRESET_COUNT %d" % len(PRESETS), "extern const char *const FX_PRESET_NAMES[FX_PRESET_COUNT];",
              "extern const float FX_PRESET_VALUES[FX_PRESET_COUNT][P_COUNT];", "", "#endif", ""]
    return "\n".join(lines)


def gen_c(cp, uh):
    lines = ["/* GENERATED by tools/gen/gen.py. Do not edit. */", '#include "params_gen.h"', ""]
    for p in PARAMS:
        if p["kind"] == "enum":
            lines.append("static const char *const OPTS_%s[] = {%s};" % (
                p["key"].upper(), ", ".join('"%s"' % o for o in p["opts"])))
    lines += ["", "const param_desc_t FX_PARAMS[P_COUNT] = {"]
    for p in PARAMS:
        k = "PK_ENUM" if p["kind"] == "enum" else "PK_FLOAT"
        o = "OPTS_%s, %d" % (p["key"].upper(), len(p["opts"])) if p["kind"] == "enum" else "0, 0"
        lines.append('    {"%s", %s, %sf, %sf, %sf, %s},' % (
            p["key"], k, float(p["mn"]), float(p["mx"]), float(p["default"]), o))
    lines += ["};", ""]
    order = sorted(range(len(PARAMS)), key=lambda i: PARAMS[i]["key"])
    lines += ["const uint16_t FX_PARAMS_SORTED[P_COUNT] = {%s};" % ", ".join(map(str, order)), ""]
    for name, obj in (("FX_CHAIN_PARAMS", cp), ("FX_UI_HIERARCHY", uh)):
        s = json.dumps(obj, separators=(",", ":"))
        lines += ["const char %s[] =\n%s;" % (name, c_str(s)), "const int %s_LEN = %d;" % (name, len(s)), ""]
    lines += ["const char *const FX_PRESET_NAMES[FX_PRESET_COUNT] = {%s};" % ", ".join('"%s"' % n for n, _ in PRESETS), ""]
    lines.append("const float FX_PRESET_VALUES[FX_PRESET_COUNT][P_COUNT] = {")
    for n, vals in PRESETS:
        for k in vals:
            assert k in BY_KEY, (n, k)
        lines.append("    {%s}, /* %s */" % (", ".join("%sf" % float(v) for v in preset_row(vals)), n))
    lines += ["};", ""]
    return "\n".join(lines)


def cards_meta():
    keys = {p["name"]: p["key"] for p in PARAMS if p["kind"] != "enum"}
    return json.dumps({"keys": keys, "disp": DISP}, separators=(",", ":"))


def with_meta(text):
    block = "/* BEGIN GENERATED META */\n  var META = %s;\n  /* END GENERATED META */" % cards_meta()
    out, n = re.subn(r"/\* BEGIN GENERATED META \*/.*?/\* END GENERATED META \*/", lambda _: block, text, flags=re.S)
    assert n == 1, "src/cards.js has no GENERATED META block"
    return out


def main():
    check = "--check" in sys.argv[1:]
    cp, uh = chain_params(), ui_hierarchy()
    cards_path = os.path.join(ROOT, "src", "cards.js")
    committed = {
        "src/dsp/params_gen.h": gen_h(),
        "src/dsp/params_gen.c": gen_c(cp, uh),
        "src/cards.js": with_meta(open(cards_path, encoding="utf-8").read()),
    }
    stale = []
    for rel, text in committed.items():
        path = os.path.join(ROOT, rel)
        old = open(path, encoding="utf-8").read() if os.path.exists(path) else None
        if old == text:
            continue
        if check:
            stale.append(rel)
        else:
            with open(path, "w", encoding="utf-8") as f:
                f.write(text)
    if stale:
        sys.exit("stale generated files (run scripts/build.sh --gen): " + ", ".join(stale))
    if not check:
        os.makedirs(os.path.join(ROOT, "build"), exist_ok=True)
        for name, obj in (("chain_params", cp), ("ui_hierarchy", uh)):
            with open(os.path.join(ROOT, "build", name + ".json"), "w", encoding="utf-8") as f:
                json.dump(obj, f, indent=1)
                f.write("\n")
        print("generated %d params, %d presets; chain_params %d B, ui_hierarchy %d B" % (
            len(PARAMS), len(PRESETS), len(json.dumps(cp, separators=(",", ":"))),
            len(json.dumps(uh, separators=(",", ":")))))


if __name__ == "__main__":
    main()
