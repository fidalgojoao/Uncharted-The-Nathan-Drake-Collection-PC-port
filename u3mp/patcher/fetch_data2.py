#!/usr/bin/env python3
"""Rebuild RPCS3's dev_hdd0/game/BCES01670DATA2/USRDIR (the U3 PS3 1.19 "game data" folder) the way the game fills it
when it goes online, so the multiplayer import does not need an online RPCS3 session first.

What the PS3 1.19 EBOOT does (see REPORT.md for addresses):
  net18.bin   NetInit worker f_2966f0 -> f_2bb774(<s3>, ".", "net18.bin"):
              GET <s3>/./net18.bin.psarc.crypt -> ND seal decode -> PSARC -> entry "net18.bin" -> <data>/net18.bin
  -7 psarcs   f_32a7b0: DC list 0x4e45f19f in net18.bin = (dir, name) pairs; f_2bb3c0(<s3>, "<dir>/<name>-7.psarc"):
              GET <s3>/<dir>/<name>-7.psarc.crypt -> ND seal decode -> <data>/<dir>/<name>-7.psarc (no unpacking)
  text1       f_32a7b0: GET <s3>/build/main/text1/<lang>.networking.crypt -> decode -> <data>/build/main/text1/<lang>.networking
  upgrades    f_9433e4: GET <upgrades url>/upgrades[-eu|-jp|-as].txt.crypt -> decode -> <data>/<same name> (plaintext,
              the .crypt suffix is kept on disk)
  http-cache  EULA page JPEGs, plain GET of the URLs in net18.bin (http://...eula.../<SET>-%i.jpg), stored unchanged as
              http-cache/<md5(url) as hex without leading zeros> plus the index http-cache/toc1
ND seal (f_93ea58 / f_93f570): 24-byte header = u32 BE plaintext length + 20-byte HMAC-SHA1; the rest is Blowfish-ECB
(big-endian words) ciphertext of the plaintext padded to 8; the HMAC covers the whole decrypted body (padding included).
Keys: the same Blowfish/HMAC key strings as the online profile seal, read from the player's decrypted 1.19 EBOOT.

No host name, address or key is stored in this file: host names/URL formats and keys come from the player's EBOOT
(and the EULA URLs from the downloaded net18.bin); the community DNS server's IPv4 is passed in by the caller.
"""
import argparse
import calendar
import email.utils
import hashlib
import hmac
import os
import random
import socket
import struct
import sys
import time
import zlib

try:
    import numpy as np
except ImportError:  # pure-Python Blowfish fallback (slow on the 55 MB Lab psarc)
    np = None

# sha256 of the net18.bin the DC recipes were made from (recipe_lib.expected_net18_sha256; a hash, not game data)
EXPECTED_NET18_SHA256 = '8c8bc870b3733beaece421beaf11fbe1cceb6d65536443535bd54cf126938da8'   # the recipes' reference


def accepted_net18():
    """Accepted net18.bin versions: tools/dcconv/net18-versions.json next to this file (the server updates net18.bin
    in place; each version is checked and listed there), else the reference only."""
    import json
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tools', 'dcconv', 'net18-versions.json')
    try:
        return set(json.load(open(p, encoding='utf-8'))['accepted'])
    except (OSError, ValueError, KeyError):
        return {EXPECTED_NET18_SHA256}
DOWNLOAD_LIST_SID = 0x4E45F19F  # net18.bin DC symbol read by f_32a7b0 (array of {char* dir; char* name})
USER_AGENT = 'DNTG-HTTPC/1.1'

# ----------------------------------------------------------------------------------------------- EBOOT (1.19) access
# Virtual addresses in the decrypted PS3 1.19 EBOOT (BCES01670 / BCUS98233 1.19 share the code image).
VA_HMAC_KEY_PTR = 0x111E610   # f_947fc0 returns *this (64-char HMAC-SHA1 key), also the profile seal key
VA_BF_KEY_PTR = 0x111E614     # Blowfish key string (20 chars) used by f_947fd8 / f_9480b4
VA_ENV_PROD = 0xDB71B0        # production environment name (the "<env>" of the S3 host)
VA_FMT_S3 = 0xDB7228          # S3 base URL format ("http://%s.<s3 domain>")
VA_FMT_UPGRADES = 0xDEDC40    # absolute upgrades URL format (".../upgrades/%s")
# sha256 of the key strings (to recognise a 1.19 EBOOT; the keys themselves are not stored here)
KEY_SHA256_PREFIX = {'hmac': '8ed14a1765bb9307', 'bf': '9ad07ad07564fdef'}


