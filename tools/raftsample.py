import otharness, struct, time, sys
g=otharness.Guest(); g.find()
seg,off=g.mb32(0x34)
print('raft table %04X:%04X'%(seg,off))
dur=float(sys.argv[1]) if len(sys.argv)>1 else 3
t0=time.time()
while time.time()-t0<dur:
    row=[]
    for i in range(12):
        o,s=struct.unpack('<HH',g.read(seg*16+off+4*i,4))
        if s==0: row.append('-'); continue
        b=g.read(s*16+o,0x34)
        w=[struct.unpack_from('<h',b,k)[0] for k in range(0,0x34,2)]
        row.append('%d:'%i+','.join(str(x) for x in w[:26]))
    print('%.2f'%(time.time()-t0)); print('\n'.join(row)); 
    time.sleep(0.5)
