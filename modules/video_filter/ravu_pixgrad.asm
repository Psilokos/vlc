%include "x86inc.asm"

SECTION_RODATA 64

vperm: dd 0x1, 0x2, 0x0, 0x0, 0x3, 0x4, 0x0, 0x0
       dd 0x9, 0xA, 0x0, 0x0, 0xB, 0xC, 0x0, 0x0

hperm: dd 0, 1, 0, 0, 2, 3, 0, 0
       dd 4, 5, 0, 0, 6, 7, 0, 0

hperm_big: dd 0x0, 0x0, 0x0, 0x4, 0x1, 0x5, 0x0, 0x0
           dd 0x0, 0x0, 0x8, 0xC, 0x9, 0xD, 0x0, 0x0

hperm_small: dd 0x2, 0x0, 0x3, 0x1, 0x4, 0x2, 0x5, 0x3
             dd 0xA, 0x8, 0xB, 0x9, 0xC, 0xA, 0xD, 0xB

norm_factors: dw 0xC45, 0xFC1, 0xFC1, 0x143A, 0x0, 0x0, 0x0, 0x0
              dw 0xFC1, 0xC45, 0x143A, 0xFC1, 0x0, 0x0, 0x0, 0x0

dd_0x15556: dd 0x15556
dd_0x80001: dd 0x80001
dq_0x8000000000: dq 0x8000000000

dd_0x8000: dd 0x8000
dq_0x80020000: dq 0x80020000

SECTION .text

INIT_ZMM avx512
cglobal ravu_compute_abd, 4, 6, 0, src, a, b, d
%define base r5-vperm
    lea                 r5, [vperm]
    mov                r4d, 0x6666
    kmovw               k3, r4d
    kxnord              k1, k1, k1
    kshiftlq            k2, k1, 32
    kshiftrq            k4, k2, 60
    kshiftlb            k5, k4, 4
    mov                r4d, 0xCCCC
    kmovw               k6, r4d
    knotw               k7, k6
    vpbroadcastd    m0{k1}, [base+dd_0x80001]
    vpbroadcastd    m1{k1}, [base+dd_0x15556]
SWAP m2, m18
SWAP m3, m19
SWAP m4, m20
SWAP m5, m21
SWAP m6, m22
SWAP m7, m23
    mova               m18, [base+vperm]
    mova               m19, [base+hperm]
    mova               m20, [base+hperm_big]
    mova               m21, [base+hperm_small]
    mova             xmm22, [base+norm_factors+ 0]
    mova             xmm23, [base+norm_factors+16]
    pxor               m30, m30
    vpbroadcastq       m31, [base+dq_0x8000000000]
    pmovzxbd            m2, [srcq+8*0]
    pmovzxbd            m4, [srcq+8*2]
    pmovzxbd            m6, [srcq+8*4]
.v_grads:
; vertical gradients
    psubd               m8, m4, m2      ; small diffs: high 256 bits used in big grads
    psubd              m10, m6, m4      ; small diffs:  low 256 bits used in big grads
    psubd              m11, m2, m6      ; big diffs
    vextracti32x8    ymm25, m8, 1
    vinserti32x8        m9, ymm26, 1    ; small mid diffs
    vpblendmb       m8{k2}, m8, m10     ; small top / bottom diffs
    pslld               m9, 3
    paddd               m9, m11
    pmulld              m8, m0          ; small gy top / bottom
    pmulld              m9, m1          ;   big gy top / bottom
    vpermd              m8, m18, m8
    vpermd              m9, m18, m9
    punpckldq           m8, m30
    punpckldq           m9, m30
.h_grads:
; horizontal gradients
    vpblendmd       m0{k3}, m0, m1
    vpermd              m3, m20, m2
    vpermd              m5, m20, m4
    vpermd              m7, m20, m6
    vpermd              m2, m21, m2
    vpermd              m4, m21, m4
    vpermd              m6, m21, m6
    ; fixme quite ugly, no phsubd in avx512.... maybe using column vectors is better
    vextracti32x8     ymm2, m2, 1
    vextracti32x8     ymm3, m3, 1
    vextracti32x8    ymm10, m4, 1
    vextracti32x8    ymm11, m5, 1
