/*****************************************************************************
 * chroma.c: VLC picture to VAAPI surface or vice versa
 *****************************************************************************
 * Copyright (C) 2017 Videolabs
 *
 * Author: Victorien Le Couviour--Tuffet <victorien.lecouviour.tuffet@gmail.com>
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

#include <assert.h>

#include <va/va.h>

#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_plugin.h>

#include "../../video_chroma/copy.h"
#include "vlc_va.h"

/*********************
 * Filter sys struct *
 *********************/

struct  filter_sys_t
{
    VADisplay           dpy;
    picture_pool_t *    dest_pics;
    copy_cache_t        cache;
};

/*************
 * Callbacks *
 *************/

static int              Open(vlc_object_t *obj);
static void             Close(vlc_object_t *obj);

static picture_t *      DownloadSurface(filter_t *filter, picture_t *src);
static picture_t *      UploadSurface(filter_t *filter, picture_t *src);

/*********
 * Tools *
 *********/

static inline void      FillPictureFromVAImage(picture_t *dest,
                                               VAImage *src, uint8_t *src_buf,
                                               copy_cache_t *cache);
static inline void      FillVAImageFromPicture(VAImage *dest_img,
                                               uint8_t *dest_buf,
                                               picture_t *dest_pic,
                                               picture_t *src,
                                               copy_cache_t *cache);

/*********************
 * Module descriptor *
 *********************/

vlc_module_begin()
    set_shortname(N_("VAAPI"))
    set_description(N_("VAAPI surface conversions"))
    set_capability("video converter", 10)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_callbacks(Open, Close)
vlc_module_end()

/********************
 * Common callbacks *
 ********************/

static int Open(vlc_object_t *obj)
{
    filter_t *const     filter = (filter_t *)obj;
    filter_sys_t *      filter_sys;
    bool                is_upload;

    if (filter->fmt_in.video.orientation != filter->fmt_out.video.orientation)
        return VLC_EGENERIC;

    if (filter->fmt_in.video.i_chroma == VLC_CODEC_VAAPI_OPAQUE &&
        (filter->fmt_out.video.i_chroma == VLC_CODEC_I420 ||
         filter->fmt_out.video.i_chroma == VLC_CODEC_I420_10L ||
         filter->fmt_out.video.i_chroma == VLC_CODEC_I420_10B))
    {
        is_upload = false;
        filter->pf_video_filter = DownloadSurface;
    }
    else if ((filter->fmt_in.video.i_chroma == VLC_CODEC_I420 ||
              filter->fmt_in.video.i_chroma == VLC_CODEC_I420_10L ||
              filter->fmt_in.video.i_chroma == VLC_CODEC_I420_10B) &&
             filter->fmt_out.video.i_chroma == VLC_CODEC_VAAPI_OPAQUE)
    {
        is_upload = true;
        filter->pf_video_filter = UploadSurface;
    }
    else
        return VLC_EGENERIC;

    if (!(filter_sys = calloc(1, sizeof(filter_sys_t))))
    {
        msg_Err(obj, "unable to allocate memory");
        return VLC_ENOMEM;
    }

    vlc_va_GetInstance(&filter_sys->dpy);

    if (is_upload)
    {
        filter_sys->dest_pics =
            vlc_va_PoolAlloc(obj, filter_sys->dpy, 3, &filter->fmt_out.video,
                             VA_RT_FORMAT_YUV420);
        if (!filter_sys->dest_pics)
        {
            vlc_va_ReleaseInstance();
            return VLC_EGENERIC;
        }
    }
    else
        filter_sys->dest_pics = NULL;

    if (CopyInitCache(&filter_sys->cache, filter->fmt_in.video.i_width))
    {
        if (is_upload)
            picture_pool_Release(filter_sys->dest_pics);
        vlc_va_ReleaseInstance();
        return VLC_EGENERIC;
    }

    filter->p_sys = filter_sys;

    return VLC_SUCCESS;
}

static void
Close(vlc_object_t *obj)
{
    filter_sys_t *const filter_sys = ((filter_t *)obj)->p_sys;

    vlc_va_ReleaseInstance();
    if (filter_sys->dest_pics)
        picture_pool_Release(filter_sys->dest_pics);
    CopyCleanCache(&filter_sys->cache);

    free(filter_sys);
}

/********************
 * Filter callbacks *
 ********************/

