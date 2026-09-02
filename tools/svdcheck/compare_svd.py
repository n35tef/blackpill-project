#!/usr/bin/env python3
"""Differentially check our hand-written register map against ST's SVD.

Ground truth is a CMSIS-SVD description of the STM32F411, which is generated
from the same internal database as ST's own headers and reference manual. We
never copy from it - we only compare numbers, so a disagreement means one of
the two is wrong and the header needs a human to look at it.

Checked:
  * peripheral base addresses
  * register offsets and widths inside every struct
  * bit position and mask of every FIELD macro

Exit code is non-zero if any mismatch is found.
"""

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path

# Our instance name -> SVD peripheral name, where the two differ.
INSTANCE_ALIAS = {
    "FLASH_R": "FLASH",       # FLASH_BASE is the memory, FLASH_R the interface
    "ADC_COMMON": "ADC_Common",
}

# Macro prefix -> SVD peripherals the macro is allowed to describe. A field is
# accepted if it matches any candidate, because e.g. TIM_ macros are shared by
# timers whose register sets are supersets of each other.
PREFIX_CANDIDATES = {
    "SPI": ["SPI1", "SPI2", "SPI3", "SPI4", "SPI5"],
    "TIM": ["TIM1", "TIM2", "TIM3", "TIM4", "TIM5", "TIM9", "TIM10", "TIM11"],
    "USART": ["USART1", "USART2", "USART6"],
    "I2C": ["I2C1", "I2C2", "I2C3"],
    "GPIO": ["GPIOA", "GPIOB", "GPIOC", "GPIOD", "GPIOE", "GPIOH"],
    "DMA": ["DMA1", "DMA2"],
    "ADC": ["ADC1", "ADC_Common"],
    "RCC": ["RCC"], "PWR": ["PWR"], "FLASH": ["FLASH"], "EXTI": ["EXTI"],
    "SYSCFG": ["SYSCFG"], "CRC": ["CRC"], "IWDG": ["IWDG"], "WWDG": ["WWDG"],
    "RTC": ["RTC"],
}

# Struct member -> SVD register name, when our layout groups things the SVD
# spells out. The SVD lists DMA stream registers as S0CR, S0NDTR, ...; we model
# one stream struct and index it, so member CR is compared against S0CR. Our
# stream struct starts at 0x10 inside the DMA block, which the SVD offsets
# already include, hence the extra bias.
STRUCT_REG_PREFIX = {"dma_stream_regs_t": "S0"}
STRUCT_OFFSET_BIAS = {"dma_stream_regs_t": 0x10}

# Members we model as an array where the SVD enumerates one register each.
ARRAY_MEMBERS = {
    ("gpio_regs_t", "AFR"): ["AFRL", "AFRH"],
    ("syscfg_regs_t", "EXTICR"): ["EXTICR1", "EXTICR2", "EXTICR3", "EXTICR4"],
    ("rtc_regs_t", "BKP"): [f"BKP{i}R" for i in range(20)],
}

# Plain renames between our member name and the SVD's.
STRUCT_REG_ALIAS = {
    ("syscfg_regs_t", "MEMRMP"): "MEMRM",
    # The SVD splits the capture/compare mode registers into an output-mode and
    # an input-mode view of the same address; we model the address once.
    ("tim_regs_t", "CCMR1"): "CCMR1_Output",
    ("tim_regs_t", "CCMR2"): "CCMR2_Output",
    ("tim_regs_t", "OR"): None,   # only on TIM2/5/11, absent from the TIM1 view
}

# Registers this SVD revision simply does not describe for the F411. They are
# in RM0383 and in ST's own headers, so they are checked by hand instead.
SVD_MISSING = {
    # Confirmed present on the F411 from ST's own device header, which the SVD
    # simply does not describe. CDR is the exception: it is a dual/triple ADC
    # result register, so on this single-ADC part the word is only reserved.
    ("adc_common_regs_t", "CDR"),
    ("rcc_regs_t", "DCKCFGR"),
    ("i2c_regs_t", "FLTR"),
}

# Macro register token -> SVD register name.
MACRO_REG_ALIAS = {
    ("DMA", "SXCR"): ["S0CR"], ("DMA", "SXNDTR"): ["S0NDTR"],
    ("DMA", "SXPAR"): ["S0PAR"], ("DMA", "SXM0AR"): ["S0M0AR"],
    ("DMA", "SXM1AR"): ["S0M1AR"], ("DMA", "SXFCR"): ["S0FCR"],
    ("SYSCFG", "MEMRMP"): ["MEMRM"],
    ("TIM", "CCMR1"): ["CCMR1_Output", "CCMR1_Input"],
    ("TIM", "CCMR2"): ["CCMR2_Output", "CCMR2_Input"],
    ("DMA", "CR"): ["S0CR"], ("DMA", "NDTR"): ["S0NDTR"],
    ("DMA", "PAR"): ["S0PAR"], ("DMA", "M0AR"): ["S0M0AR"],
    ("DMA", "M1AR"): ["S0M1AR"], ("DMA", "FCR"): ["S0FCR"],
}

