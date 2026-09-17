# OSRS Native Client - Quest / Varbit Recon Probe
# IDAPython / IDA 8-9
#
# Run:
#   File -> Script file...  (Alt+F7)
#
# Then send me the generated:
#   <database_name>_quest_varbit_probe.json
#
# This script does NOT modify the database.

import os
import json
import struct
import traceback
from collections import defaultdict

import idaapi
import ida_bytes
import ida_funcs
import ida_idaapi
import ida_lines
import ida_name
import ida_nalt
import ida_segment
import ida_ua
import ida_idp
import idautils
import idc


# --------------------------------------------------------------------------
# Configuration
# --------------------------------------------------------------------------

DEEP_SCAN = True

# Maximum number of generic bit-extraction candidates to keep.
MAX_DEEP_CANDIDATES = 120

# Strong candidates to decompile/dump in detail.
MAX_DECOMPILE_CANDIDATES = 60

# Avoid chewing forever on gigantic compiler-generated functions.
MAX_FUNC_INSNS_DEEP_SCAN = 500

# Maximum disassembly lines saved per candidate.
MAX_DISASM_LINES = 180

# Maximum pseudocode characters per function.
MAX_PSEUDOCODE_CHARS = 20000

# Context around interesting string XREF instructions.
STRING_XREF_CONTEXT = 12


KEYWORDS = [
    # Direct var terminology
    "varbit",
    "varp",
    "varps",
    "varc",
    "varclient",
    "clientvar",
    "client_var",

    # Script VM terminology
    "clientscript",
    "client script",
    "script",
    "opcode",
    "interpreter",
    "vm",

    # Quest terminology
    "quest",
    "questlist",
    "quest list",

    # Config / definitions
    "config",
    "definition",
    "archive",
    "cache",

    # Often useful errors/debug paths
    "stack",
    "operand",
]


# --------------------------------------------------------------------------
# Helpers
# --------------------------------------------------------------------------

BADADDR = ida_idaapi.BADADDR


def safe_name(ea):
    if ea in (None, BADADDR):
        return ""
    try:
        n = ida_name.get_name(ea)
        if n:
            return n
    except Exception:
        pass
    return "sub_%X" % ea


def seg_name(ea):
    try:
        s = ida_segment.getseg(ea)
        if s:
            return ida_segment.get_segm_name(s)
    except Exception:
        pass
    return ""


def func_for_ea(ea):
    try:
        return ida_funcs.get_func(ea)
    except Exception:
        return None


def func_desc(ea):
    f = func_for_ea(ea)
    if not f:
        return None

    return {
        "start": hex(f.start_ea),
        "end": hex(f.end_ea),
        "name": safe_name(f.start_ea),
        "segment": seg_name(f.start_ea),
        "size": f.end_ea - f.start_ea,
    }


def clean_disasm(ea):
    try:
        line = ida_lines.generate_disasm_line(ea, 0)
        if line:
            return ida_lines.tag_remove(line)
    except Exception:
        pass

    try:
        return idc.generate_disasm_line(ea, 0) or ""
    except Exception:
        return ""


def instruction_context(ea, radius=8):
    """
    Get a small instruction window around EA.
    """
    result = []

    cur = ea
    before = []

    for _ in range(radius):
        p = idc.prev_head(cur)
        if p == BADADDR or p >= cur:
            break
        before.append(p)
        cur = p

    before.reverse()

    addresses = before + [ea]

    cur = ea
    for _ in range(radius):
        n = idc.next_head(cur)
        if n == BADADDR or n <= cur:
            break
        addresses.append(n)
        cur = n

    for x in addresses:
        result.append({
            "ea": hex(x),
            "line": clean_disasm(x),
        })

    return result


def function_disasm(func_ea, limit=MAX_DISASM_LINES):
    f = func_for_ea(func_ea)
    if not f:
        return []

    output = []

    try:
        for ea in idautils.FuncItems(f.start_ea):
            output.append({
                "ea": hex(ea),
                "line": clean_disasm(ea),
            })

            if len(output) >= limit:
                output.append({
                    "ea": "",
                    "line": "[TRUNCATED]",
                })
                break
    except Exception as e:
        output.append({
            "ea": "",
            "line": "[ERROR: %s]" % e,
        })

    return output


