/*****************************************************************************
 * vlc_va.c: VAAPI helper for VLC
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

#include "vlc_va.h"

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include <va/va.h>

#include <vlc_common.h>
#include <vlc_fourcc.h>
#include <vlc_picture_pool.h>
#include <vlc_subpicture.h>

int vlc_va_Initialize(vlc_object_t *o, VADisplay va_dpy)
{
    VAStatus status;
    int major = 0, minor = 0;
    const char *vendor;

    status = vaInitialize(va_dpy, &major, &minor);
    if (status != VA_STATUS_SUCCESS) {
        return status;
    }

    vendor = vaQueryVendorString(va_dpy);

    msg_Info(o, "VA-API: v%d.%d (%s)\n", major, minor, vendor);

    return VA_STATUS_SUCCESS;
}

void vlc_va_Terminate(VADisplay va_dpy)
{
    if (va_dpy) {
        vaTerminate(va_dpy);
    }
}

int vlc_va_SetDisplayAttribute(VADisplay va_dpy, VADisplayAttribType type, int value)
{
    VADisplayAttribute attr;

    attr.type  = type;
    attr.value = value;
    attr.flags = VA_DISPLAY_ATTRIB_SETTABLE;

    return vaSetDisplayAttributes(va_dpy, &attr, 1);
}

/*
 * VAAPI image format
 */

VAImageFormat *vlc_va_GetImageFormats(VADisplay va_dpy, int *num_formats, int spu)
{
    VAImageFormat *formats = NULL;
    VAStatus status;
    int max_formats;

    *num_formats = 0;

    if (spu) {
        max_formats = vaMaxNumSubpictureFormats(va_dpy);
    } else {
        max_formats = vaMaxNumImageFormats(va_dpy);
    }
    if (max_formats < 1) {
        return NULL;
    }

    formats = malloc(max_formats * sizeof(*formats));
    if (!formats) {
        return NULL;
    }

    if (spu) {
        status = vaQuerySubpictureFormats(va_dpy, formats, NULL, (unsigned *)num_formats);
    } else {
        status = vaQueryImageFormats(va_dpy, formats, num_formats);
    }
    if (status != VA_STATUS_SUCCESS || *num_formats < 1) {
        free(formats);
        return NULL;
    }

    return formats;
}

int vlc_va_FindImageFormat(VADisplay va_dpy, VAImageFormat *va_format,
                           unsigned int va_fourcc, int spu)
{
    VAImageFormat *formats;
    int num_formats, i;

    formats = vlc_va_GetImageFormats(va_dpy, &num_formats, spu);

    for (i = 0; i < num_formats; i++) {
        if (formats[i].fourcc == va_fourcc) {
            if (va_format) {
                *va_format = formats[i];
            }
            free(formats);
            return VA_STATUS_SUCCESS;
        }
    }

    free(formats);
    return VA_STATUS_ERROR_UNKNOWN;
}

/*
 * Picture
 */

int vlc_va_TestPutImage(VADisplay va_dpy, VAImageFormat *va_format,
                        VASurfaceID va_surface_id, int *derive,
                        int width, int height)
{
    VAImage image;
    VAStatus status;

    status = vaCreateImage(va_dpy, va_format, width, height, &image);
    if (status != VA_STATUS_SUCCESS) {
        return status;
    }

    status = vaPutImage(va_dpy, va_surface_id, image.image_id,
                        0, 0, width, height,
                        0, 0, width, height);
    vaDestroyImage(va_dpy, image.image_id);
    if (status != VA_STATUS_SUCCESS) {
        return status;
    }

    /* test vaDeriveImage */
    if (derive) {
        *derive = 0;
        status = vaDeriveImage(va_dpy, va_surface_id, &image);
        if (status == VA_STATUS_SUCCESS) {
            if (image.format.fourcc == va_format->fourcc) {
                *derive = 1;
            }
            vaDestroyImage(va_dpy, image.image_id);
        }
    }

    return VA_STATUS_SUCCESS;
}

