; =============================================================================
; core.inc - nucleo minimo do TRS-80 Color Computer 1/2 em assembly x86 (32 bits)
;
; CPU 6809 completa (paginas 1/2/3, indexado, IRQ, SWI1-3, CWAI), SAM (modo e
; endereco do video), PIAs (teclado, VDG, DAC, IRQ de quadro) e VDG (texto,
; SG4 e os 8 modos graficos).  Independente de plataforma: o frontend so chama
; core_reset / core_frame e le a tela em [EDI+FB].
;
; Truque principal: as operacoes do 6809 sao executadas pelas instrucoes x86
; equivalentes, gravadas em tempo de execucao num "slot" (codigo
; automodificavel).  Os flags do x86 (CF, OF, AF, SF, ZF) tem a mesma
; semantica dos do 6809 (C, V, H, N, Z), entao basta copia-los.
;
; Convencoes:
;   EDI = base dos 64K de memoria do 6809; estado em [EDI-128 .. EDI-1];
;         tela 256x192 (1 byte por pixel, indice de cor) em [EDI+FB].
;   read8/write8: ECX = endereco, AL = dado; preservam EBX, EDX, ESI, EBP.
;   read16/write16: idem, mas destroem ECX.
;   TESTMODE: memoria plana de 64K sem E/S (testes diferenciais da CPU).
; =============================================================================

R_D     equ -32         ; D = A:B (palavra little-endian: B em -32, A em -31)
R_B     equ -32
R_A     equ -31
R_X     equ -30
R_Y     equ -28
R_U     equ -26
R_S     equ -24
R_PC    equ -22
R_CC    equ -20
R_DP    equ -19
V_PAGE  equ -18         ; prefixo da instrucao atual: 0, 1 ($10) ou 2 ($11)
V_CWAI  equ -17
V_CYC   equ -16         ; ciclos executados no quadro (dd)
V_TGT   equ -12         ; alvo de ciclos da linha atual (dd)
V_SAM   equ -8          ; registrador do SAM (dw)
V_LINE  equ -6          ; linha de video atual (dw)
PIA     equ -64         ; 4 portas x (ddr, or, cr, -): P0A, P0B, P1A, P1B
P0A     equ PIA
P0B     equ PIA+4
P1A     equ PIA+8
P1B     equ PIA+12
KEYM    equ -80         ; matriz do teclado: KEYM[coluna] = linhas pressionadas
FB      equ 10000h      ; tela 256x192
CYC_LINE equ 57         ; ciclos de CPU por linha (NTSC)
LINES   equ 262

%macro CYC 1
        add dword [edi+V_CYC], %1
%endmacro

; -----------------------------------------------------------------------------
; Memoria
; -----------------------------------------------------------------------------
fetch16:
        movzx ecx, word [edi+R_PC]
        add word [edi+R_PC], 2
read16:                         ; EAX = palavra big-endian em ECX
        call read8
        shl eax, 8
        push eax
        inc cx
        call read8
        or eax, [esp]
        pop ecx
        ret

fetch8:
        movzx ecx, word [edi+R_PC]
        inc word [edi+R_PC]
read8:
%ifndef TESTMODE
        cmp ecx, 0FF00h
        jb .m
        cmp ecx, 0FFF0h
        jb io_read
.m:
%endif
        movzx eax, byte [edi+ecx]
        ret

write16:
        push eax
        mov al, ah
        call write8
        pop eax
        inc cx
write8:
%ifndef TESTMODE
        cmp ecx, 8000h
        jb .m
        cmp ecx, 0FF00h
        jae io_write
        ret                     ; ROM
.m:
%endif
        mov [edi+ecx], al
        ret

pushw_s:                        ; empilha AX em S
        sub word [edi+R_S], 2
        movzx ecx, word [edi+R_S]
        jmp write16
pullw_s:                        ; desempilha AX de S
        movzx ecx, word [edi+R_S]
        add word [edi+R_S], 2
        jmp read16

load_pc:                        ; PC = palavra em [ECX]
        call read16
        mov [edi+R_PC], ax
        ret