# Field renames, keyed by (macro prefix, register, our field name).
FIELD_ALIAS = {
    ("CRC", "CR", "RESET"): "CR",
    ("I2C", "CCR", "FS"): "F_S",
    ("SPI", "SR", "FRE"): "TIFRFE",
    ("RCC", "PLLI2SCFGR", "PLLI2SN"): "PLLI2SNx",
    ("RCC", "PLLI2SCFGR", "PLLI2SR"): "PLLI2SRx",
}

# Bits that exist on the F411 but are absent from this SVD revision, so they
# cannot be machine-checked and are verified against RM0383 by hand.
FIELD_SVD_MISSING = {
    ("RCC", "APB2ENR", "SPI5EN"),   # F411 is the only F4 with SPI5 on APB2
    ("RCC", "BDCR", "LSEMOD"),
    ("PWR", "CR", "LPLVDS"),
    ("PWR", "CR", "MRLVDS"),
}


def parse_svd(path):
    """Return {peripheral: {"base": int, "regs": {NAME: {...}}}} with
    derivedFrom resolved."""
    root = ET.parse(path).getroot()
    raw, derived = {}, {}

    for p in root.findall(".//peripheral"):
        name = p.findtext("name")
        base = int(p.findtext("baseAddress"), 0)
        regs = {}
        for r in p.findall("./registers/register"):
            rname = r.findtext("name")
            entry = {
                "offset": int(r.findtext("addressOffset"), 0),
                "size": int(r.findtext("size"), 0) // 8 if r.findtext("size") else 4,
                "fields": {},
            }
            for f in r.findall("./fields/field"):
                fname = f.findtext("name")
                if f.find("bitOffset") is not None:
                    off = int(f.findtext("bitOffset"), 0)
                    width = int(f.findtext("bitWidth"), 0)
                elif f.find("lsb") is not None:
                    off = int(f.findtext("lsb"), 0)
                    width = int(f.findtext("msb"), 0) - off + 1
                else:  # bitRange "[msb:lsb]"
                    m = re.match(r"\[(\d+):(\d+)\]", f.findtext("bitRange") or "")
                    if not m:
                        continue
                    off = int(m.group(2))
                    width = int(m.group(1)) - off + 1
                entry["fields"][fname] = {"offset": off, "width": width}
            regs[rname] = entry
        raw[name] = {"base": base, "regs": regs}
        if p.get("derivedFrom"):
            derived[name] = p.get("derivedFrom")

    for name, parent in derived.items():
        if parent in raw and not raw[name]["regs"]:
            raw[name]["regs"] = raw[parent]["regs"]
    return raw


def lookup_field(fields, name):
    """Find a field, tolerating the two ways ST's SVD differs from our naming.

    ST spells some flags in mixed case (RxNE vs our RXNE), and it explodes many
    multi-bit fields into one entry per bit (PLLM0, PLLM1, ... rather than a
    single PLLM). The second case is reassembled here so the mask and position
    can still be checked rather than silently skipped.
    """
    if name in fields:
        return fields[name]

    lower = {k.lower(): v for k, v in fields.items()}
    if name.lower() in lower:
        return lower[name.lower()]

    bits = []
    i = 0
    while True:
        entry = lower.get(f"{name.lower()}{i}")
        if entry is None:
            break
        bits.append(entry)
        i += 1
    if len(bits) >= 2 and all(b["width"] == 1 for b in bits):
        offsets = sorted(b["offset"] for b in bits)
        if offsets == list(range(offsets[0], offsets[0] + len(offsets))):
            return {"offset": offsets[0], "width": len(offsets)}
    return None


def split_macro(name):
    """SPI_CR1_BR_POS -> ('SPI', 'CR1', 'BR', 'POS'). Returns None if the shape
    does not look like PREFIX_REG_FIELD."""
    kind = None
    stem = name
    for suffix in ("_POS", "_MSK"):
        if stem.endswith(suffix):
            kind, stem = suffix[1:], stem[: -len(suffix)]
            break
    parts = stem.split("_")
    if len(parts) < 3:
        return None
    return parts[0], parts[1], "_".join(parts[2:]), kind


