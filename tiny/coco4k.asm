; =============================================================================
; coco4k.asm - emulador TRS-80 Color Computer 1/2 em um executavel Windows
; (PE32, x86) de no maximo 4096 bytes.
;
; Montagem (sem linker; o cabecalho PE e escrito a mao):
;     nasm -f bin -o coco4k.exe coco4k.asm
;
; Uso: coloque no diretorio atual
;     bas.rom   Color BASIC (8K, $A000)             - opcional*
;     ext.rom   Extended Color BASIC (8K, $8000)    - opcional
;     cart.rom  cartucho (ate 16K, $C000)           - opcional
; * sem bas.rom, o RESET salta direto para o cartucho em $C000
;   (jogos que nao dependem das ROMs, como examples/game).
;
; Teclado: letras, digitos, setas, Enter, espaco, Shift; Esc = BREAK,
; Home = CLEAR, Backspace = seta esquerda.  Feche a janela para sair.
; =============================================================================
BITS 32
CPU 686

IMAGEBASE equ 400000h
SCALE_W   equ 768
SCALE_H   equ 576
STATE     equ 128           ; bytes de estado antes da memoria
AREA      equ STATE + 10000h + 256*192

        org IMAGEBASE

%ifdef SAFE_HEADER
; --- cabecalho convencional (plano B, caso o Windows recuse o compacto) -------
mz:     db 'MZ'
        times 3Ch-($-mz) db 0
        dd pe - mz
NDIRS   equ 16
%else
; --- cabecalho compacto: o PE comeca no offset 4, dentro do MZ ----------------
; O campo e_lfanew (offset 3Ch) cai sobre o SectionAlignment do cabecalho
; opcional, que vale 4: os dois valores coincidem.
mz:     db 'MZ', 0, 0
NDIRS   equ 2
%endif
pe:     db 'PE', 0, 0
        dw 014Ch                    ; i386
        dw 1                        ; uma secao
        dd 0, 0, 0
        dw opt_end - opt            ; SizeOfOptionalHeader
        dw 0103h                    ; sem relocacoes, executavel, 32 bits
opt:    dw 010Bh                    ; PE32
        db 0, 0
        dd 0, 0, 0
        dd start - IMAGEBASE        ; AddressOfEntryPoint
        dd 0, 0
        dd IMAGEBASE
        dd 4                        ; SectionAlignment  \ alinhamento baixo:
        dd 4                        ; FileAlignment     / arquivo == memoria
        dw 4, 0, 0, 0, 4, 0         ; versoes de SO, imagem, subsistema
        dd 0
        dd image_end - IMAGEBASE    ; SizeOfImage
        dd hdr_end - IMAGEBASE      ; SizeOfHeaders
        dd 0                        ; CheckSum
        dw 2                        ; subsistema GUI
        dw 0
        dd 400000h, 400000h         ; pilha: 4 MB reservados e comprometidos
        dd 100000h, 1000h           ; heap
        dd 0
        dd NDIRS                    ; NumberOfRvaAndSizes
        dd 0, 0                     ; exportacoes
        dd imports - IMAGEBASE, imports_end - imports
        times NDIRS-2 dd 0, 0
opt_end:
%ifndef SAFE_HEADER
%if (mz + 3Ch) != (opt + 32)
%error "e_lfanew deve coincidir com SectionAlignment"
%endif
%endif
        db '.text', 0, 0, 0
        dd image_end - hdr_end      ; VirtualSize
        dd hdr_end - IMAGEBASE      ; VirtualAddress
        dd image_end - hdr_end      ; SizeOfRawData
        dd hdr_end - IMAGEBASE      ; PointerToRawData
        dd 0, 0, 0
        dd 0E0000020h               ; codigo, executavel, leitura, escrita
hdr_end:

; --- importacoes ------------------------------------------------------------------
imports:
        dd 0, 0, 0, n_k32 - IMAGEBASE, k32 - IMAGEBASE
        dd 0, 0, 0, n_u32 - IMAGEBASE, u32 - IMAGEBASE
        dd 0, 0, 0, n_g32 - IMAGEBASE, g32 - IMAGEBASE
        dd 0, 0, 0, 0, 0
