%include "x86inc.asm"

SECTION .text

INIT_ZMM avx512
cglobal ravu_shuffle_weights, 4, 5, 0, swt, wt, w, h, x
    mov            r4d, 0xC
    kmovb           k1, r4d
.loop_y:
    mov             xq, wq
.loop_x:
; low 16 sets
    movu            m0, [wtq+ 0*18*2]
    movu            m1, [wtq+ 1*18*2]
    movu            m2, [wtq+ 2*18*2]
    movu            m3, [wtq+ 3*18*2]
    movu            m4, [wtq+ 4*18*2]
    movu            m5, [wtq+ 5*18*2]
    movu            m6, [wtq+ 6*18*2]
    movu            m7, [wtq+ 7*18*2]
    movu            m8, [wtq+ 8*18*2]
    movu            m9, [wtq+ 9*18*2]
    movu           m10, [wtq+10*18*2]
    movu           m11, [wtq+11*18*2]
    movu           m12, [wtq+12*18*2]
    movu           m13, [wtq+13*18*2]
    movu           m14, [wtq+14*18*2]
    movu           m15, [wtq+15*18*2]
    punpckhdq      m16, m0, m1          ; P0W45 P1W45 P0W67 P1W67 P0WCD P1WCD P0WEF P1WEF
    punpckldq       m0, m1              ; P0W01 P1W01 P0W23 P1W23 P0W89 P1W89 P0WAB P1WAB
    punpckhdq      m17, m2, m3          ; P2W45 P3W45 P2W67 P3W67 P2WCD P3WCD P2WEF P3WEF
    punpckldq       m2, m3              ; P2W01 P3W01 P2W23 P3W23 P2W89 P3W89 P2WAB P3WAB
    punpckhdq      m18, m4, m5
    punpckldq       m4, m5
    punpckhdq      m19, m6, m7
    punpckldq       m6, m7
    punpckhdq      m20, m8, m9
    punpckldq       m8, m9
    punpckhdq      m21, m10, m11
    punpckldq      m10, m11
    punpckhdq      m22, m12, m13
    punpckldq      m12, m13
    punpckhdq      m23, m14, m15
    punpckldq      m14, m15
    punpckhqdq     m15, m16, m17        ; P0W67|P1W67 P2W67|P3W67 P0WEF|P1WEF P2WEF|P3WEF
    punpcklqdq     m16, m17             ; P0W45|P1W45 P2W45|P3W45 P0WCD|P1WCD P2WCD|P3WCD
    punpckhqdq      m1, m0, m2          ; P0W23|P1W23 P2W23|P3W23 P0WAB|P1WAB P2WAB|P3WAB
    punpcklqdq      m0, m2              ; P0W01|P1W01 P2W01|P3W01 P0W89|P1W89 P2W89|P3W89 P0123W16.17
    punpckhqdq     m17, m18, m19
    punpcklqdq     m18, m19
    punpckhqdq      m3, m4, m6
    punpcklqdq      m2, m4, m6          ; p4-7
    punpckhqdq     m19, m20, m21
    punpcklqdq     m20, m21
    punpckhqdq      m5, m8, m10
    punpcklqdq      m4, m8, m10         ; p8-11
    punpckhqdq     m21, m22, m23
    punpcklqdq     m22, m23
    punpckhqdq      m7, m12, m14
    punpcklqdq      m6, m12, m14        ; p12-15
; weights 0, 1, 2, 3, 4, 5, 6, 7
    mova           xm8, xm0
    mova           xm9, xm2
    mova          xm10, xm1
    mova          xm11, xm3
    mova          xm12, xm16
    mova          xm13, xm18
    mova          xm14, xm15
    mova          xm23, xm17
    vinserti32x4   ym8, xm4, 1
    vinserti32x4   ym9, xm6, 1
    vinserti32x4  ym10, xm5, 1
    vinserti32x4  ym11, xm7, 1
    vinserti32x4  ym12, xm20, 1
    vinserti32x4  ym13, xm22, 1
    vinserti32x4  ym14, xm19, 1
    vinserti32x4  ym23, xm21, 1
    movu [swtq+32*2*0], ym8             ; 01-012389AB
    movu [swtq+32*2*1], ym9             ; 01-4567CDEF
    movu [swtq+32*2*2], ym10            ; 23-012389AB
    movu [swtq+32*2*3], ym11            ; 23-4567CDEF
    movu [swtq+32*2*4], ym12            ; 45-012389AB
    movu [swtq+32*2*5], ym13            ; 45-4567CDEF
    movu [swtq+32*2*6], ym14            ; 67-012389AB
    movu [swtq+32*2*7], ym23            ; 67-4567CDEF
