"""Focused scene/object discovery pass for the loaded OSRS x64 IDA database.

This intentionally reports evidence rather than installing offsets. It searches
for client binding names and nearby strings, then records their xrefs and a
bounded instruction window so candidate scene/object accessors can be reviewed.
"""
import json
import re

import ida_funcs
import ida_idaapi
import idaapi
import idautils
import idc

KEYWORDS = (
    "object", "gameobject", "scene", "tile", "loc", "scenery", "boundary",
    "decoration", "ground", "wall", "tree", "oak", "rock", "door", "ladder",
    "getloc", "getobject", "findobject", "objectid", "objectname",
)
BINDINGS = (
    "getNpcIdAll", "npcCoord", "npcName", "playerCoord", "worldToScreenCoord",
    "getMapOrigin", "getMapCoordinate", "coord", "scene", "tile", "object",
    "gameObject", "loc", "locId", "locName", "objectId", "objectName",
    "getObjectIdAll", "getLocIdAll", "getSceneObjects", "getTileObjects",
)
MAX_REFS = 32
MAX_INSNS = 80


def text_at(ea):
    try:
        return idc.generate_disasm_line(ea, 0) or ""
    except Exception:
        return ""


def function_dump(ea):
    fn = ida_funcs.get_func(ea)
    if not fn:
        return None
    lines = []
    cur = fn.start_ea
    count = 0
    while cur != ida_idaapi.BADADDR and cur < fn.end_ea and count < MAX_INSNS:
        line = text_at(cur)
        if line:
            lines.append({"ea": hex(cur), "asm": line})
            count += 1
        nxt = idc.next_head(cur, fn.end_ea)
        if nxt == cur:
            break
        cur = nxt
    return {"start": hex(fn.start_ea), "end": hex(fn.end_ea), "lines": lines}


def string_hits():
    hits = []
    for s in idautils.Strings():
        try:
            value = str(s)
        except Exception:
            continue
        lower = value.lower()
        if not any(k in lower for k in KEYWORDS):
            continue
        refs = []
        for xr in idautils.XrefsTo(s.ea):
            fn = ida_funcs.get_func(xr.frm)
            refs.append({
                "from": hex(xr.frm),
                "func": hex(fn.start_ea) if fn else None,
                "asm": text_at(xr.frm),
            })
            if len(refs) >= MAX_REFS:
                break
        hits.append({"ea": hex(s.ea), "value": value[:240], "refs": refs})
    return hits


def binding_hits():
    result = {}
    for wanted in BINDINGS:
        entries = []
        for s in idautils.Strings():
            try:
                value = str(s)
            except Exception:
                continue
            if value != wanted:
                continue
            for xr in idautils.XrefsTo(s.ea):
                fn = ida_funcs.get_func(xr.frm)
                item = {"string_ea": hex(s.ea), "xref": hex(xr.frm), "asm": text_at(xr.frm)}
                if fn:
                    item["function"] = function_dump(fn.start_ea)
                entries.append(item)
        if entries:
            result[wanted] = entries
    return result


def candidate_functions():
    """Return named functions whose names themselves mention object/scene terms."""
    out = []
    for ea in idautils.Functions():
        name = idc.get_func_name(ea) or ""
        if any(k in name.lower() for k in KEYWORDS):
            out.append({"ea": hex(ea), "name": name, "function": function_dump(ea)})
            if len(out) >= 200:
                break
    return out


result = {
    "image_base": hex(idaapi.get_imagebase()) if "idaapi" in globals() else None,
    "bindings": binding_hits(),
    "keyword_strings": string_hits(),
    "candidate_functions": candidate_functions(),
}
print("SCENE_OBJECT_DISCOVERY_BEGIN")
print(json.dumps(result, indent=2, sort_keys=True))
print("SCENE_OBJECT_DISCOVERY_END")
