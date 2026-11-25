import struct, sys, numpy as np
from PIL import Image
raw=open('/home/claude/vft/ram.bin','rb').read()
mem=np.frombuffer(raw,dtype=np.uint8)
B=0x48000
def s16(v): return v-65536 if v&0x8000 else v
lines=open('/home/claude/vft/g21.txt').read().split('\n')[269:836]
M=np.eye(3); T=np.zeros(3); S=np.ones(3); tex=(0,0); fmt=0; pal=(0,0); meshes=[]
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
        if op==0x8f: pal=(w&0xfff,(w>>12)&0xfff)
        if op==0x86: fmt=w&0xffffff
        if w==0x09000000: meshes.append((ws[k+1],M.copy(),T.copy(),S.copy(),(tex,pal),fmt)); k+=2; continue
        k+=1
def blk(X,Y): return B+(Y//8)*16384+(X//64)*512+(Y%8)*64+X%64
def texture(tp,fmt):
    (tx,ty),(cx,cy)=tp
    w=8<<(fmt&7); h=8<<((fmt>>3)&7)
    X=np.arange(w)+tx*2; Y=np.arange(h)[:,None]+ty
    ind=mem[blk(X,Y)].astype(int)
    PX=np.arange(256)*2+cx*2; a=blk(PX,max(cy-240,0))
    p=mem[a].astype(int)|(mem[a+1].astype(int)<<8)
    rgb=np.stack([(p&31)<<3,((p>>5)&31)<<3,((p>>10)&31)<<3],1).astype(np.float32)
    out=rgb[ind]; out[ind==0]=-1
    return out
W,H=640,360
img=np.zeros((H,W),dtype=np.float32); zb=np.full((H,W),1e9,dtype=np.float32)
K=4096.0; tris=[]
unk={}
def f16(buf,count,offset):
    h=np.frombuffer(buf,dtype='<u2',count=count,offset=offset).astype(np.int64)
    e=(h>>9)&63; m=h&511
    v=(1+m/512.0)*np.power(2.0,e-31.0); v[e==0]=0
    return np.where(h&0x8000,-v,v)
def r32(a): return struct.unpack_from('<I',raw,a-0x10000000)[0]
VB=[0]
def prims(tg):
    a=tg; vb=VB[0]; out=[]
    for _ in range(4000):
        w=r32(a); op=w>>24
        if op==0x0a: vb=r32(a+4); VB[0]=vb; a+=8; continue
        if op in (0x40,0x44,0x6f):
            fl=(w>>16)&0xff; n=w&0xffff
            if fl&0x40 and not vb: break
            if fl&0x40:
                idx=np.frombuffer(raw,dtype='<u2',count=n,offset=a+4-0x10000000)//5
                nv=int(idx.max())+1 if n else 0
                v=f16(raw,nv*10,vb-0x10000000).reshape(nv,10)
                a+=4+4*((n+1)//2)
            else:
                v=f16(raw,n*10,a+4-0x10000000).reshape(n,10); idx=np.arange(n)
                a+=4+20*n
            out.append((op,idx,v)); continue
        if op!=0x04: unk[op]=unk.get(op,0)+1
        break
    return out
for tg,M_,T_,S_,tx,fm in meshes:
    t=texture(tx,fm)
    pr=prims(tg)
    if not T_.any(): continue
    for op,idx,v in pr:
        if len(idx)<3: continue
        v=np.nan_to_num(v)
        p=(v[:,0:3]*S_*K)@M_.T+T_; z=-p[:,2]
        if op==0x44: tl=[idx[i:i+3] for i in range(0,len(idx)-2,3)]
        else: tl=[idx[i:i+3] for i in range(0,len(idx)-2)]
        for tri in tl:
            a_,b_,c_=tri
            if min(z[a_],z[b_],z[c_])<40: continue
            tris.append((p[tri,0]/z[tri],p[tri,1]/z[tri],z[tri],v[tri,6],v[tri,7],t))
print('unknown ops',{hex(k):v for k,v in unk.items()})
W,H=512,240
img=np.zeros((H,W,3),dtype=np.float32); zb=np.full((H,W),1e9,dtype=np.float32)
F=float(sys.argv[1]) if len(sys.argv)>1 else 300.0
for sx,sy,z,u,vv,t in tris:
    x=256+F*sx; y=120+F*0.47*sy
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
    th,tw=t.shape[:2]
    tu=(np.floor(uu*tw).astype(int))%tw; tv=(np.floor((1-vv2)*th).astype(int))%th
    col=t[tv,tu]
    sub=zb[y0:y1+1,x0:x1+1]; m&=zz<sub; m&=col[...,0]>=0
    sub[m]=zz[m]; img[y0:y1+1,x0:x1+1][m]=col[m]
Image.fromarray(img.astype(np.uint8)).resize((640,480)).save('/home/claude/vft/vflash-menu-render.png'); print(len(tris))