class Eboot:
    def __init__(self, path):
        self.d = d = open(path, 'rb').read()
        if d[:4] != b'\x7fELF':
            raise ValueError('%s is not a decrypted ELF (decrypt EBOOT.BIN first)' % path)
        phoff = struct.unpack('>Q', d[0x20:0x28])[0]
        phes, phn = struct.unpack('>HH', d[0x36:0x3a])
        self.segs = []
        for i in range(phn):
            t, _fl, off, va, _pa, fs, _ms, _al = struct.unpack('>IIQQQQQQ', d[phoff + i * phes:phoff + i * phes + 56])
            if t == 1:
                self.segs.append((va, off, fs))

    def off(self, va):
        for sva, off, fs in self.segs:
            if sva <= va < sva + fs:
                return off + va - sva
        raise ValueError('VA %#x not in the EBOOT image' % va)

    def u32(self, va):
        o = self.off(va)
        return struct.unpack('>I', self.d[o:o + 4])[0]

    def cstr(self, va):
        o = self.off(va)
        return self.d[o:self.d.index(b'\0', o)]

    def blowfish_init_tables(self):
        """The standard Blowfish P-array / S-boxes (pi digits) as the EBOOT carries them."""
        d = self.d
        p0 = d.find(struct.pack('>II', 0x243f6a88, 0x85a308d3))
        s0 = d.find(struct.pack('>II', 0xd1310ba6, 0x98dfb5ac))
        if p0 < 0 or s0 < 0:
            raise ValueError('Blowfish tables not found in the EBOOT')
        P = list(struct.unpack('>18I', d[p0:p0 + 72]))
        S = [list(struct.unpack('>256I', d[s0 + i * 1024:s0 + (i + 1) * 1024])) for i in range(4)]
        return P, S


def load_eboot_params(eboot_path):
    """Keys and URL bases from the player's decrypted 1.19 EBOOT."""
    e = Eboot(eboot_path)
    hk = e.cstr(e.u32(VA_HMAC_KEY_PTR))
    bk = e.cstr(e.u32(VA_BF_KEY_PTR))
    if (hashlib.sha256(hk).hexdigest()[:16], hashlib.sha256(bk).hexdigest()[:16]) != (KEY_SHA256_PREFIX['hmac'], KEY_SHA256_PREFIX['bf']):
        raise ValueError('%s does not look like the decrypted Uncharted 3 1.19 EBOOT (seal keys not found)' % eboot_path)
    env = e.cstr(VA_ENV_PROD).decode()
    s3_base = e.cstr(VA_FMT_S3).decode() % env
    upgrades_fmt = e.cstr(VA_FMT_UPGRADES).decode()
    P, S = e.blowfish_init_tables()
    return dict(hmac_key=hk, cipher=Blowfish(bk, P, S), s3_base=s3_base, upgrades_fmt=upgrades_fmt)


