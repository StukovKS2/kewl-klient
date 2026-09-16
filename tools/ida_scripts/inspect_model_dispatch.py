import idaapi, idc, idautils, ida_hexrays, json

TARGETS = [0x14007B070, 0x1400A2520, 0x1400A1C60, 0x1400A1CD0]

def disasm(ea, count=500):
    out = []
    for i in range(count):
        p = ea if i == 0 else idc.next_head(int(out[-1]['ea'], 16), ea + 0x400)
        if p == idc.BADADDR: break
        out.append({'ea': hex(p), 'text': idc.generate_disasm_line(p, 0) or ''})
    return out

def function(ea):
    f = idaapi.get_func(ea)
    if not f:
        return {'ea': hex(ea), 'error': 'no function', 'disasm': disasm(ea)}
    item = {'ea': hex(ea), 'start': hex(f.start_ea), 'end': hex(f.end_ea),
            'name': idaapi.get_name(f.start_ea), 'disasm': disasm(f.start_ea)}
    try: item['decomp'] = str(ida_hexrays.decompile(f.start_ea))
    except Exception as exc: item['decomp_error'] = repr(exc)
    return item

out = {'targets': [function(ea) for ea in TARGETS], 'refs': []}
for target in TARGETS:
    callers = []
    for x in idautils.CodeRefsTo(target, 0):
        f = idaapi.get_func(x)
        callers.append(function(f.start_ea if f else x))
    out['refs'].append({'target': hex(target), 'callers': callers})

print(json.dumps(out))
with open(r'C:\Users\trump\Desktop\KewlClient\kewl-klient\build\ida_model_dispatch.json', 'w') as f:
    f.write(json.dumps(out))
idaapi.qexit(0)
