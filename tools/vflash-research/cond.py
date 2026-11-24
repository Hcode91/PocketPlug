import re,sys
cmds=[]; nb=0
for l in open(sys.argv[1],errors='replace'):
    if l.startswith('[uart'): cmds.append('   '+l.strip()); continue
    m=re.match(r'(\w) c4(\w{6}) = (\w+)  pc (\w+)  t=(\d+)',l)
    if not m: continue
    rw,reg,v,pc,t=m.groups(); reg=int(reg,16); v=int(v,16)
    if rw=='W' and reg==0: nb=v
    elif rw=='W' and reg==8: cmds.append('t=%s cmd(%d) %06x'%(t,nb,v))
    elif reg not in (0x14,0x64,0,8,0xc): cmds.append('t=%s %s %02x=%08x pc %s'%(t,rw,reg,v,pc))
out=[]; prev=None; n=0
for c in cmds:
    k=c.split(' ',1)[1] if c.startswith('t=') else c
    if k==prev: n+=1; continue
    if n: out.append('   x%d'%n)
    out.append(c); prev=k; n=0
if n: out.append('   x%d'%n)
start=0
if len(sys.argv)>2:
    for j,x in enumerate(out):
        if sys.argv[2] in x: start=j; break
print('\n'.join(out[start:start+int(sys.argv[3]) if len(sys.argv)>3 else None]))