/*
 *
 */

static void PictureSysDestroyVAAPI(picture_sys_t *sys)
{
    vaDestroySurfaces(sys->va_dpy, &sys->va_surface_id, 1);
    free(sys);
}

static void PictureDestroyVAAPI(picture_t *pic)
{
    PictureSysDestroyVAAPI(pic->p_sys);
    free(pic);
}

static int PictureNew(VADisplay va_dpy,
                      const video_format_t *fmt,
                      picture_t **picp, VASurfaceID id,
                      VASurfaceID *render_targets,
                      int num_render_targets)
{
    picture_sys_t *sys = malloc(sizeof (*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;

    sys->va_dpy        = va_dpy;
    sys->va_surface_id = id;
    sys->va_render_targets = render_targets;
    sys->va_num_render_targets = num_render_targets;

    picture_resource_t res = {
        .p_sys = sys,
        .pf_destroy = PictureDestroyVAAPI,
    };

    picture_t *pic = picture_NewFromResource(fmt, &res);
    if (!pic) {
        PictureSysDestroyVAAPI(sys);
        return VLC_ENOMEM;
    }

    *picp = pic;
    return VLC_SUCCESS;
}

picture_pool_t *vlc_va_PoolAlloc(vlc_object_t *o, VADisplay va_dpy, unsigned requested_count,
                                 const video_format_t *restrict fmt, unsigned int va_rt_format)
{
    picture_t   *pics[requested_count];
    VASurfaceID  va_surface_ids[requested_count];
    VAStatus     status;
    unsigned     count;

    status = vaCreateSurfaces(va_dpy, va_rt_format,
                              fmt->i_visible_width, fmt->i_visible_height,
                              va_surface_ids, requested_count, NULL, 0);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(o, "vaCreateSurfaces(%d) failed: %d\n", va_rt_format, status);
        return NULL;
    }

#if 0
    /* too late to test here ? */
    status = vlc_va_TestPutImage(va_dpy, &va_format,
                                 va_surface_ids[0], NULL,
                                 fmt->i_visible_width, fmt->i_visible_height);
#endif

    for (count = 0; count < requested_count; count++) {
        int err = PictureNew(va_dpy, fmt, pics + count, va_surface_ids[count],
                             va_surface_ids, requested_count);
        if (err != VLC_SUCCESS) {
            break;
        }
    }

    if (count != requested_count) {
        vaDestroySurfaces(va_dpy, &va_surface_ids[count], requested_count - count);
        for (unsigned i = 0; i < count; ++i)
            pics[i]->p_sys->va_num_render_targets = count;
    }

    if (count == 0) {
        return NULL;
    }

    picture_pool_t *pool = picture_pool_New(count, pics);
    if (!pool) {
        while (count > 0) {
            picture_Release(pics[--count]);
        }
    }
    return pool;
}

/*
 *
 */

static int CopyPicture(vlc_object_t *o,
                       VAImage *va_image, uint8_t *base,
                       int dst_x, int dst_y, const picture_t *src)
{
    plane_t dst_planes[3];

    for (int i = 0; i < src->i_planes; i++) {
        dst_planes[i].p_pixels        = base + va_image->offsets[i];
        dst_planes[i].i_pitch         = va_image->pitches[i];
        dst_planes[i].i_visible_pitch = va_image->pitches[i];
        dst_planes[i].i_lines         = src->p[i].i_visible_lines;
        dst_planes[i].i_visible_lines = src->p[i].i_visible_lines;
        dst_planes[i].i_pixel_pitch   = src->p[i].i_pixel_pitch;
    }

    if (src->format.i_chroma == VLC_CODEC_I420 ||
        src->format.i_chroma == VLC_CODEC_I422 ||
        src->format.i_chroma == VLC_CODEC_I444) {

        plane_t tmp = dst_planes[1];
        dst_planes[1] = dst_planes[2];
        dst_planes[2] = tmp;
    }

    switch (va_image->format.fourcc) {
        case VA_FOURCC_ARGB:
        case VA_FOURCC_RGBA:
            dst_planes[0].p_pixels += dst_x * 4 + dst_y * dst_planes[0].i_pitch;
            break;

        case VA_FOURCC_IYUV:
        case VA_FOURCC_YV12:
            dst_planes[0].p_pixels += dst_x     + dst_y     * dst_planes[0].i_pitch;
            dst_planes[1].p_pixels += dst_x / 2 + dst_y / 2 * dst_planes[1].i_pitch;
            dst_planes[2].p_pixels += dst_x / 2 + dst_y / 2 * dst_planes[2].i_pitch;
            break;

        default:
            msg_Err(o, "Unsupported va fourcc (%4.4s)", (const char *)&va_image->format.fourcc);
            return VA_STATUS_ERROR_UNIMPLEMENTED;
    }

    for (int i = 0; i < src->i_planes; i++) {
        plane_CopyPixels(&dst_planes[i], &src->p[i]);
    }

    return VA_STATUS_SUCCESS;
}

int vlc_va_PutSurface(vlc_object_t *o, VADisplay va_dpy,
                      VASurfaceID va_surface_id,
                      VAImageFormat *va_image_format, const picture_t *src,
                      int in_width, int in_height, int out_width, int out_height)
{
    VAImage   surface_image;
    VAStatus  status;
    uint8_t  *base;
    int       derived = 0;

    /* create VAAPI image */

    /* try DeriveImage if no scaling required */
    if (in_width == out_width && in_height == out_height) {

        status = vaDeriveImage(va_dpy, va_surface_id, &surface_image);
        derived = (status == VA_STATUS_SUCCESS);
    }
    if (!derived) {
        /* fall back to PutImage */
        status = vaCreateImage(va_dpy, va_image_format, in_width, in_height, &surface_image);
        if (status != VA_STATUS_SUCCESS) {
            msg_Err(o, "vaCreateImage(0x%x) failed\n", va_image_format->fourcc);
            return status;
        }
    }

    /* copy bits */

    status = vaMapBuffer(va_dpy, surface_image.buf, (void **)&base);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(o, "vaMapBuffer() failed\n");
        goto out;
    }

    status = CopyPicture(o, &surface_image, base, 0, 0, src);
    vaUnmapBuffer(va_dpy, surface_image.buf);
    if (status != VA_STATUS_SUCCESS) {
        goto out;
    }

    if (!derived) {
        status = vaPutImage(va_dpy, va_surface_id, surface_image.image_id,
                            0, 0, in_width, in_height,
                            0, 0, out_width, out_height);
        if (status != VA_STATUS_SUCCESS) {
            msg_Err(o, "vaPutImage(0x%x) failed\n", va_image_format->fourcc);
            //goto out;
        }
    }

 out:
    vaDestroyImage(va_dpy, surface_image.image_id);
    return status;
}

