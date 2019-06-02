/*****************************************************************************
 * startcode_helper.h: Startcodes helpers
 *****************************************************************************
 * Copyright (C) 2016 VideoLAN Authors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#ifndef VLC_STARTCODE_HELPER_H_
#define VLC_STARTCODE_HELPER_H_

#include <vlc_block_helper.h>
#include <vlc_cpu.h>

/* That code is adapted from libav's ff_avc_find_startcode_internal
 * and i believe the trick originated from
 * https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord
 */
static inline uint8_t const *
startcode_FindAnnexB_Bits(uint8_t const *p, uint8_t const *end)
{
    uint8_t const *a = p + 4 - ((intptr_t)p & 3);

    for (end -= 3; p < a && p <= end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    for (end -= 3; p < end; p += 4) {
        uint32_t x = *(uint32_t const *)p;
        if ((x - 0x01010101) & (~x) & 0x80808080)
        {
            /* matching DW isn't faster */
            /* Looks up efficiently for an AnnexB startcode 0x00 0x00 0x01
             * by using a 4 times faster trick than single byte lookup. */
            if (p[1] == 0) {
                if (p[0] == 0 && p[2] == 1)
                    return p;
                if (p[2] == 0 && p[3] == 1)
                    return p+1;
            }
            if (p[3] == 0) {
                if (p[2] == 0 && p[4] == 1)
                    return p+2;
                if (p[4] == 0 && p[5] == 1)
                    return p+3;
            }
        }
    }

    for (end += 3; p <= end; p++) {
        if (p[0] == 0 && p[1] == 0 && p[2] == 1)
            return p;
    }

    return NULL;
}

#if defined(__i386__) || defined(__x86_64__)
uint8_t const *vlcpriv_startcode_FindAnnexB_sse2(uint8_t const *ptr,
                                                 uint8_t const *end);
uint8_t const *vlcpriv_startcode_FindAnnexB_ssse3(uint8_t const *ptr,
                                                  uint8_t const *end);
# ifdef __x86_64__
uint8_t const *vlcpriv_startcode_FindAnnexB_avx2(uint8_t const *ptr,
                                                 uint8_t const *end);
# endif
#endif

static inline block_startcode_helper_t
startcode_FindAnnexB_helper(void)
{
#if defined(__i386__) || defined(__x86_64__)
# ifdef __x86_64__
    if (vlc_CPU_AVX2())
        return vlcpriv_startcode_FindAnnexB_avx2;
    else
# endif
    if (vlc_CPU_SSSE3())
        return vlcpriv_startcode_FindAnnexB_ssse3;
    else if (vlc_CPU_SSE2())
        return vlcpriv_startcode_FindAnnexB_sse2;
    else
#endif
        return startcode_FindAnnexB_Bits;
}

#endif
