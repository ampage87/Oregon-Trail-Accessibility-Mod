#!/usr/bin/env python3
"""Create OREGONM.EXE (hooked copy) from OREGON.EXE.  Mirrors src/companion/patcher.cpp."""
import sys, hashlib
PATCHES = [  # file offset, original, replacement, description
 (0x019831, '558BEC',       'CD6101',       'text output (m17B8:00B1)'),
 (0x02EB87, '8B56D28B46D0', 'CD6102909090', 'CTR loaded (o12:0E17)'),
 (0x02F297, '558BEC',       'CD6103',       'DoDialog (o12:1527)'),
 (0x02EBBD, '558BEC',       'CD6104',       'FreeDialog (o12:0E4D)'),
 (0x012524, '558BEC',       'CD6105',       'evGetEvent (m107C:0164)'),
 (0x02FF90, '558BEC',       'CD6106',       'game start/state (o15:00E0)'),
 (0x028199, '558BEC',       'CD6107',       'hunt sprites (o6:1949)'),
 (0x02B8FF, '558BEC',       'CD6108',       'raft update (o10:057F)'),
 (0x02ECE2, '558BEC',       'CD6109',       'DrawDialog (o12:0F72)'),
]
def main(src, dst):
    d = bytearray(open(src,'rb').read())
    assert len(d) == 273648, 'unexpected OREGON.EXE size %d' % len(d)
    for off, orig, new, desc in PATCHES:
        o = bytes.fromhex(orig); n = bytes.fromhex(new)
        cur = bytes(d[off:off+len(o)])
        if cur != o:
            raise SystemExit('mismatch at %06X (%s): %s' % (off, desc, cur.hex()))
        d[off:off+len(n)] = n
    open(dst,'wb').write(d)
    print('wrote', dst, hashlib.sha1(d).hexdigest())
if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
