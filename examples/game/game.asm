*******************************************************************************
* COLETA - jogo-exemplo para TRS-80 Color Computer 1/2, SEM usar as ROMs.
*
* O jogador (bloco branco) e movido com as setas e deve coletar o alvo
* (bloco vermelho). Cada coleta soma um ponto e toca um bipe no DAC.
*
* Demonstra, sem nenhuma rotina do BASIC:
*   - inicializacao do SAM (endereco da tela, modo de memoria)
*   - inicializacao das PIAs (teclado, VDG, som)
*   - interrupcao de quadro (FS, 60 Hz) com vetor em RAM ($010C)
*   - varredura da matriz do teclado
*   - texto e semigraficos SG4 escritos direto na RAM de video
*   - som pelo DAC de 6 bits
*
* Mapa: o codigo fica no espaco de cartucho ($C000). O mesmo binario:
*   - roda no emulador embarcado (coco_load_boot_stub: RESET -> $C000)
*   - roda num CoCo real como cartucho (o BASIC salta para $C000)
*
* Montagem: python3 tools/asm6809.py game.asm game.bin
*       ou: lwasm --raw -o game.bin game.asm
*******************************************************************************

* --- hardware ---
PIA0DA  EQU     $FF00           ; teclado: linhas (entrada)
PIA0CA  EQU     $FF01
PIA0DB  EQU     $FF02           ; teclado: colunas (saida)
PIA0CB  EQU     $FF03           ; CB1 = FS (sincronismo de quadro)
PIA1DA  EQU     $FF20           ; DAC (PA2-PA7)
PIA1CA  EQU     $FF21
PIA1DB  EQU     $FF22           ; modo do VDG (PB3-PB7)
PIA1CB  EQU     $FF23           ; CB2 = habilita som
SAMCLR  EQU     $FFC0           ; bits do SAM: endereco par zera, impar seta
IRQVEC  EQU     $010C           ; vetor de IRQ em RAM (mesmo endereco do BASIC)

* --- tela ---
SCREEN  EQU     $0400           ; 32x16 caracteres
SCREND  EQU     $0600
SPACE   EQU     $60             ; espaco (fundo verde)
PLAYER  EQU     $CF             ; SG4: cor 4 (branco), 4 quadrantes
TARGET  EQU     $BF             ; SG4: cor 3 (vermelho), 4 quadrantes

* --- variaveis em RAM ---
FRAMES  EQU     $0300           ; contador de quadros (IRQ)
SCORE   EQU     $0301           ; pontos em BCD
SEED    EQU     $0302           ; semente do gerador aleatorio (16 bits)
PPOS    EQU     $0304           ; endereco do jogador na tela
TPOS    EQU     $0306           ; endereco do alvo na tela

        ORG     $C000

*******************************************************************************
* Ponto de entrada
*******************************************************************************
START   ORCC    #$50            ; desliga IRQ e FIRQ
        LDS     #$7F00
        CLRA
        TFR     A,DP

* SAM: zera todos os bits, tela em $0400 (F1), modo de 16K (M0).
        LDX     #SAMCLR
        LDB     #16
SAMLP   STA     ,X++
        DECB
        BNE     SAMLP
        STA     $FFC9           ; F1 = 1 -> tela em $0400
        STA     $FFDB           ; M0 = 1 -> RAM dinamica de 16K/64K

* PIA0: PA = entradas (linhas do teclado), PB = saidas (colunas).
        CLR     PIA0CA          ; CRA: acessa DDRA
        CLR     PIA0DA          ; DDRA = 0
        LDA     #$34            ; CRA: dados, CA2 = 0 (SEL1), sem IRQ de HS
        STA     PIA0CA
        CLR     PIA0CB
        LDA     #$FF
        STA     PIA0DB          ; DDRB = $FF
        LDA     #$34            ; CRB: dados, CB2 = 0 (SEL2), IRQ de FS ainda off
        STA     PIA0CB

* PIA1: PA1-PA7 saidas (DAC), PB3-PB7 saidas (VDG), som habilitado.
        CLR     PIA1CA
        LDA     #$FE
        STA     PIA1DA          ; DDRA
        LDA     #$34            ; CA2 = 0: motor do cassete desligado
        STA     PIA1CA
        CLR     PIA1CB
        LDA     #$F8
        STA     PIA1DB          ; DDRB
        LDA     #$3C            ; CB2 = 1: liga o multiplexador de som
        STA     PIA1CB
        CLR     PIA1DB          ; VDG: texto, CSS = 0
        LDA     #$02            ; DAC = 0, saida RS-232 em repouso
        STA     PIA1DA

* Vetor de IRQ: JMP IRQ em $010C
        LDA     #$7E
        STA     IRQVEC
        LDX     #IRQ
        STX     IRQVEC+1

* Estado inicial
        CLR     FRAMES
        CLR     SCORE
        LDD     #$ACE1
        STD     SEED
        LDX     #SCREEN+8*32+16
        STX     PPOS
        JSR     CLS
        JSR     DRAWHUD
        JSR     DRAWP
        JSR     NEWTGT

* Liga a interrupcao de quadro e libera IRQ
        LDA     #$35            ; CRB: IRQ de CB1 (FS) na borda de descida
        STA     PIA0CB
        LDA     PIA0DB          ; limpa flag pendente
        ANDCC   #$EF

