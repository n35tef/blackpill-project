#!/usr/bin/env python3
"""Extract our register map by asking the compiler, not by trusting a parser.

The headers are scanned only to collect *names* - every numeric value is
emitted as C and evaluated by the host compiler. A parsing mistake therefore
shows up as a compile error rather than as a silently wrong comparison.

Output: JSON on stdout describing peripheral bases, struct layouts and bit
macros, ready for compare_svd.py.
"""

import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
REG_DIR = REPO / "Bsp/Inc/device/regs"

STRUCT_RE = re.compile(
    r"typedef\s+struct\s*\{(?P<body>.*?)\}\s*(?P<name>\w+_t)\s*;", re.S)
MEMBER_RE = re.compile(
    r"(?:__IO|__I|__O)\s+(?P<type>uint\d+_t)\s+(?P<name>\w+)"
    r"(?:\s*\[\s*(?P<count>[^\]]+)\s*\])?\s*;")
BASE_RE = re.compile(r"^#define\s+(?P<name>\w+_BASE)\s+(?P<expr>.+?)\s*$", re.M)
INST_RE = re.compile(
    r"^#define\s+(?P<name>\w+)\s+\(\(\s*(?P<type>\w+_t)\s*\*\s*\)\s*(?P<base>\w+)\s*\)", re.M)
MACRO_RE = re.compile(
    r"^#define\s+(?P<name>[A-Z][A-Z0-9_]*)\s+(?P<expr>\S[^\n]*?)\s*$", re.M)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def collect():
    structs, bases, instances, macros = {}, [], [], []
    for header in sorted(REG_DIR.glob("*.h")):
        clean = strip_comments(header.read_text())

        for m in STRUCT_RE.finditer(clean):
            members = [{"name": mem.group("name"), "type": mem.group("type"),
                        "count": mem.group("count")}
                       for mem in MEMBER_RE.finditer(m.group("body"))]
            structs[m.group("name")] = {"file": header.name, "members": members}

        file_bases = [m.group("name") for m in BASE_RE.finditer(clean)]
        bases += file_bases

        for m in INST_RE.finditer(clean):
            instances.append({"name": m.group("name"), "type": m.group("type"),
                              "base": m.group("base"), "file": header.name})

        inst_names = {i["name"] for i in instances}
        for m in MACRO_RE.finditer(clean):
            name, expr = m.group("name"), m.group("expr").strip()
            if name.endswith("_BASE") or name in inst_names or name.endswith("_H"):
                continue
            # Value-like macros only: no pointer casts, no function macros.
            if "(" in name or re.search(r"\(\s*\w+\s*\*\s*\)", expr):
                continue
            if not re.fullmatch(r"[0-9A-Za-z_xXULul<>()\s|+~&^-]+", expr):
                continue
            macros.append({"name": name, "file": header.name})

    return structs, sorted(set(bases)), instances, macros


def build_probe(structs, bases, instances, macros):
    o = ['#include <stdio.h>', '#include <stddef.h>', '#include <stdint.h>',
         '#include "device/stm32f411.h"', '', 'int main(void)', '{',
         '    printf("{\\n");']

    def emit_map(key, items, line_fn):
        o.append(f'    printf("\\"{key}\\": {{\\n");')
        for i, it in enumerate(items):
            o.append(line_fn(it, "" if i == len(items) - 1 else ","))
        o.append('    printf("},\\n");')

    emit_map("bases", bases,
             lambda b, c: f'    printf("  \\"{b}\\": %llu{c}\\n", (unsigned long long)({b}));')

    emit_map("instances", instances,
             lambda i, c: f'    printf("  \\"{i["name"]}\\": {{\\"type\\": \\"{i["type"]}\\", '
                          f'\\"addr\\": %llu}}{c}\\n", (unsigned long long)(uintptr_t)({i["name"]}));')

    o.append('    printf("\\"structs\\": {\\n");')
    names = list(structs)
    for i, sname in enumerate(names):
        comma = "" if i == len(names) - 1 else ","
        o.append(f'    printf("  \\"{sname}\\": {{\\"size\\": %llu, \\"members\\": {{\\n",'
                 f' (unsigned long long)sizeof({sname}));')
        members = structs[sname]["members"]
        for j, mem in enumerate(members):
            mc = "" if j == len(members) - 1 else ","
            o.append(
                f'    printf("    \\"{mem["name"]}\\": {{\\"offset\\": %llu, \\"size\\": %llu}}{mc}\\n",'
                f' (unsigned long long)offsetof({sname}, {mem["name"]}),'
                f' (unsigned long long)sizeof(((({sname}*)0)->{mem["name"]})));')
        o.append(f'    printf("  }}}}{comma}\\n");')
    o.append('    printf("},\\n");')

    o.append('    printf("\\"macros\\": {\\n");')
    for i, mac in enumerate(macros):
        c = "" if i == len(macros) - 1 else ","
        o.append(f'    printf("  \\"{mac["name"]}\\": %llu{c}\\n",'
                 f' (unsigned long long)((uint64_t)({mac["name"]})));')
    o.append('    printf("}\\n");')
    o += ['    printf("}\\n");', '    return 0;', '}']
    return "\n".join(o)


def main():
    structs, bases, instances, macros = collect()
    src = build_probe(structs, bases, instances, macros)

    with tempfile.TemporaryDirectory() as tmp:
        cfile, binfile = Path(tmp) / "probe.c", Path(tmp) / "probe"
        cfile.write_text(src)
        res = subprocess.run(["gcc", "-std=c11", "-I", str(REPO / "Bsp/Inc"),
                              str(cfile), "-o", str(binfile)],
                             capture_output=True, text=True)
        if res.returncode != 0:
            sys.stderr.write(res.stderr)
            sys.stderr.write("\nprobe.c failed to compile - the header scan is wrong.\n")
            (Path(tmp).parent / "probe_failed.c").write_text(src)
            return 1
        run = subprocess.run([str(binfile)], capture_output=True, text=True)
        if run.returncode != 0:
            sys.stderr.write(run.stderr)
            return 1

    data = json.loads(run.stdout)
    data["_struct_files"] = {s: structs[s]["file"] for s in structs}
    data["_macro_files"] = {m["name"]: m["file"] for m in macros}
    json.dump(data, sys.stdout, indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