; -----------------------------------------------------------------------------
; Enderecamento: AL = modo (0 imediato, 1 direto, 2 indexado, 3 estendido),
; EDX = largura do imediato.  Retorna ECX = endereco efetivo.
; -----------------------------------------------------------------------------
ea_mode:
        dec al
        js .imm
        jz .dir
        dec al
        jz idx
        CYC 3
        call fetch16
        xchg ecx, eax
        ret
.imm:   movzx ecx, word [edi+R_PC]
        add [edi+R_PC], dx
        ret
.dir:   CYC 2
        call fetch8
        mov ah, [edi+R_DP]
        xchg ecx, eax
        ret

idx:                            ; modo indexado -> ECX
        CYC 2
        push ebx
        push edx
        push esi
        call fetch8
        xchg ebx, eax           ; BL = pos-byte
        mov eax, ebx
        shr eax, 5
        and eax, 3
        lea edx, [edi+eax*2+R_X] ; EDX -> registrador de indice
        movzx esi, word [edx]
        test bl, bl
        js .long
        mov al, bl              ; deslocamento de 5 bits com sinal
        shl al, 3
        sar al, 3
        movsx eax, al
        add esi, eax
        jmp .done
.long:  mov eax, ebx
        and eax, 0Fh
        movzx eax, byte [idxtab+eax]
        add eax, .base
        jmp eax
.base:
.i0:    inc word [edx]          ; ,R+
        jmp .post
.i1:    add word [edx], 2       ; ,R++
        jmp .post
.i2:    dec word [edx]          ; ,-R
        jmp .rel
.i3:    sub word [edx], 2       ; ,--R
.rel:   movzx esi, word [edx]
        jmp .post
.i5:    movsx eax, byte [edi+R_B]
        jmp .add
.i6:    movsx eax, byte [edi+R_A]
        jmp .add
.i8:    call fetch8
        movsx eax, al
        jmp .add
.i9:    call fetch16
        jmp .add
.iB:    movzx eax, word [edi+R_D]
        jmp .add
.iC:    call fetch8
        movsx eax, al
        jmp .pcr
.iD:    call fetch16
.pcr:   movzx esi, word [edi+R_PC]
        jmp .add
.iF:    call fetch16
        xchg esi, eax
        jmp .post
.add:   add esi, eax
.post:  test bl, 10h            ; indireto
        jz .done
        CYC 3
        movzx ecx, si
        call read16
        xchg esi, eax
.done:  movzx ecx, si
        pop esi
        pop edx
        pop ebx
        ret

idxtab: db idx.i0-idx.base, idx.i1-idx.base, idx.i2-idx.base, idx.i3-idx.base
        db idx.post-idx.base, idx.i5-idx.base, idx.i6-idx.base, idx.post-idx.base
        db idx.i8-idx.base, idx.i9-idx.base, idx.post-idx.base, idx.iB-idx.base
        db idx.iC-idx.base, idx.iD-idx.base, idx.post-idx.base, idx.iF-idx.base

; -----------------------------------------------------------------------------
; doop: executa no slot a instrucao x86 cujos 3 bytes estao em ECX (o 4o byte
; e ignorado) sobre AL/AX (acumulador) e BL/BX (operando) e atualiza CC.
; DH = mascara dos bits de CC afetados; DL = 24 (8 bits) ou 16 (16 bits).
; Retorna EAX = resultado.  Destroi EBX, ECX.
; -----------------------------------------------------------------------------
doop:
        mov [slot], ecx
        mov byte [slot+3], 90h
        mov cl, [edi+R_CC]
        and cl, 1
        neg cl                  ; CF = C do 6809, OF = 0
slot:   db 90h, 90h, 90h, 90h
        pushfd
        pop ebx                 ; flags da operacao: CF, AF, OF
        push eax
        mov cl, dl
        shl eax, cl
        pushfd
        pop ecx                 ; SF, ZF do resultado na largura certa
        and cl, 0C0h
        and bl, 3Fh
        or bl, cl               ; BL = S Z . A . . . C ; BH bit 3 = O
        mov cl, bl
        shr cl, 4
        and cl, 0Ch             ; N Z
        test bl, 1
        jz .1
        or cl, 1                ; C
.1:     test bl, 10h
        jz .2
        or cl, 20h              ; H
.2:     test bh, 8
        jz .3
        or cl, 2                ; V