def decompile_function(ea):
    try:
        import ida_hexrays

        if not ida_hexrays.init_hexrays_plugin():
            return None

        f = func_for_ea(ea)
        if not f:
            return None

        cfunc = ida_hexrays.decompile(f.start_ea)
        if not cfunc:
            return None

        lines = []

        for sl in cfunc.get_pseudocode():
            lines.append(ida_lines.tag_remove(sl.line))

        text = "\n".join(lines)

        if len(text) > MAX_PSEUDOCODE_CHARS:
            text = text[:MAX_PSEUDOCODE_CHARS]
            text += "\n/* [TRUNCATED] */"

        return text

    except Exception as e:
        return "[DECOMPILATION ERROR: %s]" % e


def function_code_refs(func_ea):
    """
    Collect outgoing code references/calls.
    """
    f = func_for_ea(func_ea)
    if not f:
        return []

    refs = {}
    try:
        for ea in idautils.FuncItems(f.start_ea):
            for target in idautils.CodeRefsFrom(ea, False):
                tf = func_for_ea(target)
                actual = tf.start_ea if tf else target

                refs[actual] = {
                    "ea": hex(actual),
                    "name": safe_name(actual),
                    "segment": seg_name(actual),
                }
    except Exception:
        pass

    return list(refs.values())


def function_data_refs(func_ea):
    """
    Collect outgoing data references/global references.
    """
    f = func_for_ea(func_ea)
    if not f:
        return []

    refs = {}

    try:
        for ea in idautils.FuncItems(f.start_ea):
            for target in idautils.DataRefsFrom(ea):
                if target not in refs:
                    refs[target] = {
                        "ea": hex(target),
                        "name": safe_name(target),
                        "segment": seg_name(target),
                    }
    except Exception:
        pass

    return list(refs.values())


def callers_of(func_ea):
    f = func_for_ea(func_ea)
    if not f:
        return []

    callers = {}

    try:
        for x in idautils.XrefsTo(f.start_ea, 0):
            caller = func_for_ea(x.frm)
            if not caller:
                continue

            callers[caller.start_ea] = {
                "ea": hex(caller.start_ea),
                "name": safe_name(caller.start_ea),
                "xref_ea": hex(x.frm),
            }
    except Exception:
        pass

    return list(callers.values())


# --------------------------------------------------------------------------
# String reconnaissance
# --------------------------------------------------------------------------

def collect_interesting_strings():
    print("[*] Scanning strings...")

    results = []

    try:
        strings = idautils.Strings()
        strings.setup(
            strtypes=[
                ida_nalt.STRTYPE_C,
                ida_nalt.STRTYPE_C_16,
            ]
        )
    except Exception:
        strings = idautils.Strings()

    for s in strings:
        try:
            value = str(s)
            low = value.lower()

            matched = [k for k in KEYWORDS if k in low]

            if not matched:
                continue

            entry = {
                "ea": hex(s.ea),
                "segment": seg_name(s.ea),
                "value": value[:1000],
                "keywords": matched,
                "xrefs": [],
            }

            for x in idautils.XrefsTo(s.ea, 0):
                fx = func_for_ea(x.frm)

                xentry = {
                    "from": hex(x.frm),
                    "line": clean_disasm(x.frm),
                    "function": func_desc(x.frm),
                    "context": instruction_context(
                        x.frm,
                        STRING_XREF_CONTEXT
                    ),
                }

                entry["xrefs"].append(xentry)

            results.append(entry)

        except Exception:
            continue

    print("[+] Interesting strings:", len(results))
    return results


# --------------------------------------------------------------------------
# Varbit mask table search
# --------------------------------------------------------------------------

def make_mask_sequences():
    """
    Classic RuneScape varbit mask table commonly looks like:

      1
      3
      7
      15
      31
      63
      ...
      0x7FFFFFFF
      0xFFFFFFFF

    Search several prefixes so compiler/linker layout differences don't hurt us.
    """

    standard = []

    value = 2
    for _ in range(32):
        standard.append((value - 1) & 0xFFFFFFFF)
        value = (value << 1) & 0xFFFFFFFFFFFFFFFF

    sequences = []

    # Long sequences = very strong evidence.
    for n in [16, 12, 10, 8, 7, 6]:
        sequences.append({
            "name": "classic_masks_%d" % n,
            "values": standard[:n],
        })

    # Some implementations might include zero first.
    with_zero = [0] + standard

    for n in [10, 8, 7]:
        sequences.append({
            "name": "zero_plus_masks_%d" % n,
            "values": with_zero[:n],
        })

    return sequences


