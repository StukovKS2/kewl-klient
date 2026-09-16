import idaapi,idc,idautils,ida_hexrays,json
addrs=[0x14002BE90,0x1400699290,0x14006BB8A0,0x14006BBB90,0x14006BBE10,0x14006BC010,0x14006BC190,0x14006BC390]
out=[]
for ea in addrs:
 f=idaapi.get_func(ea)
 if not f: continue
 try:t=str(ida_hexrays.decompile(f.start_ea))
 except Exception as e:t='ERR '+repr(e)
 out.append({'ea':hex(ea),'name':idaapi.get_name(f.start_ea),'text':t})
# collect constructors writing known render vtables and callers of wrapper
vnames=['off_140B93150','off_140B93160','off_140C000E8','off_140C000F8','off_140C000C8']
vt=[]
for n in vnames:
 ea=idaapi.get_name_ea(idaapi.BADADDR,n)
 if ea==idaapi.BADADDR: continue
 refs=[]
 for x in idautils.DataRefsTo(ea):
  f=idaapi.get_func(x); refs.append({'ea':hex(x),'func':hex(f.start_ea) if f else None,'name':idaapi.get_name(f.start_ea) if f else ''})
 vt.append({'name':n,'ea':hex(ea),'refs':refs})
print(json.dumps({'decomp':out,'vtable_refs':vt}))
