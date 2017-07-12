/*****************************************************************************
 * cvpx_cvpx.c: core video buffer to picture converter
 *****************************************************************************
 * Copyright (C) 2015-2017 VLC authors, VideoLAN and VideoLabs
 *
 * Authors: Victorien Le Couviour--Tuffet <victorien.lecouiour.tuffet@gmail.com>
 *          Thomas Guillem <thomas@gllm.fr>
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

#include <QuartzCore/QuartzCore.h>

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_picture.h>
#include <vlc_modules.h>
#include "../codec/vt_utils.h"

struct filter_sys_t
{
    VTPixelTransferSessionRef vttransfer;
};

static vlc_fourcc_t const supported_chromas[] = { VLC_CODEC_CVPX_BGRA,
                                                  VLC_CODEC_CVPX_I420,
                                                  VLC_CODEC_CVPX_NV12,
                                                  VLC_CODEC_CVPX_UYVY };

static picture_t *
Filter(filter_t *filter, picture_t *src)
{
    CVPixelBufferRef src_cvpx = cvpxpic_get_ref(src);
    assert(src_cvpx);

    picture_t *dst = filter_NewPicture(filter);
    if (!dst)
    {
        picture_Release(src);
        return NULL;
    }

    CVPixelBufferRef dst_cvpx = cvpxpic_get_ref(dst);
    assert(dst_cvpx);

    if (VTPixelTransferSessionTransferImage(filter->p_sys->vttransfer,
                                            src_cvpx, dst_cvpx) != noErr)
    {
        picture_Release(dst);
        picture_Release(src);
        return NULL;
    }

    picture_CopyProperties(dst, src);
    picture_Release(src);
    return dst;
}

static int
Open(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *)obj;

    unsigned int i;
#define CHECK_CHROMA(fourcc) \
    i = 0; \
    while (i < ARRAY_SIZE(supported_chromas) && \
           fourcc != supported_chromas[i]) \
        ++i; \
    if (i == ARRAY_SIZE(supported_chromas)) \
        return VLC_EGENERIC; \

    CHECK_CHROMA(filter->fmt_in.video.i_chroma)
    CHECK_CHROMA(filter->fmt_out.video.i_chroma)

    filter->p_sys = calloc(1, sizeof(filter_sys_t));
    if (!filter->p_sys)
        return VLC_ENOMEM;

    if (VTPixelTransferSessionCreate(NULL, &filter->p_sys->vttransfer)
        != noErr)
    {
        free(filter->p_sys);
        return VLC_EGENERIC;
    }

    filter->pf_video_filter = Filter;

    return VLC_SUCCESS;
}

static void
Close(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *)obj;

    VTPixelTransferSessionInvalidate(filter->p_sys->vttransfer);
    CFRelease(filter->p_sys->vttransfer);
    free(filter->p_sys);
}

vlc_module_begin ()
    set_description("Conversions between CoreVideo buffers")
    set_capability("video converter", 10)
    set_callbacks(Open, Close)
vlc_module_end ()
