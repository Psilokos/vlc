/*****************************************************************************
 * algo_basic.c : Basic algorithms for the VLC deinterlacer
 *****************************************************************************
 * Copyright (C) 2000-2011 VLC authors and VideoLAN
 *
 * Author: Sam Hocevar <sam@zoy.org>
 *         Damien Lucas <nitrox@videolan.org>  (Bob, Blend)
 *         Laurent Aimar <fenrir@videolan.org> (Bob, Blend)
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

#ifdef HAVE_CONFIG_H
#   include "config.h"
#endif

#include <stdint.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_picture.h>
#include <vlc_filter.h>
#include <vlc_cpu.h>
#include "deinterlace.h"
#include "algo_basic.h"
#include "merge.h"

/*****************************************************************************
 * RenderDiscard: only keep TOP or BOTTOM field, discard the other.
 *****************************************************************************/

static int RenderDiscard( filter_t *p_filter,
                          picture_t *p_outpic, picture_t *p_pic )
{
    VLC_UNUSED(p_filter);
    int i_plane;

    /* Copy image and skip lines */
    for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )
    {
        uint8_t *p_in, *p_out_end, *p_out;

        p_in = p_pic->p[i_plane].p_pixels;

        p_out = p_outpic->p[i_plane].p_pixels;
        p_out_end = p_out + p_outpic->p[i_plane].i_pitch
                             * p_outpic->p[i_plane].i_visible_lines;

        for( ; p_out < p_out_end ; )
        {
            memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );

            p_out += p_outpic->p[i_plane].i_pitch;
            p_in += 2 * p_pic->p[i_plane].i_pitch;
        }
    }
    return VLC_SUCCESS;
}

single_pic_renderer_t DiscardRenderer(unsigned pixel_size)
{
    VLC_UNUSED(pixel_size);
    return RenderDiscard;
}

/*****************************************************************************
 * RenderBob: renders a BOB picture - simple copy
 *****************************************************************************/

static int RenderBob( filter_t *p_filter,
                      picture_t *p_outpic, picture_t *p_pic,
                      int order, int i_field )
{
    VLC_UNUSED(p_filter);
    VLC_UNUSED(order);
    int i_plane;

    /* Copy image and skip lines */
    for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )
    {
        uint8_t *p_in, *p_out_end, *p_out;

        p_in = p_pic->p[i_plane].p_pixels;
        p_out = p_outpic->p[i_plane].p_pixels;
        p_out_end = p_out + p_outpic->p[i_plane].i_pitch
                             * p_outpic->p[i_plane].i_visible_lines;

        /* For BOTTOM field we need to add the first line */
        if( i_field == 1 )
        {
            memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );
            p_in += p_pic->p[i_plane].i_pitch;
            p_out += p_outpic->p[i_plane].i_pitch;
        }

        p_out_end -= 2 * p_outpic->p[i_plane].i_pitch;

        for( ; p_out < p_out_end ; )
        {
            memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );

            p_out += p_outpic->p[i_plane].i_pitch;

            memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );

            p_in += 2 * p_pic->p[i_plane].i_pitch;
            p_out += p_outpic->p[i_plane].i_pitch;
        }

        memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );

        /* For TOP field we need to add the last line */
        if( i_field == 0 )
        {
            p_in += p_pic->p[i_plane].i_pitch;
            p_out += p_outpic->p[i_plane].i_pitch;
            memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );
        }
    }
    return VLC_SUCCESS;
}

ordered_renderer_t BobRenderer(unsigned pixel_size)
{
    VLC_UNUSED(pixel_size);
    return RenderBob;
}

/*****************************************************************************
 * RenderLinear: BOB with linear interpolation
 *****************************************************************************/