# ----------------------------------------------------------------------------------------------- Blowfish (BE, ECB)
class Blowfish:
    def __init__(self, key, P, S):
        self.P = list(P)
        self.S = [list(s) for s in S]
        k = 0
        for i in range(18):
            w = 0
            for _ in range(4):
                w = (w << 8) | key[k % len(key)]
                k += 1
            self.P[i] ^= w
        l = r = 0
        for i in range(0, 18, 2):
            l, r = self._enc(l, r)
            self.P[i], self.P[i + 1] = l, r
        for b in range(4):
            for i in range(0, 256, 2):
                l, r = self._enc(l, r)
                self.S[b][i], self.S[b][i + 1] = l, r
        if np is not None:
            self.nP = np.array(self.P, dtype=np.uint32)
            self.nS = [np.array(s, dtype=np.uint32) for s in self.S]

    def _f(self, x):
        S = self.S
        return ((((S[0][x >> 24] + S[1][(x >> 16) & 255]) & 0xffffffff) ^ S[2][(x >> 8) & 255]) + S[3][x & 255]) & 0xffffffff

    def _enc(self, l, r):
        P = self.P
        for i in range(16):
            l ^= P[i]
            r ^= self._f(l)
            l, r = r, l
        l, r = r, l
        return l ^ P[17], r ^ P[16]

    def _dec(self, l, r):
        P = self.P
        for i in range(17, 1, -1):
            l ^= P[i]
            r ^= self._f(l)
            l, r = r, l
        l, r = r, l
        return l ^ P[0], r ^ P[1]

    def _np_rounds(self, data, order, last):
        a = np.frombuffer(data, dtype='>u4').astype(np.uint32)
        L, R = a[0::2].copy(), a[1::2].copy()
        P, S0, S1, S2, S3 = self.nP, self.nS[0], self.nS[1], self.nS[2], self.nS[3]
        for i in order:
            L ^= P[i]
            R ^= ((S0[L >> 24] + S1[(L >> 16) & 255]) ^ S2[(L >> 8) & 255]) + S3[L & 255]
            L, R = R, L
        L, R = R, L
        R ^= P[last[1]]
        L ^= P[last[0]]
        out = np.empty(len(a), dtype='>u4')
        out[0::2], out[1::2] = L, R
        return out.tobytes()

    def decrypt(self, data):
        """ECB over whole 8-byte blocks; a trailing partial block is left as is (like f_947fd8, len >> 3 blocks)."""
        n = len(data) & ~7
        if n == 0:
            return bytes(data)
        if np is not None:
            return self._np_rounds(bytes(data[:n]), range(17, 1, -1), (0, 1)) + bytes(data[n:])
        out = bytearray()
        for i in range(0, n, 8):
            l, r = struct.unpack_from('>II', data, i)
            out += struct.pack('>II', *self._dec(l, r))
        return bytes(out) + bytes(data[n:])

    def encrypt(self, data):
        n = len(data) & ~7
        if np is not None and n:
            return self._np_rounds(bytes(data[:n]), range(16), (17, 16)) + bytes(data[n:])
        out = bytearray()
        for i in range(0, n, 8):
            l, r = struct.unpack_from('>II', data, i)
            out += struct.pack('>II', *self._enc(l, r))
        return bytes(out) + bytes(data[n:])


# ----------------------------------------------------------------------------------------------- ND seal (.crypt)
class SealError(ValueError):
    pass


def decode_crypt(raw, params):
    """A downloaded *.crypt body -> plaintext (f_93ea58 + f_93f570). Raises SealError on a bad MAC."""
    if len(raw) < 24:
        raise SealError('short .crypt body (%d bytes)' % len(raw))
    length = struct.unpack('>I', raw[:4])[0]
    mac = raw[4:24]
    plain = params['cipher'].decrypt(raw[24:])
    if not hmac.compare_digest(hmac.new(params['hmac_key'], plain, hashlib.sha1).digest(), mac):
        raise SealError('HMAC mismatch (wrong keys or corrupt download)')
    if length > len(plain):
        raise SealError('header length %d > body %d' % (length, len(plain)))
    return plain[:length]


def encode_crypt(plain, params, pad=b'\0'):
    """Inverse of decode_crypt (for the offline round-trip test only; the server's pad bytes are unknown)."""
    body = plain + pad * (-len(plain) % 8)
    mac = hmac.new(params['hmac_key'], body, hashlib.sha1).digest()
    return struct.pack('>I', len(plain)) + mac + params['cipher'].encrypt(body)


