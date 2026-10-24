; =============================================================================
; coco4k.asm - emulador TRS-80 Color Computer 1/2 em um executavel Windows
; (PE32, x86) de no maximo 4096 bytes.
;
; Montagem (sem linker; o cabecalho PE e escrito a mao):
;     nasm -f bin -o coco4k.exe coco4k.asm
; Opcoes:
;     -DSAFE_HEADER     cabecalho PE convencional (sem sobrepor o MZ)
;     -DSTATIC_IMPORTS  tabela de importacao convencional em vez de
;                       carregar as funcoes do Windows por hash
;     -DNO_AUDIO        sem som (as variantes acima so cabem em 4 KB assim)
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
STATE     equ 208           ; bytes antes da memoria: tabela de funcoes,
                            ; variaveis do programa e estado do nucleo
NBUF      equ 8             ; buffers de audio em anel (1 quadro cada)
ASLOT     equ 32 + 262      ; WAVEHDR + 262 amostras (1 por linha)
AUD       equ 10000h + 256*192  ; buffers de audio, depois da tela
AREA      equ STATE + AUD + NBUF*ASLOT

; EBP aponta para EDI-128 durante todo o programa:
;   [EBP+0..27]   MSG do PeekMessage (inicio do estado do nucleo)
;   [EBP-8]       horario do proximo quadro (ms * 3)
;   [EBP-12]      dispositivo de audio (HWAVEOUT)
;   [EBP-16]      indice do buffer de audio atual
;   [EBP-80..]    ponteiros das funcoes do Windows (FN_nome)
NEXTFRAME equ -8
HWO       equ -12
AIDX      equ -16
FNBASE    equ -80

%ifndef NO_AUDIO
%define AUDIO               ; o nucleo grava as amostras do DAC
%endif

; Hash dos nomes (calculado pelo NASM): h = ror(h,13) + (c | 20h)
%macro HASHOF 1
  %assign __h 0
  %strlen __n %1
  %assign __i 1
  %rep __n
    %substr __c %1 __i
    %assign __h ((__h >> 13) | (__h << 19)) & 0FFFFFFFFh
    %assign __h (__h + (__c | 20h)) & 0FFFFFFFFh
    %assign __i __i+1
  %endrep
%endmacro

%assign __fn 0
%macro IMPORT 1                 ; hash da funcao e FN_nome = deslocamento
  %defstr __s %1
  HASHOF __s
  dd __h
  %assign FN_%1 FNBASE + __fn*4
  %assign __fn __fn+1
%endmacro
%macro IMPORT_END 0
  dd 0
%endmacro

%macro API 1                    ; chama uma funcao do Windows
%ifdef STATIC_IMPORTS
        call [%1]
%else
        call [ebp+FN_%1]
%endif
%endmacro

        org IMAGEBASE

%ifdef SAFE_HEADER
; --- cabecalho convencional (plano B, caso o Windows recuse o compacto) -------
mz:     db 'MZ'
        times 3Ch-($-mz) db 0
        dd pe - mz
NDIRS   equ 16
%elifdef STATIC_IMPORTS
; --- cabecalho compacto: o PE comeca no offset 4, dentro do MZ ----------------
; O campo e_lfanew (offset 3Ch) cai sobre o SectionAlignment do cabecalho
; opcional, que vale 4: os dois valores coincidem.
mz:     db 'MZ', 0, 0
NDIRS   equ 2
%else
mz:     db 'MZ', 0, 0
NDIRS   equ 0                       ; sem importacoes: nenhum diretorio
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
%if NDIRS > 0
        dd 0, 0                     ; exportacoes
%ifdef STATIC_IMPORTS
        dd imports - IMAGEBASE, imports_end - imports
%else
        dd 0, 0
%endif
        times NDIRS-2 dd 0, 0
%endif
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

%ifdef STATIC_IMPORTS
; --- importacoes convencionais (plano B) -------------------------------------------
imports:
        dd 0, 0, 0, n_k32 - IMAGEBASE, k32 - IMAGEBASE
        dd 0, 0, 0, n_u32 - IMAGEBASE, u32 - IMAGEBASE
        dd 0, 0, 0, n_g32 - IMAGEBASE, g32 - IMAGEBASE
%ifdef AUDIO
        dd 0, 0, 0, n_wmm - IMAGEBASE, wmm - IMAGEBASE
