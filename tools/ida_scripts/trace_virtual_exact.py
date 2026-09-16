import idaapi,idc,idautils,ida_hexrays,json
addrs=[0x14002BB40,0x14002BE90,0x1400699290,0x14006BB8A0,0x14006BBB90,0x14006BBE10,0x14006BC010,0x14006BC190,0x14006BC390,0x14002BC60,0x14002BD30,0x14002BF50,0x1406BBA10,0x1406BBF70,0x1406BC2F0,0x1406BC4F0]
out=[]
for ea in addrs:
 f=idaapi.get_func(ea)
 if not f: out.append({'ea':hex(ea),'error':'no func'}); continue
 try:t=str(ida_hexrays.decompile(f.start_ea))
 except Exception as e:t='ERR '+repr(e)
 out.append({'ea':hex(ea),'name':idaapi.get_name(f.start_ea),'text':t})
# all code refs to known render vtables and direct data refs to offsets in wrapper-like code
print(json.dumps(out))