.3:     and cl, dh
        not dh
        and [edi+R_CC], dh
        or [edi+R_CC], cl
        pop eax
        ret

; operacoes de 1 operando (NEG..CLR) pelo nibble baixo do opcode:
; 3 bytes de codigo x86 + mascara de CC
utab:   db 0F6h,0D8h,90h,0Fh,  0F6h,0D8h,90h,0Fh,  0F6h,0D8h,90h,0Fh  ; NEG
        db 0F9h,0F6h,0D0h,0Fh                                      ; COM (stc; not)
        db 0D0h,0E8h,90h,0Dh,  0D0h,0E8h,90h,0Dh                   ; LSR
        db 0D0h,0D8h,90h,0Dh                                       ; ROR (rcr)
        db 0D0h,0F8h,90h,0Dh                                       ; ASR (sar)
        db 0D0h,0E0h,90h,0Fh                                       ; ASL (shl)
        db 0D0h,0D0h,90h,0Fh                                       ; ROL (rcl)
        db 0FEh,0C8h,90h,0Eh,  0FEh,0C8h,90h,0Eh                   ; DEC
        db 0FEh,0C0h,90h,0Eh                                       ; INC
        db 084h,0C0h,90h,0Eh                                       ; TST
        db 090h,090h,90h,00h                                       ; (JMP)
        db 030h,0C0h,90h,0Fh                                       ; CLR (xor)

unary:                          ; EAX = valor, BL = opcode -> EAX
        movzx ecx, bl
        and cl, 0Fh
        mov ecx, [utab+ecx*4]
        mov edx, ecx
        shr edx, 16
        mov dl, 24
        jmp doop

; operacoes com acumulador de 8 bits: opcode x86 (forma "op al,bl") e mascara
; (CMP e BIT usam sub/and, com o resultado descartado: o N/Z vem dele)
alutab: db 28h,0Fh, 28h,0Fh, 18h,0Fh, 0,0        ; SUB CMP SBC -
        db 20h,0Eh, 20h,0Eh, 88h,0Eh, 0,0        ; AND BIT LD -
        db 30h,0Eh, 10h,2Fh, 08h,0Eh, 00h,2Fh    ; EOR ADC OR ADD

; nibble baixo -> tipo de operacao de 16 bits (FF = 8 bits)
op16idx: db 0FFh,0FFh,0FFh,0,  0FFh,0FFh,0FFh,0FFh
         db 0FFh,0FFh,0FFh,0FFh, 1,4,2,3
; tipo*2+lado: opcode x86 (FE = BSR/JSR, FF = gravacao), mascara,
; registrador nas paginas 1, 2 e 3
op16tab:
        db 28h,0Fh, R_D,R_D,R_U         ; SUBD / CMPD / CMPU
        db 00h,0Fh, R_D,R_D,R_D         ; ADDD
        db 38h,0Fh, R_X,R_Y,R_S         ; CMPX / CMPY / CMPS
        db 88h,0Eh, R_D,R_D,R_D         ; LDD
        db 88h,0Eh, R_X,R_Y,R_Y         ; LDX / LDY
        db 88h,0Eh, R_U,R_S,R_S         ; LDU / LDS
        db 0FFh,0Eh, R_X,R_Y,R_Y        ; STX / STY
        db 0FFh,0Eh, R_U,R_S,R_S        ; STU / STS
        db 0FEh,0,  R_D,R_D,R_D         ; BSR / JSR
        db 0FFh,0Eh, R_D,R_D,R_D        ; STD

; -----------------------------------------------------------------------------
; Execucao de uma instrucao
; -----------------------------------------------------------------------------
step:
        mov al, [edi+P0B+2]     ; IRQ = flag de FS e IRQ habilitada na PIA0
        and al, 81h
        cmp al, 81h
        jne .noirq
        test byte [edi+R_CC], 10h
        jnz .noirq
        CYC 19
        lea esi, [edi+R_S]
        cmp byte [edi+V_CWAI], 0
        jne .nopush
        or byte [edi+R_CC], 80h
        mov dl, 0FFh
        call push_regs
.nopush:
        mov byte [edi+V_CWAI], 0
        or byte [edi+R_CC], 10h
        mov ecx, 0FFF8h
        jmp load_pc