/*
 * Subpictures
 */

  vlc_va_subpicture *vlc_va_SubpictureNew(void)
{
    vlc_va_subpicture *spu = calloc(1, sizeof(*spu));
    if (spu) {
        spu->va_subpicture_id  = VA_INVALID_ID;
        spu->va_image.image_id = VA_INVALID_ID;
    }
    return spu;
}

static void DestroySubpicture(VADisplay va_dpy, vlc_va_subpicture *spu)
{
    if (spu->va_subpicture_id != VA_INVALID_ID) {
        vaDestroySubpicture(va_dpy, spu->va_subpicture_id);
        spu->va_subpicture_id = VA_INVALID_ID;
    }
    if (spu->va_image.image_id != VA_INVALID_ID) {
        vaDestroyImage(va_dpy, spu->va_image.image_id);
        spu->va_image.image_id = VA_INVALID_ID;
    }
}

void vlc_va_SubpictureDestroy(VADisplay va_dpy, vlc_va_subpicture *spu)
{
    DestroySubpicture(va_dpy, spu);
    free(spu);
}

static int CreateSubpicture(vlc_object_t *o, VADisplay va_dpy, vlc_va_subpicture *spu,
                            VAImageFormat *va_format, int width, int height)
{
    VAStatus status;
    void *base = NULL;

    status = vaCreateImage(va_dpy, va_format, width, height, &spu->va_image);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(o, "vaCreateImage(SPU) failed: %d\n", status);
        goto error;
    }

    status = vaCreateSubpicture(va_dpy, spu->va_image.image_id, &spu->va_subpicture_id);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(o, "vaCreateSubpicture() failed: %d\n", status);
        goto error;
    }

    status = vaMapBuffer(va_dpy, spu->va_image.buf, &base);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(o, "vaMapBuffer(SPU) failed: %d\n", status);
        goto error;
    }

    memset(base, 0, spu->va_image.data_size);

    status = vaUnmapBuffer(va_dpy, spu->va_image.buf);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(o, "vaUnmapBuffer(SPU) failed: %d\n", status);
        goto error;
    }

    spu->place.x = 0;
    spu->place.y = 0;
    spu->place.w = width;
    spu->place.h = height;

    return VA_STATUS_SUCCESS;

