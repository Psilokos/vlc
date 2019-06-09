;*****************************************************************************
;* startcode.asm: SIMD optimized startcode helpers
;*****************************************************************************
;* Copyright (C) 2019 VideoLAN Authors
;*
;* This program is free software; you can redistribute it and/or modify it
;* under the terms of the GNU Lesser General Public License as published by
;* the Free Software Foundation; either version 2.1 of the License, or
;* (at your option) any later version.
;*
;* This program is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
;* GNU Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public License
;* along with this program; if not, write to the Free Software Foundation,
;* Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
;*****************************************************************************

%include "x86inc.asm"

SECTION_RODATA 16

pd_0x01000000: times 4 dd 0x01000000

SECTION .text

INIT_XMM sse2
cglobal startcode_FindAnnexB, 2, 7, 6, ptr, end, size
    mov          sizeq, endq
    sub          sizeq, ptrq
    cmp          sized, 16
    jl .end

    LEA            r6q, pd_0x01000000
    mova            m0, [r6q]
.loop:
    movu            m1, [ptrq]
    psrldq          m2, m1, 1
    psrldq          m3, m1, 2
    punpckhbw       m4, m1, m2  ; 899A ABBC ____ ____
    punpcklbw       m1, m2      ; 0112 2334 4556 6778
    punpckhbw       m5, m2, m3  ; 9AAB BCCD ____ ____
    punpcklbw       m2, m3      ; 1223 3445 5667 7889
    punpckldq       m3, m4, m5  ; 899A 9AAB ABBC BCCD
    pcmpeqd         m1, m0
    pcmpeqd         m2, m0
    pcmpeqd         m3, m0
    movmskps       r3d, m1
    movmskps       r4d, m2
    movmskps       r5d, m3
    tzcnt          r3d, r3d
    tzcnt          r4d, r4d
    tzcnt          r5d, r5d
    shl            r3d, 1
    lea            r4d, [r4d*2+1]
    add            r5d, 8
    cmp            r4d, r3d
    cmovl          r3d, r4d
    cmp            r5d, r3d
    cmovl          r3d, r5d
    cmp            r3d, 32
    jl .found
    add           ptrq, 12
    sub          sized, 12
    cmp          sized, 16
    jge .loop

DEFINE_ARGS ptr, _, size, tmp
.end:
    sub          sized, 3
    jl .ret_null
.end_loop:
    xor           tmpd, tmpd
    test   word [ptrq], 0xFFFF
    cmovz         tmpw, [ptrq+1]
    xor           tmpd, 0x0100
    jz .ret
    inc           ptrq
    dec          sized
    jge .end_loop
.ret_null:
    xor           ptrq, ptrq
.ret:
%if ARCH_X86_32
    mov            eax, ptrd
%else
    mov            rax, ptrq
%endif
    RET

.found:
    lea            rax, [ptrq+r3q]
    RET
