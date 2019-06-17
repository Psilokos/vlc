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

#define RENDER_LINEAR(feature, bpc)                                         \
static int RenderLinear##bpc##Bit##feature( filter_t *p_filter,             \
                                            picture_t *p_outpic,            \
                                            picture_t *p_pic,               \
                                            int order, int i_field )        \
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
            Merge##bpc##Bit##feature( p_out, p_in,                          \
                                      p_in + 2 * stride, stride);           \
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

RENDER_LINEAR(Generic, 8)
RENDER_LINEAR(Generic, 16)
#if defined(CAN_COMPILE_SSE2)
RENDER_LINEAR(SSE2, 8)
RENDER_LINEAR(SSE2, 16)
#endif

ordered_renderer_t LinearRenderer(unsigned pixel_size)
{
#if defined(CAN_COMPILE_SSE2)
    if (vlc_CPU_SSE2())
        return pixel_size & 1 ? RenderLinear8BitSSE2 : RenderLinear16BitSSE2;
    else
#endif
        return pixel_size & 1 ? RenderLinear8BitGeneric : RenderLinear16BitGeneric;
}

/*****************************************************************************
 * RenderMean: Half-resolution blender
 *****************************************************************************/

#define RENDER_MEAN(feature, bpc)                                           \
static int RenderMean##bpc##Bit##feature( filter_t *p_filter,               \
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
            Merge##bpc##Bit##feature( p_out, p_in,                          \
                                      p_in + p_pic->p[i_plane].i_pitch,     \
                                      p_pic->p[i_plane].i_pitch );          \
                                                                            \
            p_out += p_outpic->p[i_plane].i_pitch;                          \
            p_in += 2 * p_pic->p[i_plane].i_pitch;                          \
        }                                                                   \
    }                                                                       \
    return VLC_SUCCESS;                                                     \
}

RENDER_MEAN(Generic, 8)
RENDER_MEAN(Generic, 16)
#if defined(CAN_COMPILE_SSE2)
RENDER_MEAN(SSE2, 8)
RENDER_MEAN(SSE2, 16)
#endif

single_pic_renderer_t MeanRenderer(unsigned pixel_size)
{
#if defined(CAN_COMPILE_SSE2)
    if (vlc_CPU_SSE2())
        return pixel_size & 1 ? RenderMean8BitSSE2 : RenderMean16BitSSE2;
    else
#endif
        return pixel_size & 1 ? RenderMean8BitGeneric : RenderMean16BitGeneric;
}

/*****************************************************************************
 * RenderBlend: Full-resolution blender
 *****************************************************************************/
#define RENDER_BLEND(feature, bpc)                                          \
static int RenderBlend##bpc##Bit##feature( filter_t *p_filter,              \
                                           picture_t *p_outpic,             \
                                           picture_t *p_pic )               \
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
            Merge##bpc##Bit##feature( p_out, p_in,                          \
                                      p_in + p_pic->p[i_plane].i_pitch,     \
                                      p_pic->p[i_plane].i_pitch );          \
                                                                            \
            p_out += p_outpic->p[i_plane].i_pitch;                          \
            p_in  += p_pic->p[i_plane].i_pitch;                             \
        }                                                                   \
    }                                                                       \
    return VLC_SUCCESS;                                                     \
}

RENDER_BLEND(Generic, 8)
RENDER_BLEND(Generic, 16)
#if defined(CAN_COMPILE_SSE2)
RENDER_BLEND(SSE2, 8)
RENDER_BLEND(SSE2, 16)
#endif

single_pic_renderer_t BlendRenderer(unsigned pixel_size)
{
#if defined(CAN_COMPILE_SSE2)
    if (vlc_CPU_SSE2())
        return pixel_size & 1 ? RenderBlend8BitSSE2 : RenderBlend16BitSSE2;
    else
#endif
        return pixel_size & 1 ? RenderBlend8BitGeneric : RenderBlend16BitGeneric;
}