#define RENDER_LINEAR(merge, bpc, feature)                                  \
static int RenderLinear##bpc##Bit_##feature( filter_t *p_filter,            \
                                             picture_t *p_outpic,           \
                                             picture_t *p_pic,              \
                                             int order, int i_field )       \
{                                                                           \
    VLC_UNUSED(p_filter); VLC_UNUSED(order);                                \
    int i_plane;                                                            \
                                                                            \
    /* Copy image and skip lines */                                         \
    for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )              \
    {                                                                       \
        uint8_t *p_in, *p_out_end, *p_out;                                  \
                                                                            \
        p_in = p_pic->p[i_plane].p_pixels;                                  \
        p_out = p_outpic->p[i_plane].p_pixels;                              \
        p_out_end = p_out + p_outpic->p[i_plane].i_pitch                    \
                             * p_outpic->p[i_plane].i_visible_lines;        \
                                                                            \
        /* For BOTTOM field we need to add the first line */                \
        if( i_field == 1 )                                                  \
        {                                                                   \
            memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );               \
            p_in += p_pic->p[i_plane].i_pitch;                              \
            p_out += p_outpic->p[i_plane].i_pitch;                          \
        }                                                                   \
                                                                            \
        p_out_end -= 2 * p_outpic->p[i_plane].i_pitch;                      \
                                                                            \
        for( ; p_out < p_out_end ; )                                        \
        {                                                                   \
            memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );               \
                                                                            \
            p_out += p_outpic->p[i_plane].i_pitch;                          \
                                                                            \
            ptrdiff_t stride = p_pic->p[i_plane].i_pitch;                   \
            merge( p_out, p_in, p_in + 2 * stride, stride);                 \
                                                                            \
            p_in += 2 * p_pic->p[i_plane].i_pitch;                          \
            p_out += p_outpic->p[i_plane].i_pitch;                          \
        }                                                                   \
                                                                            \
        memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );                   \
                                                                            \
        /* For TOP field we need to add the last line */                    \
        if( i_field == 0 )                                                  \
        {                                                                   \
            p_in += p_pic->p[i_plane].i_pitch;                              \
            p_out += p_outpic->p[i_plane].i_pitch;                          \
            memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );               \
        }                                                                   \
    }                                                                       \
    return VLC_SUCCESS;                                                     \
}

#define RENDER_LINEAR_SIMD(bpc, feature)                                    \
                                                                            \
void vlcpriv_deint_linear_##bpc##bit_##feature(uint8_t *dst,                \
                                               ptrdiff_t dst_stride,        \
                                               uint8_t const *src,          \
                                               ptrdiff_t src_stride,        \
                                               unsigned int w, unsigned h,  \
                                               int field);                  \
                                                                            \
static int RenderLinear##bpc##Bit_##feature(filter_t *filter,               \
                                            picture_t *opic,                \
                                            picture_t *ipic,                \
                                            int order, int field)           \
{                                                                           \
    VLC_UNUSED(filter); VLC_UNUSED(order);                                  \
    for (int plane = 0 ; plane < ipic->i_planes ; ++plane)                  \
    {                                                                       \
        void *dst = opic->p[plane].p_pixels;                                \
        void *src = ipic->p[plane].p_pixels;                                \
        ptrdiff_t dst_stride = opic->p[plane].i_pitch;                      \
        ptrdiff_t src_stride = opic->p[plane].i_pitch;                      \
        unsigned int w = opic->p[plane].i_visible_pitch / (bpc / 8);        \
        unsigned int h = opic->p[plane].i_visible_lines;                    \
        vlcpriv_deint_linear_##bpc##bit_##feature(dst, dst_stride,          \
                                                  src, src_stride,          \
                                                  w, h, field);             \
    }                                                                       \
    return VLC_SUCCESS;                                                     \
}