imports_end:
k32:
_lopen:           dd h_lopen - IMAGEBASE
_lread:           dd h_lread - IMAGEBASE
Sleep:            dd h_Sleep - IMAGEBASE
GetTickCount:     dd h_GetTickCount - IMAGEBASE
ExitProcess:      dd h_ExitProcess - IMAGEBASE
                  dd 0
u32:
CreateWindowExA:  dd h_CreateWindowExA - IMAGEBASE
PeekMessageA:     dd h_PeekMessageA - IMAGEBASE
DispatchMessageA: dd h_DispatchMessageA - IMAGEBASE
GetDC:            dd h_GetDC - IMAGEBASE
GetAsyncKeyState: dd h_GetAsyncKeyState - IMAGEBASE
IsWindow:         dd h_IsWindow - IMAGEBASE
                  dd 0
g32:
StretchDIBits:    dd h_StretchDIBits - IMAGEBASE
                  dd 0

n_k32:  db 'KERNEL32.dll', 0
n_u32:  db 'USER32.dll', 0
n_g32:  db 'GDI32.dll', 0
h_lopen:            db 0, 0, '_lopen', 0
h_lread:            db 0, 0, '_lread', 0
h_Sleep:            db 0, 0, 'Sleep', 0
h_GetTickCount:     db 0, 0, 'GetTickCount', 0
h_ExitProcess:      db 0, 0, 'ExitProcess', 0
h_CreateWindowExA:  db 0, 0, 'CreateWindowExA', 0
h_PeekMessageA:     db 0, 0, 'PeekMessageA', 0
h_DispatchMessageA: db 0, 0, 'DispatchMessageA', 0
h_GetDC:            db 0, 0, 'GetDC', 0
h_GetAsyncKeyState: db 0, 0, 'GetAsyncKeyState', 0
h_IsWindow:         db 0, 0, 'IsWindow', 0
h_StretchDIBits:    db 0, 0, 'StretchDIBits', 0

; --- dados ------------------------------------------------------------------------
s_static: db 'static', 0
s_title:  db 'CoCo 4K', 0
s_ext:    db 'ext.rom', 0
s_bas:    db 'bas.rom', 0
s_cart:   db 'cart.rom', 0

; vetores usados sem bas.rom ($FFF2-$FFFF): interrupcoes na RAM como no
; BASIC e RESET no cartucho
stubvec: dw 0001h, 0301h, 0F01h, 0C01h, 0601h, 0901h, 00C0h   ; big-endian

; BITMAPINFO 8 bits, de cima para baixo, com a paleta do CoCo
bmi:    dd 40, 256, -192
        dw 1, 8
        dd 0, 0, 0, 0, 11, 0
        ; B, G, R, 0
        db 0Ah,0FFh,0Ah,0,  43h,0FFh,0FFh,0,  0FFh,17h,2Ah,0,  22h,05h,0BCh,0  ; verde amarelo azul vermelho
        db 0FFh,0FFh,0FFh,0, 6Fh,0D4h,0Ah,0,  0FFh,1Ch,0FFh,0, 08h,4Ah,0FFh,0  ; buff ciano magenta laranja
        db 09h,09h,09h,0,   00h,3Fh,00h,0,   00h,16h,6Ah,0                      ; preto verde-escuro laranja-escuro

; teclas fora das faixas de letras/digitos: codigo virtual, tecla do CoCo
; (tecla = linha*8 + coluna)
keytab: db 26h,27, 28h,28, 25h,29, 27h,30, 20h,31, 08h,29    ; setas, espaco, backspace
        db 0Dh,48, 24h,49, 1Bh,50, 10h,55                     ; enter clear break shift
        db 0BAh,43, 0DEh,42, 0BCh,44, 0BDh,45, 0BEh,46, 0BFh,47, 0DBh,0 ; ; : , - . / @
        db 0

%include "core.inc"

; -----------------------------------------------------------------------------
; Programa principal
; -----------------------------------------------------------------------------
load_rom:                       ; ESI = nome, EBX = destino, ECX = tamanho
        push ecx
        lea eax, [edi+ebx]
        push eax
        push 0
        push esi
        call [_lopen]
        push eax
        call [_lread]           ; arquivo ausente: _lread(-1) apenas falha
        ret