# ----------------------------------------------------------------------------------------------- PSARC reader
def psarc_entries(data):
    """{name: bytes} for a PSARC (zlib/lzma, any block size). Manifest entry 0 holds the newline-separated names."""
    if data[:4] != b'PSAR':
        raise ValueError('not a PSARC')
    _ver, comp, toc_len, ent_sz, n, block = struct.unpack('>I4sIIII', data[4:28])
    ents = []
    o = 32
    for _ in range(n):
        e = data[o:o + ent_sz]
        ents.append((struct.unpack('>I', e[16:20])[0], int.from_bytes(e[20:25], 'big'), int.from_bytes(e[25:30], 'big')))
        o += ent_sz
    bw = 1
    while (1 << (8 * bw)) < block:
        bw += 1
    zs = [int.from_bytes(data[p:p + bw], 'big') for p in range(o, toc_len, bw)]

    def read(idx):
        bi, size, off = ents[idx]
        out = bytearray()
        while len(out) < size:
            want = min(block, size - len(out))
            z = zs[bi] or block            # 0 = a full stored block
            chunk = data[off:off + z]
            if z == want:                  # stored (compressed size == plain size)
                out += chunk
            elif comp == b'lzma' and chunk[:1] != b'\x78':
                import lzma
                out += lzma.decompress(chunk)
            else:
                out += zlib.decompress(chunk)
            off += z
            bi += 1
        return bytes(out[:size])

    names = read(0).decode('latin1').split('\n') if n else []
    res = {}
    for i, nm in enumerate(names, 1):
        if i < n and nm:
            res[nm.lstrip('/')] = read(i)  # flags bit 1 = absolute names ("/x"); stripped either way
    return res


def psarc_member(data, name):
    """The entry the game opens through the mounted archive: <data>/<sub>/<name>/<name> -> member <name>."""
    ents = psarc_entries(data)
    for k, v in ents.items():
        if k.lower() == name.lower() or k.lower().endswith('/' + name.lower()):
            return v
    if len(ents) == 1:
        return next(iter(ents.values()))
    raise KeyError('%s not in PSARC (has %s)' % (name, ', '.join(sorted(ents))[:200]))


# ----------------------------------------------------------------------------------------------- net18.bin (DC00)
def dc_symbol(dc, sid):
    if dc[:4] != b'DC00':
        raise ValueError('net18.bin is not a DC00 file')
    n = struct.unpack('>I', dc[0x14:0x18])[0]
    for i in range(n):
        s, _t, off = struct.unpack('>III', dc[0x18 + i * 12:0x24 + i * 12])
        if s == sid:
            return off
    raise KeyError('DC symbol %#x not found' % sid)


def _dc_str(dc, off):
    return dc[off:dc.index(b'\0', off)].decode('latin1')


def download_list(net18):
    """[(dir, name)] the game fetches as <dir>/<name>-7.psarc (f_32a7b0)."""
    o = dc_symbol(net18, DOWNLOAD_LIST_SID)
    cnt, arr = struct.unpack('>II', net18[o:o + 8])
    return [(_dc_str(net18, struct.unpack('>I', net18[arr + i * 8:arr + i * 8 + 4])[0]),
             _dc_str(net18, struct.unpack('>I', net18[arr + i * 8 + 4:arr + i * 8 + 8])[0])) for i in range(cnt)]


def eula_sets(net18):
    """{set: (url_format, pages)} from net18.bin's EULA table (entries {country*, ?, ?, pages, char* url_fmt})."""
    out = {}
    p = net18.find(b'http://')
    while p != -1:
        e = net18.index(b'\0', p)
        s = net18[p:e]
        if s.endswith(b'%i.jpg') and b'eula' in s:
            key = struct.pack('>I', p)
            q = net18.find(key)
            while q != -1:
                if q % 4 == 0 and q >= 4:
                    pages = struct.unpack('>I', net18[q - 4:q])[0]
                    if 0 < pages < 100:
                        name = s.decode().rsplit('/', 1)[1].replace('%i.jpg', '').rstrip('-_')
                        out[name] = (s.decode(), pages)
                        break
                q = net18.find(key, q + 1)
        p = net18.find(b'http://', e)
    return out


def cache_name(url):
    """http-cache file name: md5(url) printed as one hex number (leading zeros dropped)."""
    return format(int.from_bytes(hashlib.md5(url.encode()).digest(), 'big'), 'x')


def rtc_tick(t=None):
    """CellRtcTick: microseconds since 0001-01-01 00:00 UTC (toc1 time field)."""
    return int(((time.time() if t is None else t) + 62135596800) * 1000000)


