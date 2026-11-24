import sys,capstone
d=open(sys.argv[1],'rb').read(); base=int(sys.argv[2],16); pc=int(sys.argv[3],16); n=int(sys.argv[4])
thumb=len(sys.argv)>5
md=capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB if thumb else capstone.CS_MODE_ARM)
off=pc-base
for i in md.disasm(d[off:off+n*4], pc):
    extra=''
    if i.mnemonic.startswith('ldr') and '[pc, #' in i.op_str:
        try:
            imm=int(i.op_str.split('#')[1].rstrip(']'),16 if '0x' in i.op_str.split('#')[1] else 10)
            a=i.address+8+imm; v=int.from_bytes(d[a-base:a-base+4],'little'); extra='  ; =0x%08x'%v
        except: pass
    print('%08x: %-8s %s%s'%(i.address,i.mnemonic,i.op_str,extra))