static picture_t *
DownloadSurface(filter_t *filter, picture_t *src_pic)
{
    VADisplay const     va_dpy = filter->p_sys->dpy;
    picture_t *         dest = filter_NewPicture(filter);
    VAImage             src_img;
    void *              src_buf;

    if (!dest)
    {
        msg_Err(filter, "filter_NewPicture failed");
        goto ret;
    }
    picture_CopyProperties(dest, src_pic);

    if (vlc_va_DeriveImage(VLC_OBJECT(filter),
                           va_dpy, src_pic->p_sys->va_surface_id, &src_img)
        || vlc_va_MapBuffer(VLC_OBJECT(filter), va_dpy, src_img.buf, &src_buf))
        goto error;

    FillPictureFromVAImage(dest, &src_img, src_buf, &filter->p_sys->cache);

    if (vlc_va_UnmapBuffer(VLC_OBJECT(filter), va_dpy, src_img.buf)
        || vlc_va_DestroyImage(VLC_OBJECT(filter), va_dpy, src_img.image_id))
        goto error;

ret:
    picture_Release(src_pic);
    return dest;

error:
    picture_Release(dest);
    dest = NULL;
    goto ret;
}

static picture_t *
UploadSurface(filter_t *filter, picture_t *src)
{
    VADisplay const     va_dpy = filter->p_sys->dpy;
    picture_t *         dest_pic = picture_pool_Get(filter->p_sys->dest_pics);
    VAImage             dest_img;
    void *              dest_buf;

    if (!dest_pic)
    {
        msg_Err(filter, "cannot retrieve picture from the dest pics pool");
        goto ret;
    }
    picture_CopyProperties(dest_pic, src);

    if (vlc_va_DeriveImage(VLC_OBJECT(filter),
                           va_dpy, dest_pic->p_sys->va_surface_id, &dest_img)
        || vlc_va_MapBuffer(VLC_OBJECT(filter), va_dpy,
                            dest_img.buf, &dest_buf))
        goto error;

    FillVAImageFromPicture(&dest_img, dest_buf, dest_pic,
                           src, &filter->p_sys->cache);

    if (vlc_va_UnmapBuffer(VLC_OBJECT(filter), va_dpy, dest_img.buf)
        || vlc_va_DestroyImage(VLC_OBJECT(filter), va_dpy, dest_img.image_id))
        goto error;

ret:
    picture_Release(src);
    return dest_pic;

error:
    picture_Release(dest_pic);
    dest_pic = NULL;
    goto ret;
}

/*************************************
 * Tool function for DownloadSurface *
 *************************************/

static inline void
FillPictureFromVAImage(picture_t *dest,
                       VAImage *src_img, uint8_t *src_buf, copy_cache_t *cache)
{
    switch (src_img->format.fourcc)
    {
    case VA_FOURCC_NV12:
    {
        uint8_t *       src_planes[2] = { src_buf + src_img->offsets[0],
                                          src_buf + src_img->offsets[1] };
        size_t          src_pitches[2] = { src_img->pitches[0],
                                           src_img->pitches[1] };

        CopyFromNv12ToI420(dest, src_planes, src_pitches,
                           src_img->height, cache);
        break;
    }
    case VA_FOURCC_P010:
        break;
    default:
        break;
    }
}

/***********************************
 * Tool function for UploadSurface *
 ***********************************/

static inline void
FillVAImageFromPicture(VAImage *dest_img, uint8_t *dest_buf,
                       picture_t *dest_pic, picture_t *src,
                       copy_cache_t *cache)
{
    switch (src->format.i_chroma)
    {
    case VLC_CODEC_I420:
    {
        uint8_t *       src_planes[3] = { src->p[Y_PLANE].p_pixels,
                                          src->p[U_PLANE].p_pixels,
                                          src->p[V_PLANE].p_pixels };
        size_t          src_pitches[3] = { src->p[Y_PLANE].i_pitch,
                                           src->p[U_PLANE].i_pitch,
                                           src->p[V_PLANE].i_pitch };
        void *const     tmp[2] = { dest_pic->p[0].p_pixels,
                                   dest_pic->p[1].p_pixels };

        dest_pic->p[0].p_pixels = dest_buf + dest_img->offsets[0];
        dest_pic->p[1].p_pixels = dest_buf + dest_img->offsets[1];
        dest_pic->p[0].i_pitch = dest_img->pitches[0];
        dest_pic->p[1].i_pitch = dest_img->pitches[1];

        CopyFromI420ToNv12(dest_pic, src_planes, src_pitches,
                           src->format.i_height, cache);

        dest_pic->p[0].p_pixels = tmp[0];
        dest_pic->p[1].p_pixels = tmp[1];

        break;
    }
    case VLC_CODEC_I420_10L || VLC_CODEC_I420_10B:
        break;
    default:
        break;
    }
}
