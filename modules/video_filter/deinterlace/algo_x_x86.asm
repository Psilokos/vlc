;*****************************************************************************
;* algo_x_x86.c : X algorithm for the VLC deinterlacer
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

SECTION .data

pw_4: times 8 dw 4

SECTION .text

INIT_XMM sse2

cglobal deint_render_x_band_8x8 5, 15, 8, 12, dst, ds, src, ss, mbx, modx, \
                                              x, x0, h, dst_base, src_base, \
                                              ss3, fr, ff
%ifidn modxd, modxm
    mov          [rsp], modxd
%endif
    mov      src_baseq, srcq
    mov      dst_baseq, dstq
    dec           mbxd
    xor            x0d, x0d
    pxor            m7, m7
.loop:
    lea           ss3q, [ssq*3]
    mov             hd, 8
    pxor            m7, m7
.detect_loop:
; todo save m2, m3, load m0, m1 before
    movq            m0, [srcq+ssq*0]
    movq            m1, [srcq+ssq*1]
    movq            m2, [srcq+ssq*2]
    movq            m3, [srcq+ss3q ]
    punpcklbw       m0, m7
    punpcklbw       m1, m7
    punpcklbw       m2, m7
    punpcklbw       m3, m7
    psubw           m4, m0, m1
    psubw           m0, m2
    psubw           m2, m1
    psubw           m3, m1
; todo rework the detection by using absdiff, might be faster
    pmaddwd         m4, m4
    pmaddwd         m0, m0
    pmaddwd         m2, m2
    pmaddwd         m3, m3
    paddd           m2, m4
    paddd           m3, m0
    psrldq          m4, m2, 8
    psrldq          m0, m3, 8
    paddd           m2, m4
    paddd           m3, m0
    psrlq           m4, m2, 32
    psrlq           m0, m3, 32
    paddd           m2, m4
    paddd           m3, m0
    movd           frd, m2
    movd           ffd, m3
    cmp            frd, 32
    setg           r8b
    imul           frd, 6
    shl            ffd, 3
    cmp            frd, ffd
    setge          r7b
    and             r8, r7
    jnz .detect_found
    lea      src_baseq, [src_baseq+ssq*2]
    mov           srcq, src_baseq
    sub             hd, 2
    jge .detect_loop
    jmp .merge
.detect_found:
    or             x0d, 0
    jz .field_e
    cmp            x0d, mbx
    je .field_e
.field:
DEFINE_ARGS dst, ds, src, ss, mbx, modx, \
            x, x0, h, dst_base, src_base, srcx, c0, c1, c2
    mov             hd, 8
.field_loop_y:
    movq            m0, [srcq]
    movq        [dstq], m0
    add           dstq, dsq
    mov          srcxq, srcq
    xor             xd, xd
.field_loop_x:
; todo if we rearrange the args, we can avoid store/load of rcx
; at each iteration
; todo use 16 bytes instead of 8 (2 loop iters at once)
    movq            m0, [srcxq+ssq*0-2]
    movq            m1, [srcxq+ssq*0-3]
    movq            m2, [srcxq+ssq*0-4]
    movq            m3, [srcxq+ssq*2-2]
    psadbw          m4, m0, [srcxq+ssq*2-4]
    psadbw          m1, [srcxq+ssq*2-3]
    psadbw          m2, m3
    movd           c2d, m4
    movd           c1d, m1
    movd           c0d, m2
    mov        [rsp+4], rcx
    cmp            c0d, c1d
    setl          r12b
    setl          r14b
    cmp            c2d, c1d
    setl          r13b
    setl            cl
    shr           r12b, cl ; fixme seems like only cl is okay here => set cl?
    mov             cl, r14b
    cmovz         r12d, 2       ; else
    cmovz         r14d, 2
    cmovnz        r12d, 1       ; if (c0 < c1 && c1 <= c2)
    cmovnz        r14d, 3
    shr           r13b, cl
    cmovz         r12d, 3       ; else if (c2 < c1 && c1 <= c0)
    cmovz         r14d, 1
    mov            rcx, [rsp+4]
    movd            m1, r12
    movd            m2, r14
    psrlq           m0, m1
    psrlq           m3, m2
    pavgb           m0, m3
    movd     [dstq+xq], m0
    inc          srcxq
    inc             xd
    and             xd, 8
    jz .field_loop_x
    lea           srcq, [srcq+ssq*2]
    add           dstq, dsq
    sub             hd, 2
    jg .field_loop_y
    jmp .loop_end
.field_e:
    mov             hd, 8
