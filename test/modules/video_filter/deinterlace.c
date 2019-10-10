/*****************************************************************************
 * deinterlace.c : tests for the SIMD optimized deinterlacers
 *****************************************************************************
 * Copyright (C) 2019 VideoLAN
 *
 * Author: Victorien Le Couviour--Tuffet victorien@videolan.org
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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <vlc_common.h>
#include <vlc_cpu.h>
#include <vlc_picture.h>
#include "../../modules/video_filter/deinterlace/deinterlace.h"

static struct cpu_feature
{
    char const *name;
    int flag;
} const cpu_features[] =
{
#if defined(__i386__) || defined(__x86_64__)
    { "SSE2",   VLC_CPU_SSE2 },
    { "SSSE3",  VLC_CPU_SSSE3 },
#endif
#ifdef __x86_64__
    { "AVX2",   VLC_CPU_AVX2 },
#endif
    { 0 }
};

static inline void
setup_iovfmt(video_format_t *ifmt, video_format_t *ofmt,
             bool half_height, int bpc)
{
    vlc_fourcc_t chroma = bpc == 8 ? VLC_CODEC_I420 : VLC_CODEC_I420_10L;
    unsigned w = rand() % (128 - 1 + 1) + 1;
    unsigned h = rand() % (128 - 4 + 1) + 4;
    video_format_Setup(ifmt, chroma, w, h, w, h, w, h);
    *ofmt = *ifmt;
    if (half_height)
    {
        ofmt->i_height /= 2;
        ofmt->i_visible_height /= 2;
        ofmt->i_y_offset /= 2;
        ofmt->i_sar_den /= 2;
    }
}

static inline picture_t *
create_picture(video_format_t *fmt, bool initialize)
{
    picture_t *pic = picture_NewFromFormat(fmt);
    if (!pic)
        return NULL;

    for (int plane = 0; plane < pic->i_planes; ++plane)
        for (int i = 0; i < pic->p[plane].i_lines; ++i)
        {
            ptrdiff_t const stride = pic->p[plane].i_pitch;
            for (int j = 0; j < stride; j += sizeof(int))
            {
                int *ptr = (int *)(pic->p[plane].p_pixels + i * stride + j);
                *ptr = 0xDEADBEEF;
            }
            if (initialize && i < pic->p[plane].i_visible_lines)
                for (int j = 0; j < pic->p[plane].i_visible_pitch; ++j)
                    pic->p[plane].p_pixels[i * stride + j] = rand() % 256;
        }
    return pic;
}

static int
init_deinterlacer(picture_t **srcpic_ptr,
                  picture_t **cdstpic_ptr, picture_t **adstpic_ptr,
                  bool half_height, int bpc)
{
    video_format_t in_fmt, out_fmt;
    setup_iovfmt(&in_fmt, &out_fmt, half_height, bpc);
    *srcpic_ptr = create_picture(&in_fmt, true);
    if (*srcpic_ptr == NULL) goto error;
    *cdstpic_ptr = create_picture(&out_fmt, false);
    if (*cdstpic_ptr == NULL) goto error;
    *adstpic_ptr = create_picture(&out_fmt, false);
    if (*adstpic_ptr == NULL) goto error;
    return VLC_SUCCESS;
error:
    if (*cdstpic_ptr)
        picture_Release(*cdstpic_ptr);
    if (*srcpic_ptr)
        picture_Release(*srcpic_ptr);
    return VLC_EGENERIC;
}

static void
destroy_deinterlacer(picture_t *srcpic, picture_t *cdstpic, picture_t *adstpic)
{
    picture_Release(srcpic);
    picture_Release(cdstpic);
    picture_Release(adstpic);
}

int main(void)
{
    int ret = 0;
    srand(time(NULL));

#define TEST_DEINTERLACER(type, mode, Mode, half_height, bpc, ...)          \
    puts("deinterlace " #mode " " #bpc "-bit:");                            \
    vlc_CPU_mask(~0);                                                       \
    type##_renderer_t render_##mode##bpc##_cref = Mode##Renderer(bpc / 8);  \
    assert(render_##mode##bpc##_cref);                                      \
    for (struct cpu_feature const *feature = cpu_features;                  \
         feature->name; ++feature)                                          \
    {                                                                       \
        vlc_CPU_unmask(feature->flag);                                      \
        type##_renderer_t render_##mode##bpc##_simd = Mode##Renderer(bpc / 8);\
        vlc_CPU_mask(feature->flag);                                        \
        int has_impl = render_##mode##bpc##_simd != Mode##Renderer(bpc / 8);\
        vlc_CPU_unmask(feature->flag);                                      \
        if (!has_impl)                                                      \
            continue;                                                       \
                                                                            \
        printf(" - %-5s : ", feature->name);                                \
        bool fail = false;                                                  \
        for (int i = 0; i < 256; ++i)                                       \
        {                                                                   \
            picture_t *src;                                                 \
            picture_t *cdst;                                                \
            picture_t *adst;                                                \
            if (init_deinterlacer(&src, &cdst, &adst, half_height, bpc))    \
            {                                                               \
                puts("CANNOT EXECUTE");                                     \
                break;                                                      \
            }                                                               \
                                                                            \
            render_##mode##bpc##_cref(NULL, cdst, src, ##__VA_ARGS__);      \
            render_##mode##bpc##_simd(NULL, adst, src, ##__VA_ARGS__);      \
                                                                            \
            for (int plane = 0; !fail && plane < cdst->i_planes; ++plane)   \
            {                                                               \
                size_t plane_size =                                         \
                    cdst->p[plane].i_lines * cdst->p[plane].i_pitch;        \
                if (memcmp(cdst->p[plane].p_pixels,                         \
                           adst->p[plane].p_pixels, plane_size))            \
                    fail = true;                                            \
            }                                                               \
                                                                            \
            destroy_deinterlacer(src, cdst, adst);                          \
            if (fail)                                                       \
            {                                                               \
                ret = 1;                                                    \
                break;                                                      \
            }                                                               \
        }                                                                   \
        printf("%s\n", !fail ? "OK" : "KO");                                \
    }

    TEST_DEINTERLACER(ordered, linear, Linear, false, 8, 0, i & 1)
    TEST_DEINTERLACER(ordered, linear, Linear, false, 16, 0, i & 1)
    TEST_DEINTERLACER(single_pic, mean, Mean, true, 8)
    TEST_DEINTERLACER(single_pic, mean, Mean, true, 16)
    TEST_DEINTERLACER(single_pic, blend, Blend, false, 8)
    TEST_DEINTERLACER(single_pic, blend, Blend, false, 16)

    return ret;
}