def find_all_bytes(blob, needle):
    pos = 0
    while True:
        idx = blob.find(needle, pos)
        if idx < 0:
            break
        yield idx
        pos = idx + 1


def search_mask_tables():
    print("[*] Searching data segments for varbit mask tables...")

    sequences = make_mask_sequences()
    matches = {}
    searched_segments = []

    for seg_ea in idautils.Segments():
        seg = ida_segment.getseg(seg_ea)

        if not seg:
            continue

        size = seg.end_ea - seg.start_ea
        name = ida_segment.get_segm_name(seg)

        # No reason to scan executable code for a static DWORD mask array
        # unless IDA has weird segment metadata.
        if seg.perm & ida_segment.SEGPERM_EXEC:
            continue

        # Protect against giant mapped segments.
        if size <= 0 or size > 512 * 1024 * 1024:
            continue

        print(
            "    scanning %-16s %s - %s (%d bytes)"
            % (
                name,
                hex(seg.start_ea),
                hex(seg.end_ea),
                size
            )
        )

        searched_segments.append({
            "name": name,
            "start": hex(seg.start_ea),
            "end": hex(seg.end_ea),
            "size": size,
        })

        try:
            blob = ida_bytes.get_bytes(seg.start_ea, size)
        except Exception:
            blob = None

        if not blob:
            continue

        for seq in sequences:
            needle = b"".join(
                struct.pack("<I", x)
                for x in seq["values"]
            )

            for off in find_all_bytes(blob, needle):
                ea = seg.start_ea + off

                # Merge matches at same address, preferring strongest sequence.
                if ea not in matches:
                    matches[ea] = {
                        "ea": hex(ea),
                        "segment": name,
                        "patterns": [],
                        "xrefs": [],
                    }

                matches[ea]["patterns"].append(seq["name"])

    # Gather XREFs.
    for ea, item in matches.items():
        xrefs = []

        try:
            for x in idautils.XrefsTo(ea, 0):
                xrefs.append({
                    "from": hex(x.frm),
                    "line": clean_disasm(x.frm),
                    "function": func_desc(x.frm),
                    "context": instruction_context(x.frm, 10),
                })
        except Exception:
            pass

        item["xrefs"] = xrefs

    # Deduplicate nested matches: one table may match 6-, 7-, 8-, 10-entry
    # prefixes at exactly the same EA; that's intentional and useful.
    output = list(matches.values())

    print("[+] Mask-table candidates:", len(output))
    return searched_segments, output


# --------------------------------------------------------------------------
# Generic bit-extraction function scan
# --------------------------------------------------------------------------

SHIFT_MNEMS = {
    "shr",
    "sar",
    "shrx",
    "sarx",
}

BIT_EXTRACT_MNEMS = {
    "bextr",
    "bzhi",
}

AND_MNEMS = {
    "and",
}


def score_bit_function(func_ea):
    """
    Look for functions resembling:

        value >> shift
        value & mask

    or BMI instructions such as BEXTR/BZHI.

    This will naturally produce false positives, so it is only used as
    supporting evidence.
    """

    f = func_for_ea(func_ea)
    if not f:
        return None

    shifts = 0
    ands = 0
    bextrs = 0
    memrefs = 0
    instruction_count = 0

    interesting_lines = []

    try:
        for ea in idautils.FuncItems(f.start_ea):
            instruction_count += 1

            if instruction_count > MAX_FUNC_INSNS_DEEP_SCAN:
                break

            mnem = idc.print_insn_mnem(ea).lower()

            interesting = False

            if mnem in SHIFT_MNEMS:
                shifts += 1
                interesting = True

            elif mnem in AND_MNEMS:
                ands += 1
                interesting = True

            elif mnem in BIT_EXTRACT_MNEMS:
                bextrs += 1
                interesting = True

            try:
                if list(idautils.DataRefsFrom(ea)):
                    memrefs += 1
            except Exception:
                pass

            if interesting and len(interesting_lines) < 30:
                interesting_lines.append({
                    "ea": hex(ea),
                    "line": clean_disasm(ea),
                })

    except Exception:
        return None

    if bextrs == 0 and not (shifts > 0 and ands > 0):
        return None

    score = 0

    score += shifts * 3
    score += ands * 2
    score += bextrs * 12

    # Accessing global tables is interesting for varp/definition access.
    score += min(memrefs, 10)

    # Getters are frequently small.
    if instruction_count <= 30:
        score += 10
    elif instruction_count <= 60:
        score += 6
    elif instruction_count <= 120:
        score += 3

    return {
        "start": f.start_ea,
        "start_hex": hex(f.start_ea),
        "name": safe_name(f.start_ea),
        "segment": seg_name(f.start_ea),
        "size": f.end_ea - f.start_ea,
        "instructions_examined": instruction_count,
        "shifts": shifts,
        "ands": ands,
        "bextr_like": bextrs,
        "data_ref_instructions": memrefs,
        "score": score,
        "interesting_lines": interesting_lines,
    }


