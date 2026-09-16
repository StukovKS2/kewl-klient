import idaapi,ida_hexrays,idautils,json
addrs=[0x140048590,0x1400582B0,0x140058590,0x140031460,0x1400A3C70,0x140190650]
out=[]
for ea in addrs:
 f=idaapi.get_func(ea)
 if not f: continue
 try:t=str(ida_hexrays.decompile(f.start_ea))
 except Exception as e:t='ERR '+repr(e)
 out.append({'ea':hex(ea),'name':idaapi.get_name(f.start_ea),'text':t})
print(json.dumps(out))
