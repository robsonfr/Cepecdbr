#!/usr/bin/env python3
"""Gera uma ROM de teste de 8K (no lugar do Color BASIC, em $A000) que
configura a PIA1/SAM, preenche a tela de texto, escreve uma mensagem,
desenha blocos semigraficos SG4 e ecoa o teclado na linha 3.
Codigo 6809 montado a mao."""
import sys

code = bytes([
    0x10, 0xCE, 0x7F, 0x00,       # A000 LDS #$7F00
    0xB7, 0xFF, 0xDB,             # A004 STA $FFDB   SAM M0=1 (16K), como o BASIC
    0xB7, 0xFF, 0xD5,             # A007 STA $FFD5   SAM P1=1: sem efeito em 16K
    0x86, 0xF8,                   # A00A LDA #$F8
    0xB7, 0xFF, 0x22,             # A00C STA $FF22   DDRB da PIA1
    0x86, 0x34,                   # A00F LDA #$34
    0xB7, 0xFF, 0x23,             # A011 STA $FF23   CRB: acesso aos dados
    0x7F, 0xFF, 0x22,             # A014 CLR $FF22   VDG alfanumerico
    0xB7, 0xFF, 0xC9,             # A017 STA $FFC9   SAM F1=1 -> tela em $0400
    0x8E, 0x04, 0x00,             # A01A LDX #$0400
    0x86, 0x60,                   # A01D LDA #$60    espaco
    0xA7, 0x80,                   # A01F STA ,X+
    0x8C, 0x06, 0x00,             # A021 CMPX #$0600
    0x26, 0xF9,                   # A024 BNE $A01F
    0x31, 0x8D, 0x00, 0x36,       # A026 LEAY msg,PCR (msg em $A060)
    0x8E, 0x04, 0x00,             # A02A LDX #$0400
    0xA6, 0xA0,                   # A02D LDA ,Y+
    0x27, 0x04,                   # A02F BEQ $A035
    0xA7, 0x80,                   # A031 STA ,X+
    0x20, 0xF8,                   # A033 BRA $A02D
    0x8E, 0x04, 0x20,             # A035 LDX #$0420
    0x86, 0x8F,                   # A038 LDA #$8F
    0xA7, 0x80,                   # A03A STA ,X+
    0x8B, 0x10,                   # A03C ADDA #$10
    0x24, 0xFA,                   # A03E BCC $A03A
    # --- configura PIA0 para varrer o teclado ---
    0x86, 0xFF,                   # A040 LDA #$FF
    0xB7, 0xFF, 0x02,             # A042 STA $FF02   DDRB da PIA0 (colunas = saida)
    0x86, 0x04,                   # A045 LDA #$04
    0xB7, 0xFF, 0x03,             # A047 STA $FF03   CRB: dados
    0xB7, 0xFF, 0x01,             # A04A STA $FF01   CRA: dados (DDRA=0 entradas)
    # --- laco: coluna 0 (PB0=0), mostra linhas lidas em $0440 ---
    0x86, 0xFE,                   # A04D LDA #$FE
    0xB7, 0xFF, 0x02,             # A04F STA $FF02
    0xB6, 0xFF, 0x00,             # A052 LDA $FF00
    0x84, 0x7F,                   # A055 ANDA #$7F
    0xB7, 0x04, 0x40,             # A057 STA $0440
    0x20, 0xF1,                   # A05A BRA $A04D
])
msg = b"HELLO, COCO! EMULADOR EM C"
vdg = bytes([0x40 | (c & 0x3F) for c in msg]) + b"\x00"

rom = bytearray([0xFF] * 0x2000)
rom[0:len(code)] = code
rom[0x60:0x60 + len(vdg)] = vdg
rom[0x1FFE] = 0xA0
rom[0x1FFF] = 0x00
open(sys.argv[1] if len(sys.argv) > 1 else "testrom.rom", "wb").write(rom)
