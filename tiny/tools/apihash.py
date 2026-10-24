#!/usr/bin/env python3
"""
apihash.py - Confere, nas DLLs de um Windows (ou do Wine), se as funcoes que o
coco4k carrega por hash podem ser encontradas com seguranca.

O hash e o mesmo do coco4k.asm (macro HASHOF) e do laco em assembly: para
cada caractere c, h = ror(h, 13) + (c | 20h), sem incluir o zero final.  O
"| 20h" ignora maiusculas/minusculas (o Windows 10 guarda o nome do modulo
como "KERNEL32.DLL"; o Windows 7, como "kernel32.dll").

Verificacoes, para cada funcao usada:
  - existe na DLL;
  - nenhum OUTRO nome exportado pela mesma DLL tem o mesmo hash (colisao);
  - nao e uma exportacao encaminhada (ex.: HeapAlloc -> NTDLL.RtlAllocateHeap),
    que o carregador do coco4k nao trata.
E tambem que o hash de "kernel32.dll" nao colide com outros modulos comuns.

Uso:
    python3 apihash.py C:\\Windows\\SysWOW64          (Windows de 64 bits)
    python3 apihash.py C:\\Windows\\System32          (Windows de 32 bits)
    python3 apihash.py /usr/lib/i386-linux-gnu/wine/i386-windows   (Wine)
Termina com codigo 1 se encontrar algum problema.
"""
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def ror(x, n):
    return ((x >> n) | (x << (32 - n))) & 0xFFFFFFFF


def api_hash(name):
    h = 0
    for c in name.encode():
        h = (ror(h, 13) + (c | 0x20)) & 0xFFFFFFFF
    return h


def imports_from_asm():
    """Le a lista 'IMPORT nome' / 'IMPORT_DLL' do coco4k.asm."""
    src = open(os.path.join(HERE, '..', 'coco4k.asm'), encoding='utf-8').read()
    dlls = [['kernel32', []]]
    names = re.findall(r"^\s*db\s+'([a-z0-9]+)',\s*0", src.split('dllnames:', 1)[1].split('\n\n', 1)[0], re.M)
    for line in src.split('apihashes:', 1)[1].splitlines():
        line = line.split(';', 1)[0].strip()
        m = re.match(r'IMPORT\s+(\w+)', line)
        if m:
            dlls[-1][1].append(m.group(1))
        elif line.startswith('IMPORT_END'):
            if not names:
                break
            dlls.append([names.pop(0), []])
        elif line and not line.startswith('%'):
            break
    return [d for d in dlls if d[1]]


def exports(path):
    d = open(path, 'rb').read()
    pe = struct.unpack_from('<I', d, 0x3C)[0]
    nsec = struct.unpack_from('<H', d, pe + 6)[0]
    optsz = struct.unpack_from('<H', d, pe + 20)[0]
    opt = pe + 24
    magic = struct.unpack_from('<H', d, opt)[0]
    ddir = opt + (96 if magic == 0x10B else 112)
    erva, esize = struct.unpack_from('<II', d, ddir)
    secs = [struct.unpack_from('<8sIIII', d, opt + optsz + 40 * i) for i in range(nsec)]

    def off(rva):
        for _, vs, va, rs, ro in secs:
            if va <= rva < va + max(vs, rs):
                return rva - va + ro
        raise ValueError('RVA fora das secoes')

    def cstr(rva):
        o = off(rva)
        return d[o:d.index(b'\0', o)].decode('latin-1')

    e = off(erva)
    _, nnames, afun, anames, aord = struct.unpack_from('<IIIII', d, e + 20)
    out = {}
    for i in range(nnames):
        name = cstr(struct.unpack_from('<I', d, off(anames) + 4 * i)[0])
        o = struct.unpack_from('<H', d, off(aord) + 2 * i)[0]
        frva = struct.unpack_from('<I', d, off(afun) + 4 * o)[0]
        out[name] = cstr(frva) if erva <= frva < erva + esize else None
    return out, magic


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 2
    base = argv[1]
    problems = 0
    for mod in ('ntdll.dll', 'kernelbase.dll', 'user32.dll', 'gdi32.dll'):
        if api_hash(mod) == api_hash('kernel32.dll'):
            print('PROBLEMA: hash de %s colide com kernel32.dll' % mod)
            problems += 1
    for dll, funcs in imports_from_asm():
        path = os.path.join(base, dll + '.dll')
        if not os.path.exists(path):
            print('PROBLEMA: %s nao encontrado' % path)
            problems += 1
            continue
        ex, magic = exports(path)
        if magic != 0x10B:
            print('AVISO: %s e de 64 bits; o coco4k usa as DLLs de 32 bits '
                  '(SysWOW64 no Windows de 64 bits)' % path)
        byhash = {}
        for n in ex:
            byhash.setdefault(api_hash(n), []).append(n)
        print('%s.dll: %d nomes exportados' % (dll, len(ex)))
        for f in funcs:
            h = api_hash(f)
            status = 'ok'
            if f not in ex:
                status = 'PROBLEMA: nao exportada'
            elif len(byhash[h]) > 1:
                status = 'PROBLEMA: colide com ' + ', '.join(n for n in byhash[h] if n != f)
            elif ex[f]:
                status = 'PROBLEMA: encaminhada para ' + ex[f]
            if status != 'ok':
                problems += 1
            print('   %-18s %08Xh  %s' % (f, h, status))
    print('nenhum problema encontrado' if not problems else '%d problema(s)' % problems)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
