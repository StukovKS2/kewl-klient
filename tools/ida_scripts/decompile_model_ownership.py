import idaapi, idc, idautils, ida_hexrays, json, os

TARGETS = [0x1400A4D80, 0x1400A2520, 0x14007B070, 0x1405CEF80]

def decomp(ea):
    f = idaapi.get_func(ea)
    if not f:
        return {'ea': hex(ea), 'error': 'no function'}
    try:
        text = str(ida_hexrays.decompile(f.start_ea))
    except Exception as exc:
        text = 'ERR ' + repr(exc)
    return {'ea': hex(ea), 'start': hex(f.start_ea),
            'end': hex(f.end_ea), 'name': idaapi.get_name(f.start_ea),
            'text': text}

out = {'targets': [decomp(ea) for ea in TARGETS], 'refs': []}
for target in TARGETS:
    refs = []
    for xref in idautils.CodeRefsTo(target, 0):
        f = idaapi.get_func(xref)
        if f:
            refs.append(decomp(f.start_ea))
    out['refs'].append({'target': hex(target), 'callers': refs})

# The release helper observed in the bridge decomp. Collect its callers too;
# these are the places where ownership of the returned pair can be settled.
for name in ['sub_140044D30', 'sub_140044D30']:
    ea = idaapi.get_name_ea(idaapi.BADADDR, name)
    if ea == idaapi.BADADDR:
        continue
    out['release_helper'] = decomp(ea)
    out['release_callers'] = [decomp(idaapi.get_func(x).start_ea)
                              for x in idautils.CodeRefsTo(ea, 0)
                              if idaapi.get_func(x)]
    break

payload = json.dumps(out)
print(payload)
root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
with open(os.path.join(root, 'build', 'ida_model_ownership.json'), 'w', encoding='utf-8') as f:
    f.write(payload)
idaapi.qexit(0)
