import struct, sys, numpy as np
from PIL import Image, ImageDraw
raw=open('/home/claude/vft/ram.bin','rb').read()
def s16(v): return v-65536 if v&0x8000 else v
lines=open('/home/claude/vft/g21.txt').read().split('\n')[269:836]
M=np.eye(3); T=np.zeros(3); S=np.ones(3); meshes=[]
i=0
while i<len(lines):
    l=lines[i]
    if l.startswith('L '):
        ws=[int(x,16) for x in l.split()[2:]]; k=1
        while k<len(ws):
            w=ws[k]
            if w>>24==0x20:
                reg=(w>>16)&0xff; n=w&0xffff; nw=(n+1)//2; data=ws[k+1:k+1+nw]; hs=[]
                for dw in data: hs+= [s16(dw&0xffff), s16(dw>>16)]
                hs=hs[:n]
                if reg==0x40: M=np.array(hs,dtype=float).reshape(3,3)/4096
                if reg==0x4c: T=np.array(hs,dtype=float)
                if reg==0x3c: S=np.array(hs,dtype=float)/4096
                k+=1+nw; continue
            if w==0x09000000:
                tg=ws[k+1]; meshes.append((tg,M.copy(),T.copy(),S.copy())); k+=2; continue
            k+=1
    i+=1
print(len(meshes),'meshes')
def mesh(tg):
    o=tg-0x10000000; op,vb,cnt=struct.unpack_from('<III',raw,o)
    if op!=0x0a000000: return None
    n=cnt&0xffff; idx=np.frombuffer(raw,dtype='<u2',count=n,offset=o+12)//5
    nv=int(idx.max())+1
    v=np.frombuffer(raw,dtype='<f2',count=nv*10,offset=vb-0x10000000).reshape(nv,10).astype(float)
    return idx,v,cnt
if __name__=='__main__':
    K=float(sys.argv[1]) if len(sys.argv)>1 else 4096
    pts=[]
    for tg,M_,T_,S_ in meshes:
        m=mesh(tg)
        if not m: continue
        idx,v,cnt=m
        p=(v[:,0:3]*S_*K)@M_.T+T_
        pts.append(p)
    P=np.concatenate(pts); print('K',K,'n',len(P),'z range',np.percentile(P[:,2],[1,50,99]).round(0),'x',np.percentile(P[:,0],[1,50,99]).round(0),'y',np.percentile(P[:,1],[1,50,99]).round(0))
