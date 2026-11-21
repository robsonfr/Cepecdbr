#!/usr/bin/env python3
"""Gera ROMs de teste para os 8 modos graficos do VDG (CSS 0 e 1):
programam SAM e PIA1 e preenchem a tela com um padrao. Uso: gfxroms.py DIR"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
from asm6809 import assemble

SRC = """
        ORG $A000
START   LDS #$7F00
        LDX #$FFC0
        LDB #16
L0      STA ,X++
        DECB
        BNE L0
{sam}
        LDA #$F8
        STA $FF22
        LDA #$34
        STA $FF23
        LDA #{mode}
        STA $FF22
        LDX #$0E00
        LDD #$1B2D
L1      STA ,X+
        ADDA #$47
        EXG A,B
        CMPX #$2600
        BNE L1
L2      BRA L2
        ORG $BFFE
        FDB START
"""
out = sys.argv[1]
for gm in range(8):
    v = {0: 1, 7: 6}.get(gm, gm)
    sam = ["        STA $FFC7", "        STA $FFC9", "        STA $FFCB"]  # F0-F2: $0E00
    for b in range(3):
        if v & (1 << b):
            sam.append("        STA $FF%02X" % (0xC1 + 2 * b))
    for css in (0, 1):
        mode = 0x80 | (gm << 4) | (css << 3)
        _, blob, _, _ = assemble(SRC.format(sam="\n".join(sam), mode="$%02X" % mode))
        name = os.path.join(out, "gfx%d%d.rom" % (gm, css))
        open(name, "wb").write(blob)
