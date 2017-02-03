/*****************************************************************************
 * vlc_va.h: VAAPI helper for VLC
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

#ifndef VLC_VA_H

#include <stdint.h>

#include <va/va.h>

#include <vlc_common.h>
#include <vlc_fourcc.h>
#include <vlc_picture_pool.h>


/*
 * VAAPI display
 */

int vlc_va_Initialize(vlc_object_t *o, VADisplay va_dpy);
void vlc_va_Terminate(VADisplay va_dpy);

int vlc_va_SetDisplayAttribute(VADisplay va_dpy, VADisplayAttribType type, int value);


/*
 * VAAPI image format
 */

VAImageFormat *vlc_va_GetImageFormats(VADisplay va_dpy, int *num_formats, int spu);

int vlc_va_FindImageFormat(VADisplay va_dpy, VAImageFormat *va_format,
                           unsigned int va_fourcc, int spu);

static inline
int vlc_va_OrientationToVaRotation(int orientation, int *va_rotation)
{
    switch (orientation) {
        case ORIENT_TOP_LEFT:    *va_rotation = VA_ROTATION_NONE; break;
        case ORIENT_ROTATED_90:  *va_rotation = VA_ROTATION_90;   break;
        case ORIENT_ROTATED_180: *va_rotation = VA_ROTATION_180;  break;
        case ORIENT_ROTATED_270: *va_rotation = VA_ROTATION_270;  break;
        default:
            *va_rotation = VA_ROTATION_NONE;
            return VA_STATUS_ERROR_UNIMPLEMENTED;
    }
    return VA_STATUS_SUCCESS;
}

static inline
int vlc_va_VaFourcc(vlc_fourcc_t fourcc,
                    unsigned int *va_fourcc,
                    unsigned int *va_rt_format)
{
    switch (fourcc)
    {
        case VLC_CODEC_I420:
        case VLC_CODEC_YV12:
            *va_fourcc    = VA_FOURCC_YV12;
            *va_rt_format = VA_RT_FORMAT_YUV420;
            break;
        case VLC_CODEC_NV12:
            *va_fourcc    = VA_FOURCC_NV12;
            *va_rt_format = VA_RT_FORMAT_YUV420;
            break;
        case VLC_CODEC_I422:
            *va_fourcc    = VA_FOURCC_422H;
            *va_rt_format = VA_RT_FORMAT_YUV422;
            break;
        case VLC_CODEC_UYVY:
            *va_fourcc    = VA_FOURCC_UYVY;
            *va_rt_format = VA_RT_FORMAT_YUV422;
            break;
        case VLC_CODEC_I444:
            *va_fourcc    = VA_FOURCC_444P;
            *va_rt_format = VA_RT_FORMAT_YUV444;
            break;
        default:
            return VA_STATUS_ERROR_UNIMPLEMENTED;
    }
    return VA_STATUS_SUCCESS;
}

/*
 * Picture
 */

struct picture_sys_t {
    VADisplay     va_dpy;
    VASurfaceID   va_surface_id;

    /* The following can be used to create a VAContextID via vaCreateContext */
    VASurfaceID*  va_render_targets;
    int           va_num_render_targets;
};

picture_pool_t *vlc_va_PoolAlloc(vlc_object_t *o, VADisplay va_dpy, unsigned requested_count,
                                 const video_format_t *fmt, unsigned int va_rt_format);

/*
 * VAAPI images
 */

int vlc_va_TestPutImage(VADisplay va_dpy, VAImageFormat *va_format,
                        VASurfaceID va_surface_id, int *derive,
                        int width, int height);

int vlc_va_PutSurface(vlc_object_t *o, VADisplay va_dpy,
                      VASurfaceID va_surface_id,
                      VAImageFormat *va_image_format, const picture_t *src,
                      int in_width, int in_height, int out_width, int out_height);

/*
 * Subpicture
 */

typedef struct {
    unsigned int x;
    unsigned int y;
    unsigned int w;
    unsigned int h;
} vlc_va_rect;

typedef struct {
    vlc_va_rect    place; /* may be different than VAImage dimensions */

    VASubpictureID va_subpicture_id;
    VAImage        va_image;
} vlc_va_subpicture;

vlc_va_subpicture *vlc_va_SubpictureNew(void);

int vlc_va_SubpictureUpdate(vlc_object_t *o, VADisplay va_dpy, VAImageFormat *va_format,
                            vlc_va_subpicture *spu, subpicture_t *subpic);

void vlc_va_SubpictureDestroy(VADisplay va_dpy, vlc_va_subpicture *spu);

#endif /* VLC_VA_H */