%endif
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
%ifdef AUDIO
wmm:
waveOutOpen:          dd h_waveOutOpen - IMAGEBASE
waveOutPrepareHeader: dd h_waveOutPrepareHeader - IMAGEBASE
waveOutWrite:         dd h_waveOutWrite - IMAGEBASE
                      dd 0
n_wmm:  db 'WINMM.dll', 0
h_waveOutOpen:          db 0, 0, 'waveOutOpen', 0
h_waveOutPrepareHeader: db 0, 0, 'waveOutPrepareHeader', 0
h_waveOutWrite:         db 0, 0, 'waveOutWrite', 0
%endif

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
%else
; --- funcoes do Windows carregadas por hash -----------------------------------------
; A kernel32 e localizada pela PEB; as outras DLLs, carregadas com LoadLibraryA.
; Cada IMPORT_END encerra a lista de uma DLL e carrega a proxima de 'dllnames'.
; Confira colisoes/encaminhamentos com: python3 tools/apihash.py <dir. das DLLs>
dllnames:
        db 'user32', 0
        db 'gdi32', 0
%ifdef AUDIO
        db 'winmm', 0
%endif
        db 0

apihashes:
        IMPORT LoadLibraryA         ; precisa ser a primeira
        IMPORT _lopen
        IMPORT _lread
        IMPORT Sleep
        IMPORT GetTickCount
        IMPORT ExitProcess
        IMPORT_END
        IMPORT CreateWindowExA
        IMPORT PeekMessageA
        IMPORT DispatchMessageA
        IMPORT GetDC
        IMPORT GetAsyncKeyState
        IMPORT IsWindow
        IMPORT_END
        IMPORT StretchDIBits
        IMPORT_END
%ifdef AUDIO
        IMPORT waveOutOpen
        IMPORT waveOutPrepareHeader
        IMPORT waveOutWrite
        IMPORT_END
%endif

%if FN_LoadLibraryA != FNBASE || FNBASE + __fn*4 > AIDX
%error "tabela de funcoes nao cabe na area reservada"
%endif
HASHOF 'kernel32.dll'
%assign K32HASH __h

; resolve: preenche [EBP+FN_*] com os enderecos das funcoes
resolve:
        mov eax, [fs:30h]           ; PEB
        mov eax, [eax+0Ch]          ; PEB->Ldr
        mov ecx, [eax+14h]          ; InMemoryOrderModuleList (1o modulo)
.mod:   mov esi, [ecx+28h]          ; BaseDllName.Buffer (UTF-16)
        mov ebx, [ecx+10h]          ; DllBase
        mov ecx, [ecx]              ; proximo modulo
        xor eax, eax
        cdq
.mc:    lodsw                       ; hash do nome do modulo
        test eax, eax
        jz .me
        or al, 20h
        ror edx, 13
        add edx, eax
        jmp .mc
.me:    cmp edx, K32HASH
        jne .mod

        mov esi, apihashes
        lea edx, [ebp+FNBASE]       ; destino dos ponteiros
        mov ecx, dllnames
.l:     lodsd
        test eax, eax
        jz .dll
        pushad
        call find_export
        mov [esp+28], eax           ; EAX devolvido pelo popad
        popad
        mov [edx], eax
        add edx, 4
        jmp .l
.dll:   cmp byte [ecx], 0           ; acabaram as DLLs?
        je .x
        push edx
        push ecx
        push ecx
        API LoadLibraryA
        xchg ebx, eax               ; EBX = base da nova DLL
        pop ecx
        pop edx
.sk:    inc ecx                     ; avanca para o proximo nome
        cmp byte [ecx-1], 0
        jne .sk
        jmp .l
.x:     ret

; find_export: EBX = base da DLL, EAX = hash -> EAX = endereco da funcao
find_export:
        xchg ebp, eax               ; EBP = hash (restaurado pelo popad)
        mov eax, [ebx+3Ch]          ; cabecalho PE
        mov edi, [ebx+eax+78h]      ; diretorio de exportacao
        add edi, ebx
        mov ecx, [edi+18h]          ; NumberOfNames
.next:  dec ecx
        mov esi, [edi+20h]          ; AddressOfNames
        add esi, ebx
        mov esi, [esi+ecx*4]
        add esi, ebx
        xor eax, eax
        cdq
.h:     lodsb
        test al, al
        jz .e
        or al, 20h
        ror edx, 13
        add edx, eax
        jmp .h