def deep_scan_bit_functions():
    if not DEEP_SCAN:
        return []

    print("[*] Deep-scanning functions for bitfield extraction patterns...")

    candidates = []
    count = 0

    for func_ea in idautils.Functions():
        count += 1

        if count % 5000 == 0:
            print("    functions examined:", count)

        candidate = score_bit_function(func_ea)

        if candidate:
            candidates.append(candidate)

    candidates.sort(
        key=lambda x: (
            x["score"],
            -x["size"]
        ),
        reverse=True
    )

    candidates = candidates[:MAX_DEEP_CANDIDATES]

    print("[+] Generic bitfield candidates:", len(candidates))
    return candidates


# --------------------------------------------------------------------------
# Build strong candidate set
# --------------------------------------------------------------------------

def gather_candidate_functions(strings, mask_tables, deep_candidates):
    scores = defaultdict(int)
    reasons = defaultdict(list)

    # Mask-table XREFs are extremely interesting.
    for table in mask_tables:
        for x in table.get("xrefs", []):
            fd = x.get("function")
            if not fd:
                continue

            ea = int(fd["start"], 16)
            scores[ea] += 100

            reasons[ea].append(
                "references potential classic varbit mask table at %s"
                % table["ea"]
            )

    # Keyword string XREFs.
    for item in strings:
        kw = item["keywords"]

        weight = 0

        if any(
            x in kw
            for x in [
                "varbit",
                "varp",
                "varps",
                "clientvar",
                "varclient",
            ]
        ):
            weight = 90

        elif "quest" in kw:
            weight = 45

        elif any(
            x in kw
            for x in [
                "clientscript",
                "client script",
                "opcode",
                "interpreter",
            ]
        ):
            weight = 40

        elif "script" in kw:
            weight = 20

        else:
            weight = 8

        for x in item.get("xrefs", []):
            fd = x.get("function")

            if not fd:
                continue

            ea = int(fd["start"], 16)

            scores[ea] += weight

            reasons[ea].append(
                "references string %r (%s)"
                % (
                    item["value"][:120],
                    ", ".join(kw)
                )
            )

    # Generic bitfield shape.
    for item in deep_candidates:
        ea = item["start"]
        scores[ea] += item["score"]

        reasons[ea].append(
            "bitfield pattern: shifts=%d ands=%d bextr=%d"
            % (
                item["shifts"],
                item["ands"],
                item["bextr_like"],
            )
        )

    ranked = []

    for ea, score in scores.items():
        ranked.append({
            "ea": ea,
            "ea_hex": hex(ea),
            "name": safe_name(ea),
            "score": score,
            "reasons": reasons[ea],
        })

    ranked.sort(
        key=lambda x: x["score"],
        reverse=True
    )

    return ranked


# --------------------------------------------------------------------------
# Detailed candidate dumping
# --------------------------------------------------------------------------

def dump_candidate_details(ranked):
    print("[*] Dumping strongest candidates...")

    output = []

    for i, item in enumerate(
        ranked[:MAX_DECOMPILE_CANDIDATES]
    ):
        ea = item["ea"]

        print(
            "    [%02d] %s %s score=%d"
            % (
                i + 1,
                hex(ea),
                safe_name(ea),
                item["score"]
            )
        )

        f = func_for_ea(ea)
        if not f:
            continue

        entry = {
            "rank": i + 1,
            "ea": hex(ea),
            "name": safe_name(ea),
            "segment": seg_name(ea),
            "size": f.end_ea - f.start_ea,
            "score": item["score"],
            "reasons": item["reasons"],
            "callers": callers_of(ea),
            "callees": function_code_refs(ea),
            "data_refs": function_data_refs(ea),
            "pseudocode": decompile_function(ea),
            "disassembly": function_disasm(ea),
        }

        output.append(entry)

    return output


