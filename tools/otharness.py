#!/usr/bin/env python3
"""Linux test harness: attach to a running DOSBox process, find the OTMOD
mailbox in guest RAM, decode ring records and inject input.

usage:  otharness.py watch            -- print ring records as they arrive
        otharness.py click X Y | key WORD | hold 0/1 | state | info
"""
import os, re, struct, sys, time, subprocess

SIG = b'OTACCESS-MBOX-1\0'

class Guest:
    def __init__(self, pid=None):
        self.pid = pid or int(subprocess.check_output(['pgrep', '-n', '-x', 'dosbox']).split()[0])
        self.mem = open('/proc/%d/mem' % self.pid, 'r+b', buffering=0)
        self.membase = None
        self.mbox = None

    def regions(self):
        for line in open('/proc/%d/maps' % self.pid):
            a, perms = line.split()[:2]
            if 'r' in perms and 'w' in perms:
                lo, hi = (int(x, 16) for x in a.split('-'))
                yield lo, hi

    def find(self):
        for lo, hi in self.regions():
            if hi - lo > 512 * 1024 * 1024:
                continue
            pos = lo
            while pos < hi:
                n = min(4 << 20, hi - pos)
                try:
                    self.mem.seek(pos)
                    buf = self.mem.read(n)
                except OSError:
                    break
                i = buf.find(SIG)
                while i >= 0:
                    host = pos + i
                    self_lin = struct.unpack_from('<I', buf, i + 0x10)[0] if i + 0x14 <= len(buf) else None
                    if self_lin and 0x400 < self_lin < 0xA0000:
                        self.membase = host - self_lin
                        self.mbox = self_lin
                        return True
                    i = buf.find(SIG, i + 1)
                if pos + n >= hi:
                    break
                pos += n - 64
        return False

    def read(self, lin, n):
        self.mem.seek(self.membase + lin)
        return self.mem.read(n)

    def write(self, lin, data):
        self.mem.seek(self.membase + lin)
        self.mem.write(data)

    def w16(self, lin):
        return struct.unpack('<H', self.read(lin, 2))[0]

    def mb16(self, off):
        return self.w16(self.mbox + off)

    def mb32(self, off):
        o, s = struct.unpack('<HH', self.read(self.mbox + off, 4))
        return s, o

    def set_mb16(self, off, v):
        self.write(self.mbox + off, struct.pack('<H', v & 0xffff))

    # ---- input queue
    def inject(self, typ, a=0, b=0, c=0):
        h = self.mb16(0x38)
        t = self.mb16(0x3A)
        if (h - t) & 0xffff >= 16:
            raise RuntimeError('input queue full')
        slot = self.mbox + 0x40 + (h & 15) * 8
        self.write(slot, struct.pack('<4H', typ, a & 0xffff, b & 0xffff, c & 0xffff))
        self.set_mb16(0x38, h + 1)

    def click(self, x, y):
        self.inject(2, x, y)

    def key(self, word):
        self.inject(1, word)

    # ---- ring
    def ring_reader(self):
        size = self.mb16(0x24)
        roff = self.mb16(0x26)
        pos = self.mb16(0x20)
        seq = self.mb16(0x22)
        while True:
            head = self.mb16(0x20)
            if head == pos:
                yield None
                continue
            avail = (head - pos) % size
            raw = self.read(self.mbox + roff, size)
            buf = raw + raw
            while avail >= 13:
                rec = buf[pos:pos + 13 + 255]
                if rec[0] != 0xA5:
                    pos = head
                    yield ('resync',)
                    break
                typ = rec[1]
                rseq, p1, p2, p3, p4 = struct.unpack_from('<Hhhhh', rec, 2)
                ln = rec[12]
                txt = rec[13:13 + ln].decode('latin1')
                pos = (pos + 13 + ln) % size
                avail -= 13 + ln
                yield (typ, rseq, p1, p2, p3 & 0xffff, p4 & 0xffff, txt)


NAMES = {1: 'TEXT', 2: 'CTR', 3: 'DLGRUN', 4: 'DLGFREE', 6: 'STATE', 7: 'HUNT', 8: 'RAFT'}

def main():
    g = Guest()
    while not g.find():
        print('mailbox not found yet...')
        time.sleep(0.5)
    print('mailbox at linear %05X, membase %X' % (g.mbox, g.membase))
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'info'
    if cmd == 'watch':
        for r in g.ring_reader():
            if r is None:
                time.sleep(0.02)
                continue
            if r[0] == 'resync':
                print('-- resync')
                continue
            typ, seq, p1, p2, p3, p4, txt = r
            print('%-7s #%04X p1=%d p2=%d ret=%04X:%04X %r' % (NAMES.get(typ, typ), seq, p1, p2, p4, p3, txt), flush=True)
    elif cmd == 'click':
        g.click(int(sys.argv[2]), int(sys.argv[3]))
    elif cmd == 'key':
        g.key(int(sys.argv[2], 0))
    elif cmd == 'hold':
        g.set_mb16(0x1E, int(sys.argv[2]))
    elif cmd == 'info':
        for name, off in [('cs_evt', 0x16), ('dgroup', 0x18), ('heart', 0x1A), ('polls', 0x1C), ('hold', 0x1E),
                          ('rhead', 0x20), ('rseq', 0x22), ('holding', 0x3C), ('lastid', 0xC4), ('hooks', 0xC6), ('dropped', 0xC8)]:
            print('%-8s %04X' % (name, g.mb16(off)))
        for name, off in [('state', 0x28), ('dlg', 0x2C), ('hunt', 0x30), ('raft', 0x34)]:
            print('%-8s %04X:%04X' % ((name,) + g.mb32(off)))

if __name__ == '__main__':
    main()
