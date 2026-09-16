# find_ground_item_anchors.py -- sweep the IDB for the client's own Lua binding names
# around ground items / scene item stacks, and report the functions that reference each.
#
# Method per .claude/skills/deob/SKILL.md: anchor on a name the client registers itself,
# then walk to the registration and the leaf it registers. The candidate list is
# pattern-based because the exact spellings are unknown -- this sweep REPORTS, it does
# not conclude. Names that look like bindings but are not (launch-arg parsers, Graphics
# drawing helpers) are a known trap; the surrounding strings are dumped so the caller
# can judge context before trusting a hit.
#
# Run headless:
#   idat.exe -A -S"...\\find_ground_item_anchors.py" build\\ida\\osclient.exe
# Output:
#   tools/ground_item_anchors.json   (results; the caller reads THIS, not stdout)
#   stdout: one RESULT_JSON line (same payload) for console runs

import idautils
import ida_segment
import ida_bytes
import ida_funcs
import ida_name
import json
import os

# Exact candidates first (highest signal), then regex families.
EXACT = [
    "groundItem", "groundItems", "getGroundItem", "groundItemsAt", "groundItemAt",
    "getGroundItems", "tileItem", "tileItems", "getTileItems", "getTileItem",
    "itemStack", "getItemStack", "sceneItem", "getSceneItem", "groundObj",
    "getGroundObj", "floorItem", "getFloorItem", "getGroundItemStack",
    "groundItemStack", "itemAtTile", "getItemAtTile", "tileStack",
    # container-adjacent shapes we already know the family of:
    "invGetObjId", "invGetNum", "invSize",
]
FAMILIES = [
    "ground", "tileitem", "flooritem", "itemstack", "sceneitem", "despawn",
    "landeditem", "dropped",
]
# Bindings we already trust, as sanity calibration: if the sweep cannot find these,
# the pass itself is broken rather than the targets missing.
CALIBRATION = {"npcCoord", "npcName", "invGetObjId", "worldToScreenCoord", "getVarp"}


def seg_bytes(seg):
    return ida_bytes.get_bytes(seg.start_ea, seg.end_ea - seg.start_ea) or b""


def refs_to(ea):
    out = []
    for xr in idautils.XrefsTo(ea):
        f = ida_funcs.get_func(xr.frm)
        out.append({
            "from": hex(xr.frm),
            "func": hex(f.start_ea) if f else None,
            "funcend": hex(f.end_ea) if f else None,
        })
    return out


def neighbours(ea, span=96):
    """Strings physically near the hit -- the context that catches
    'names that look like bindings but are not'."""
    out = []
    lo, hi = max(0, ea - span), ea + span
    for s in idautils.Strings():
        if lo <= s.ea <= hi:
            try:
                out.append(str(s))
            except Exception:
                pass
    return out[:12]


results = {"exact": {}, "family": {}, "missing_calibration": sorted(CALIBRATION)}
seen_strings = set()

for seg in ida_segment.get_segm_by_name(".rdata"), ida_segment.get_segm_by_name(".data"):
    if seg is None:
        continue
    data = seg_bytes(seg)
    if not data:
        continue
    base = seg.start_ea
    low = data.lower()

    # --- exact names (byte-exact, NUL-terminated: needle + b"\\x00")
    for name in EXACT + sorted(CALIBRATION):
        needle = name.encode() + b"\\x00"
        start = 0
        while True:
            i = low.find(needle, start)
            if i < 0:
                break
            start = i + 1
            ea = base + i
            key = (name, hex(ea))
            if key in seen_strings:
                continue
            seen_strings.add(key)
            bucket = results["exact"] if name not in CALIBRATION else results.setdefault("calibration", {})
            bucket.setdefault(name, []).append({
                "ea": hex(ea),
                "refs": refs_to(ea),
                "context": neighbours(ea),
            })

    # --- family substrings (cast a wider net; expect noise)
    for fam in FAMILIES:
        start = 0
        hits = 0
        while hits < 200:  # cap per family
            i = low.find(fam.encode(), start)
            if i < 0:
                break
            start = i + 1
            # only report if it sits inside a printable ASCII run (a string, not code)
            lo_b = i
            while lo_b > 0 and 0x20 <= data[lo_b - 1] < 0x7F:
                lo_b -= 1
            hi_b = i
            while hi_b < len(data) and 0x20 <= data[hi_b] < 0x7F:
                hi_b += 1
            word = data[lo_b:hi_b].decode("ascii", "replace")
            if not (2 <= len(word) <= 48) or word.lower() == fam:
                continue
            ea = base + lo_b
            if (word, ea) in seen_strings:
                continue
            seen_strings.add((word, ea))
            hits += 1
            results["family"].setdefault(fam, []).append({
                "string": word,
                "ea": hex(ea),
                "refs": refs_to(ea),
            })

missing_cal = [n for n in CALIBRATION
               if n not in results.get("calibration", {})]

results["missing_calibration"] = missing_cal
payload = json.dumps(results)

out_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "ground_item_anchors.json")
with open(out_path, "w") as fh:
    fh.write(payload)

print("RESULT_JSON=" + payload[:2000])
print("WROTE " + out_path)
