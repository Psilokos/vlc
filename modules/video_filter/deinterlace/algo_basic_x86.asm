;*****************************************************************************
;* algo_basic_x86.c : Basic algorithms for the VLC deinterlacer
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

%macro AVG 2
 %if pxsize == 1
    pavgb               %1, %2
 %else
    pavgw               %1, %2
 %endif
%endmacro

%macro COPY_LINE 1
    xor                 xd, xd
.%1:
    mova                m0, [srcq+xq*pxsize]
    mova  [dstq+xq*pxsize], m0
    add                 xd, mmsize / pxsize
    cmp                 xd, wd
    jl .%1
%endmacro

%macro DEINT_LINEAR 1
cglobal deint_linear_%1bit, 7, 11, 2, dst, ds, src, ss, w, h, field, \
                                      dst_tmp, src_tmp, src2, x
 %assign pxsize %1 / 8
    or          fieldd, 0           ; 0 = keep top | 1 = keep bottom
    jz .main_loop
    COPY_LINE skip_top_field
    dec                 hd
    add               srcq, ssq
    add               dstq, dsq
.main_loop:
    sub                 hd, 2
    jle .last_line_start
    lea              src2q, [srcq+ssq*2]
    COPY_LINE keep_field
    add               dstq, dsq
    mov           src_tmpq, srcq
    mov           dst_tmpq, dstq
    xor                 xd, xd
.main_loop_x:
    mova                m0, [src_tmpq]
    AVG                 m0, [src2q]
    mova        [dst_tmpq], m0
    add           src_tmpq, mmsize
    add           dst_tmpq, mmsize
    add              src2q, mmsize
    add                 xd, mmsize / pxsize
    cmp                 xd, wd
    jl .main_loop_x
    lea               srcq, [srcq+ssq*2]
    add               dstq, dsq
    jmp .main_loop
.last_line_start:
    COPY_LINE last_line
    or              fieldd, 0
    jnz .ret
    add               srcq, ssq
    add               dstq, dsq
    COPY_LINE skip_bottom_field
.ret:
    RET
%endmacro

INIT_XMM sse2

DEINT_LINEAR 8

DEINT_LINEAR 16
