import sys; sys.path.insert(0,'/home/claude/vftools')
from scene import *
out=Image.new('RGB',(1040,500),(0,0,0))
for n,K in enumerate((1024,4096)):
    tris=[]
    for tg,M_,T_,S_ in meshes:
        m=mesh(tg)
        if not m: continue
        idx,v,cnt=m
        p=(v[:,0:3]*S_*K)@M_.T+T_
        z=-p[:,2]; ok=z>50
        sx=np.where(ok,p[:,0]/np.maximum(z,1),np.nan); sy=np.where(ok,p[:,1]/np.maximum(z,1),np.nan)
        for t in range(0,len(idx)-2,3):
            a,b,c=idx[t:t+3]
            if ok[a] and ok[b] and ok[c]: tris.append(((sx[a],sy[a]),(sx[b],sy[b]),(sx[c],sy[c])))
    A=np.array(tris).reshape(-1,2); lo=np.percentile(A,2,axis=0); hi=np.percentile(A,98,axis=0); sc=min(500/(hi[0]-lo[0]),480/(hi[1]-lo[1]))
    d=ImageDraw.Draw(out)
    for tr in tris:
        q=[(n*520+10+(x-lo[0])*sc, 10+(y-lo[1])*sc) for x,y in tr]
        d.polygon(q,outline=(0,255,0))
    print(K,len(tris),lo,hi)
out.save('/home/claude/vft/wire.png')