#define RENDER_LINEAR_ARM(bpc, feature) \
    RENDER_LINEAR(merge##bpc##_##feature, bpc, feature)

RENDER_LINEAR(Merge8BitGeneric, 8, c)
RENDER_LINEAR(Merge16BitGeneric, 16, c)
#if defined(__i386__) || defined(__x86_64__)
RENDER_LINEAR_SIMD(8, sse2)
RENDER_LINEAR_SIMD(16, sse2)
#endif
#if defined(CAN_COMPILE_ARM)
RENDER_LINEAR_ARM(8, arm_neon)
RENDER_LINEAR_ARM(16, arm_neon)
RENDER_LINEAR_ARM(8, armv6)
RENDER_LINEAR_ARM(16, armv6)
#endif
#if defined(CAN_COMPILE_SVE)
RENDER_LINEAR_ARM(8, arm_sve)
RENDER_LINEAR_ARM(16, arm_sve)
#endif
#if defined(CAN_COMPULE_ARM64)
RENDER_LINEAR_ARM(8, arm64_neon)
RENDER_LINEAR_ARM(16, arm64_neon)
#endif

ordered_renderer_t LinearRenderer(unsigned pixel_size)
{
#if defined(__i386__) || defined(__x86_64__)
    if (vlc_CPU_SSE2())
        return pixel_size & 1 ? RenderLinear8Bit_sse2 : RenderLinear16Bit_sse2;
    else
#endif
#if defined(CAN_COMPILE_ARM)
    if (vlc_CPU_ARM_NEON())
        return pixel_size & 1 ? RenderLinear8Bit_arm_neon : RenderLinear16Bit_arm_neon;
    else
    if (vlc_CPU_ARMv6())
        return pixel_size & 1 ? RenderLinear8Bit_armv6 : RenderLinear16Bit_armv6;
    else
#endif
#if defined(CAN_COMPILE_SVE)
    if (vlc_CPU_ARM_SVE())
        return pixel_size & 1 ? RenderLinear8Bit_arm_sve : RenderLinear16Bit_arm_sve;
    else
#endif
#if defined(CAN_COMPULE_ARM64)
    if (vlc_CPU_ARM_NEON())
        return pixel_size & 1 ? RenderLinear8Bit_arm64_neon : RenderLinear16Bit_arm64_neon;
    else
#endif
        return pixel_size & 1 ? RenderLinear8Bit_c : RenderLinear16Bit_c;
}

/*****************************************************************************
 * RenderMean: Half-resolution blender
 *****************************************************************************/

#define RENDER_MEAN(merge, bpc, feature)                                    \
static int RenderMean##bpc##Bit_##feature( filter_t *p_filter,              \
                                          picture_t *p_outpic,              \
                                          picture_t *p_pic )                \
{                                                                           \
    VLC_UNUSED(p_filter);                                                   \
    int i_plane;                                                            \
                                                                            \
    /* Copy image and skip lines */                                         \
    for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )              \
    {                                                                       \
        uint8_t *p_in, *p_out_end, *p_out;                                  \
                                                                            \
        p_in = p_pic->p[i_plane].p_pixels;                                  \
                                                                            \
        p_out = p_outpic->p[i_plane].p_pixels;                              \
        p_out_end = p_out + p_outpic->p[i_plane].i_pitch                    \
                             * p_outpic->p[i_plane].i_visible_lines;        \
                                                                            \
        /* All lines: mean value */                                         \
        for( ; p_out < p_out_end ; )                                        \
        {                                                                   \
            merge( p_out, p_in, p_in + p_pic->p[i_plane].i_pitch,           \
                   p_pic->p[i_plane].i_pitch );                             \
                                                                            \
            p_out += p_outpic->p[i_plane].i_pitch;                          \
            p_in += 2 * p_pic->p[i_plane].i_pitch;                          \
        }                                                                   \
    }                                                                       \
    return VLC_SUCCESS;                                                     \
}

#define RENDER_MEAN_SIMD(bpc, feature)                                      \
                                                                            \
void vlcpriv_deint_mean_##bpc##bit_##feature(uint8_t *dst,                  \
                                             ptrdiff_t dst_stride,          \
                                             uint8_t const *src,            \
                                             ptrdiff_t src_stride,          \
                                             unsigned int w, unsigned h);   \
                                                                            \
