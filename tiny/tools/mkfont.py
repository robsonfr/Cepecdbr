#!/usr/bin/env python3
"""Gera tiny/font.inc a partir de src/core/font6847.c.

Formato: fluxo de bits LSB-first (ordem da instrucao BT do x86).
O bit k = (caractere*7 + linha)*5 + coluna vale 1 se o pixel esta aceso.
Cada caractere ocupa 7 linhas (linhas 3..9 da celula de 12) x 5 colunas
(colunas 2..6 da celula de 8): 64 * 35 bits = 280 bytes.
"""
import re
import sys

src = open(sys.argv[1]).read()
rows = re.findall(r'\{([^{}]*)\}', src.split('vdg_font[64][12] =', 1)[1])
assert len(rows) == 64, len(rows)
bits = []
for r in rows:
    vals = [int(v, 16) for v in r.split(',')]
    assert len(vals) == 12
    for line in range(3, 10):
        for col in range(5):
            bits.append((vals[line] >> (5 - col)) & 1)
data = bytearray((len(bits) + 7) // 8)
for k, b in enumerate(bits):
    if b:
        data[k >> 3] |= 1 << (k & 7)
out = ["; Gerado por tools/mkfont.py a partir de src/core/font6847.c - nao editar.",
       "; 64 caracteres x 7 linhas x 5 colunas, bits LSB-first (%d bytes)" % len(data),
       "font:"]
for i in range(0, len(data), 20):
    out.append("        db " + ",".join("%d" % b for b in data[i:i + 20]))
open(sys.argv[2], "w").write("\n".join(out) + "\n")
