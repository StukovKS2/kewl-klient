import idaapi,idc,idautils,ida_hexrays,json
for target in [0x14002BC60,0x14002BDC0,0x14002BB40,0x14002BE90]:
 print('TARGET',hex(target),idaapi.get_name(target))
 for x in idautils.CodeRefsTo(target,0):
  f=idaapi.get_func(x); print('REF',hex(x),hex(f.start_ea) if f else None,idaapi.get_name(f.start_ea) if f else '')
# decompile functions that reference constructor directly
out=[]
for x in idautils.CodeRefsTo(0x14002BC60,0):
 f=idaapi.get_func(x)
 if f:
  try:t=str(ida_hexrays.decompile(f.start_ea))
  except Exception as e:t='ERR '+repr(e)
  out.append({'start':hex(f.start_ea),'name':idaapi.get_name(f.start_ea),'text':t})
print(json.dumps(out))