.field_e_loop:
    movq            m0, [srcq+ssq*0]
    pavgb           m1, m0, [srcq+ssq*2]
    movq  [dstq+dsq*0], m0
    movq  [dstq+dsq*1], m1
    lea           srcq, [srcq+ssq*2]
    lea           dstq, [dstq+dsq*2]
    sub             hd, 2
    jg .field_e_loop
    jmp .loop_end
.merge:
    mova            m4, [pw_4] ; todo load that in prologue
    mov             hd, 8
.merge_loop:
    movq            m0, [srcq+ssq*0]
    movq            m1, [srcq+ssq*1]
    movq            m2, [srcq+ssq*2]
    punpcklbw       m5, m0, m7
    punpcklbw       m1, m1, m7
    punpcklbw       m2, m2, m7
    psllw           m3, m1, 1
    psllw           m1, m1, 2
    paddw           m1, m5
    paddw           m1, m2
    paddw           m1, m3
    paddw           m1, m4
    psrlw           m1, 3
    packuswb        m1, m1
    movq  [dstq+dsq*0], m0
    movq  [dstq+dsq*1], m1
    lea           srcq, [srcq+ssq*2]
    lea           dstq, [dstq+dsq*2]
    sub             hd, 2
    jg .merge_loop
.loop_end:
    add      src_baseq, 8
    add      dst_baseq, 8
    mov           srcq, src_baseq
    mov           dstq, dst_baseq
    inc            x0d
    cmp            x0d, mbxd
    jle .loop
    or     dword [rsp], 0
    jz .ret
DEFINE_ARGS dst, ds, src, ss, w, h
    mov             wd, [rsp]
    mov             hd, 8
    call mangle(private_prefix %+ _deint_render_x_blk_NxN_ %+ cpuname).start
.ret:
    RET

cglobal deint_render_x_blk_Nx8 6, 12, 6, dst, ds, src, ss, w, h, \
                                         y, fr, ff, fc, shift
    mova            m4, [pw_4]
    psrlw           m4, 1
.start:
    mov         shiftd, 8
    sub         shiftd, wd
    shr             wd, 1
    sub             hd, 2
    xor             yd, yd
    xor            fcd, fcd
    movd            m0, shiftd
    pcmpeqd         m3, m3
    psrldq          m3, m0
; fixme ensure the core give us a + 7 bytes on the right
; while keeping lines aligned
.detect_loop:
    movq            m0, [srcq+ssq*0]
    movq            m1, [srcq+ssq*1]
    movq            m2, [srcq+ssq*2]
    pand            m0, m3
    pand            m1, m3
    pand            m2, m3
    psadbw          m1, m0
    psadbw          m2, m0
    movd           frd, m1
    movd           ffd, m2
    cmp            ffd, frd
    setl          r10b
    cmp            frd, wd
    setl          r11b
    and           r10d, r11d
    add            fcd, r10d
    lea           srcq, [srcq+ssq*2]
    add             yd, 2
    cmp             yd, hd
    jl .detect_loop
    mov           srcq, src_baseq
    and            fcd, ~1
    jz .frame
    xor             yd, yd
.field_loop:
; todo reuse last line, mova mmreg, mmreg instead of reload
    movq            m0, [srcq+ssq*0]
    pavgb           m1, m0, [srcq+ssq*2]
    movq  [dstq+dsq*0], m0
    movq  [dstq+dsq*1], m1
    lea           srcq, [srcq+ssq*2]
    lea           dstq, [dstq+dsq*2]
    add             yd, 2
    cmp             yd, hd
    jl .field_loop
    jmp .end
.frame:
    xor             yd, yd
    mova            m4, [pw_4] ; fixme load that in prologue
    psrlw           m4, 1
.frame_loop:
    movq            m0, [srcq+ssq*0]
    movq            m1, [srcq+ssq*1]
    movq            m2, [srcq+ssq*2]
    punpcklbw       m1, m0, m7
    punpcklbw       m2, m7
    punpcklbw       m3, m7
    psllw           m2, 1
    paddw           m1, m4
    paddw           m1, m3
    paddw           m1, m2
    psrlw           m1, 2
    packuswb        m1, m1
    movq  [dstq+dsq*0], m0
    movq  [dstq+dsq*1], m1
    lea           srcq, [srcq+ssq*2]
    lea           dstq, [dstq+ssq*2]
    add             yd, 2
    cmp             yd, hd
    jl .frame_loop
.end:
    movq            m0, [srcq+ssq*0]
    pavgb           m1, m0, [srcq+ssq*1]
    movq  [dstq+dsq*0], m0
    movq  [dstq+dsq*1], m1
.ret:
    RET
