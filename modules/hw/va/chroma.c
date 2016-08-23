/*****************************************************************************
 * chroma.c: VLC picture to VAAPI surface
 *****************************************************************************
 * Copyright (C) 2016 VLC authors and VideoLAN
 *
 * Authors: Petri Hintukainen <phintuka@gmail.com>
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
# include "config.h"
#endif

#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>

#include <va/va.h>

#include "vlc_va.h"


struct filter_sys_t
{
    VAImageFormat va_image_format;
};

static void Flush(filter_t *filter)
{
    VLC_UNUSED(filter);
}

static picture_t *UploadSurface(filter_t *filter, picture_t *src)
{
    filter_sys_t  *sys = filter->p_sys;
    VAStatus       status;
    picture_t     *dst = NULL;
    picture_sys_t *picsys;

    dst = filter_NewPicture(filter);
    if (dst == NULL) {
        msg_Err(filter, "filter_NewPicture failed\n");
        goto error;
    }

    picsys = dst->p_sys;
    assert(picsys != NULL);
    assert(picsys->va_dpy != NULL);

    status = vlc_va_PutSurface(VLC_OBJECT(filter), picsys->va_dpy, picsys->va_surface_id,
                               &sys->va_image_format, src,
                               filter->fmt_in.video.i_width,
                               filter->fmt_in.video.i_visible_height,
                               filter->fmt_out.video.i_width,
                               filter->fmt_out.video.i_visible_height);
    if (status != VA_STATUS_SUCCESS) {
        goto error;
    }

    picture_CopyProperties(dst, src);
    picture_Release(src);

    return dst;

 error:
    if (dst) {
        picture_Release(dst);
    }
    picture_Release(src);
    return NULL;
}

static void OutputClose(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *)obj;
    filter_sys_t *sys = filter->p_sys;

    free(sys);
}

static int OutputOpen(vlc_object_t *obj)
{
    filter_t      *filter = (filter_t *)obj;
    unsigned int   va_rt_format;
    unsigned int   va_fourcc;
    picture_t     *pic = NULL;
    picture_sys_t *picsys;
    VAStatus       status;

    if (filter->fmt_out.video.i_chroma != VLC_CODEC_VAAPI_OPAQUE) {
        return VLC_EGENERIC;
    }

    if (filter->fmt_in.video.orientation != filter->fmt_out.video.orientation) {
        return VLC_EGENERIC;
    }

    status = vlc_va_VaFourcc(filter->fmt_in.video.i_chroma, &va_fourcc, &va_rt_format);
    if (status != VA_STATUS_SUCCESS) {
        return VLC_EGENERIC;
    }

    msg_Dbg(filter, "fourcc: %4.4s, %dx%d --> %dx%d",
            (const char *)&filter->fmt_in.video.i_chroma,
            filter->fmt_in.video.i_visible_width,
            filter->fmt_in.video.i_visible_height,
            filter->fmt_out.video.i_visible_width,
            filter->fmt_out.video.i_visible_height);

    filter_sys_t *sys = calloc(1, sizeof (*sys));
    if (unlikely(sys == NULL)) {
        return VLC_ENOMEM;
    }
    filter->p_sys = sys;

    /* check output picture */

    pic = filter_NewPicture(filter);
    if (pic == NULL) {
        msg_Err(filter, "filter_NewPicture() failed");
        goto error;
    }
    assert(pic->format.i_chroma == VLC_CODEC_VAAPI_OPAQUE);

    picsys = pic->p_sys;
    assert(picsys != NULL);
    assert(picsys->va_dpy != NULL);

    /* find VAAPI image format */

    status = vlc_va_FindImageFormat(picsys->va_dpy, &sys->va_image_format, va_fourcc, 0);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(filter, "No VAAPI image format for %4.4s", (const char *)&va_fourcc);
        goto error;
    }

    /* test PutImage */

    status = vlc_va_TestPutImage(picsys->va_dpy, &sys->va_image_format,
                                 picsys->va_surface_id, NULL,
                                 filter->fmt_in.video.i_width,
                                 filter->fmt_in.video.i_visible_height);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(filter, "vlc_va_TestPutImage() failed: %d\n", status);
        goto error;
    }

    picture_Release(pic);

    filter->pf_video_filter = UploadSurface;
    filter->pf_flush = Flush;
    return VLC_SUCCESS;

error:
    if (pic) {
        picture_Release(pic);
    }
    OutputClose(obj);
    return VLC_EGENERIC;
}


vlc_module_begin()
    set_shortname(N_("VAAPI"))
    set_description(N_("VAAPI surface conversions"))
    set_capability("video converter", 10)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_callbacks(OutputOpen, OutputClose)
vlc_module_end()
