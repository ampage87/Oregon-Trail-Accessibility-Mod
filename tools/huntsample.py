import otharness, struct, time, sys
g=otharness.Guest(); g.find()
seg,off=g.mb32(0x30)
print('hunt table %04X:%04X'%(seg,off))
t0=time.time()
dur=float(sys.argv[1]) if len(sys.argv)>1 else 3
while time.time()-t0<dur:
    row=[]
    for i in range(19):
        o,s=struct.unpack('<HH',g.read(seg*16+off+4*i,4))
        b=g.read(s*16+o,0x30)
        w=lambda k: struct.unpack_from('<h',b,k)[0]
        typ=w(0x1a)
        if typ==0: break
        if typ in (2,3,6):
            row.append('s%d t%d k%d x%d y%d w%d h%d sp%d int%d dir%d'%(i,typ,w(0x1e),w(4),w(6),w(0xa),w(8),w(0x18),w(0x10),w(0x1c)))
    print('%.3f'%(time.time()-t0), ' | '.join(row))
    time.sleep(0.05)
