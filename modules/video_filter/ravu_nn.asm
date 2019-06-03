%include "x86inc.asm"

SECTION_RODATA 64

pd_0x4000: times 16 dd 0x4000

SECTION .text

INIT_ZMM avx512
cglobal ravu_compute_pixels, 4, 13, 0, dst, src, wt, w, h, stride, wstride, \
                                       dst_base, src_base, wt_base, x, stride3, stride5
    movifnidn       hd, hm
%if WIN64
    mov        strideq, stridem
%endif
    mov       wstrideq, wstridem
    lea           r10q, [strideq*2+2]
    sub           srcq, r10q
    mov      dst_baseq, dstq
    mov      src_baseq, srcq
    mov       wt_baseq, wtq
    lea       stride3q, [strideq*3]
    lea       stride5q, [strideq*5]
.loop_y:
    mov             xq, wq
.loop_x:
    pxor            m0, m0
    pxor            m1, m1
    pmovzxbw        m2, [srcq+strideq*0+0]
    pmovzxbw        m3, [srcq+strideq*1+0]
    pmovzxbw        m4, [srcq+strideq*2+0]
    pmovzxbw        m5, [srcq+stride3q +0]
    pmovzxbw        m6, [srcq+strideq*4+0]
    pmovzxbw        m7, [srcq+stride5q +0]
    pmovzxbw        m8, [srcq+stride5q +5]
    pmovzxbw        m9, [srcq+strideq*4+5]
    pmovzxbw       m10, [srcq+stride3q +5]
    pmovzxbw       m11, [srcq+strideq*2+5]
    pmovzxbw       m12, [srcq+strideq*1+5]
    pmovzxbw       m13, [srcq+strideq*0+5]
    paddw          m14, m2, m8
    paddw          m15, m3, m9
    paddw          m16, m4, m10
    paddw          m17, m5, m11
    paddw          m18, m6, m12
    paddw          m19, m7, m13
    punpcklwd      m20, m14, m15
    punpckhwd      m21, m14, m15
    punpcklwd      m22, m16, m17
    punpckhwd      m23, m16, m17
    punpcklwd      m24, m18, m19
    punpckhwd      m25, m18, m19
    movu           m26, [wtq+0*32*2]
    movu           m27, [wtq+1*32*2]
    movu           m28, [wtq+2*32*2]
    movu           m29, [wtq+3*32*2]
    movu           m30, [wtq+4*32*2]
    movu           m31, [wtq+5*32*2]
    vpdpwssds       m0, m20, m26
    vpdpwssds       m1, m21, m27
    vpdpwssds       m0, m22, m28
    vpdpwssds       m1, m23, m29
    vpdpwssds       m0, m24, m30
    vpdpwssds       m1, m25, m31

    pmovzxbw        m2, [srcq+strideq*0+1]
    pmovzxbw        m3, [srcq+strideq*1+1]
    pmovzxbw        m4, [srcq+strideq*2+1]
    pmovzxbw        m5, [srcq+stride3q +1]
    pmovzxbw        m6, [srcq+strideq*4+1]
    pmovzxbw        m7, [srcq+stride5q +1]
    pmovzxbw        m8, [srcq+stride5q +4]
    pmovzxbw        m9, [srcq+strideq*4+4]
    pmovzxbw       m10, [srcq+stride3q +4]
    pmovzxbw       m11, [srcq+strideq*2+4]
    pmovzxbw       m12, [srcq+strideq*1+4]
    pmovzxbw       m13, [srcq+strideq*0+4]
    paddw          m14, m2, m8
    paddw          m15, m3, m9
    paddw          m16, m4, m10
    paddw          m17, m5, m11
    paddw          m18, m6, m12
    paddw          m19, m7, m13
    punpcklwd      m20, m14, m15
    punpckhwd      m21, m14, m15
    punpcklwd      m22, m16, m17
    punpckhwd      m23, m16, m17
    punpcklwd      m24, m18, m19
    punpckhwd      m25, m18, m19
    movu           m26, [wtq+6*32*2]
    movu           m27, [wtq+7*32*2]
    movu           m28, [wtq+8*32*2]
    movu           m29, [wtq+9*32*2]
    movu           m30, [wtq+10*32*2]
    movu           m31, [wtq+11*32*2]
    vpdpwssds       m0, m20, m26
    vpdpwssds       m1, m21, m27
    vpdpwssds       m0, m22, m28
    vpdpwssds       m1, m23, m29
    vpdpwssds       m0, m24, m30
    vpdpwssds       m1, m25, m31

    pmovzxbw        m2, [srcq+strideq*0+2]
    pmovzxbw        m3, [srcq+strideq*1+2]
    pmovzxbw        m4, [srcq+strideq*2+2]
    pmovzxbw        m5, [srcq+stride3q +2]
    pmovzxbw        m6, [srcq+strideq*4+2]
    pmovzxbw        m7, [srcq+stride5q +2]
    pmovzxbw        m8, [srcq+stride5q +3]
    pmovzxbw        m9, [srcq+strideq*4+3]
    pmovzxbw       m10, [srcq+stride3q +3]
    pmovzxbw       m11, [srcq+strideq*2+3]
    pmovzxbw       m12, [srcq+strideq*1+3]
    pmovzxbw       m13, [srcq+strideq*0+3]
    paddw          m14, m2, m8
    paddw          m15, m3, m9
    paddw          m16, m4, m10
    paddw          m17, m5, m11
    paddw          m18, m6, m12
    paddw          m19, m7, m13
    punpcklwd      m20, m14, m15
    punpckhwd      m21, m14, m15
    punpcklwd      m22, m16, m17
    punpckhwd      m23, m16, m17
    punpcklwd      m24, m18, m19
    punpckhwd      m25, m18, m19
    movu           m26, [wtq+12*32*2]
    movu           m27, [wtq+13*32*2]
    movu           m28, [wtq+14*32*2]
    movu           m29, [wtq+15*32*2]
    movu           m30, [wtq+16*32*2]
    movu           m31, [wtq+17*32*2]
    vpdpwssds       m0, m20, m26
    vpdpwssds       m1, m21, m27
    vpdpwssds       m0, m22, m28
    vpdpwssds       m1, m23, m29
    vpdpwssds       m0, m24, m30
    vpdpwssds       m1, m25, m31
    paddd           m0, [pd_0x4000]
    paddd           m1, [pd_0x4000]
    psrad           m0, 15
    psrad           m1, 15
    packusdw        m0, m1
    packuswb        m0, m0
    vpermq          m0, m0, q3120
    movu          [dstq+ 0], xm0
    vextracti32x4 [dstq+16], m0, 2

    add           dstq, 32
    add           srcq, 32
    add            wtq, 18*32*2
    sub             xq, 32
    jg .loop_x
    add      dst_baseq, strideq
    add      src_baseq, strideq
    lea       wt_baseq, [wt_baseq+wstrideq*2]
    mov           dstq, dst_baseq
    mov           srcq, src_baseq
    mov            wtq, wt_baseq
    dec             hq
    jg .loop_y
    RET