start:
        mov ecx, AREA/1000h + 2 ; reserva a area na pilha (tocando cada pagina)
.probe: sub esp, 1000h
        mov [esp], eax
        loop .probe
        mov edi, esp
        mov ecx, AREA
        xor eax, eax
        rep stosb
        lea edi, [esp+STATE]

        mov esi, stubvec        ; vetores padrao (sem BASIC)
        push edi
        add edi, 0FFF2h
        movsd
        movsd
        movsd
        movsw
        pop edi

        mov esi, s_ext
        mov ebx, 8000h
        mov ecx, 2000h
        call load_rom
        mov esi, s_bas
        mov bh, 0A0h
        call load_rom
        cmp eax, 2000h
        jne .nobas
        push edi                ; com BASIC: vetores de $BFF0
        lea esi, [edi+0BFF0h]
        add edi, 0FFF0h
        movsd
        movsd
        movsd
        movsd
        pop edi
.nobas: mov esi, s_cart
        mov bh, 0C0h
        mov ecx, 3F00h
        call load_rom
        call core_reset

        xor eax, eax            ; janela usando a classe pronta "static"
        push eax
        push eax
        push eax
        push eax
        push SCALE_H + 40
        push SCALE_W + 16
        push 64
        push 64
        push 10CF0000h          ; WS_OVERLAPPEDWINDOW | WS_VISIBLE
        push s_title
        push s_static
        push eax
        call [CreateWindowExA]
        xchg ebx, eax           ; EBX = janela
        push ebx
        call [GetDC]
        xchg esi, eax           ; ESI = contexto de desenho
        call [GetTickCount]
        imul ebp, eax, 3        ; EBP = proximo quadro (ms * 3)

main:   xor eax, eax            ; mensagens
        push 1
        push eax
        push eax
        push eax
        lea ecx, [edi-STATE]
        push ecx
        call [PeekMessageA]
        test eax, eax
        jz .nomsg
        lea ecx, [edi-STATE]
        push ecx
        call [DispatchMessageA]
        jmp main
.nomsg: push ebx
        call [IsWindow]
        test eax, eax
        jnz .alive
        push eax
        call [ExitProcess]
.alive: call keyboard
        call core_frame

        push 0CC0020h           ; SRCCOPY
        push 0                  ; DIB_RGB_COLORS
        push bmi
        lea eax, [edi+FB]
        push eax
        push 192
        push 256
        push 0
        push 0
        push SCALE_H
        push SCALE_W
        push 0
        push 0
        push esi
        call [StretchDIBits]

        add ebp, 50             ; 1000/60 ms (em unidades de 1/3 ms)
.wait:  call [GetTickCount]
        lea eax, [eax+eax*2]
        cmp eax, ebp
        jns main
        push 1
        call [Sleep]
        jmp .wait

keyboard:                       ; preenche a matriz KEYM com as teclas do PC
        pushad
        xor eax, eax
        mov [edi+KEYM], eax
        mov [edi+KEYM+4], eax
        mov ebx, '0'            ; digitos e letras
.l:     push ebx
        call [GetAsyncKeyState]
        test ax, ax
        jns .n
        mov eax, ebx
        sub eax, 16             ; '0'..'9' -> 32..41
        cmp bl, '9'
        jbe .set
        sub eax, 48             ; 'A'..'Z' -> 1..26
        cmp bl, 'A'
        jb .n
.set:   call setkey
.n:     inc ebx
        cmp bl, 'Z'
        jbe .l
        mov esi, keytab         ; demais teclas
.t:     lodsw
        test al, al
        jz .x
        movzx ebx, ah
        movzx eax, al
        push eax
        call [GetAsyncKeyState]
        test ax, ax
        jns .t
        mov eax, ebx
        call setkey
        jmp .t
.x:     popad
        ret

setkey: ; EAX = linha*8 + coluna -> bit (coluna*8 + linha) da matriz
        mov ecx, eax
        shr ecx, 3
        and eax, 7
        lea eax, [eax*8+ecx]
        bts [edi+KEYM], eax
        ret

        align 4
image_end:
