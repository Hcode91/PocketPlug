import struct,sys
from capstone import *
d=open('/home/claude/vft/ram.bin','rb').read()
md=Cs(CS_ARCH_ARM,CS_MODE_ARM)
def r32(a): return struct.unpack_from('<I',d,a-0x10000000)[0]
def fstart(a):
    while True:
        w=r32(a)
        if (w&0xffff0000)==0xe92d0000 or w==0xe52de004 or ((w&0xfffff000)==0xe24dd000 and (r32(a-4)&0xff000000) not in (0xe5000000,0xe9000000,0xe2000000,0xe1000000,0xe3000000,0xeb000000,0x0a000000,0x1a000000,0xea000000)): return a
        a-=4
def callers(T,lo=0x10999000,hi=0x10ae0000):
    out=[]
    for a in range(lo,hi,4):
        w=r32(a)
        if (w&0x0e000000)==0x0a000000:
            off=w&0xffffff
            if off&0x800000: off-=0x1000000
            if a+8+off*4==T: out.append(a)
    return out
def dis(a,n):
    for i in md.disasm(d[a-0x10000000:a-0x10000000+n*4],a):
        s=i.op_str
        if i.mnemonic=='ldr' and '[pc, #' in s:
            off=int(s.split('#')[1].rstrip(']'),16); s+='   ; =%08x'%r32(i.address+8+off)
        print('%08x %s %s'%(i.address,i.mnemonic,s))