# --------------------------------------------------------------------------
# Imports
# --------------------------------------------------------------------------

def collect_interesting_imports():
    result = []

    def imp_cb(ea, name, ordinal):
        if not name:
            return True

        low = name.lower()

        if any(k in low for k in KEYWORDS):
            result.append({
                "ea": hex(ea),
                "name": name,
                "ordinal": ordinal,
            })

        return True

    try:
        qty = ida_nalt.get_import_module_qty()

        for i in range(qty):
            module = ida_nalt.get_import_module_name(i)

            before = len(result)

            ida_nalt.enum_import_names(i, imp_cb)

            for x in result[before:]:
                x["module"] = module

    except Exception:
        pass

    return result


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------

def main():
    print("=" * 72)
    print("OSRS QUEST / VARBIT NATIVE CLIENT PROBE")
    print("=" * 72)

    input_path = ida_nalt.get_input_file_path()
    idb_path = idc.get_idb_path()

    print("[*] Input:", input_path)
    print("[*] IDB:  ", idb_path)
    print("[*] Base: ", hex(idaapi.get_imagebase()))

    report = {
        "metadata": {
            "input_file": input_path,
            "root_filename": ida_nalt.get_root_filename(),
            "idb_path": idb_path,
            "imagebase": hex(idaapi.get_imagebase()),
            "ida_version": idaapi.get_kernel_version(),
            "deep_scan": DEEP_SCAN,
        },
        "segments": [],
        "interesting_imports": [],
        "interesting_strings": [],
        "mask_search_segments": [],
        "mask_tables": [],
        "generic_bitfield_candidates": [],
        "ranked_candidates": [],
        "candidate_details": [],
    }

    # Segment map
    for ea in idautils.Segments():
        seg = ida_segment.getseg(ea)

        if not seg:
            continue

        report["segments"].append({
            "name": ida_segment.get_segm_name(seg),
            "start": hex(seg.start_ea),
            "end": hex(seg.end_ea),
            "size": seg.end_ea - seg.start_ea,
            "perm": seg.perm,
        })

    report["interesting_imports"] = collect_interesting_imports()

    strings = collect_interesting_strings()
    report["interesting_strings"] = strings

    searched_segments, mask_tables = search_mask_tables()

    report["mask_search_segments"] = searched_segments
    report["mask_tables"] = mask_tables

    deep_candidates = deep_scan_bit_functions()

    # Remove integer-only internal field before serialization after ranking.
    report["generic_bitfield_candidates"] = [
        {
            k: v
            for k, v in x.items()
            if k != "start"
        }
        for x in deep_candidates
    ]

    ranked = gather_candidate_functions(
        strings,
        mask_tables,
        deep_candidates
    )

    report["ranked_candidates"] = [
        {
            "ea": x["ea_hex"],
            "name": x["name"],
            "score": x["score"],
            "reasons": x["reasons"],
        }
        for x in ranked[:250]
    ]

    report["candidate_details"] = dump_candidate_details(ranked)

    # Write next to IDB.
    base = os.path.splitext(idb_path)[0]
    outfile = base + "_quest_varbit_probe.json"

    try:
        with open(
            outfile,
            "w",
            encoding="utf-8"
        ) as fp:
            json.dump(
                report,
                fp,
                indent=2,
                ensure_ascii=False
            )

    except Exception:
        # Fallback to user's home dir.
        outfile = os.path.join(
            os.path.expanduser("~"),
            "osrs_quest_varbit_probe.json"
        )

        with open(
            outfile,
            "w",
            encoding="utf-8"
        ) as fp:
            json.dump(
                report,
                fp,
                indent=2,
                ensure_ascii=False
            )

    print()
    print("=" * 72)
    print("[+] COMPLETE")
    print("[+] Report:", outfile)
    print("[+] Keyword strings:", len(strings))
    print("[+] Mask tables:", len(mask_tables))
    print("[+] Bitfield candidates:", len(deep_candidates))
    print("[+] Ranked candidates:", len(ranked))
    print("=" * 72)

    print()
    print("Send me the generated JSON file.")
    print(
        "The highest-value sections are mask_tables and "
        "candidate_details."
    )


try:
    main()
except Exception:
    print("[!] SCRIPT FAILED")
    traceback.print_exc()