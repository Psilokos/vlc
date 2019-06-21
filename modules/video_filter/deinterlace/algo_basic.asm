;*****************************************************************************
;* algo_basic.c : Basic algorithms for the VLC deinterlacer
;*****************************************************************************
;* Copyright (C) 2019 VideoLAN
;*
;* Author: Victorien Le Couviour--Tuffet victorien@videolan.org
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

SECTION .text

;*****************************************************************************

INIT_XMM sse2

cglobal deint_linear_8bit, 6, 7, 2, dst, src, w, h, stride, field, src2, x
    or          fieldd, 0
    jz .main_loop
    xor             xd, xd
.first_line:
    mova            m0, [srcq+xq]
    mova     [dstq+xq], m0
    add             xd, 16
    cmp             xd, wd
    jl .first_line
    add           srcq, strideq
    add           dstq, strideq
.main_loop:
    lea          src2q, [srcq+strideq*2]
DEFINE_ARGS dst, src, w, h, dst1, field, src2, x
    add          dst1q, dstq
    xor             xd, xd
.main_loop_x:
    mova            m0, [srcq+xq]
    pavgb           m1, m0, [src2q+xq]
    mova     [dstq+xq], m0
    mova    [dst1q+xq], m1
    add             xd, 16
    cmp             xd, wd
    jl .main_loop_x
    sub          dst1q, dstq
DEFINE_ARGS dst, src, w, h, stride, field, src2, x
    mov           srcq, src2q
    lea           dstq, [dstq+strideq*2]
    sub             hd, 2
    jg .main_loop
    or          fieldd, 0
    jnz .ret
    xor             xd, xd
.last_line:
    mova            m0, [srcq+xq]
    mova     [dstq+xq], m0
    add             xd, 16
    cmp             xd, wd
    jl .last_line
.ret:
    RET

cglobal deint_linear_16bit, 6, 7, 2, dst, src, w, h, stride, field, src2, x
    or          fieldd, 0
    jz .main_loop
    xor             xd, xd
.first_line:
    mova            m0, [srcq+xq*2]
    mova   [dstq+xq*2], m0
    add             xd, 8
    cmp             xd, wd
    jl .first_line
    add           srcq, strideq
    add           dstq, strideq
    lea          src2q, [srcq+strideq*2]
DEFINE_ARGS dst, src, w, h, dst1, field, src2, x
.main_loop:
    xor             xd, xd
    add          dst1q, dstq
.main_loop_x:
    mova            m0, [srcq+xq*2]
    pavgw           m1, m0, [src2q+xq*2]
    mova   [dstq+xq*2], m0
    mova  [dst1q+xq*2], m1
    add             xd, 8
    cmp             xd, wd
    jl .main_loop_x
    sub          dst1q, dstq
DEFINE_ARGS dst, src, w, h, stride, field, src2, x
    mov           srcq, src2q
    add           dstq, strideq
    lea          src2q, [srcq+strideq*2]
    dec             hd
    jg .main_loop
    or          fieldd, 0
    jnz .ret
    xor             xd, xd
.last_line:
    mova            m0, [srcq+xq*2]
    mova   [dstq+xq*2], m0
    add             xd, 8
    cmp             xd, wd
    jl .last_line
.ret:
    RET

;*****************************************************************************
