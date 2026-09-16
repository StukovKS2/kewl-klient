import idaapi,idc,idautils,ida_hexrays,json
refs=list(idautils.CodeRefsTo(0x14002BB40,0))
out=[]
for x in refs:
 f=idaapi.get_func(x)
 if not f: continue
 try:t=str(ida_hexrays.decompile(f.start_ea))
 except Exception as e:t='ERR '+repr(e)
 out.append({'xref':hex(x),'start':hex(f.start_ea),'end':hex(f.end_ea),'name':idaapi.get_name(f.start_ea),'text':t})
print(json.dumps(out))