def check(bsp, svd):
    problems, stats, unmapped, manual = [], defaultdict(int), [], []

    # ---- base addresses -------------------------------------------------
    for iname, inst in sorted(bsp["instances"].items()):
        sname = INSTANCE_ALIAS.get(iname, iname)
        if sname not in svd:
            stats["base_unmapped"] += 1
            continue
        if inst["addr"] != svd[sname]["base"]:
            problems.append(
                f"BASE  {iname}: ours 0x{inst['addr']:08X} != SVD {sname} "
                f"0x{svd[sname]['base']:08X}")
        else:
            stats["base_ok"] += 1

    # ---- register offsets ------------------------------------------------
    # Map each struct type to a representative SVD peripheral via an instance.
    type_to_periph = {}
    for iname, inst in bsp["instances"].items():
        sname = INSTANCE_ALIAS.get(iname, iname)
        if sname in svd:
            type_to_periph.setdefault(inst["type"], sname)
    type_to_periph.setdefault("dma_stream_regs_t", "DMA1")

    for tname, tinfo in sorted(bsp["structs"].items()):
        periph = type_to_periph.get(tname)
        if not periph:
            stats["struct_unmapped"] += 1
            continue
        regs = svd[periph]["regs"]
        rprefix = STRUCT_REG_PREFIX.get(tname, "")
        bias = STRUCT_OFFSET_BIAS.get(tname, 0)
        for mname, mem in tinfo["members"].items():
            ours = mem["offset"] + bias

            if (tname, mname) in SVD_MISSING:
                stats["reg_svd_missing"] += 1
                problems.append(
                    f"REG?  {tname}.{mname} (ours 0x{ours:02X}): this SVD has no "
                    f"such register for the F411 - confirmed by hand instead")
                continue

            names = ARRAY_MEMBERS.get((tname, mname))
            if names:
                for i, svd_name in enumerate(names):
                    cand = regs.get(svd_name)
                    if cand is None:
                        stats["reg_unmapped"] += 1
                        continue
                    want = ours + 4 * i
                    if want != cand["offset"]:
                        problems.append(
                            f"REG   {tname}.{mname}[{i}]: ours 0x{want:02X} != SVD "
                            f"{periph}.{svd_name} 0x{cand['offset']:02X}")
                    else:
                        stats["reg_ok"] += 1
                continue

            alias = STRUCT_REG_ALIAS.get((tname, mname), mname)
            if alias is None:
                stats["reg_unmapped"] += 1
                continue
            svd_name = rprefix + alias
            cand = regs.get(svd_name)
            if cand is None:
                stats["reg_unmapped"] += 1
                problems.append(
                    f"REG?  {tname}.{mname}: no register named '{svd_name}' in "
                    f"SVD {periph} (offset 0x{ours:02X}) - verify by hand")
                continue
            if ours != cand["offset"]:
                problems.append(
                    f"REG   {tname}.{mname}: ours 0x{ours:02X} != SVD "
                    f"{periph}.{svd_name} 0x{cand['offset']:02X}")
            else:
                stats["reg_ok"] += 1

    # ---- bit fields ------------------------------------------------------
    for mname, value in sorted(bsp["macros"].items()):
        parsed = split_macro(mname)
        if not parsed:
            stats["macro_skipped"] += 1
            continue
        prefix, reg, field, kind = parsed
        cands = PREFIX_CANDIDATES.get(prefix)
        if not cands:
            stats["macro_skipped"] += 1
            continue
        reg_names = MACRO_REG_ALIAS.get((prefix, reg), [reg])
        if (prefix, reg, field) in FIELD_SVD_MISSING:
            stats["macro_svd_missing"] += 1
            manual.append(f"{mname} = 0x{value:X}")
            continue
        field = FIELD_ALIAS.get((prefix, reg, field), field)

        found = None
        for periph in cands:
            pregs = svd.get(periph, {}).get("regs", {})
            for rn in reg_names:
                f = lookup_field(pregs.get(rn, {}).get("fields", {}), field)
                if f:
                    found = (periph, rn, f)
                    break
            if found:
                break
        if not found:
            stats["macro_unmapped"] += 1
            unmapped.append(mname)
            continue
        periph, reg, f = found
        if kind == "POS":
            expect = f["offset"]
        elif kind == "MSK":
            expect = ((1 << f["width"]) - 1) << f["offset"]
        else:
            if f["width"] != 1:
                stats["macro_unmapped"] += 1
                continue
            expect = 1 << f["offset"]

        if value != expect:
            problems.append(
                f"BIT   {mname} = 0x{value:X} != 0x{expect:X} (SVD {periph}.{reg}.{field} "
                f"bit {f['offset']} width {f['width']})")
        else:
            stats["macro_ok"] += 1

    return problems, stats, sorted(unmapped), sorted(manual)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bsp_json")
    ap.add_argument("svd")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--show-unmapped", action="store_true")
    args = ap.parse_args()

    bsp = json.load(open(args.bsp_json))
    svd = parse_svd(args.svd)
    problems, stats, unmapped, manual = check(bsp, svd)

    print(f"checked against {Path(args.svd).name}: "
          f"{stats['base_ok']} bases, {stats['reg_ok']} registers, "
          f"{stats['macro_ok']} bit fields verified")
    print(f"not comparable: {stats['macro_unmapped']} field macros, "
          f"{stats['reg_unmapped']} registers, {stats['macro_skipped']} non-field macros")

    if manual and not args.quiet:
        print("\n-- present on the F411 but absent from this SVD, checked by hand --")
        for m in manual:
            print("  " + m)

    if args.show_unmapped:
        print("\n-- field macros with no SVD counterpart --")
        for u in unmapped:
            print("  " + u)

    real = [p for p in problems if not p.startswith("REG?")]
    soft = [p for p in problems if p.startswith("REG?")]
    if soft and not args.quiet:
        print("\n-- needs manual confirmation --")
        for p in soft:
            print("  " + p)
    if real:
        print(f"\n-- {len(real)} MISMATCH(ES) --")
        for p in real:
            print("  " + p)
        return 1
    print("\nno mismatches")
    return 0


if __name__ == "__main__":
    sys.exit(main())
