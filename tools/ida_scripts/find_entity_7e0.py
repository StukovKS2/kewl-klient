import idaapi,idc,idautils,json
hits=[]
for fs in idautils.Functions():
 f=idaapi.get_func(fs); ea=f.start_ea
 while ea<f.end_ea:
  asm=idc.generate_disasm_line(ea,0) or ''
  if '7E0h' in asm or '7e0h' in asm:
   hits.append({'ea':hex(ea),'func':hex(f.start_ea),'name':idaapi.get_name(f.start_ea),'asm':asm})
  ins=idaapi.insn_t(); n=idaapi.decode_insn(ins,ea)
  if not n: break
  ea+=n
print(json.dumps(hits))