.e:     cmp edx, ebp
        jne .next
        mov esi, [edi+24h]          ; AddressOfNameOrdinals
        add esi, ebx
        movzx ecx, word [esi+ecx*2]
        mov esi, [edi+1Ch]          ; AddressOfFunctions
        add esi, ebx
        mov eax, [esi+ecx*4]
        add eax, ebx
        ret
%endif

; --- dados ------------------------------------------------------------------------
s_static: db 'static', 0
s_title:  db 'CoCo 4K', 0
s_ext:    db 'ext.rom', 0
s_bas:    db 'bas.rom', 0
s_cart:   db 'cart.rom', 0

; vetores usados sem bas.rom ($FFF2-$FFFF): interrupcoes na RAM como no
; BASIC e RESET no cartucho
stubvec: dw 0001h, 0301h, 0F01h, 0C01h, 0601h, 0901h, 00C0h   ; big-endian

%ifdef AUDIO
; WAVEFORMATEX: PCM 8 bits mono, 1 amostra por linha de video
; (262 linhas * 59,923 quadros/s = 15700 Hz)
wfx:    dw 1, 1
        dd 15700, 15700
        dw 1, 8, 0
%endif

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
        API _lopen
        push eax
        API _lread           ; arquivo ausente: _lread(-1) apenas falha
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
        lea ebp, [edi-128]
%ifndef STATIC_IMPORTS
        call resolve
%endif

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
        API CreateWindowExA
        xchg ebx, eax           ; EBX = janela
        push ebx
        API GetDC
        xchg esi, eax           ; ESI = contexto de desenho
%ifdef AUDIO
        xor eax, eax            ; waveOutOpen(&hwo, WAVE_MAPPER, &wfx, 0, 0, 0)
        push eax
        push eax
        push eax
        push wfx
        push -1
        lea ecx, [ebp+HWO]
        push ecx
        API waveOutOpen         ; sem dispositivo: HWO fica 0 e o resto falha
%endif
        API GetTickCount
        imul eax, eax, 3
        mov [ebp+NEXTFRAME], eax ; proximo quadro (ms * 3)

main:   xor eax, eax            ; mensagens
        push 1
        push eax
        push eax
        push eax
        push ebp                ; MSG em [EBP]
        API PeekMessageA
        test eax, eax
        jz .nomsg
        push ebp
        API DispatchMessageA
        jmp main
.nomsg: push ebx
        API IsWindow
        test eax, eax
        jnz .alive
        push eax
        API ExitProcess
.alive: call keyboard
%ifdef AUDIO
        movzx eax, byte [ebp+AIDX]  ; buffer de audio deste quadro
        imul edx, eax, ASLOT
        lea edx, [edi+AUD+edx]      ; EDX -> WAVEHDR
        lea eax, [edx+32]
        mov [edx], eax              ; lpData
        mov dword [edx+4], LINES    ; dwBufferLength
        mov [edi+V_APTR], eax       ; o nucleo grava as amostras aqui
        push edx
        call core_frame
        pop edx
        test byte [edx+16], 10h     ; WHDR_INQUEUE: ainda tocando, descarta
        jnz .busy
        mov eax, [ebp+HWO]
        push 32
        push edx
        push eax
        push 32
        push edx
        push eax
        API waveOutPrepareHeader
        API waveOutWrite
        inc byte [ebp+AIDX]
        and byte [ebp+AIDX], NBUF-1
.busy:
%else
        call core_frame
%endif

        xor ecx, ecx
        push 0CC0020h           ; SRCCOPY
        push ecx                ; DIB_RGB_COLORS
        push bmi
        lea eax, [edi+FB]
        push eax
        push 192
        push 256
        push ecx
        push ecx
        push SCALE_H
        push SCALE_W
        push ecx
        push ecx
        push esi
        API StretchDIBits

        add dword [ebp+NEXTFRAME], 50 ; 1000/60 ms (em unidades de 1/3 ms)
.wait:  API GetTickCount
        lea eax, [eax+eax*2]
        cmp eax, [ebp+NEXTFRAME]
        jns main
        push 1
        API Sleep
        jmp .wait

keyboard:                       ; preenche a matriz KEYM com as teclas do PC
        pushad
        xor eax, eax
        mov [edi+KEYM], eax
        mov [edi+KEYM+4], eax
        mov ebx, '0'            ; digitos e letras
.l:     push ebx
        API GetAsyncKeyState
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
        API GetAsyncKeyState
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
