#!/usr/bin/env python3
"""apk-gates.py — static APK verification gates for CI. Stdlib only.

Subcommands:
    16k       16 KiB page-size packaging gate. For every lib/<abi>/*.so
              inside the APK: ELF PT_LOAD p_align must be 0x4000 and the
              STORED zip entry's data offset must be 16384-aligned.
    manifest  Binary AndroidManifest.xml (AXML) security gate: permission
              allowlist, exported components, debuggable flag evidence.

Usage: apk-gates.py {16k|manifest} <apk>
Exit: 0 = PASS, 1 = FAIL, 2 = usage/parse error.
"""

import struct
import sys
import zipfile

PAGE = 0x4000  # 16384
PT_LOAD = 1
ANDROID_NS = 'http://schemas.android.com/apk/res/android'
ZIP_LOCAL_SIG = 0x04034B50
ZIP_STORED = 0

EM_NAMES = {3: 'x86', 40: 'ARM', 62: 'x86_64', 183: 'AArch64'}


def fail(msg):
    print('FAIL: %s' % msg)


# ---------------------------------------------------------------------------
# 16k gate
# ---------------------------------------------------------------------------

def zip_data_offset(apk_bytes, info):
    """Offset of the entry's file data inside the APK (local header parse)."""
    off = info.header_offset
    sig, = struct.unpack_from('<I', apk_bytes, off)
    if sig != ZIP_LOCAL_SIG:
        raise ValueError('bad local header signature at 0x%x' % off)
    name_len, extra_len = struct.unpack_from('<HH', apk_bytes, off + 26)
    return off + 30 + name_len + extra_len


def elf_pt_loads(data):
    """Return (elf_kind, e_machine, [(p_offset, p_align), ...]) for PT_LOADs."""
    if len(data) < 0x40 or data[:4] != b'\x7fELF':
        raise ValueError('not an ELF file')
    ei_class, e_machine = data[4], struct.unpack_from('<H', data, 0x12)[0]
    if ei_class == 2:
        phoff, = struct.unpack_from('<Q', data, 0x20)
        phentsize, phnum = struct.unpack_from('<HH', data, 0x36)
        off_field, off_fmt = 0x08, '<Q'
        align_field, align_fmt = 0x30, '<Q'
        kind = 'ELF64'
    elif ei_class == 1:
        phoff, = struct.unpack_from('<I', data, 0x1C)
        phentsize, phnum = struct.unpack_from('<HH', data, 0x2A)
        off_field, off_fmt = 0x04, '<I'
        align_field, align_fmt = 0x1C, '<I'
        kind = 'ELF32'
    else:
        raise ValueError('unknown EI_CLASS %d' % ei_class)
    if phnum == 0xFFFF:
        raise ValueError('PN_XNUM extended program-header count unsupported')
    if phoff + phentsize * phnum > len(data):
        raise ValueError('program headers truncated')
    loads = []
    for i in range(phnum):
        base = phoff + i * phentsize
        p_type, = struct.unpack_from('<I', data, base)
        if p_type == PT_LOAD:
            p_offset, = struct.unpack_from(off_fmt, data, base + off_field)
            p_align, = struct.unpack_from(align_fmt, data, base + align_field)
            loads.append((p_offset, p_align))
    return kind, e_machine, loads


def gate_16k(apk_path):
    with open(apk_path, 'rb') as fh:
        apk_bytes = fh.read()
    zf = zipfile.ZipFile(apk_path)
    libs = [i for i in zf.infolist()
            if i.filename.startswith('lib/') and i.filename.endswith('.so')]

    failures = []
    rows = []
    if not libs:
        failures.append('no lib/<abi>/*.so entries found in APK')
    for info in libs:
        problems = []
        stored = info.compress_type == ZIP_STORED
        if not stored:
            problems.append('entry compressed (method %d); extractNativeLibs='
                            'false requires STORED' % info.compress_type)
        data_off = None
        off_ok = 'n/a'
        if stored:
            try:
                data_off = zip_data_offset(apk_bytes, info)
                off_ok = data_off % PAGE == 0
                if not off_ok:
                    problems.append('zip data offset 0x%x not %d-aligned'
                                    % (data_off, PAGE))
            except ValueError as exc:
                problems.append('local header: %s' % exc)
        try:
            kind, machine, loads = elf_pt_loads(zf.read(info))
        except ValueError as exc:
            kind, machine, loads = 'err', 0, []
            problems.append('ELF: %s' % exc)
        if not loads:
            problems.append('no PT_LOAD segments')
        aligns = [a for _, a in loads]
        bad = [a for a in aligns if a != PAGE]
        if bad:
            problems.append('PT_LOAD p_align %s != 0x%x'
                            % (['0x%x' % a for a in bad], PAGE))
        verdict = 'PASS' if not problems else 'FAIL'
        rows.append((info.filename, info.file_size, stored,
                     ('0x%x' % data_off) if data_off is not None else '-',
                     off_ok, kind, EM_NAMES.get(machine, str(machine)),
                     ['0x%x' % a for a in aligns], verdict,
                     '; '.join(problems)))

    print('16 KiB page-size packaging gate')
    print('%-36s %9s %6s %-10s %-5s %-5s %-8s %-22s %s'
          % ('entry', 'size', 'stored', 'data_off', 'algn', 'elf', 'machine',
             'pt_load_aligns', 'verdict'))
    for r in rows:
        print('%-36s %9d %-6s %-10s %-5s %-5s %-8s %-22s %s%s'
              % (r[0], r[1], r[2], r[3], r[4], r[5], r[6],
                 ','.join(r[7]), r[8],
                 ('  <- ' + r[9]) if r[9] else ''))
    failures += ['%s: %s' % (r[0], r[9]) for r in rows if r[8] == 'FAIL']
    if failures:
        for f in failures:
            fail(f)
        print('RESULT: FAIL')
        return 1
    print('RESULT: PASS — %d native libraries, all PT_LOAD p_align=0x%x, '
          'all zip entries STORED and %d-aligned' % (len(rows), PAGE, PAGE))
    return 0


# ---------------------------------------------------------------------------
# manifest gate (AXML parser)
# ---------------------------------------------------------------------------

# Android resource-map fallback names for attribute IDs we assert on.
ANDROID_ATTRS = {
    0x01010000: 'theme', 0x01010001: 'label', 0x01010003: 'name',
    0x0101000F: 'debuggable', 0x01010010: 'exported',
    0x0101020C: 'minSdkVersion', 0x0101021B: 'versionCode',
    0x0101021C: 'versionName', 0x01010270: 'targetSdkVersion',
    0x01010280: 'allowBackup', 0x01010282: 'screenOrientation',
    0x0101028E: 'required', 0x010103AF: 'supportsRtl',
    0x010104EA: 'extractNativeLibs', 0x010104EC: 'usesCleartextTraffic',
}

TYPE_NAMES = {
    0x00: 'null', 0x01: 'ref', 0x02: 'attr', 0x03: 'string', 0x04: 'float',
    0x05: 'dimen', 0x06: 'fraction', 0x10: 'int', 0x11: 'hex',
    0x12: 'bool', 0x1C: 'color', 0x1D: 'color', 0x1E: 'color', 0x1F: 'color',
}


class Attr:
    __slots__ = ('name', 'ns', 'dtype', 'raw', 'data')

    def __init__(self, name, ns, dtype, raw, data):
        self.name, self.ns, self.dtype, self.raw, self.data = \
            name, ns, dtype, raw, data

    @property
    def boolean(self):
        if self.dtype == 0x12:
            return self.data != 0
        if self.raw is not None:
            return self.raw.lower() == 'true'
        return self.data != 0

    @property
    def text(self):
        if self.raw is not None:
            return self.raw
        if self.dtype == 0x12:
            return 'true' if self.data else 'false'
        if self.dtype == 0x11:
            return '0x%x' % self.data
        if self.dtype == 0x01:
            return '@0x%08x' % self.data
        return str(self.data)


class Elem:
    __slots__ = ('name', 'attrs', 'children')

    def __init__(self, name):
        self.name, self.attrs, self.children = name, [], []

    def attr(self, name):
        for a in self.attrs:
            if a.name == name:
                return a
        return None

    def attr_text(self, name, default=None):
        a = self.attr(name)
        return a.text if a is not None else default

    def attr_bool(self, name):
        a = self.attr(name)
        return a.boolean if a is not None else None


def parse_axml(data):
    """Parse binary AndroidManifest.xml; return the root Elem."""
    ctype, hsize, csize = struct.unpack_from('<HHI', data, 0)
    if ctype != 0x0003:
        raise ValueError('not AXML (root type 0x%04x)' % ctype)

    # --- string pool -------------------------------------------------------
    sp = hsize
    stype, shsize, _ = struct.unpack_from('<HHI', data, sp)
    if stype != 0x0001:
        raise ValueError('expected string pool, got 0x%04x' % stype)
    scount, _, flags, sstart, _ = struct.unpack_from('<IIIII', data, sp + 8)
    utf8 = bool(flags & 0x100)
    offs = struct.unpack_from('<%dI' % scount, data, sp + shsize)
    sbase = sp + sstart

    def sget(i):
        if i == 0xFFFFFFFF or i >= scount:
            return None
        o = sbase + offs[i]
        if utf8:
            l16 = data[o]  # utf-16 length (unused), 1-2 bytes
            o += 1
            if l16 & 0x80:
                o += 1
            ln = data[o]  # utf-8 byte length, 1-2 bytes
            o += 1
            if ln & 0x80:
                ln = ((ln & 0x7F) << 8) | data[o]
                o += 1
            return data[o:o + ln].decode('utf-8', 'replace')
        ln, = struct.unpack_from('<H', data, o)
        o += 2
        if ln & 0x8000:
            hi, = struct.unpack_from('<H', data, o)
            ln = ((ln & 0x7FFF) << 16) | hi
            o += 2
        return data[o:o + ln * 2].decode('utf-16-le', 'replace')

    strings = [sget(i) for i in range(scount)]

    # --- chunks ------------------------------------------------------------
    resmap = []
    root = None
    stack = []
    off = sp + struct.unpack_from('<I', data, sp + 4)[0]
    while off < csize:
        t, hs, sz = struct.unpack_from('<HHI', data, off)
        if t == 0x0180:  # resource map
            resmap = list(struct.unpack_from('<%dI' % ((sz - hs) // 4),
                                             data, off + hs))
        elif t == 0x0102:  # START_ELEMENT
            _, name_idx = struct.unpack_from('<II', data, off + 16)
            astart, asize, acount = struct.unpack_from('<HHH', data, off + 24)
            el = Elem(sget(name_idx))
            for i in range(acount):
                ab = off + 16 + astart + i * asize
                ans, aname, araw, _, _, dtype, dval = \
                    struct.unpack_from('<IIIHBBI', data, ab)
                aname_s = sget(aname)
                if aname_s is None and aname < len(resmap):
                    aname_s = ANDROID_ATTRS.get(
                        resmap[aname], 'res:0x%08x' % resmap[aname])
                el.attrs.append(Attr(aname_s, sget(ans), dtype,
                                     sget(araw), dval))
            if stack:
                stack[-1].children.append(el)
            elif root is None:
                root = el
            stack.append(el)
        elif t == 0x0103:  # END_ELEMENT
            if stack:
                stack.pop()
        off += sz
    return root


def is_launcher(el):
    """Element has an intent-filter with MAIN action + LAUNCHER category."""
    for f in el.children:
        if f.name != 'intent-filter':
            continue
        actions = {a.attr_text('name') for a in f.children
                   if a.name == 'action'}
        cats = {c.attr_text('name') for c in f.children
                if c.name == 'category'}
        if ('android.intent.action.MAIN' in actions
                and 'android.intent.category.LAUNCHER' in cats):
            return True
    return False


def gate_manifest(apk_path):
    zf = zipfile.ZipFile(apk_path)
    root = parse_axml(zf.read('AndroidManifest.xml'))
    if root is None or root.name != 'manifest':
        fail('no <manifest> root element')
        return 1
    failures = []

    app = next((c for c in root.children if c.name == 'application'), None)
    uses_sdk = next((c for c in root.children if c.name == 'uses-sdk'), None)

    # --- evidence -----------------------------------------------------------
    print('manifest gate — parsed evidence')
    print('  package=%r versionCode=%s versionName=%r'
          % (root.attr_text('package'), root.attr_text('versionCode'),
             root.attr_text('versionName')))
    if uses_sdk is not None:
        print('  uses-sdk: minSdk=%s targetSdk=%s'
              % (uses_sdk.attr_text('minSdkVersion'),
                 uses_sdk.attr_text('targetSdkVersion')))
    perms = sorted({c.attr_text('name')
                    for c in root.children
                    if c.name in ('uses-permission', 'uses-permission-sdk-23')})
    feats = sorted({c.attr_text('name')
                    for c in root.children if c.name == 'uses-feature'})
    print('  uses-permission: %s' % (perms or '(none)'))
    print('  uses-feature:    %s' % (feats or '(none)'))

    # --- permission allowlist ------------------------------------------------
    expected = ['android.permission.RECORD_AUDIO']
    if perms != expected:
        failures.append('uses-permission set %r != expected %r'
                        % (perms, expected))

    # --- exported components -------------------------------------------------
    components = []
    if app is not None:
        for c in app.children:
            if c.name in ('activity', 'activity-alias', 'service',
                          'receiver', 'provider'):
                explicit = c.attr_bool('exported')
                launcher = is_launcher(c)
                has_filter = any(ch.name == 'intent-filter'
                                 for ch in c.children)
                effective = explicit if explicit is not None else has_filter
                components.append((c.name, c.attr_text('name'), explicit,
                                   launcher, effective))
    print('  components:')
    for tag, name, explicit, launcher, effective in components:
        print('    %-14s %-40s exported=%s launcher=%s effective=%s'
              % (tag, name, explicit, launcher, effective))
        if tag in ('service', 'receiver', 'provider') and effective:
            failures.append('%s %r is exported (explicit=%s, effective)'
                            % (tag, name, explicit))
        elif tag in ('activity', 'activity-alias') and effective \
                and not launcher:
            failures.append('%s %r exported but is not the launcher activity'
                            % (tag, name))

    # --- application flags ----------------------------------------------------
    if app is None:
        failures.append('no <application> element')
    else:
        flags = {k: app.attr_text(k) for k in
                 ('debuggable', 'allowBackup', 'usesCleartextTraffic',
                  'extractNativeLibs', 'supportsRtl')}
        print('  application: %s' % ' '.join('%s=%s' % kv
                                             for kv in sorted(flags.items())))
        dbg = app.attr_bool('debuggable')
        # Expected on a debug build: printed as evidence, asserted true.
        if dbg is not True:
            failures.append('android:debuggable absent or not true '
                            '(expected for debug APK)')

    if failures:
        for f in failures:
            fail(f)
        print('RESULT: FAIL')
        return 1
    print('RESULT: PASS — only RECORD_AUDIO permission, no exported '
          'non-launcher components, debuggable=true (debug build)')
    return 0


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    gate, apk = argv[1], argv[2]
    try:
        if gate == '16k':
            return gate_16k(apk)
        if gate == 'manifest':
            return gate_manifest(apk)
    except (OSError, zipfile.BadZipFile, ValueError, struct.error) as exc:
        fail('%s: %s' % (apk, exc))
        return 1
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
