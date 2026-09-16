import idaapi,idc,idautils,json,re
hits=[]
for fs in idautils.Functions():
 f=idaapi.get_func(fs); ea=f.start_ea
 while ea<f.end_ea:
  asm=(idc.generate_disasm_line(ea,0) or '')
  low=asm.lower().replace(' ','')
  if any(p in low for p in ['[rcx+18h]','[rdi+18h]','[rsi+18h]','[rbx+18h]','[r14+18h]','[r15+18h]','[rcx+20h]','[rdi+20h]','[rsi+20h]','[rbx+20h]']):
   hits.append({'ea':hex(ea),'func':hex(f.start_ea),'name':idaapi.get_name(f.start_ea),'asm':asm})
  ins=idaapi.insn_t(); n=idaapi.decode_insn(ins,ea)
  if not n: break
  ea+=n
print(json.dumps(hits))
