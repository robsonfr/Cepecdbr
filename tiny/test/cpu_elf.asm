; Nucleo do coco4k compilado como objeto ELF32 para os testes em C.
; CPU_ONLY: memoria plana de 64K, sem E/S (teste diferencial da CPU).
BITS 32
CPU 686
%ifdef CPU_ONLY
%define TESTMODE
%endif
section .smc progbits alloc exec write align=16
%include "core.inc"

global t_step, t_reset
%ifndef TESTMODE
global t_frame
t_frame:
        push ebp
        push edi
        push esi
        push ebx
        mov edi, [esp+20]
        call core_frame
        pop ebx
        pop esi
        pop edi
        pop ebp
        ret
%endif
t_step:
        push ebp
        push edi
        push esi
        push ebx
        mov edi, [esp+20]
        call step
        pop ebx
        pop esi
        pop edi
        pop ebp
        ret
t_reset:
        push ebp
        push edi
        push esi
        push ebx
        mov edi, [esp+20]
        call core_reset
        pop ebx
        pop esi
        pop edi
        pop ebp
        ret