INIT_YMM avx2
    ; fixme m2 / m12 diffs are useless ; same for big diff, so:
    ; could phsubd m8, m6
    ;   and phsubd m9, m7
    phsubd              m2, m6
    phsubd              m3, m7
    phsubd              m4, m4
    phsubd             m10, m10
    phsubd              m5, m5
    phsubd             m11, m11
    vpermq              m2, m2, q3120
    vpermq              m3, m3, q3120
    vpermq              m4, m4, q3120
    vpermq             m10, m10, q3120
    vpermq              m5, m5, q3120
    vpermq             m11, m11, q3120
INIT_ZMM avx512
SWAP m2, m18
SWAP m3, m19
SWAP m4, m20
SWAP m5, m21
SWAP m6, m22
SWAP m7, m23
; fixme use ymm till pmovzx or .abd
    vinserti32x4        m4, xmm10, 1
    vinserti32x4        m5, xmm11, 1
    pslld           m2{k3}, m2, 3
    pslld           m4{k3}, m4, 3
    paddd           m2{k3}, m2, m3
    paddd           m4{k3}, m4, m5
    pmulld              m2, m0          ; gx top / bottom
    pmulld              m4, m0          ; gx mid
    pmovzxdq            m2, ymm2
    pmovzxdq            m4, ymm4
.abd:
; calc_abd
    pmuldq              m3, m2, m8
    pmuldq              m5, m4, m9
    pmuldq              m2, m2
    pmuldq              m4, m4
    pmuldq              m8, m8
    pmuldq              m9, m9
    paddq               m2, m31
    paddq               m4, m31
    paddq               m3, m31
    paddq               m5, m31
    paddq               m8, m31
    paddq               m9, m31
    ; fixme shift by 41 to avoid unsigned 16-bit values => also fix added value for rounding
    psrlq               m2, 40          ; gx * gx top / bottom
    psrlq               m4, 40          ; gx * gx mid
    vpsraq              m3, 40          ; gx * gy top / bottom
    vpsraq              m5, 40          ; gx * gy mid
    psrlq               m8, 40          ; gy * gy top / bottom
    psrlq               m9, 40          ; gy * gy mid
    packusdw            m2, m4
    packssdw            m3, m5
    packusdw            m8, m9
; attempt to fix blindly cause can't fucking debug.......
    vpbroadcastd       m14, [dd_0x8000]
    psubd               m2, m14
    psubd               m3, m14
    psubd               m8, m14
; end of fucking blind fix attempt
    packssdw            m2, m30
    packssdw            m3, m30
    packssdw            m8, m30
    vextracti128      xmm4, m2, 1
    vextracti128      xmm7, m3, 1
    vextracti128     xmm10, m8, 1
    vextracti128      xmm5, m2, 2
    vextracti128      xmm8, m3, 2
    vextracti128     xmm11, m8, 2
    vextracti128      xmm6, m2, 3
    vextracti128      xmm9, m3, 3
    vextracti128     xmm12, m8, 3
.vnni:
INIT_XMM avx512
SWAP m31, m15
    pxor                m0, m0
    pxor                m1, m1
    pxor               m13, m13
    vpdpwssds           m0, m2, m22
    vpdpwssds           m0, m4, m23
    vpdpwssds           m0, m5, m22
    vpdpwssds           m0, m6, m23
    vpdpwssds           m1, m3, m22
    vpdpwssds           m1, m7, m23
    vpdpwssds           m1, m8, m22
    vpdpwssds           m1, m9, m23
    vpdpwssds          m13, m24, m22
    vpdpwssds          m13, m10, m23
    vpdpwssds          m13, m11, m22
    vpdpwssds          m13, m12, m23
    phaddd              m0, m0
    phaddd              m0, m0
    phaddd              m1, m1
    phaddd              m1, m1
    phaddd             m13, m13
    phaddd             m13, m13
    pmovsxdq            m0, m0
    pmovsxdq            m1, m1
    pmovsxdq           m13, m13
    movq               m15, [dq_0x80020000]
    paddq               m0, m15
    paddq               m1, m15
    paddq              m13, m15
    psrlq              m31, 24
    paddd               m0, m31
    paddd               m1, m31
    paddd              m13, m31
    psrld               m0, 16
    psrad               m1, 16
    psrld              m13, 16
    movd              [aq], m0
    movd              [bq], m1
    movd              [dq], m13
    RET
