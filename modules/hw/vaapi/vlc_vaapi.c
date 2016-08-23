/*****************************************************************************
 * vlc_vaapi.c: VAAPI helper for VLC
 *****************************************************************************
 * Copyright (C) 2017 VLC authors, VideoLAN and VideoLabs
 *
 * Authors: Thomas Guillem <thomas@gllm.fr>
 *          Petri Hintukainen <phintuka@gmail.com>
 *          Victorien Le Couviour--Tuffet <victorien.lecouviour.tuffet@gmail.com>
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

#include "vlc_vaapi.h"

#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>

#include <va/va.h>

#include <vlc_common.h>
#include <vlc_fourcc.h>
#include <vlc_picture_pool.h>
#include <vlc_subpicture.h>

/*****************
 * VAAPI display *
 *****************/

int vlc_vaapi_Initialize(vlc_object_t *o, VADisplay va_dpy)
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

void vlc_vaapi_Terminate(VADisplay va_dpy)
{
    if (va_dpy) {
        vaTerminate(va_dpy);
    }
}

/**************************
 * VAAPI create & destroy *
 **************************/

int
vlc_vaapi_CreateConfig(vlc_object_t *o, VADisplay dpy, VAProfile profile,
                       VAEntrypoint entrypoint, VAConfigAttrib *attrib_list,
                       int num_attribs, VAConfigID *p_conf)
{
    VA_CALL(o, vaCreateConfig, dpy, profile,
            entrypoint, attrib_list, num_attribs, p_conf);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_CreateContext(vlc_object_t *o, VADisplay dpy, VAConfigID conf,
                        int pic_w, int pic_h, int flag,
                        VASurfaceID *render_targets, int num_render_targets,
                        VAContextID *p_ctx)
{
    VA_CALL(o, vaCreateContext, dpy, conf, pic_w, pic_h, flag,
            render_targets, num_render_targets, p_ctx);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_CreateBuffer(vlc_object_t *o, VADisplay dpy, VAContextID ctx,
                       VABufferType type, unsigned int size,
                       unsigned int num_elements, void *data,
                       VABufferID *buf_id)
{
    VA_CALL(o, vaCreateBuffer, dpy, ctx, type,
            size, num_elements, data, buf_id);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_DeriveImage(vlc_object_t *o,
                      VADisplay va_dpy, VASurfaceID surface, VAImage *image)
{
    VA_CALL(o, vaDeriveImage, va_dpy, surface, image);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_DestroyConfig(vlc_object_t *o, VADisplay dpy, VAConfigID conf)
{
    VA_CALL(o, vaDestroyConfig, dpy, conf);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_DestroyContext(vlc_object_t *o, VADisplay dpy, VAContextID ctx)
{
    VA_CALL(o, vaDestroyContext, dpy, ctx);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_DestroyBuffer(vlc_object_t *o, VADisplay dpy, VABufferID buf)
{
    VA_CALL(o, vaDestroyBuffer, dpy, buf);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_DestroyImage(vlc_object_t *o, VADisplay dpy, VAImageID image)
{
    VA_CALL(o, vaDestroyImage, dpy, image);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

/***********************
 * VAAPI buffer access *
 ***********************/

int
vlc_vaapi_MapBuffer(vlc_object_t *o, VADisplay dpy,
                    VABufferID buf_id, void **p_buf)
{
    VA_CALL(o, vaMapBuffer, dpy, buf_id, p_buf);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_UnmapBuffer(vlc_object_t *o, VADisplay dpy, VABufferID buf)
{
    VA_CALL(o, vaUnmapBuffer, dpy, buf);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

/*****************
 * VAAPI queries *
 *****************/

int
vlc_vaapi_IsEntrypointAvailable(vlc_object_t *o,
                                VADisplay dpy, VAEntrypoint entrypoint)
{
    VAEntrypoint *      entrypoints;
    int                 num_entrypoints;
    int                 ret;

    if (vlc_vaapi_QueryEntrypoints(o, dpy, &entrypoints, &num_entrypoints))
        return VLC_EGENERIC;

    for (int i = 0; i < num_entrypoints; ++i)
        if (entrypoint == entrypoints[i])
        {
            ret = VLC_SUCCESS;
            goto end;
        }

    ret = VLC_EGENERIC;

end:
    free(entrypoints);
    return ret;
}

int
vlc_vaapi_QueryEntrypoints(vlc_object_t *o, VADisplay dpy,
                           VAEntrypoint **p_entrypoints,
                           int *p_num_entrypoints)
{
    int num_entrypoints;

    if ((num_entrypoints = vaMaxNumEntrypoints(dpy)) <= 0)
        return VLC_EGENERIC;
    if (!(*p_entrypoints = malloc(num_entrypoints * sizeof(VAEntrypoint))))
    {
        msg_Err(o, "unable to allocate memory");
        return VLC_ENOMEM;
    }

    VA_CALL(o, vaQueryConfigEntrypoints, dpy,
            VAProfileNone, *p_entrypoints, &num_entrypoints);
    *p_num_entrypoints = num_entrypoints;

    return VLC_SUCCESS;

error:
    free(*p_entrypoints);
    *p_entrypoints = NULL;
    return VLC_EGENERIC;
}

int
vlc_vaapi_IsVideoProcFilterAvailable(vlc_object_t *o, VADisplay dpy,
                                     VAContextID ctx, VAProcFilterType filter)
{
    VAProcFilterType    filters[VAProcFilterCount];
    unsigned int        num_filters = VAProcFilterCount;

    VA_CALL(o, vaQueryVideoProcFilters, dpy, ctx, filters, &num_filters);
    for (unsigned int i = 0; i < num_filters; ++i)
        if (filter == filters[i])
            return VLC_SUCCESS;
    return VLC_EGENERIC;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_QueryVideoProcFilterCaps(vlc_object_t *o, VADisplay dpy,
                                   VAContextID ctx, VAProcFilterType filter,
                                   void *caps, unsigned int *p_num_caps)
{
    VA_CALL(o, vaQueryVideoProcFilterCaps, dpy,
            ctx, filter, caps, p_num_caps);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_QueryVideoProcPipelineCaps(vlc_object_t *o, VADisplay dpy,
                                     VAContextID ctx, VABufferID *filters,
                                     unsigned int num_filters,
                                     VAProcPipelineCaps *pipeline_caps)
{
    VA_CALL(o, vaQueryVideoProcPipelineCaps, dpy,
            ctx, filters, num_filters, pipeline_caps);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

/*******************
 * VAAPI rendering *
 *******************/

int
vlc_vaapi_BeginPicture(vlc_object_t *o, VADisplay dpy,
                       VAContextID ctx, VASurfaceID surface)
{
    VA_CALL(o, vaBeginPicture, dpy, ctx, surface);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_RenderPicture(vlc_object_t *o, VADisplay dpy, VAContextID ctx,
                        VABufferID *buffers, int num_buffers)
{
    VA_CALL(o, vaRenderPicture, dpy, ctx, buffers, num_buffers);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

int
vlc_vaapi_EndPicture(vlc_object_t *o, VADisplay dpy, VAContextID ctx)
{
    VA_CALL(o, vaEndPicture, dpy, ctx);
    return VLC_SUCCESS;
error: return VLC_EGENERIC;
}

/******************
 * VAAPI pictures *
 ******************/

VASurfaceID *vlc_vaapi_CreateSurfaces(vlc_object_t *o, VADisplay va_dpy,
                                      unsigned count,
                                      const video_format_t *restrict fmt,
                                      unsigned va_rt_format)
{
    VASurfaceID         *va_surface_ids = malloc(count * sizeof(VASurfaceID));
    VAStatus            status;

    if (!va_surface_ids)
        return NULL;

    VASurfaceAttrib attribs[1] = {
        {
            .type = VASurfaceAttribPixelFormat,
            .flags = VA_SURFACE_ATTRIB_SETTABLE,
            .value.type    = VAGenericValueTypeInteger,
            .value.value.i = VA_FOURCC_NV12,
        }
    };
    status = vaCreateSurfaces(va_dpy, va_rt_format,
                              fmt->i_visible_width, fmt->i_visible_height,
                              va_surface_ids, count, attribs,
                              sizeof(attribs) / sizeof(attribs[0]));
    if (status != VA_STATUS_SUCCESS)
    {
        msg_Err(o, "vaCreateSurfaces(%d) failed: %d\n", va_rt_format, status);
        free(va_surface_ids);
        return NULL;
    }
    return va_surface_ids;
}

static void pool_pic_destroy_cb(picture_t *pic)
{
    free(pic);
}

picture_pool_t *vlc_vaapi_PoolNew(vlc_object_t *o, VASurfaceID *va_surface_ids,
                                  unsigned count,
                                  const video_format_t *restrict fmt)
{
    (void) o;
    picture_t *pics[count];
    for (unsigned i = 0; i < count; i++)
    {
        picture_resource_t rsc = {
            .p_sys = (void *)(uintptr_t) va_surface_ids[i],
            .pf_destroy = pool_pic_destroy_cb,
        };
        pics[i] = picture_NewFromResource(fmt, &rsc);
        if (pics[i] == NULL)
        {
            count = i;
            goto error;
        }
    }

    picture_pool_t *pool = picture_pool_New(count, pics);
    if (!pool)
        goto error;

    return pool;

error:
    while (count > 0)
        picture_Release(pics[--count]);
    return NULL;
}
