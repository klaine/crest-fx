#!/usr/bin/env python3
"""Static realtime-safety check of the built crest-fx.so.

Reads the ELF dynamic symbol table without external tools and verifies:
  * the file is a 64-bit little-endian aarch64 shared object,
  * it exports move_audio_fx_init_v2,
  * every undefined symbol is on the whitelist (no malloc, file I/O, threads...).
"""
import struct
import sys

ALLOWED = {
    # string / memory
    "memcpy", "memset", "memmove", "memcmp", "strcmp", "strncmp", "strlen", "strcpy", "strncpy",
    # number parsing / formatting (no allocation, no I/O)
    "strtof", "strtol", "atoi", "snprintf",
    # libm
    "sinf", "cosf", "sincosf", "tanf", "expf", "exp2f", "logf", "powf", "sqrtf", "floorf", "fabsf", "roundf",
    "lrintf", "log1pf", "fminf", "fmaxf", "tanhf",
    # compiler / runtime plumbing
    "__stack_chk_fail", "__stack_chk_guard", "__cxa_finalize", "__gmon_start__",
    "_ITM_deregisterTMCloneTable", "_ITM_registerTMCloneTable", "__errno_location",
    "__isnanf", "__isinff", "__finitef", "__fpclassifyf",
    # clock for the debug-only cpu_us key
    "clock_gettime",
}


def main(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"\x7fELF" or d[4] != 2 or d[5] != 1:
        sys.exit("not a 64-bit little-endian ELF")
    if struct.unpack_from("<H", d, 18)[0] != 183:
        sys.exit("not aarch64")
    e_shoff, = struct.unpack_from("<Q", d, 40)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", d, 58)
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        name, typ, flags, addr, off, size, link, info, align, entsize = struct.unpack_from("<IIQQQQIIQQ", d, o)
        secs.append(dict(type=typ, off=off, size=size, link=link, entsize=entsize))
    dynsym = next((s for s in secs if s["type"] == 11), None)  # SHT_DYNSYM
    if dynsym is None:
        sys.exit("no .dynsym")
    strtab = secs[dynsym["link"]]
    undefined, exported = [], []
    for i in range(dynsym["size"] // dynsym["entsize"]):
        o = dynsym["off"] + i * dynsym["entsize"]
        st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", d, o)
        if st_name == 0:
            continue
        s = strtab["off"] + st_name
        name = d[s:d.index(b"\0", s)].decode()
        (undefined if st_shndx == 0 else exported).append(name)
    bad = sorted(n for n in undefined if n not in ALLOWED)
    if "move_audio_fx_init_v2" not in exported:
        sys.exit("move_audio_fx_init_v2 is not exported")
    if bad:
        sys.exit("forbidden undefined symbols in crest-fx.so: " + ", ".join(bad))
    print("crest-fx.so ok: aarch64, exports move_audio_fx_init_v2, %d undefined symbols all whitelisted" % len(undefined))


if __name__ == "__main__":
    main(sys.argv[1])