error:
    DestroySubpicture(va_dpy, spu);
    if (status == VA_STATUS_SUCCESS) {
        status = VA_STATUS_ERROR_UNKNOWN;
    }
    return status;
}

static int SubpictureRect(const subpicture_t *subpic, vlc_va_rect *r)
{
    unsigned int x0 = 0xffff, y0 = 0xffff, x1 = 0, y1 = 0;
    for (subpicture_region_t *r = subpic->p_region; r != NULL; r = r->p_next) {
        x0 = __MIN(x0, (unsigned)r->i_x);
        y0 = __MIN(y0, (unsigned)r->i_y);
        x1 = __MAX(x1, r->i_x + r->fmt.i_visible_width);
        y1 = __MAX(y1, r->i_y + r->fmt.i_visible_height);
    }
    if (x1 < x0 || y1 < y0) {
        return -1;
    }
    r->x = x0;
    r->y = y0;
    r->w = x1 - x0;
    r->h = y1 - y0;
    return 0;
}

int vlc_va_SubpictureUpdate(vlc_object_t *o, VADisplay va_dpy, VAImageFormat *va_format,
                            vlc_va_subpicture *spu, subpicture_t *subpic)
{
    VAStatus status;
    void *base = NULL;
    vlc_va_rect rect;

    /* empty subpicture ? */
    if (!subpic || SubpictureRect(subpic, &rect) < 0) {
        DestroySubpicture(va_dpy, spu);
        return VA_STATUS_ERROR_UNKNOWN;
    }

    /* size changed ? */
    if (rect.w != spu->place.w || rect.h != spu->place.h) {
        DestroySubpicture(va_dpy, spu);
        /* TODO: can update (bind new picture to subpicture) */
    }

    if (spu->va_subpicture_id == VA_INVALID_ID) {
        status = CreateSubpicture(o, va_dpy, spu, va_format, rect.w, rect.h);
        if (status != VA_STATUS_SUCCESS) {
            return status;
        }
    }

    spu->place = rect;

    status = vaMapBuffer(va_dpy, spu->va_image.buf, &base);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(o, "vaMapBuffer(SPU) failed: %d", status);
        return status;
    }

    if (!subpic->p_region || subpic->p_region->p_next) {
        /* no regions or more than one region */
        memset(base, 0, spu->va_image.data_size);
    }

    for (subpicture_region_t *r = subpic->p_region; r != NULL; r = r->p_next) {
        if (r->p_picture) {
            status = CopyPicture(o, &spu->va_image, base,
                                 r->i_x - rect.x, r->i_y - rect.y,
                                 r->p_picture);
            assert(status == VA_STATUS_SUCCESS);
        }
    }

    status = vaUnmapBuffer(va_dpy, spu->va_image.buf);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(o, "vaUmmapBuffer(SPU) failed: %d", status);
        return status;
    }

    return VA_STATUS_SUCCESS;
}