*******************************************************************************
* Laco principal: um passo a cada 4 quadros (15 movimentos por segundo)
*******************************************************************************
MAIN    LDA     FRAMES
        ADDA    #4
WAITF   CMPA    FRAMES
        BNE     WAITF
        JSR     MOVE
        BRA     MAIN

*******************************************************************************
* IRQ: so trata a interrupcao de quadro
*******************************************************************************
IRQ     LDA     PIA0CB
        BPL     IRQX            ; nao foi o FS
        LDA     PIA0DB          ; ler PB limpa a flag
        INC     FRAMES
IRQX    RTI

*******************************************************************************
* MOVE: le as setas (linha 3 do teclado) e move o jogador
* Colunas: cima=3 baixo=4 esquerda=5 direita=6
*******************************************************************************
MOVE    LDX     PPOS
        LDA     #$F7            ; coluna 3: seta para cima
        STA     PIA0DB
        LDA     PIA0DA
        BITA    #$08
        BNE     MV1
        CMPX    #SCREEN+64      ; linha 1 e o limite (linha 0 = placar)
        BLO     MV1
        LEAX    -32,X
MV1     LDA     #$EF            ; coluna 4: seta para baixo
        STA     PIA0DB
        LDA     PIA0DA
        BITA    #$08
        BNE     MV2
        CMPX    #SCREND-32
        BHS     MV2
        LEAX    32,X
MV2     LDA     #$DF            ; coluna 5: seta para a esquerda
        STA     PIA0DB
        LDA     PIA0DA
        BITA    #$08
        BNE     MV3
        TFR     X,D
        ANDB    #$1F
        BEQ     MV3
        LEAX    -1,X
MV3     LDA     #$BF            ; coluna 6: seta para a direita
        STA     PIA0DB
        LDA     PIA0DA
        BITA    #$08
        BNE     MV4
        TFR     X,D
        ANDB    #$1F
        CMPB    #$1F
        BEQ     MV4
        LEAX    1,X
MV4     LDA     #$FF
        STA     PIA0DB
        CMPX    PPOS
        BEQ     MVX             ; nao se moveu
        LDY     PPOS
        LDA     #SPACE
        STA     ,Y              ; apaga a posicao antiga
        STX     PPOS
        JSR     DRAWP
        CMPX    TPOS
        BNE     MVX
        LDA     SCORE           ; coletou: soma 1 ponto (BCD)
        ADDA    #1
        DAA
        STA     SCORE
        JSR     DRAWHUD
        JSR     BEEP
        JSR     NEWTGT
MVX     RTS

*******************************************************************************
* DRAWP: desenha o jogador (X = PPOS na saida)
*******************************************************************************
DRAWP   LDX     PPOS
        LDA     #PLAYER
        STA     ,X
        RTS

*******************************************************************************
* NEWTGT: sorteia nova posicao para o alvo (linhas 1-15, fora do jogador)
*******************************************************************************
NEWTGT  JSR     RAND
        EORB    FRAMES          ; mistura o tempo do jogador
        ANDA    #$01            ; D = 0..511
        CMPD    #32             ; evita a linha 0 (placar)
        BLO     NEWTGT
        ADDD    #SCREEN
        CMPD    PPOS
        BEQ     NEWTGT
        STD     TPOS
        TFR     D,X
        LDA     #TARGET
        STA     ,X
        RTS

*******************************************************************************
* RAND: LFSR de 16 bits (8 passos). Retorna D.
*******************************************************************************
RAND    LDX     #8
RANDLP  LDD     SEED
        LSRA
        RORB
        BCC     RAND1
        EORA    #$B4
RAND1   STD     SEED
        LEAX    -1,X
        BNE     RANDLP
        RTS

*******************************************************************************
* CLS: limpa a tela
*******************************************************************************
CLS     LDX     #SCREEN
        LDA     #SPACE
CLSLP   STA     ,X+
        CMPX    #SCREND
        BNE     CLSLP
        RTS

*******************************************************************************
* DRAWHUD: escreve a linha 0 e o placar (ASCII OR $40 = codigo do VDG)
*******************************************************************************
DRAWHUD LDX     #SCREEN
        LDY     #HUDTXT
DHLP    LDA     ,Y+
        BEQ     DHNUM
        ORA     #$40
        STA     ,X+
        BRA     DHLP
DHNUM   LDA     SCORE
        LSRA
        LSRA
        LSRA
        LSRA
        ORA     #$70            ; digito '0'..'9'
        STA     SCREEN+8
        LDA     SCORE
        ANDA    #$0F
        ORA     #$70
        STA     SCREEN+9
        RTS

HUDTXT  FCC     "PONTOS: 00      USE AS SETAS"
        FCB     0

*******************************************************************************
* BEEP: onda quadrada no DAC (~1,3 kHz por ~0,1 s)
*******************************************************************************
BEEP    LDB     #120
BEEPLP  LDA     #$FE            ; DAC no maximo
        STA     PIA1DA
        BSR     DELAY
        LDA     #$02            ; DAC no minimo
        STA     PIA1DA
        BSR     DELAY
        DECB
        BNE     BEEPLP
        RTS

DELAY   LDX     #40
DLYLP   LEAX    -1,X
        BNE     DLYLP
        RTS

        END     START
