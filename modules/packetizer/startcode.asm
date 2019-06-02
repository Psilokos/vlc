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

shuf_pad2dw: db 0x00, 0x01, 0x02, 0x80, 0x03, 0x04, 0x05, 0x80
             db 0x06, 0x07, 0x08, 0x80, 0x09, 0x0A, 0x0B, 0x80

pd_0x00010000: times 4 dd 0x00010000

SECTION .text

INIT_XMM sse2
cglobal startcode_FindAnnexB, 2, 7, 6, ptr, end, size
    mov          sizeq, endq
    sub          sizeq, ptrq
    cmp          sized, 16
    jl .end

    LEA            r6q, pd_0x00010000
    mova            m0, [r6q]
    pslld           m0, 8
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

INIT_XMM ssse3
cglobal startcode_FindAnnexB, 2, 7, 5, ptr, end, size
%define base r6q-shuf_pad2dw
    mov          sizeq, endq
    sub          sizeq, ptrq
    cmp          sized, 16
    jl .end

    LEA            r6q, shuf_pad2dw
    mova            m0, [base+shuf_pad2dw]
    mova            m1, [base+pd_0x00010000]
.loop:
    movu            m2, [ptrq]
    psrldq          m3, m2, 1
    psrldq          m4, m2, 2
    pshufb          m2, m0
    pshufb          m3, m0
    pshufb          m4, m0
    pcmpeqd         m2, m1
    pcmpeqd         m3, m1
    pcmpeqd         m4, m1
    movmskps       r3d, m2
    movmskps       r4d, m3
    movmskps       r5d, m4
    tzcnt          r3d, r3d
    tzcnt          r4d, r4d
    tzcnt          r5d, r5d
    cmp            r4d, r3d
    cmovle         r3d, r4d
    setle          r4b
    cmp            r5d, r3d
    cmovle         r3d, r5d
    setle          r5b
    test           r3d, 32
    jz .found
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
    lea            r3d, [r3d*3]
    mov            r6d, r4d
    xor            r4d, r5d
    and            r4d, r6d
    shl            r5d, 1
    or             r4d, r5d
    add            r3d, r4d
    lea            rax, [ptrq+r3q]
    RET

%if ARCH_X86_64

INIT_YMM avx2
cglobal startcode_FindAnnexB, 2, 7, 5, ptr, end, size
%define base r6q-shuf_pad2dw
    lea            r6q, [shuf_pad2dw]
    vbroadcasti128  m0, [base+shuf_pad2dw]
    vbroadcasti128  m1, [base+pd_0x00010000]
    mov          sizeq, endq
    sub          sizeq, ptrq
    cmp          sizeq, 28
    jl .xmm

.loop:
    movu           xm2, [ptrq+ 0]
    vinserti128     m2, [ptrq+12], 1
    psrldq          m3, m2, 1
    psrldq          m4, m2, 2
    pshufb          m2, m0
    pshufb          m3, m0
    pshufb          m4, m0
    pcmpeqd         m2, m1
    pcmpeqd         m3, m1
    pcmpeqd         m4, m1
    movmskps       r3d, m2
    movmskps       r4d, m3
    movmskps       r5d, m4
    tzcnt          r3d, r3d
    tzcnt          r4d, r4d
    tzcnt          r5d, r5d
    cmp            r4d, r3d
    cmovle         r3d, r4d
    setle          r4b
    cmp            r5d, r3d
    cmovle         r3d, r5d
    setle          r5b
    test           r3d, 32
    jz .found
    add           ptrq, 24
    sub          sized, 24
    cmp          sized, 28
    jge .loop

INIT_XMM avx2
.xmm:
    cmp          sized, 16
    jl .end
    movu            m2, [ptrq]
    psrldq          m3, m2, 1
    psrldq          m4, m2, 2
    pshufb          m2, m0
    pshufb          m3, m0
    pshufb          m4, m0
    pcmpeqd         m2, m1
    pcmpeqd         m3, m1
    pcmpeqd         m4, m1
    movmskps       r3d, m2
    movmskps       r4d, m3
    movmskps       r5d, m4
    tzcnt          r3d, r3d
    tzcnt          r4d, r4d
    tzcnt          r5d, r5d
    cmp            r4d, r3d
    cmovle         r3d, r4d
    setle          r4b
    cmp            r5d, r3d
    cmovle         r3d, r5d
    setle          r5b
    test           r3d, 32
    jnz .end

.found:
    lea            r3d, [r3d*3]
    mov            r6d, r4d
    xor            r4d, r5d
    and            r4d, r6d
    shl            r5d, 1
    or             r4d, r5d
    add            r3d, r4d
    lea            rax, [ptrq+r3q]
    RET

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
    mov            rax, ptrq
    RET

%endif