static int RenderMean##bpc##Bit_##feature(filter_t *filter,                 \
                                          picture_t *opic,                  \
                                          picture_t *ipic)                  \
{                                                                           \
    VLC_UNUSED(filter);                                                     \
    for (int plane = 0 ; plane < ipic->i_planes ; ++plane)                  \
    {                                                                       \
        uint8_t *dst = opic->p[plane].p_pixels;                             \
        uint8_t *src = ipic->p[plane].p_pixels;                             \
        ptrdiff_t dst_stride = opic->p[plane].i_pitch;                      \
        ptrdiff_t src_stride = ipic->p[plane].i_pitch;                      \
        unsigned int w = opic->p[plane].i_visible_pitch / (bpc / 8);        \
        unsigned int h = opic->p[plane].i_visible_lines;                    \
        vlcpriv_deint_mean_##bpc##bit_##feature(dst, dst_stride,            \
                                                src, src_stride,            \
                                                w, h);                      \
    }                                                                       \
    return VLC_SUCCESS;                                                     \
}

#define RENDER_MEAN_ARM(bpc, feature) \
    RENDER_MEAN(merge##bpc##_##feature, bpc, feature)

RENDER_MEAN(Merge8BitGeneric, 8, c)
RENDER_MEAN(Merge16BitGeneric, 16, c)
#if defined(__i386__) || defined(__x86_64__)
RENDER_MEAN_SIMD(8, sse2)
RENDER_MEAN_SIMD(16, sse2)
#endif
#if defined(CAN_COMPILE_ARM)
RENDER_MEAN_ARM(8, arm_neon)
RENDER_MEAN_ARM(16, arm_neon)
RENDER_MEAN_ARM(8, armv6)
RENDER_MEAN_ARM(16, armv6)
#endif
#if defined(CAN_COMPILE_SVE)
RENDER_MEAN_ARM(8, arm_sve)
RENDER_MEAN_ARM(16, arm_sve)
#endif
#if defined(CAN_COMPULE_ARM64)
RENDER_MEAN_ARM(8, arm64_neon)
RENDER_MEAN_ARM(16, arm64_neon)
#endif

single_pic_renderer_t MeanRenderer(unsigned pixel_size)
{
#if defined(__i386__) || defined(__x86_64__)
    if (vlc_CPU_SSE2())
        return pixel_size & 1 ? RenderMean8Bit_sse2 : RenderMean16Bit_sse2;
    else
#endif
#if defined(CAN_COMPILE_ARM)
    if (vlc_CPU_ARM_NEON())
        return pixel_size & 1 ? RenderMean8Bit_arm_neon : RenderMean16Bit_arm_neon;
    else
    if (vlc_CPU_ARMv6())
        return pixel_size & 1 ? RenderMean8Bit_armv6 : RenderMean16Bit_armv6;
    else
#endif
#if defined(CAN_COMPILE_SVE)
    if (vlc_CPU_ARM_SVE())
        return pixel_size & 1 ? RenderMean8Bit_arm_sve : RenderMean16Bit_arm_sve;
    else
#endif
#if defined(CAN_COMPULE_ARM64)
    if (vlc_CPU_ARM_NEON())
        return pixel_size & 1 ? RenderMean8Bit_arm64_neon : RenderMean16Bit_arm64_neon;
    else
#endif
        return pixel_size & 1 ? RenderMean8Bit_c : RenderMean16Bit_c;
}

/*****************************************************************************
 * RenderBlend: Full-resolution blender
 *****************************************************************************/

#define RENDER_BLEND(merge, bpc, feature)                                   \
static int RenderBlend##bpc##Bit_##feature( filter_t *p_filter,             \
                                            picture_t *p_outpic,            \
                                            picture_t *p_pic )              \
{                                                                           \
    VLC_UNUSED(p_filter);                                                   \
    int i_plane;                                                            \
                                                                            \
    /* Copy image and skip lines */                                         \
    for( i_plane = 0 ; i_plane < p_pic->i_planes ; i_plane++ )              \
    {                                                                       \
        uint8_t *p_in, *p_out_end, *p_out;                                  \
                                                                            \
        p_in = p_pic->p[i_plane].p_pixels;                                  \
                                                                            \
        p_out = p_outpic->p[i_plane].p_pixels;                              \
        p_out_end = p_out + p_outpic->p[i_plane].i_pitch                    \
                             * p_outpic->p[i_plane].i_visible_lines;        \
                                                                            \
        /* First line: simple copy */                                       \
        memcpy( p_out, p_in, p_pic->p[i_plane].i_pitch );                   \
        p_out += p_outpic->p[i_plane].i_pitch;                              \
                                                                            \
        /* Remaining lines: mean value */                                   \
        for( ; p_out < p_out_end ; )                                        \
        {                                                                   \
            merge( p_out, p_in, p_in + p_pic->p[i_plane].i_pitch,           \
                   p_pic->p[i_plane].i_pitch );                             \
                                                                            \
            p_out += p_outpic->p[i_plane].i_pitch;                          \
            p_in  += p_pic->p[i_plane].i_pitch;                             \
        }                                                                   \
    }                                                                       \
    return VLC_SUCCESS;                                                     \
}