def make_toc1(entries):
    """entries: [(url, size, tick)] -> toc1 bytes (32 bytes each: md5, u32 size, u32 0, u64 tick)."""
    return b''.join(hashlib.md5(u.encode()).digest() + struct.pack('>IIQ', sz, 0, tk) for u, sz, tk in entries)


def parse_toc1(b):
    return [(b[i:i + 16], struct.unpack('>IIQ', b[i + 16:i + 32])) for i in range(0, len(b) - 31, 32)]


# ----------------------------------------------------------------------------------------------- network (not run offline)
def resolve(hostname, dns_ipv4, timeout=5.0):
    """A record of hostname from the given DNS server (raw UDP query, port 53)."""
    qid = random.randrange(65536)
    q = struct.pack('>HHHHHH', qid, 0x0100, 1, 0, 0, 0)
    for label in hostname.rstrip('.').split('.'):
        q += bytes([len(label)]) + label.encode()
    q += b'\0' + struct.pack('>HH', 1, 1)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        s.sendto(q, (dns_ipv4, 53))
        while True:
            d, _ = s.recvfrom(1500)
            if d[:2] == q[:2]:
                break
    finally:
        s.close()
    if d[3] & 0x0f:
        raise OSError('DNS error rcode %d for %s' % (d[3] & 0x0f, hostname))
    an = struct.unpack('>H', d[6:8])[0]
    o = 12
    while d[o]:
        o += d[o] + 1
    o += 5

    def skip_name(o):
        while True:
            if d[o] & 0xc0 == 0xc0:
                return o + 2
            if d[o] == 0:
                return o + 1
            o += d[o] + 1
    for _ in range(an):
        o = skip_name(o)
        ty, cl, _ttl, ln = struct.unpack('>HHIH', d[o:o + 10])
        o += 10
        if ty == 1 and cl == 1 and ln == 4:
            return socket.inet_ntoa(d[o:o + 4])
        o += ln
    raise OSError('no A record for %s' % hostname)


def _split_url(url):
    if '://' in url:
        url = url.split('://', 1)[1]
    hostport, _, path = url.partition('/')
    host, _, port = hostport.partition(':')
    return host, int(port or 80), '/' + path


def fetch(url, ip=None, timeout=30.0):
    """Plain HTTP/1.1 GET like the game's s3-download task. Returns (status, headers dict lower-case, body).
    ip: connect there instead of resolving the URL host (Host header keeps the URL host)."""
    host, port, path = _split_url(url)
    c = socket.create_connection((ip or host, port), timeout=timeout)
    req = ('GET %s HTTP/1.1\r\nHost: %s\r\nDate: %s\r\nUser-Agent: %s\r\nAccept: */*\r\nConnection: close\r\n\r\n'
           % (path, host, email.utils.formatdate(usegmt=True), USER_AGENT))
    try:
        c.sendall(req.encode())
        buf = bytearray()
        while True:
            k = c.recv(1 << 16)
            if not k:
                break
            buf += k
    finally:
        c.close()
    head, _, body = bytes(buf).partition(b'\r\n\r\n')
    lines = head.decode('latin1').split('\r\n')
    status = int(lines[0].split()[1])
    hdr = {}
    for ln in lines[1:]:
        k, _, v = ln.partition(':')
        hdr[k.strip().lower()] = v.strip()
    if 'chunked' in hdr.get('transfer-encoding', '').lower():
        out, p = bytearray(), 0
        while p < len(body):
            e = body.index(b'\r\n', p)
            n = int(body[p:e].split(b';')[0], 16)
            p = e + 2
            if n == 0:
                break
            out += body[p:p + n]
            p += n + 2
        body = bytes(out)
    elif 'content-length' in hdr:
        body = body[:int(hdr['content-length'])]
    return status, hdr, body