.noirq: cmp byte [edi+V_CWAI], 0
        je .run
        CYC 8
        ret
.run:   mov byte [edi+V_PAGE], 0
decode: call fetch8
        CYC 2
        xchg ebx, eax           ; BL = opcode
        mov eax, ebx
        shr eax, 4
        test bl, bl
        js op_hi
        cmp al, 1
        je grp1
        cmp al, 2
        je branch
        cmp al, 3
        je grp3

; --- $00-$0F, $40-$7F: operacoes de 1 operando --------------------------------
rmw:    sub al, 4
        jb .dir
        cmp al, 2
        jae .mem
        lea esi, [edi+R_A]      ; 4x = A, 5x = B
        sub esi, eax
        movzx eax, byte [esi]
        call unary
        mov [esi], al
        ret
.dir:   mov al, 1
.mem:   call ea_mode
        mov al, bl
        and al, 0Fh
        cmp al, 0Eh
        jne .op
        mov [edi+R_PC], cx      ; JMP
        ret
.op:    push ecx
        push ebx
        call read8
        call unary
        pop ebx
        pop ecx
        and bl, 0Fh
        cmp bl, 0Dh             ; TST nao grava
        je .r
        CYC 2
        jmp write8
.r:     ret

; --- $80-$FF: operacoes com acumuladores e registradores de 16 bits ----------
op_hi:  movzx edx, bl
        and edx, 0Fh
        lea esi, [edi+R_A]
        test bl, 40h
        jz .a
        dec esi                 ; B
.a:     mov al, bl
        shr al, 4
        and al, 3               ; modo
        movzx ecx, byte [op16idx+edx]
        cmp cl, 0FFh
        jne op16
        cmp dl, 7
        je .st8
        push edx
        xor edx, edx
        inc edx
        call ea_mode
        call read8
        pop edx
        xchg ebx, eax
        movzx eax, byte [esi]
        mov cl, dl
        and cl, 0Bh
        dec cl                  ; CMP (1) e BIT (5): descarta o resultado
        jnz .w
        lea esi, [edi-128]
.w:     mov ecx, 0D80090h
        mov ch, [alutab+edx*2]
        mov dh, [alutab+edx*2+1]
        mov dl, 24
        call doop
        mov [esi], al
        ret
.st8:   call ea_mode            ; STA / STB
        movzx eax, byte [esi]
        mov ebx, eax
        push ecx
        mov ecx, 0D88890h       ; mov al,bl -> N Z, V = 0
        mov edx, 0E18h
        call doop
        pop ecx
        jmp write8

op16:   add ecx, ecx
        test bl, 40h
        jz .a
        inc ecx
.a:     lea ebp, [ecx+ecx*4+op16tab]
        movzx edx, byte [edi+V_PAGE]
        movsx esi, byte [ebp+2+edx]
        add esi, edi            ; ESI -> registrador
        mov cl, [ebp]
        cmp cl, 0FEh
        je jsr_bsr
        cmp cl, 0FFh
        je .store
        test dl, dl             ; paginas 2/3: SUBD vira CMPD/CMPU
        jz .b
        cmp cl, 28h
        jne .b
        mov cl, 38h
.b:     push ecx
        mov edx, 2
        call ea_mode
        call read16
        xchg ebx, eax
        pop ecx
        movzx eax, word [esi]
        cmp cl, 38h             ; comparacoes: sub com resultado descartado
        jne .c
        mov cl, 28h
        lea esi, [edi-128]
.c:     mov ch, cl
        inc ch                  ; forma de 16 bits: op+1 com prefixo 66h
        mov cl, 66h
        and ecx, 0FFFFh
        or ecx, 0D80000h
        mov dh, [ebp+1]
        mov dl, 16
        call doop
        mov [esi], ax
        ret
.store: call ea_mode
        movzx eax, word [esi]
        mov ebx, eax
        push ecx
        mov ecx, 0D88966h       ; mov ax,bx
        mov edx, 0E10h
        call doop
        pop ecx
        jmp write16

jsr_bsr:                        ; AL = modo (0 = BSR)
        test al, al
        jnz .jsr
        call fetch8
        movsx ecx, al
        add cx, [edi+R_PC]
        jmp jsr_go
