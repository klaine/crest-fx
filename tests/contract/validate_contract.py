#!/usr/bin/env python3
"""Validate Crest FX's Move contract: module.json, build/chain_params.json and build/ui_hierarchy.json.

The chain host opens modules/audio_fx/<id>/<id>.so whatever `dsp` says, so the two must agree, and a
wrong name fails with nothing on screen.
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
errors = []


def load(rel):
    with open(os.path.join(ROOT, rel), encoding="utf-8") as f:
        return json.load(f)


def err(msg):
    errors.append(msg)


mod = load("src/module.json")
cp = load("build/chain_params.json")
uh = load("build/ui_hierarchy.json")

for k in ("id", "name", "abbrev", "version", "api_version", "description", "author", "dsp", "component_type", "capabilities"):
    if k not in mod:
        err("module.json: missing " + k)
if mod.get("component_type") != "audio_fx":
    err("module.json: component_type must be audio_fx")
if mod.get("dsp") != mod.get("id", "") + ".so":
    err("module.json: dsp must be <id>.so for an audio FX")
if mod.get("api_version") != 2:
    err("module.json: api_version must be 2")
if len(mod.get("abbrev", "")) != 4 or mod.get("abbrev", "").upper() != mod.get("abbrev"):
    err("module.json: abbrev must be 4 caps")
if mod.get("author") != "AI & Klaine":
    err("module.json: author")
caps = mod.get("capabilities", {})
for k in ("chainable", "requires_continuous_processing"):
    if caps.get(k) is not True:
        err("module.json: capabilities." + k)
if os.path.getsize(os.path.join(ROOT, "src/module.json")) > 8192:
    err("module.json: over the loader's 8 KB")

keys = {}
for e in cp:
    if e["key"] in keys:
        err("chain_params: duplicate " + e["key"])
    keys[e["key"]] = e
    sn = e.get("short_name", "")
    if len(sn) > 5:
        err("chain_params: short_name over 5 characters: " + sn)
    if e["type"] == "enum":
        if not e.get("options"):
            err("chain_params: no options: " + e["key"])
        if not 0 <= e.get("default", -1) < len(e.get("options", [])):
            err("chain_params: default out of range: " + e["key"])
        for o in e.get("short_options", e.get("options", [])):
            if "short_options" in e and len(o) > 4:
                err("chain_params: short option over 4 characters: " + o)
        if "short_options" in e and len(e["short_options"]) != len(e["options"]):
            err("chain_params: short_options length: " + e["key"])
    elif "default" in e and not e["min"] <= e["default"] <= e["max"]:
        err("chain_params: default out of range: " + e["key"])
    if e["type"] == "float" and e.get("card_script") != "cards.js#crfx":
        err("chain_params: no card: " + e["key"])
if len(json.dumps(cp, separators=(",", ":"))) > 31 * 1024:
    err("chain_params: over the 31 KiB limit")

levels = uh.get("levels", {})
if "root" not in levels:
    err("ui_hierarchy: no root")
for name, lv in levels.items():
    if len(lv.get("knobs", [])) > 8:
        err("ui_hierarchy: %s has more than 8 knobs" % name)
    for k in lv.get("knobs", []):
        if k not in keys:
            err("ui_hierarchy: %s: unknown knob %s" % (name, k))
    for p in lv.get("params", []):
        if isinstance(p, str):
            if p not in keys:
                err("ui_hierarchy: %s: unknown param %s" % (name, p))
        elif "level" in p:
            if p["level"] not in levels:
                err("ui_hierarchy: %s: unknown level %s" % (name, p["level"]))
        elif p.get("key") not in keys:
            err("ui_hierarchy: %s: unknown param %s" % (name, p.get("key")))
    if "list_param" in lv and lv["list_param"] not in keys:
        err("ui_hierarchy: %s: unknown list_param" % name)

# Every parameter is reachable from a page.
reached = {k for lv in levels.values() for k in lv.get("knobs", [])}
for k in keys:
    if k != "preset" and k not in reached:
        err("ui_hierarchy: no page has " + k)

if errors:
    print("\n".join("  FAIL " + e for e in errors))
    sys.exit(1)
print("contract ok: %d params, %d pages" % (len(cp), len(levels)))