# ----------------------------------------------------------------------------------------------- build the folder
def _write(path, data, last_modified=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.dl'
    with open(tmp, 'wb') as f:
        f.write(data)
    os.replace(tmp, path)
    if last_modified:  # the game sets the file time to the Last-Modified of the reply (f_2bb774 / f_93f570)
        try:
            t = calendar.timegm(email.utils.parsedate(last_modified))
            os.utime(path, (t, t))
        except (TypeError, ValueError, OverflowError):
            pass


def build_data2(out_dir, dns_ipv4, ps3_eboot_path, text_ids=(7,), upgrades_names=('upgrades.txt.crypt',),
                eula=('BCUS-EN',), log=print):
    """Download and decode everything the import reads from BCES01670DATA2/USRDIR into out_dir.
    Returns a list of problems (empty = complete). Network is only touched here."""
    prm = load_eboot_params(ps3_eboot_path)
    problems = []
    ips = {}

    def get(url):
        host = _split_url(url)[0]
        if host not in ips:
            ips[host] = resolve(host, dns_ipv4)
        st, hdr, body = fetch(url, ips[host])
        if st != 200:
            raise OSError('HTTP %d for %s' % (st, url))
        return hdr, body

    def get_crypt(url):
        hdr, body = get(url)
        return hdr.get('last-modified'), decode_crypt(body, prm)

    # 1. net18.bin (f_2bb774): <s3>/./net18.bin.psarc.crypt -> PSARC member net18.bin
    lm, arc = get_crypt(prm['s3_base'] + '/./net18.bin.psarc.crypt')
    net18 = psarc_member(arc, 'net18.bin')
    _write(os.path.join(out_dir, 'net18.bin'), net18, lm)
    h = hashlib.sha256(net18).hexdigest()
    log('net18.bin %d bytes sha256 %s' % (len(net18), h))
    if h not in accepted_net18():
        problems.append('net18.bin sha256 %s is a version the recipes do not know yet (report this hash)' % h)

    # 2. level / actor packs (f_32a7b0 list)
    for d, n in download_list(net18):
        rel = '%s/%s-7.psarc' % (d, n)
        try:
            lm, data = get_crypt('%s/%s.crypt' % (prm['s3_base'], rel))
            _write(os.path.join(out_dir, *rel.split('/')), data, lm)
            log('%s %d bytes' % (rel, len(data)))
        except (OSError, ValueError) as ex:
            problems.append('%s: %s' % (rel, ex))

    # 3. text1/<lang>.networking
    for t in text_ids:
        rel = 'build/main/text1/%d.networking' % t
        try:
            lm, data = get_crypt('%s/%s.crypt' % (prm['s3_base'], rel))
            _write(os.path.join(out_dir, *rel.split('/')), data, lm)
            log('%s %d bytes' % (rel, len(data)))
        except (OSError, ValueError) as ex:
            problems.append('%s: %s' % (rel, ex))

    # 4. upgrades list (kept under its .crypt name, content is the decoded text)
    for name in upgrades_names:
        try:
            lm, data = get_crypt(prm['upgrades_fmt'] % name)
            _write(os.path.join(out_dir, name), data, lm)
            log('%s %d bytes' % (name, len(data)))
        except (OSError, ValueError) as ex:
            # not read by the import (the store's upgrade list): a warning, not a reason to stop
            log('note: %s not downloaded (%s); the import does not need it' % (name, ex))

    # 5. EULA pages into http-cache (plain JPEG bodies) + toc1
    sets = eula_sets(net18)
    toc = []
    for s in eula:
        if s not in sets:
            problems.append('EULA set %s not in net18.bin' % s)
            continue
        fmt, pages = sets[s]
        for i in range(1, pages + 1):
            url = fmt % i
            try:
                _hdr, body = get(url)
                _write(os.path.join(out_dir, 'http-cache', cache_name(url)), body)
                toc.append((url, len(body), rtc_tick()))
            except (OSError, ValueError) as ex:
                problems.append('EULA %s: %s' % (url, ex))
    if toc:
        _write(os.path.join(out_dir, 'http-cache', 'toc1'), make_toc1(toc))
    # cinema23/<user>/ (saved cinema clips of players) is per-user and not read by the import: not fetched.
    return problems


# ----------------------------------------------------------------------------------------------- offline test
def sha256_file(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def offline_test(ref, eboot, u2_dir=None, full=False):
    """No network. Checks every decode step against the reference folder (and U2's on-disk .crypt pairs)."""
    ok = True

    def check(cond, msg):
        nonlocal ok
        print(('PASS ' if cond else 'FAIL ') + msg)
        ok &= bool(cond)

    prm = load_eboot_params(eboot)
    check(True, 'EBOOT 1.19 keys + URL bases read at run time (s3 base has %d chars)' % len(prm['s3_base']))

    # A. real ciphertext: U2 keeps sealed + plain copies of text1/*.networking (same ND seal, same keys)
    if u2_dir and os.path.isdir(u2_dir):
        for f in sorted(os.listdir(u2_dir)):
            if f.endswith('.crypt') and os.path.isfile(os.path.join(u2_dir, f[:-6])):
                raw = open(os.path.join(u2_dir, f), 'rb').read()
                try:
                    plain = decode_crypt(raw, prm)
                    same = plain == open(os.path.join(u2_dir, f[:-6]), 'rb').read()
                    check(same, 'real .crypt %s: MAC ok, decoded == %s (%d bytes)' % (f, f[:-6], len(plain)))
                    pad = prm['cipher'].decrypt(raw[24:])[len(plain):]
                    print('     pad bytes after plaintext: %s' % (pad.hex() or '(none)'))
                except SealError as ex:
                    check(False, 'real .crypt %s: %s' % (f, ex))

    # B. net18.bin and its download list
    n18p = os.path.join(ref, 'net18.bin')
    net18 = open(n18p, 'rb').read()
    check(hashlib.sha256(net18).hexdigest() == EXPECTED_NET18_SHA256, 'reference net18.bin sha256 == recipe hash')
    lst = download_list(net18)
    want = sorted('%s/%s-7.psarc' % x for x in lst)
    have = sorted(os.path.relpath(os.path.join(dp, f), ref).replace('\\', '/')
                  for dp, _dn, fn in os.walk(os.path.join(ref, 'build', 'main')) for f in fn if f.endswith('-7.psarc'))
    check(want == have, 'net18 download list (%d entries) == reference build/main *-7.psarc set' % len(want))

    # C. seal round trip on reference files (proves decode == inverse of the format; server pad bytes unknown)
    files = ['net18.bin', 'upgrades.txt.crypt', 'build/main/text1/7.networking'] + want
    for rel in files:
        p = os.path.join(ref, *rel.split('/'))
        if not os.path.isfile(p):
            check(False, 'missing reference %s' % rel)
            continue
        if not full and os.path.getsize(p) > 4 << 20:
            print('SKIP round trip %s (%d MB, use --full)' % (rel, os.path.getsize(p) >> 20))
            continue
        data = open(p, 'rb').read()
        dec = decode_crypt(encode_crypt(data, prm), prm)
        check(hashlib.sha256(dec).digest() == hashlib.sha256(data).digest(), 'seal round trip %s (%d bytes)' % (rel, len(data)))

    # D. PSARC member extraction (net18 wrapper) on a synthetic one-file archive + reader on the reference psarcs
    arc = _make_psarc({'net18.bin': net18})
    check(psarc_member(arc, 'net18.bin') == net18, 'PSARC wrapper -> net18.bin byte-identical (synthetic archive)')
    for rel in want[:3]:
        try:
            ents = psarc_entries(open(os.path.join(ref, *rel.split('/')), 'rb').read())
            check(len(ents) > 0, 'PSARC reader parses %s (%d members)' % (rel, len(ents)))
        except Exception as ex:  # noqa
            check(False, 'PSARC reader %s: %s' % (rel, ex))

    # E. http-cache: EULA bodies are stored unchanged; names = md5(url); toc1 matches
    hc = os.path.join(ref, 'http-cache')
    if os.path.isdir(hc):
        sets = eula_sets(net18)
        url_of = {cache_name(fmt % i): fmt % i for fmt, pages in sets.values() for i in range(1, pages + 1)}
        cached = [f for f in os.listdir(hc) if f != 'toc1']
        mapped = {f: url_of.get(f) for f in cached}
        check(all(mapped.values()), 'http-cache: %d/%d files named md5(EULA url from net18)' % (sum(map(bool, mapped.values())), len(cached)))
        print('     sets: %s' % sorted(set(u.rsplit('/', 1)[1].rsplit('-', 1)[0] for u in mapped.values() if u)))
        for f in cached:
            b = open(os.path.join(hc, f), 'rb').read(3)
            if b != b'\xff\xd8\xff':
                check(False, 'http-cache/%s is not a raw JPEG body' % f)
        if os.path.isfile(os.path.join(hc, 'toc1')):
            toc = parse_toc1(open(os.path.join(hc, 'toc1'), 'rb').read())
            sizes = {format(int.from_bytes(m, 'big'), 'x'): sz for m, (sz, _z, _t) in toc}
            check(all(os.path.getsize(os.path.join(hc, f)) == sizes.get(f) for f in cached),
                  'toc1: %d entries, md5 + size agree with the cached files' % len(toc))
            rebuilt = make_toc1([(url_of[f], sizes[f], t) for (m, (sz, _z, t)) in toc
                                 for f in [format(int.from_bytes(m, 'big'), 'x')] if f in url_of])
            check(rebuilt == open(os.path.join(hc, 'toc1'), 'rb').read(), 'toc1 writer reproduces toc1 byte-for-byte (given the times)')
        print('     no raw .crypt downloads are cached in http-cache (it only holds the plain EULA JPEGs)')
    print('RESULT', 'PASS' if ok else 'FAIL')
    return ok


def _make_psarc(files, block=0x10000):
    """Minimal zlib PSARC writer (test helper)."""
    names = list(files)
    blobs = ['\n'.join(names).encode()] + [files[n] for n in names]
    zsizes, data, ents = [], bytearray(), []
    for b in blobs:
        ents.append((len(zsizes), len(b), len(data)))
        for i in range(0, max(len(b), 1), block):
            c = b[i:i + block]
            z = zlib.compress(c, 9)
            if len(z) >= len(c):
                z = c
            zsizes.append(len(z) if len(z) < block else 0)
            data += z
    n = len(blobs)
    toc_len = 32 + n * 30 + 2 * len(zsizes)
    toc = bytearray()
    for i, (bi, sz, off) in enumerate(ents):
        md5 = bytes(16) if i == 0 else hashlib.md5(names[i - 1].upper().encode()).digest()
        toc += md5 + struct.pack('>I', bi) + sz.to_bytes(5, 'big') + (off + toc_len).to_bytes(5, 'big')
    toc += b''.join(struct.pack('>H', z) for z in zsizes)
    return b'PSAR' + struct.pack('>HH4sIIIII', 1, 4, b'zlib', toc_len, 30, n, block, 2) + bytes(toc) + bytes(data)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--ps3-elf', default=os.environ.get('RECIPE_PS3_ELF'), help='decrypted PS3 1.19 EBOOT (ELF)')
    ap.add_argument('--offline', action='store_true', help='run the offline decode test (no network)')
    ap.add_argument('--ref', help='reference BCES01670DATA2/USRDIR (offline test)')
    ap.add_argument('--u2-crypt-dir', help='folder with U2 *.networking.crypt + plain pairs (offline test, optional)')
    ap.add_argument('--full', action='store_true', help='offline: round-trip the big psarcs too')
    ap.add_argument('--out', help='output USRDIR to build (online)')
    ap.add_argument('--dns', help='IPv4 of the community DNS server (online)')
    a = ap.parse_args()
    if not a.ps3_elf:
        ap.error('--ps3-elf (or RECIPE_PS3_ELF) is required')
    if a.offline:
        if not a.ref:
            ap.error('--offline needs --ref')
        sys.exit(0 if offline_test(a.ref, a.ps3_elf, a.u2_crypt_dir, a.full) else 1)
    if not (a.out and a.dns):
        ap.error('--out and --dns are required to download')
    probs = build_data2(a.out, a.dns, a.ps3_elf)
    for p in probs:
        print('PROBLEM', p)
    sys.exit(1 if probs else 0)


if __name__ == '__main__':
    main()