.jsr:   call ea_mode
jsr_go: push ecx
        mov ax, [edi+R_PC]
        call pushw_s
        pop ecx
        mov [edi+R_PC], cx
        CYC 3
        ret

; --- desvios $20-$2F (e longos com prefixo $10) ------------------------------
branch: cmp byte [edi+V_PAGE], 1
        jne .s
        call fetch16
        jmp .t
.s:     call fetch8
        movsx eax, al
.t:     xchg ecx, eax
        mov al, bl
        call cond
        jnc .n
        add [edi+R_PC], cx
.n:     CYC 1
        ret

cond:   ; AL (nibble baixo) = condicao do 6809 -> CF = 1 se verdadeira
        and eax, 0Fh
        mov al, [jcctab+eax]
        mov [.j], al
        movzx eax, byte [edi+R_CC]
        mov edx, eax
        and edx, 0Ch
        shl edx, 4              ; N -> SF, Z -> ZF
        test al, 2
        jz .1
        or dh, 8                ; V -> OF
.1:     and al, 1               ; C -> CF
        or eax, edx
        push eax
        popfd
.j:     db 70h, 2               ; jcc correspondente (salta o "clc; ret")
        clc
        ret
        stc
        ret
; BRA BRN BHI BLS BCC BCS BNE BEQ BVC BVS BPL BMI BGE BLT BGT BLE
jcctab: db 0EBh,0A8h,77h,76h,73h,72h,75h,74h,71h,70h,79h,78h,7Dh,7Ch,7Fh,7Eh

; --- $10-$1F -----------------------------------------------------------------
grp1:   mov eax, ebx
        and eax, 0Fh
        movzx eax, byte [g1tab+eax]
        add eax, g1base
        jmp eax
g1base:
pg2:    mov byte [edi+V_PAGE], 1
        jmp decode
pg3:    mov byte [edi+V_PAGE], 2
        jmp decode
lbra:   call fetch16
        add [edi+R_PC], ax
        CYC 3
nop_:   ret
lbsr:   call fetch16
        add ax, [edi+R_PC]
        xchg ecx, eax
        jmp jsr_go
daa_:   mov ah, [edi+R_CC]
        mov cl, ah
        shr cl, 1
        and cl, 10h             ; H -> AF
        and ah, 1               ; C -> CF
        or ah, cl
        sahf
        mov al, [edi+R_A]
        daa
        setc bl
        push ebx
        mov [edi+R_A], al
        movzx eax, al
        mov ecx, 90C084h        ; test al,al
        mov edx, 0E18h
        call doop
        pop ebx
        or [edi+R_CC], bl
        ret
orcc:   call fetch8
        or [edi+R_CC], al
        ret
andcc:  call fetch8
        and [edi+R_CC], al
        ret
sex_:   mov al, [edi+R_B]
        cbw
        mov [edi+R_D], ax
        mov ecx, 0C08566h       ; test ax,ax
        mov edx, 0C10h
        jmp doop
xfer:   call fetch8             ; EXG ($1E) / TFR ($1F)
        xchg edx, eax           ; DL = pos-byte
        mov eax, edx
        shr eax, 4
        call rget
        push eax                ; valor de r1
        mov eax, edx
        and eax, 0Fh
        cmp bl, 1Fh
        je .t
        call rget
        xchg ecx, eax           ; valor de r2
        mov eax, edx
        shr eax, 4
        call rset               ; r1 = r2
        mov eax, edx
        and eax, 0Fh
.t:     pop ecx
        jmp rset                ; r2 = r1

g1tab:  db pg2-g1base, pg3-g1base, nop_-g1base, nop_-g1base
        db nop_-g1base, nop_-g1base, lbra-g1base, lbsr-g1base
        db nop_-g1base, daa_-g1base, orcc-g1base, nop_-g1base
        db andcc-g1base, sex_-g1base, xfer-g1base, xfer-g1base

rget:   ; AL = codigo do registrador -> EAX (8 bits viram $FFxx)
        cmp al, 8
        jae .b
        cmp al, 5
        ja .x
        movzx eax, word [edi+eax*2+R_D]
        ret