#define RENDER_BLEND_SIMD(bpc, feature) \
    RENDER_BLEND(Merge##bpc##Bit##feature, bpc, feature)
#define RENDER_BLEND_ARM(bpc, feature) \
    RENDER_BLEND(merge##bpc##_##feature, bpc, feature)

RENDER_BLEND(Merge8BitGeneric, 8, c)
RENDER_BLEND(Merge16BitGeneric, 16, c)
#if defined(CAN_COMPILE_SSE2)
RENDER_BLEND_SIMD(8, SSE2)
RENDER_BLEND_SIMD(16, SSE2)
#endif
#if defined(CAN_COMPILE_ARM)
RENDER_BLEND_ARM(8, arm_neon)
RENDER_BLEND_ARM(16, arm_neon)
RENDER_BLEND_ARM(8, armv6)
RENDER_BLEND_ARM(16, armv6)
#endif
#if defined(CAN_COMPILE_SVE)
RENDER_BLEND_ARM(8, arm_sve)
RENDER_BLEND_ARM(16, arm_sve)
#endif
#if defined(CAN_COMPULE_ARM64)
RENDER_BLEND_ARM(8, arm64_neon)
RENDER_BLEND_ARM(16, arm64_neon)
#endif

single_pic_renderer_t BlendRenderer(unsigned pixel_size)
{
#if defined(CAN_COMPILE_SSE2)
    if (vlc_CPU_SSE2())
        return pixel_size & 1 ? RenderBlend8Bit_SSE2 : RenderBlend16Bit_SSE2;
    else
#endif
#if defined(CAN_COMPILE_ARM)
    if (vlc_CPU_ARM_NEON())
        return pixel_size & 1 ? RenderBlend8Bit_arm_neon : RenderBlend16Bit_arm_neon;
    else
    if (vlc_CPU_ARMv6())
        return pixel_size & 1 ? RenderBlend8Bit_armv6 : RenderBlend16Bit_armv6;
    else
#endif
#if defined(CAN_COMPILE_SVE)
    if (vlc_CPU_ARM_SVE())
        return pixel_size & 1 ? RenderBlend8Bit_arm_sve : RenderBlend16Bit_arm_sve;
    else
#endif
#if defined(CAN_COMPULE_ARM64)
    if (vlc_CPU_ARM_NEON())
        return pixel_size & 1 ? RenderBlend8Bit_arm64_neon : RenderBlend16Bit_arm64_neon;
    else
#endif
        return pixel_size & 1 ? RenderBlend8Bit_c : RenderBlend16Bit_c;
}
