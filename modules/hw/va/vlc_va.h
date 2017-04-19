/*****************************************************************************
 * vlc_va.h: VAAPI helper for VLC
 *****************************************************************************
 * Copyright (C) 2016 VLC authors and VideoLAN
 *
 * Authors: Petri Hintukainen <phintuka@gmail.com>
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

#ifndef VLC_VA_H
# define VLC_VA_H

#include <stdint.h>

#include <va/va.h>

#include <vlc_common.h>
#include <vlc_fourcc.h>
#include <vlc_picture_pool.h>

/* This macro is designed to wrap any VA call, and in case of failure,
   display the VA error string and return VLC_EGENERIC */
#define VA_CALL(o, fail_actions, f, args...)            \
    do                                                  \
    {                                                   \
        VAStatus    s;                                  \
                                                        \
        if ((s = f(args)) != VA_STATUS_SUCCESS)         \
        {                                               \
            msg_Err(o, "%s: %s", #f, vaErrorStr(s));    \
            fail_actions                                \
            return VLC_EGENERIC;                        \
        }                                               \
    } while (0)

/**************************
 * VA instance management *
 **************************/

/* Allocates the VA instance and sets the reference counter to 1 */
int     vlc_va_CreateInstance(VADisplay dpy);

/* Retrieve the VA instance and increases the reference counter by 1 */
void    vlc_va_GetInstance(VADisplay *dpy);

/* Decreases the reference counter by 1 and frees the instance if that counter
   reaches 0. */
int     vlc_va_ReleaseInstance(void);

/*****************
 * VAAPI display *
 *****************/

int vlc_va_Initialize(vlc_object_t *o, VADisplay va_dpy);
void vlc_va_Terminate(VADisplay va_dpy);

int vlc_va_SetDisplayAttribute(VADisplay va_dpy, VADisplayAttribType type, int value);

/**************************
 * VAAPI create & destroy *
 **************************/

/* Creates a VA configuration, definining the entrypoint and the video profile
   (VAProfileNone for the post-processing entrypoint) */
int     vlc_va_CreateConfig(vlc_object_t *o, VADisplay dpy, VAProfile profile,
                            VAEntrypoint entrypoint,
                            VAConfigAttrib *attrib_list, int num_attribs,
                            VAConfigID *p_conf);

/* Creates a VA context from the VA configuration and the width / height of the
   pictures to process */
int     vlc_va_CreateContext(vlc_object_t *o, VADisplay dpy, VAConfigID conf,
                             int pic_w, int pic_h, int flag,
                             VASurfaceID *render_targets,
                             int num_render_targets,
                             VAContextID *p_ctx);

/* Creates a VA buffer for 'num_elements' elements of 'size' bytes and
   initalized with 'data'. If 'data' is NULL, then the content of the buffer is
   undefined. */
int     vlc_va_CreateBuffer(vlc_object_t *o, VADisplay dpy, VAContextID ctx,
                            VABufferType type, unsigned int size,
                            unsigned int num_elements, void *data,
                            VABufferID *buf_id);

/* Creates a VA image from a VA surface */
int     vlc_va_DeriveImage(vlc_object_t *o, VADisplay va_dpy,
                           VASurfaceID surface, VAImage *image);

/* Destroys a VA configuration */
int     vlc_va_DestroyConfig(vlc_object_t *o, VADisplay dpy, VAConfigID conf);

/* Destroys a VA context */
int     vlc_va_DestroyContext(vlc_object_t *o, VADisplay dpy, VAContextID ctx);

/* Destroys a VA buffer */
int     vlc_va_DestroyBuffer(vlc_object_t *o, VADisplay dpy, VABufferID buf);

/* Destroys a VA image */
int     vlc_va_DestroyImage(vlc_object_t *o, VADisplay dpy, VAImageID image);

/***********************
 * VAAPI buffer access *
 ***********************/

/* Maps the specified buffer to '*p_buf' */
int     vlc_va_MapBuffer(vlc_object_t *o, VADisplay dpy,
                         VABufferID buf_id, void **p_buf);

/* Unmaps the specified buffer so that the driver can read from it */
int     vlc_va_UnmapBuffer(vlc_object_t *o, VADisplay dpy, VABufferID buf);

/*****************
 * VAAPI queries *
 *****************/

/* Retrieves the list of available VA entrypoints from the driver */
int     vlc_va_QueryEntrypoints(vlc_object_t *o, VADisplay dpy,
                                VAEntrypoint **p_entrypoints,
                                int *num_entrypoints);

/* Checks if the specified VA entrypoint is available */
int     vlc_va_IsEntrypointAvailable(vlc_object_t *o, VADisplay dpy,
                                     VAEntrypoint entrypoint);

/* Checks if the specified filter is available */
int     vlc_va_IsVideoProcFilterAvailable(vlc_object_t *o,
                                          VADisplay dpy, VAContextID ctx,
                                          VAProcFilterType filter);

/* Retrieves the list of available capabilities of a filter */
int     vlc_va_QueryVideoProcFilterCaps(vlc_object_t *o, VADisplay dpy,
                                        VAContextID ctx,
                                        VAProcFilterType filter, void *caps,
                                        unsigned int *p_num_caps);

/* Retrieves the available capabilities of the pipeline */
int     vlc_va_QueryVideoProcPipelineCaps(vlc_object_t *o, VADisplay dpy,
                                          VAContextID ctx, VABufferID *filters,
                                          unsigned int num_filters,
                                          VAProcPipelineCaps *pipeline_caps);

/*******************
 * VAAPI rendering *
 *******************/

/* Tells the driver the specified surface is the next surface to render. */
int     vlc_va_BeginPicture(vlc_object_t *o, VADisplay dpy,
                            VAContextID ctx, VASurfaceID surface);

/* Send buffers (describing rendering operations to perform on the current
   surface) to the driver, which are automatically destroyed afterwards. */
int     vlc_va_RenderPicture(vlc_object_t *o, VADisplay dpy, VAContextID ctx,
                             VABufferID *buffers, int num_buffers);

/* Tells the driver it can begins to process all the pending operations
   (specified with vlc_va_RenderPicture) on the current surface */
int     vlc_va_EndPicture(vlc_object_t *o, VADisplay dpy, VAContextID ctx);

/**********************
 * VAAPI image format *
 **********************/

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

/***********
 * Picture *
 ***********/

struct picture_sys_t {
    VADisplay     va_dpy;
    VASurfaceID   va_surface_id;

    /* The following can be used to create a VAContextID via vaCreateContext */
    VASurfaceID*  va_render_targets;
    int           va_num_render_targets;
    unsigned int* p_va_render_targets_ref_cnt;
};

picture_pool_t *vlc_va_PoolAlloc(vlc_object_t *o, VADisplay va_dpy, unsigned requested_count,
                                 const video_format_t *fmt, unsigned int va_rt_format);

/****************
 * VAAPI images *
 ****************/

int vlc_va_TestPutImage(VADisplay va_dpy, VAImageFormat *va_format,
                        VASurfaceID va_surface_id, int *derive,
                        int width, int height);

int vlc_va_PutSurface(vlc_object_t *o, VADisplay va_dpy,
                      VASurfaceID va_surface_id,
                      VAImageFormat *va_image_format, const picture_t *src,
                      int in_width, int in_height, int out_width, int out_height);

/**************
 * Subpicture *
 **************/

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