.b:     cmp al, 11
        ja .x
        movsx eax, byte [r8tab+eax-8]
        movzx eax, byte [edi+eax]
        or ah, 0FFh
        ret
.x:     mov eax, 0FFFFh
        ret

rset:   ; AL = codigo do registrador, ECX = valor
        cmp al, 8
        jae .b
        cmp al, 5
        ja .x
        mov [edi+eax*2+R_D], cx
.x:     ret
.b:     cmp al, 11
        ja .x
        movsx eax, byte [r8tab+eax-8]
        mov [edi+eax], cl
        ret

r8tab:  db R_A, R_B, R_CC, R_DP

; --- $30-$3F -----------------------------------------------------------------
grp3:   mov eax, ebx
        and eax, 0Fh
        movzx eax, byte [g3tab+eax]
        add eax, g3base
        jmp eax
g3base:
lea_:   call idx
        mov eax, ebx
        and eax, 3
        mov edx, eax
        shr edx, 1
        xor eax, edx            ; X Y S U -> X Y U S
        mov [edi+eax*2+R_X], cx
        cmp bl, 32h
        jae .r
        and byte [edi+R_CC], 0FBh
        test cx, cx
        jnz .r
        or byte [edi+R_CC], 4
.r:     ret
pshpul: call fetch8
        xchg edx, eax
        lea esi, [edi+R_S]
        test bl, 2
        jz .s
        lea esi, [edi+R_U]
.s:     CYC 3
        test bl, 1
        jz push_regs
        jmp pull_regs
rts_:   call pullw_s
        mov [edi+R_PC], ax
        CYC 3
        ret
abx_:   movzx eax, byte [edi+R_B]
        add [edi+R_X], ax
        ret
rti_:   lea esi, [edi+R_S]
        mov dl, 1
        call pull_regs
        mov dl, 80h
        test byte [edi+R_CC], 80h
        jz pull_regs
        mov dl, 0FEh
        jmp pull_regs
cwai_:  call fetch8
        and [edi+R_CC], al
        or byte [edi+R_CC], 80h
        lea esi, [edi+R_S]
        mov dl, 0FFh
        call push_regs
        mov byte [edi+V_CWAI], 1
        ret
mul_:   mov al, [edi+R_A]
        mul byte [edi+R_B]
        mov [edi+R_D], ax
        and byte [edi+R_CC], 0FAh
        test ax, ax
        jnz .1
        or byte [edi+R_CC], 4
.1:     test al, al
        jns .2
        or byte [edi+R_CC], 1
.2:     CYC 9
nop3:   ret
swi_:   or byte [edi+R_CC], 80h
        lea esi, [edi+R_S]
        mov dl, 0FFh
        call push_regs
        movzx eax, byte [edi+V_PAGE]
        test eax, eax
        jnz .v
        or byte [edi+R_CC], 50h ; SWI mascara IRQ e FIRQ
.v:     movzx ecx, word [swivec+eax*2]
        CYC 15
        jmp load_pc

g3tab:  db lea_-g3base, lea_-g3base, lea_-g3base, lea_-g3base
        db pshpul-g3base, pshpul-g3base, pshpul-g3base, pshpul-g3base
        db nop3-g3base, rts_-g3base, abx_-g3base, rti_-g3base
        db cwai_-g3base, mul_-g3base, nop3-g3base, swi_-g3base

swivec: dw 0FFFAh, 0FFF4h, 0FFF2h

; --- empilhar / desempilhar: DL = pos-byte, ESI -> registrador de pilha -------
pshtab: db R_PC, 0, R_Y, R_X, R_DP, R_B, R_A, R_CC

reg_of: ; EBX = indice na pshtab -> EAX = deslocamento do registrador
        movsx eax, byte [pshtab+ebx]
        test eax, eax
        jnz .r
        mov eax, esi            ; "o outro" ponteiro de pilha: S <-> U
        sub eax, edi
        xor al, 0Eh
        movsx eax, al
.r:     ret

push_regs:
        push ebx
        xor ebx, ebx
.l:     shl dl, 1
        jnc .n
        call reg_of
        cmp bl, 4
        jae .b
        movzx eax, word [edi+eax]
        sub word [esi], 2
        movzx ecx, word [esi]
        call write16
        jmp .c