; weights 8, 9, 10, 11, 12, 13, 14, 15
    vextracti32x4  xm8, m0, 1
    vextracti32x4  xm9, m2, 1
    vextracti32x4 xm10, m1, 1
    vextracti32x4 xm11, m3, 1
    vextracti32x4 xm12, m16, 1
    vextracti32x4 xm13, m18, 1
    vextracti32x4 xm14, m15, 1
    vextracti32x4 xm23, m17, 1
    vpblendmq  ym8{k1}, ym8, ym4
    vpblendmq  ym9{k1}, ym9, ym6
    vpblendmq ym10{k1}, ym10, ym5
    vpblendmq ym11{k1}, ym11, ym7
    vpblendmq ym12{k1}, ym12, ym20
    vpblendmq ym13{k1}, ym13, ym22
    vpblendmq ym14{k1}, ym14, ym19
    vpblendmq ym23{k1}, ym23, ym21
.just_fuck_off:
    movu [swtq+32*2* 8], ym8            ; 89-012389AB
    movu [swtq+32*2* 9], ym9            ; 89-4567CDEF
    movu [swtq+32*2*10], ym10           ; AB-012389AB
    movu [swtq+32*2*11], ym11           ; AB-4567CDEF
    movu [swtq+32*2*12], ym12           ; CD-012389AB
    movu [swtq+32*2*13], ym13           ; CD-4567CDEF
    movu [swtq+32*2*14], ym14           ; EF-012389AB
    movu [swtq+32*2*15], ym23           ; EF-4567CDEF
; weights 16, 17
    vextracti32x4  xm8, m0, 2
    vextracti32x4  xm9, m2, 2
    vextracti32x4 xm10, m4, 2
    vextracti32x4 xm11, m6, 2
    vinserti32x4    m8, xm10, 1
    vinserti32x4    m9, xm11, 1
    movu [swtq+32*2*16], ym8            ; 1011-012389AB
    movu [swtq+32*2*17], ym9            ; 1011-012389AB
; high 16 sets
    movu            m0, [wtq+16*18*2]
    movu            m1, [wtq+17*18*2]
    movu            m2, [wtq+18*18*2]
    movu            m3, [wtq+19*18*2]
    movu            m4, [wtq+20*18*2]
    movu            m5, [wtq+21*18*2]
    movu            m6, [wtq+22*18*2]
    movu            m7, [wtq+23*18*2]
    movu            m8, [wtq+24*18*2]
    movu            m9, [wtq+25*18*2]
    movu           m10, [wtq+26*18*2]
    movu           m11, [wtq+27*18*2]
    movu           m12, [wtq+28*18*2]
    movu           m13, [wtq+29*18*2]
    movu           m14, [wtq+30*18*2]
    movu           m15, [wtq+31*18*2]
    punpckhdq      m16, m0, m1          ; P0W45 P1W45 P0W67 P1W67 P0WCD P1WCD P0WEF P1WEF
    punpckldq       m0, m1              ; P0W01 P1W01 P0W23 P1W23 P0W89 P1W89 P0WAB P1WAB
    punpckhdq      m17, m2, m3          ; P2W45 P3W45 P2W67 P3W67 P2WCD P3WCD P2WEF P3WEF
    punpckldq       m2, m3              ; P2W01 P3W01 P2W23 P3W23 P2W89 P3W89 P2WAB P3WAB
    punpckhdq      m18, m4, m5
    punpckldq       m4, m5
    punpckhdq      m19, m6, m7
    punpckldq       m6, m7
    punpckhdq      m20, m8, m9
    punpckldq       m8, m9
    punpckhdq      m21, m10, m11
    punpckldq      m10, m11
    punpckhdq      m22, m12, m13
    punpckldq      m12, m13
    punpckhdq      m23, m14, m15
    punpckldq      m14, m15
    punpckhqdq     m15, m16, m17        ; P0W67|P1W67 P2W67|P3W67 P0WEF|P1WEF P2WEF|P3WEF
    punpcklqdq     m16, m17             ; P0W45|P1W45 P2W45|P3W45 P0WCD|P1WCD P2WCD|P3WCD
    punpckhqdq      m1, m0, m2          ; P0W23|P1W23 P2W23|P3W23 P0WAB|P1WAB P2WAB|P3WAB
    punpcklqdq      m0, m2              ; P0W01|P1W01 P2W01|P3W01 P0W89|P1W89 P2W89|P3W89 P0123W16.17
    punpckhqdq     m17, m18, m19
    punpcklqdq     m18, m19
    punpckhqdq      m3, m4, m6
    punpcklqdq      m2, m4, m6          ; p4-7
    punpckhqdq     m19, m20, m21
    punpcklqdq     m20, m21
    punpckhqdq      m5, m8, m10
    punpcklqdq      m4, m8, m10         ; p8-11
    punpckhqdq     m21, m22, m23
    punpcklqdq     m22, m23
    punpckhqdq      m7, m12, m14
    punpcklqdq      m6, m12, m14        ; p12-15
