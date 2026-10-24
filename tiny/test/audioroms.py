#!/usr/bin/env python3
"""Gera ROMs que tocam ondas quadradas no DAC (lacos de espera com modos
de enderecamento diferentes) e uma com o som desligado. Uso: audioroms.py DIR"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
from asm6809 import assemble

SRC = """
        ORG $A000
START   LDS #$7F00
        CLR $FF01
        LDA #$34
        STA $FF01
        CLR $FF03
        STA $FF03
        CLR $FF21
        LDA #$FE
        STA $FF20
        LDA #$34
        STA $FF21
        CLR $FF23
        LDA #$F8
        STA $FF22
        LDA #{crb}
        STA $FF23
LOOP    LDA #$FC
        STA $FF20
{delay}
        LDA #$00
        STA $FF20
{delay}
        BRA LOOP
        ORG $BFFE
        FDB START
"""
DELAYS = {
    'leax':  "        LDX #{n}\nD{k}    LEAX -1,X\n        BNE D{k}",
    'decb':  "        LDB #{n}\nD{k}    DECB\n        BNE D{k}",
    'mem':   "        LDX #{n}\nD{k}    LDA ,X+\n        CMPX #{e}\n        BNE D{k}",
    'jsr':   "        LDB #{n}\nD{k}    JSR SUB{k}\n        DECB\n        BNE D{k}\n        BRA E{k}\nSUB{k}   RTS\nE{k}    NOP",
}
out = sys.argv[1]
for name, d in DELAYS.items():
    n = {'leax': 60, 'decb': 120, 'mem': 0x1000, 'jsr': 30}[name]
    def mk(k):
        return d.format(n=n, k=k, e=n + 40)
    src = SRC.format(crb="$3C", delay=mk(1).replace("D1", "DA").replace("SUB1", "SA").replace("E1", "EA") + "\n" + "", )
    src = SRC.format(crb="$3C", delay="{delay}")
    parts = src.split("{delay}")
    src = parts[0] + mk(1) + parts[1] + mk(2) + parts[2]
    open(os.path.join(out, "snd_%s.rom" % name), "wb").write(assemble(src)[1])
src = SRC.format(crb="$34", delay="{delay}")
parts = src.split("{delay}")
src = parts[0] + DELAYS['decb'].format(n=120, k=1) + parts[1] + DELAYS['decb'].format(n=120, k=2) + parts[2]
open(os.path.join(out, "snd_off.rom"), "wb").write(assemble(src)[1])
