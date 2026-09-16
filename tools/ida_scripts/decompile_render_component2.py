import idaapi,ida_hexrays,json
addrs=[0x14002C180,0x14002C330,0x14002C390,0x14002BFE0,0x14002BDC0,0x14002D290,0x14002D2F0,0x140032550,0x140032750]
out=[]
for ea in addrs:
 f=idaapi.get_func(ea)
 if not f: continue
 try:t=str(ida_hexrays.decompile(f.start_ea))
 except Exception as e:t='ERR '+repr(e)
 out.append({'ea':hex(ea),'name':idaapi.get_name(f.start_ea),'text':t})
print(json.dumps(out))
