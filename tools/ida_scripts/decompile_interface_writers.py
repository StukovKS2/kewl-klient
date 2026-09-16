import idaapi,ida_hexrays,json
addrs=[0x1400265E0,0x140029AE0,0x14002C180,0x14002C330,0x1400329C0,0x140033790,0x140034170,0x140039230]
out=[]
for ea in addrs:
 f=idaapi.get_func(ea)
 if not f: continue
 try:t=str(ida_hexrays.decompile(f.start_ea))
 except Exception as e:t='ERR '+repr(e)
 out.append({'ea':hex(ea),'name':idaapi.get_name(f.start_ea),'text':t})
print(json.dumps(out))
