# From a start address, follow bl calls (depth N) and report hardware literals each function uses.
import struct,sys
d=open(sys.argv[1],'rb').read(); base=0x10000000
start=int(sys.argv[2],16); depth=int(sys.argv[3])
def func_range(a):
    # scan forward until a return pattern (ldm..pc / mov pc,lr / pop pc) - up to 4KB
    i=a
    while i<a+0x1000:
        w=struct.unpack('<I',d[i-base:i-base+4])[0]
        if w in (0xe1a0f00e,) or (w&0xffff8000)==0xe8bd8000 or (w&0xffff8000)==0xe8fd8000 or w==0xe49df004: return a,i+4
        i+=4
    return a,i
seen=set()
def visit(a,lvl):
    if a in seen or lvl>depth or not (base<=a<base+len(d)): return
    seen.add(a)
    s,e=func_range(a); hw=set(); calls=[]
    for i in range(s,e,4):
        w=struct.unpack('<I',d[i-base:i-base+4])[0]
        if (w&0x0f7f0000)==0x051f0000:
            imm=w&0xfff; t=i+8+(imm if w&(1<<23) else -imm)
            if base<=t<base+len(d)-4:
                v=struct.unpack('<I',d[t-base:t-base+4])[0]
                if 0x90000000<=v<0xe0000000: hw.add(hex(v))
        if (w>>24)==0xeb:
            off=w&0xffffff
            if off&0x800000: off-=0x1000000
            calls.append(i+8+off*4)
    if hw: print('  '*lvl+hex(a), sorted(hw))
    for c in calls: visit(c,lvl+1)
visit(start,0)
