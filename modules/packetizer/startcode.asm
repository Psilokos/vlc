;*****************************************************************************
;* startcode.asm: SIMD optimized startcodes helpers
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

INIT_XMM ssse3
cglobal startcode_FindAnnexB, 2, 7, 5, ptr, end, size, msk0, msk1, msk2
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
    palignr         m3, m2, m2, 1
    palignr         m4, m2, m2, 2
    pshufb          m2, m0
    pshufb          m3, m0
    pshufb          m4, m0
    pcmpeqd         m2, m1
    pcmpeqd         m3, m1
    pcmpeqd         m4, m1
    movmskps     msk0d, m2
    movmskps     msk1d, m3
    movmskps     msk2d, m4
    tzcnt          r3d, msk0d
    tzcnt          r4d, msk1d
    tzcnt          r5d, msk2d
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