.b:     mov al, [edi+eax]
        dec word [esi]
        movzx ecx, word [esi]
        call write8
.c:     CYC 1
.n:     inc ebx
        cmp bl, 8
        jb .l
        pop ebx
        ret

pull_regs:
        push ebx
        mov ebx, 7
.l:     shr dl, 1
        jnc .n
        call reg_of
        push eax
        movzx ecx, word [esi]
        cmp bl, 4
        jae .b
        add word [esi], 2
        call read16
        pop ecx
        mov [edi+ecx], ax
        jmp .c
.b:     inc word [esi]
        call read8
        pop ecx
        mov [edi+ecx], al
.c:     CYC 1
.n:     dec ebx
        jns .l
        pop ebx
        ret

%ifndef TESTMODE
; -----------------------------------------------------------------------------
; E/S: PIAs ($FF00-$FF3F) e SAM ($FFC0-$FFDF)
; -----------------------------------------------------------------------------
pia_ptr:                        ; ECX = endereco -> EBX -> porta (ddr, or, cr)
        mov ebx, ecx
        and ebx, 2              ; bit 1 do endereco = porta B
        add ebx, ebx            ; porta*4
        test cl, 20h            ; bit 5 = PIA1
        jz .p
        add ebx, 8
.p:     lea ebx, [edi+ebx+PIA]
        ret

io_read:
        push ebx
        push edx
        mov al, 0FFh
        cmp ecx, 0FF40h
        jae .x
        call pia_ptr
        mov al, [ebx+2]
        test cl, 1              ; registrador de controle
        jnz .x
        mov al, [ebx]
        test byte [ebx+2], 4    ; DDR selecionado
        jz .x
        and byte [ebx+2], 3Fh   ; ler os dados limpa as flags de IRQ
        mov al, [ebx+1]
        test cl, 20h
        jnz .p1
        test cl, 2
        jnz .x                  ; PIA0 B: colunas
        ; PIA0 A: linhas do teclado conforme as colunas selecionadas
        mov dl, [edi+P0B+1]
        mov al, 0FFh
        xor ebx, ebx
.k:     shr dl, 1
        jc .kn
        mov dh, [edi+KEYM+ebx]
        not dh
        and al, dh
.kn:    inc ebx
        cmp bl, 8
        jb .k
        jmp .x
.p1:    test cl, 2
        jz .pa
        or al, 4                ; PIA1 B: PB2 = RAM de 16K/64K
        jmp .x
.pa:    and al, 0FEh            ; PIA1 A: entrada do cassete = 0
.x:     movzx eax, al
        pop edx
        pop ebx
        ret

io_write:
        push ebx
        cmp ecx, 0FFC0h
        jae .sam
        cmp ecx, 0FF40h
        jae .x
        call pia_ptr
        test cl, 1
        jz .d
        and al, 3Fh             ; controle: bits 6-7 sao flags (so leitura)
        and byte [ebx+2], 0C0h
        or [ebx+2], al
        jmp .x
.d:     test byte [ebx+2], 4
        jz .ddr
        inc ebx
.ddr:   mov [ebx], al
        jmp .x
.sam:   cmp ecx, 0FFE0h
        jae .x
        mov ebx, ecx
        shr ebx, 1
        and ebx, 0Fh
        test cl, 1
        jz .clr
        bts [edi+V_SAM], ebx
        jmp .x
.clr:   btr [edi+V_SAM], ebx
.x:     pop ebx
        ret

; -----------------------------------------------------------------------------
; Quadro: 262 linhas de 57 ciclos, IRQ de FS ao fim da area visivel, video
; -----------------------------------------------------------------------------
core_frame:
        pushad
        mov word [edi+V_LINE], 0
.line:  add dword [edi+V_TGT], CYC_LINE
.run:   mov eax, [edi+V_CYC]
        cmp eax, [edi+V_TGT]
        jge .eol
        call step
        jmp .run
.eol:   cmp word [edi+V_LINE], 230
        jne .nf
        or byte [edi+P0B+2], 80h ; flag de FS (sincronismo de quadro)
.nf:
%ifdef AUDIO
        call audio_sample
