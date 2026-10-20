import otharness, struct, time, sys
g=otharness.Guest(); g.find()
dur=float(sys.argv[1]) if len(sys.argv)>1 else 20
t0=time.time()
while time.time()-t0<dur:
    seg,off=g.mb32(0x34)
    objs=[]
    for i in range(5):
        o,s=struct.unpack('<HH',g.read(seg*16+off+4*i,4))
        if s==0: break
        b=g.read(s*16+o,0x34)
        w=lambda k: struct.unpack_from('<h',b,k)[0]
        objs.append((w(0x20),w(0x22),w(0x24),w(0x26),w(0x30)))
    if objs:
        r=objs[0]
        s='raft x=%d-%d y=%d | '%(r[0],r[0]+r[2],r[1])
        s+=' '.join('rock%d x=%d-%d y=%d a=%d'%(i,o[0],o[0]+o[2],o[1],o[4]) for i,o in enumerate(objs[1:4],1))
        s+=' | mark y=%d'%objs[4][1] if len(objs)>4 else ''
        print('%5.1f %s'%(time.time()-t0,s),flush=True)
    time.sleep(float(sys.argv[2]) if len(sys.argv)>2 else 0.4)