; weights 0, 1, 2, 3, 4, 5, 6, 7
    mova           xm8, xm0
    mova           xm9, xm2
    mova          xm10, xm1
    mova          xm11, xm3
    mova          xm12, xm16
    mova          xm13, xm18
    mova          xm14, xm15
    mova          xm23, xm17
    vinserti32x4   ym8, xm4, 1
    vinserti32x4   ym9, xm6, 1
    vinserti32x4  ym10, xm5, 1
    vinserti32x4  ym11, xm7, 1
    vinserti32x4  ym12, xm20, 1
    vinserti32x4  ym13, xm22, 1
    vinserti32x4  ym14, xm19, 1
    vinserti32x4  ym23, xm21, 1
    movu [swtq+32*2*0+32], ym8             ; 01-012389AB
    movu [swtq+32*2*1+32], ym9             ; 01-4567CDEF
    movu [swtq+32*2*2+32], ym10            ; 23-012389AB
    movu [swtq+32*2*3+32], ym11            ; 23-4567CDEF
    movu [swtq+32*2*4+32], ym12            ; 45-012389AB
    movu [swtq+32*2*5+32], ym13            ; 45-4567CDEF
    movu [swtq+32*2*6+32], ym14            ; 67-012389AB
    movu [swtq+32*2*7+32], ym23            ; 67-4567CDEF
; weights 8, 9, 10, 11, 12, 13, 14, 15
    vextracti32x4  xm8, m0, 1
    vextracti32x4  xm9, m2, 1
    vextracti32x4 xm10, m1, 1
    vextracti32x4 xm11, m3, 1
    vextracti32x4 xm12, m16, 1
    vextracti32x4 xm13, m18, 1
    vextracti32x4 xm14, m15, 1
    vextracti32x4 xm23, m17, 1
    vpblendmq  ym8{k1}, ym8, ym4
    vpblendmq  ym9{k1}, ym9, ym6
    vpblendmq ym10{k1}, ym10, ym5
    vpblendmq ym11{k1}, ym11, ym7
    vpblendmq ym12{k1}, ym12, ym20
    vpblendmq ym13{k1}, ym13, ym22
    vpblendmq ym14{k1}, ym14, ym19
    vpblendmq ym23{k1}, ym23, ym21
    movu [swtq+32*2* 8+32], ym8            ; 89-012389AB
    movu [swtq+32*2* 9+32], ym9            ; 89-4567CDEF
    movu [swtq+32*2*10+32], ym10           ; AB-012389AB
    movu [swtq+32*2*11+32], ym11           ; AB-4567CDEF
    movu [swtq+32*2*12+32], ym12           ; CD-012389AB
    movu [swtq+32*2*13+32], ym13           ; CD-4567CDEF
    movu [swtq+32*2*14+32], ym14           ; EF-012389AB
    movu [swtq+32*2*15+32], ym23           ; EF-4567CDEF
; weights 16, 17
    vextracti32x4  xm8, m0, 2
    vextracti32x4  xm9, m2, 2
    vextracti32x4 xm10, m4, 2
    vextracti32x4 xm11, m6, 2
    vinserti32x4    m8, xm10, 1
    vinserti32x4    m9, xm11, 1
    movu [swtq+32*2*16+32], ym8            ; 1011-012389AB
    movu [swtq+32*2*17+32], ym9            ; 1011-4567CDEF
    add            wtq, 18*2*32
    add           swtq, 18*2*32
    sub             xq, 32
    jg .loop_x
    dec             hq
    jg .loop_y
    RET