%endif
        inc word [edi+V_LINE]
        cmp word [edi+V_LINE], LINES
        jb .line
        sub dword [edi+V_CYC], CYC_LINE*LINES
        sub dword [edi+V_TGT], CYC_LINE*LINES
        call render
        popad
        ret

; render: gera os 256x192 pixels em [EDI+FB] a partir da RAM de video
render:
        pushad
        movzx ebp, byte [edi+P1B+1]     ; modo do VDG: PB3-PB7
        movzx esi, word [edi+V_SAM]
        shr esi, 3
        and esi, 7Fh
        shl esi, 9                      ; endereco base da tela
        lea edx, [edi+FB]
        xor ecx, ecx
.y:     xor ebx, ebx
.x:     call pixel
        mov [edx], al
        inc edx
        inc bl
        jnz .x
        inc ecx
        cmp cl, 192
        jb .y
        popad
        ret

pixel:  ; ECX = y, EBX = x, EBP = modo, ESI = base -> AL = indice de cor
        push ecx
        push edx
        test ebp, 80h
        jnz .gfx
        mov eax, ecx            ; texto: linha da celula = y % 12
        xor edx, edx
        push 12
        pop ecx
        div ecx
        shl eax, 5
        mov ecx, ebx
        shr ecx, 3
        add eax, ecx
        add eax, esi
        movzx eax, ax
        movzx eax, byte [edi+eax]
        test al, al
        js .sg4
        mov ecx, ebx            ; caractere 5x7 nas colunas 2-6, linhas 3-9
        and ecx, 7
        sub ecx, 2
        cmp ecx, 4
        ja .off
        sub edx, 3
        cmp edx, 6
        ja .off
        push eax
        and eax, 3Fh
        imul eax, eax, 7
        add eax, edx
        imul eax, eax, 5
        add eax, ecx
        bt [font], eax
        pop eax
        setc cl
        jmp .txt
.off:   xor ecx, ecx
.txt:   shr al, 6               ; inverso
        xor cl, al
        and ecx, 1
        test ebp, 8             ; CSS
        jz .t0
        inc ecx
        inc ecx
.t0:    mov al, [tcol+ecx]
        jmp .r
.sg4:   mov ecx, 3              ; semigrafico 4: 2x2 blocos
        cmp edx, 6
        jb .t
        dec ecx
        dec ecx
.t:     test bl, 4
        jz .l
        dec ecx
.l:     bt eax, ecx
        jnc .blk
        shr al, 4
        and al, 7
        jmp .r
.gfx:   mov eax, ebp            ; graficos
        shr eax, 4
        and eax, 7
        movzx edx, byte [gtab+eax]
        push eax
        push edx
        and edx, 0Fh            ; linhas repetidas
        mov eax, ecx
        mov ecx, edx
        xor edx, edx
        div ecx
        pop edx
        and edx, 0F0h           ; bytes por linha
        imul eax, edx
        mov ecx, ebx
        imul ecx, edx
        shr ecx, 5              ; posicao do bit na linha
        mov edx, ecx
        shr edx, 3
        add eax, edx
        add eax, esi
        movzx eax, ax
        movzx eax, byte [edi+eax]
        pop edx
        test dl, 1
        jz .c4
        not ecx                 ; 2 cores
        and ecx, 7
        bt eax, ecx
        jnc .blk
        xor eax, eax
        jmp .css
.c4:    and ecx, 6              ; 4 cores
        xor ecx, 6
        shr eax, cl
        and eax, 3
.css:   test ebp, 8
        jz .r
        add al, 4
        jmp .r
.blk:   mov al, 8               ; preto
.r:     pop edx
        pop ecx
        ret

tcol:   db 9, 0, 10, 7          ; fundo/frente do texto (verde, laranja)
gtab:   db 13h, 13h, 23h, 12h, 22h, 11h, 21h, 21h   ; bytes/16 << 4 | repeticao

%include "font.inc"
%endif ; TESTMODE

; -----------------------------------------------------------------------------
core_reset:                     ; zera o estado e carrega PC do vetor de RESET
        push edi
        sub edi, 128
        xor eax, eax
        mov ecx, 128
        rep stosb
        pop edi
        mov byte [edi+R_CC], 50h
        mov ecx, 0FFFEh
        jmp load_pc
