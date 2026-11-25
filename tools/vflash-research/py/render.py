import struct, sys, numpy as np
from PIL import Image
raw=open('/home/claude/vft/ram.bin','rb').read()
mem=np.frombuffer(raw,dtype=np.uint8)
B=0x48000
def s16(v): return v-65536 if v&0x8000 else v
lines=open('/home/claude/vft/g21.txt').read().split('\n')[269:836]
M=np.eye(3); T=np.zeros(3); S=np.ones(3); tex=(0,0); fmt=0; meshes=[]
for l in lines:
    if not l.startswith('L '): continue
    ws=[int(x,16) for x in l.split()[2:]]; k=1
    while k<len(ws):
        w=ws[k]; op=w>>24
        if op==0x20:
            reg=(w>>16)&0xff; n=w&0xffff; nw=(n+1)//2; hs=[]
            for dw in ws[k+1:k+1+nw]: hs+=[s16(dw&0xffff),s16(dw>>16)]
            hs=hs[:n]
            if reg==0x40: M=np.array(hs,dtype=float).reshape(3,3)/4096
            if reg==0x4c: T=np.array(hs,dtype=float)
            if reg==0x3c: S=np.array(hs,dtype=float)/4096
            k+=1+nw; continue
        if op==0x87: tex=(w&0xfff,(w>>12)&0xfff)
        if op==0x86: fmt=w&0xffffff
        if w==0x09000000: meshes.append((ws[k+1],M.copy(),T.copy(),S.copy(),tex,fmt)); k+=2; continue
        k+=1
def texture(tx,ty,fmt):
    w=8<<(fmt&7); h=8<<((fmt>>3)&7)
    X=np.arange(w)+tx*2; Y=np.arange(h)[:,None]+ty
    a=B+(Y//8)*16384+(X//64)*512+(Y%8)*64+X%64
    return mem[a].astype(np.float32)
W,H=640,360
img=np.zeros((H,W),dtype=np.float32); zb=np.full((H,W),1e9,dtype=np.float32)
K=4096.0; tris=[]
for tg,M_,T_,S_,tx,fm in meshes:
    o=tg-0x10000000; op,vb,cnt=struct.unpack_from('<III',raw,o)
    if op!=0x0a000000: continue
    n=cnt&0xffff; idx=np.frombuffer(raw,dtype='<u2',count=n,offset=o+12)//5; nv=int(idx.max())+1
    v=np.frombuffer(raw,dtype='<f2',count=nv*10,offset=vb-0x10000000).reshape(nv,10).astype(float)
    p=(v[:,0:3]*S_*K)@M_.T+T_; z=-p[:,2]
    t=texture(tx[0],tx[1],fm)
    for i in range(0,len(idx)-2,3):
        a,b,c=idx[i:i+3]
        if min(z[a],z[b],z[c])<50: continue
        tris.append((p[[a,b,c],0]/z[[a,b,c]],p[[a,b,c],1]/z[[a,b,c]],z[[a,b,c]],v[[a,b,c],6],v[[a,b,c],7],t))
A=np.concatenate([np.stack([t[0],t[1]],1) for t in tris]); lo=np.percentile(A,3,axis=0); hi=np.percentile(A,97,axis=0)
sc=min(W/(hi[0]-lo[0]),H/(hi[1]-lo[1]))
for sx,sy,z,u,vv,t in tris:
    x=(sx-lo[0])*sc; y=(sy-lo[1])*sc
    x0=max(int(np.floor(x.min())),0); x1=min(int(np.ceil(x.max())),W-1); y0=max(int(np.floor(y.min())),0); y1=min(int(np.ceil(y.max())),H-1)
    if x1<x0 or y1<y0: continue
    gx,gy=np.meshgrid(np.arange(x0,x1+1)+0.5,np.arange(y0,y1+1)+0.5)
    den=(y[1]-y[2])*(x[0]-x[2])+(x[2]-x[1])*(y[0]-y[2])
    if abs(den)<1e-9: continue
    l0=((y[1]-y[2])*(gx-x[2])+(x[2]-x[1])*(gy-y[2]))/den; l1=((y[2]-y[0])*(gx-x[2])+(x[0]-x[2])*(gy-y[2]))/den; l2=1-l0-l1
    m=(l0>=0)&(l1>=0)&(l2>=0)
    if not m.any(): continue
    iz=l0/z[0]+l1/z[1]+l2/z[2]; zz=1/iz
    uu=(l0*u[0]/z[0]+l1*u[1]/z[1]+l2*u[2]/z[2])*zz; vv2=(l0*vv[0]/z[0]+l1*vv[1]/z[1]+l2*vv[2]/z[2])*zz
    th,tw=t.shape
    tu=(np.floor(uu*tw).astype(int))%tw; tv=(np.floor(vv2*th).astype(int))%th
    col=t[tv,tu]
    sub=zb[y0:y1+1,x0:x1+1]; m&=zz<sub
    sub[m]=zz[m]; img[y0:y1+1,x0:x1+1][m]=col[m]
Image.fromarray(img.astype(np.uint8)).save('/home/claude/vft/vflash-scene-grey.png'); print(len(tris))
