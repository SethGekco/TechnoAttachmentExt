import struct


class MD:
    def __init__(self, path):
        self.f = open(path, 'rb')
        sig, ver, nstreams, rva = struct.unpack('<IIII', self.f.read(16))
        assert sig == 0x504d444d, hex(sig)
        self.f.seek(rva)
        dirs = [struct.unpack('<III', self.f.read(12)) for _ in range(nstreams)]
        self.streams = {t: (sz, r) for t, sz, r in dirs}
        self.ranges = []
        if 9 in self.streams:
            sz, r = self.streams[9]
            self.f.seek(r)
            nranges, base = struct.unpack('<QQ', self.f.read(16))
            off = base
            for _ in range(nranges):
                start, size = struct.unpack('<QQ', self.f.read(16))
                self.ranges.append((start, size, off))
                off += size
        self.ranges.sort()

    def read(self, addr, n):
        out = b''
        while n > 0:
            for start, size, off in self.ranges:
                if start <= addr < start + size:
                    take = min(n, start + size - addr)
                    self.f.seek(off + (addr - start))
                    out += self.f.read(take)
                    addr += take
                    n -= take
                    break
            else:
                return None
        return out

    def u32(self, addr):
        b = self.read(addr, 4)
        return struct.unpack('<I', b)[0] if b else None

    def cstr(self, addr, n=64):
        b = self.read(addr, n)
        if not b:
            return None
        z = b.find(b'\0')
        return b[:z if z >= 0 else n].decode('latin1')

    def modules(self):
        if 4 not in self.streams:
            return []
        sz, r = self.streams[4]
        self.f.seek(r)
        n, = struct.unpack('<I', self.f.read(4))
        out = []
        for _ in range(n):
            rec = self.f.read(108)
            base, size, csum, ts, namerva = struct.unpack('<QIIII', rec[:24])
            cur = self.f.tell()
            self.f.seek(namerva)
            ln, = struct.unpack('<I', self.f.read(4))
            nm = self.f.read(ln).decode('utf-16-le')
            self.f.seek(cur)
            out.append((base, size, nm))
        return out
