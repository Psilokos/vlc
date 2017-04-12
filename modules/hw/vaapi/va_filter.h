/*****************************************************************************
 * va_filter.h: common functions/structures/types/macros for VAAPI filters
 *****************************************************************************
 * Copyright (C) 2017 VLC authors, VideoLAN and VideoLabs
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

#ifndef VA_FILTER_H
# define VA_FILTER_H

# include <assert.h>

# include <va/va.h>

# include <vlc_common.h>
# include <vlc_filter.h>
# include <vlc_picture.h>

# include "vlc_vaapi.h"

/*
** Common VA filter descriptor
*/
struct  va_filter_desc
{
    VADisplay           dpy;
    VAConfigID          conf;
    VAContextID         ctx;
    VABufferID          buf;
    VASurfaceID *       surface_ids;
};

/*
** Filter sys common members struct
*/
struct  filter_sys_common_members
{
    struct va_filter_desc       va;
    picture_pool_t *            dest_pics;
    bool                        b_pipeline_fast;
};

# define FILTER_SYS_COMMON_MEMBERS              \
    struct filter_sys_common_members        b;

# define DEST_PICS_POOL_SZ      3

# define GET_FILTER_SYS_COMMON_MEMBERS(f)                               \
    ((struct filter_sys_common_members *)((filter_t *)f)->p_sys)

# define GET_VA_FILTER_DESC(f)                          \
    ((struct va_filter_desc *)((filter_t *)f)->p_sys)

/*
** Common filter params compound type
*/
typedef struct filter_params    filter_params_t;

/*
** Common callbacks
*/
static int      Open(vlc_object_t *);
static void     Close(vlc_object_t *);

/*****************************************************************************/

/*
** Open FP types definitions
*/

/* A function whose address is stored in a pointer of this type must:
   initialize all the data of the filter_sys
   (except for its address and its base data). */
typedef int     (*pf_init_specific_int_data_t)(filter_t *filter);

/* A function whose address is stored in a pointer of this type must:
   query the driver for the filter capabilities,
   and setup the parameters of the VLC filter (for example modes,
   values, VLC and driver ranges of these values, etc.). */
typedef int     (*pf_setup_filter_params_t)(filter_t *filter,
                                            filter_params_t *filter_params);

/* A function whose address is stored in a pointer of this type must:
   allocate and set the parameters pointer of the VA filter (which will be
   freed automatically),
   set the size of one parameter,
   set the number of parameters,
   and if not reset during the filtering process (using
   pf_upd_va_filter_params_t), set their values, otherwise clear them. */
typedef int     (*pf_init_va_filter_params_t)
(filter_t *filter, filter_params_t *p_filter_params, void **p_va_filter_params,
 unsigned int *p_va_filter_param_sz, unsigned int *p_num_va_filter_params);

/* A function whose address is stored in a pointer of this type must:
   set the I/O color standards arrays and their corresponding 'num_*' fields
   within the VAProcPipelineCaps structure for a query to the driver, query
   that follows the invocation of the function this FP points to. */
typedef int     (*pf_init_pipeline_caps_query_t)
(filter_t *filter, VAProcPipelineCaps *pipeline_caps);

/* A function whose address is stored in a pointer of this type must:
   use the values (retrieved by querying the driver for pipeline capabilities)
   within the VAProcPipelineCaps structure. */
typedef int     (*pf_setup_specific_pipeline_caps_t)
(filter_t *filter, VAProcPipelineCaps const *pipeline_caps);

/*
** Open & Close FP type definition
*/

/* A function whose address is stored in a pointer of this type must:
   release all the data of the filter_sys
   (except for its base data and its address). */
typedef void    (*pf_release_specific_int_data_t)(filter_t *filter);

/*
** Filter FP types definition
*/

/* A function whose address is stored in a pointer of this type must:
   set the values of the parameters of the VA filter. */
typedef int     (*pf_upd_va_filter_params_t)(filter_t *filter,
                                             void *va_filter_params);

/* A function whose address is stored in a pointer of this type must:
   do every needed update on the data of the filter used in a function called
   during the update of the pipeline parameters (through a function pointer of
   type pf_upd_specific_pipeline_params_t). */
typedef int     (*pf_prep_rendsurf_t)(filter_t *filter);

/* A function whose address is stored in a pointer of this type must:
   update some pipeline parameters (except for the 'surface', the 'filters',
   the 'num_filters' fields, as well as the 'VA_PROC_PIPELINE_FAST' value of
   the 'pipeline_flags' field). */
typedef void    (*pf_upd_specific_pipeline_params_t)
(filter_t *filter, VAProcPipelineParameterBuffer *pipeline_params);

/*****************************************************************************/

/*
** Common Open functions decls
*/

/* Initializes all the data of a filter, queries and configure the driver to
   filter pictures. See 'Open FP types definitions' section for a decription
   of the expected behaviour of the provided function pointers.
   'pf_InitSpecificInternalData' can be NULL.
   'pf_SetupFilterParams' can be NULL.
   'pf_InitVAFilterParams' cannot be NULL.
   'pf_InitPipelineCapsQuery' can be NULL.
   'pf_SetupSpecificPipelineCaps' can be NULL.
   NOTE: You don't need to provide the 'filter_sys_sz' argument as it is
   processed through a macro named like this function. Plz, consider this
   argument is not to be provided.
   NOTE: Please, note as well that you can inline all the functions passed to
   this one, as they will only be called at one place each, within fully inline
   code. */
static inline int       Open_InitializeFilter
(vlc_object_t *                         obj,
 size_t                                 filter_sys_sz,
 VAProcFilterType                       filter_type,
 filter_params_t *                      filter_params,
 pf_init_specific_int_data_t            pf_InitSpecificInternalData,
 pf_setup_filter_params_t               pf_SetupFilterParams,
 pf_init_va_filter_params_t             pf_InitVAFilterParams,
 pf_init_pipeline_caps_query_t          pf_InitPipelineCapsQuery,
 pf_setup_specific_pipeline_caps_t      pf_SetupSpecificPipelineCaps);

/* Queries the driver for the supported capabilities of a specific filter. */
static inline int       Open_QueryVideoProcFilterCaps
(filter_t *filter, VAProcFilterType filter_type,
 void *caps, unsigned int *p_num_caps);

/* Calls 'pf_ReleaseSpecificInternalData', if provided, then releases the base
   data and frees the filter internal pointer.
   NOTE: Please, note that you can inline the function passed as 2nd parameter,
   as it will only be called at one place, within fully inline code. */
static inline void      Open_ReleaseInternalData
(vlc_object_t *obj,
 pf_release_specific_int_data_t pf_ReleaseSpecificInternalData);

/*
** Commmon Close function decl
*/

/* Calls 'pf_ReleaseSpecificInternalData', if provided, then releases the base
   data and frees the filter internal pointer.
   NOTE: Please, note that you can inline the function passed as 2nd parameter,
   as it will only be called at one place, within fully inline code. */
static inline void      Close_ReleaseInternalData
(vlc_object_t *obj,
 pf_release_specific_int_data_t pf_ReleaseSpecificInternalData);

/*
** Common Filter function decl
*/

/* Filters a picture. See 'Filter FP types definitions' section for a
   decription of the expected behaviour of the provided function pointers.
   'pf_UpdateVAFilterParams' can be NULL.
   'pf_PrepareRenderSurface' can be NULL.
   'pf_UpdateSpecificPipelineParams' can be NULL.
   NOTE: Please, note that you can inline all the functions passed to this one,
   as they will only be called at one place each, within fully inline code. */
static inline picture_t *       Filter
(filter_t *                             filter,
 picture_t *                            src,
 pf_upd_va_filter_params_t              pf_UpdateVAFilterParams,
 pf_prep_rendsurf_t                     pf_PrepareRenderSurface,
 pf_upd_specific_pipeline_params_t      pf_UpdateSpecificPipelineParams);

/*****************************************************************************/

/*
** Common internal Open functions decls
*/

/* Checks whether the output format is the expected one or not and that the
   input and output formats are similar. */
static inline int       Open_CheckImgFmt(filter_t *filter);

/* Allocates the internal pointer of the filter, set its memory area to zero
   and initializes the base data. It finally calls the provided function if
   any. */
static inline int       Open_InitInternalData
(filter_t *filter, size_t filter_sys_sz,
 pf_init_specific_int_data_t pf_InitSpecificInternalData);

/* Creates the VA context and config, stored in the base data. */
static inline int       Open_SetupVADescriptor(vlc_object_t *obj);

/* Queries the driver for filter support. */
static inline int       Open_QueryVideoProcFilter
(vlc_object_t *obj, VAProcFilterType filter_type);

/* Calls the provided function, then creates the VA filter parameters buffer
   providing the one whose address is passed to 'pf_InitVAFilterParams' (which
   is supposed to allocate it), and finally frees this buffer. */
static inline int       Open_SetupVAFilterParams
(filter_t *filter, filter_params_t *filter_params,
 pf_init_va_filter_params_t pf_InitVAFilterParams);

/* If not NULL, calls 'pf_InitPipelineCapsQuery', then queries the driver for
   its pipeline capabilities, and finally calls 'pf_SetupSpecificPipelineCaps',
   if provided. */
static inline int       Open_SetupPipelineCaps
(filter_t *filter, pf_init_pipeline_caps_query_t pf_InitPipelineCapsQuery,
 pf_setup_specific_pipeline_caps_t pf_SetupSpecificPipelineCaps);

/*
** Common internal Filter functions decls
*/

/* Retrieves a picture from the destination picture pool. */
static inline int       Filter_GetDestPic(filter_t *filter,
                                          picture_t *src, picture_t **p_dest);

/* Maps the VA filter parameters buffer, then calls the provided function, and
   finally unmaps the buffer. */
static inline int       Filter_UpdateVAFilterParams
(filter_t *filter, pf_upd_va_filter_params_t pf_UpdateVAFilterParams);

/* Updates the VA pipeline to render the current picture. Calls
   'pf_PrepareRenderSurface' before starting the surface rendering.
   Calls 'pf_UpdateSpecificPipelineParams' during the surface rendering. */
static inline int       Filter_RenderPicture
(filter_t *filter, VASurfaceID dest_surface, VASurfaceID src_surface,
 pf_prep_rendsurf_t pf_PrepareRenderSurface,
 pf_upd_specific_pipeline_params_t pf_UpdateSpecificPipelineParams);

/* Creates, maps fills and unmaps the pipeline parameters buffer, finally tells
   the driver we are ready rendering for the rendering process to begin. */
static inline int       Filter_RenderSurface
(filter_t *filter, VASurfaceID src_surface,
 pf_upd_specific_pipeline_params_t pf_UpdateSpecificPipelineParams);

/* Sets the pipeline parameters, then calls 'pf_UpdateSpecificPipelineParams',
   if provided. */
static inline void      Filter_UpdatePipelineParams
(filter_t *filter,
 VAProcPipelineParameterBuffer *pipeline_params,
 VASurfaceID src_surface,
 pf_upd_specific_pipeline_params_t pf_UpdateSpecificPipelineParams);

/*****************************************************************************/

/*
** Common Open functions definitions
*/
static inline int
Open_InitializeFilter
(vlc_object_t *                         obj,
 size_t                                 filter_sys_sz,
 VAProcFilterType                       filter_type,
 filter_params_t *                      filter_params,
 pf_init_specific_int_data_t            pf_InitSpecificInternalData,
 pf_setup_filter_params_t               pf_SetupFilterParams,
 pf_init_va_filter_params_t             pf_InitVAFilterParams,
 pf_init_pipeline_caps_query_t          pf_InitPipelineCapsQuery,
 pf_setup_specific_pipeline_caps_t      pf_SetupSpecificPipelineCaps)
{
    filter_t *const     filter = (filter_t *)obj;

    return !Open_CheckImgFmt(filter)
        && !Open_InitInternalData(filter, filter_sys_sz,
                                  pf_InitSpecificInternalData)
        && !Open_SetupVADescriptor(obj)
        && !Open_QueryVideoProcFilter(obj, filter_type)
        && (!pf_SetupFilterParams ||
            !pf_SetupFilterParams(filter, filter_params))
        && !Open_SetupVAFilterParams(filter, filter_params,
                                     pf_InitVAFilterParams)
        && !Open_SetupPipelineCaps(filter, pf_InitPipelineCapsQuery,
                                   pf_SetupSpecificPipelineCaps)
        ? VLC_SUCCESS : VLC_EGENERIC;
}

# define Open_InitializeFilter(o, t, p, pf_0, pf_1, pf_2, pf_3, pf_4)   \
    Open_InitializeFilter(o, sizeof(filter_sys_t), t, p,                \
                          pf_0, pf_1, pf_2, pf_3, pf_4)

static inline int
Open_CheckImgFmt(filter_t *filter)
{
    return filter->fmt_out.video.i_chroma == VLC_CODEC_VAAPI_420 &&
        video_format_IsSimilar(&filter->fmt_out.video, &filter->fmt_in.video)
        ? VLC_SUCCESS : VLC_EGENERIC;
}

static inline int
Open_InitInternalData(filter_t *filter, size_t filter_sys_sz,
                      pf_init_specific_int_data_t pf_InitSpecificInternalData)
{
    filter_sys_t *                      filter_sys;
    struct filter_sys_common_members *  base;

    if (!(filter_sys = calloc(1, filter_sys_sz)))
    {
        msg_Err(filter, "unable to allocate memory");
        return VLC_ENOMEM;
    }
    filter->p_sys = filter_sys;

    base = GET_FILTER_SYS_COMMON_MEMBERS(filter);
    if (!(base->va.dpy = vlc_vaapi_GetInstance()))
        return VLC_EGENERIC;

    base->va.conf = VA_INVALID_ID;
    base->va.ctx = VA_INVALID_ID;
    base->va.buf = VA_INVALID_ID;

    base->va.surface_ids =
        vlc_vaapi_CreateSurfaces(VLC_OBJECT(filter), base->va.dpy,
                                 DEST_PICS_POOL_SZ, &filter->fmt_out.video,
                                 VA_RT_FORMAT_YUV420);
    if (!base->va.surface_ids)
        return VLC_EGENERIC;
    base->dest_pics =
        vlc_vaapi_PoolNew(VLC_OBJECT(filter), base->va.surface_ids,
                          DEST_PICS_POOL_SZ, &filter->fmt_out.video);
    if (!base->dest_pics)
    {
        vaDestroySurfaces(base->va.dpy,
                          base->va.surface_ids, DEST_PICS_POOL_SZ);
        return VLC_EGENERIC;
    }

    return pf_InitSpecificInternalData
        ? pf_InitSpecificInternalData(filter)
        : VLC_SUCCESS;
}

static inline int
Open_SetupVADescriptor(vlc_object_t *obj)
{
    filter_t *const                     filter = (filter_t *)obj;
    struct va_filter_desc *const        va_filter_desc =
        GET_VA_FILTER_DESC(filter);

    if (vlc_vaapi_IsEntrypointAvailable(obj, va_filter_desc->dpy,
                                        VAEntrypointVideoProc)
        || vlc_vaapi_CreateConfig(obj, va_filter_desc->dpy, VAProfileNone,
                                  VAEntrypointVideoProc, NULL, 0,
                                  &va_filter_desc->conf)
        || vlc_vaapi_CreateContext(obj, va_filter_desc->dpy,
                                   va_filter_desc->conf,
                                   filter->fmt_out.video.i_width,
                                   filter->fmt_out.video.i_height,
                                   0, va_filter_desc->surface_ids,
                                   DEST_PICS_POOL_SZ, &va_filter_desc->ctx))
        return VLC_EGENERIC;

    return VLC_SUCCESS;
}

static inline int
Open_QueryVideoProcFilter(vlc_object_t *obj, VAProcFilterType filter_type)
{
    struct va_filter_desc *const        va_filter_desc =
        GET_VA_FILTER_DESC(obj);

    return vlc_vaapi_IsVideoProcFilterAvailable(obj,
                                                va_filter_desc->dpy,
                                                va_filter_desc->ctx,
                                                filter_type);
}

static inline int
Open_QueryVideoProcFilterCaps(filter_t *filter, VAProcFilterType filter_type,
                              void *caps, unsigned int *p_num_caps)
{
    struct va_filter_desc *const        va_filter_desc =
        GET_VA_FILTER_DESC(filter);

    return vlc_vaapi_QueryVideoProcFilterCaps(VLC_OBJECT(filter),
                                              va_filter_desc->dpy,
                                              va_filter_desc->ctx,
                                              filter_type, caps, p_num_caps);
}

static int
Open_SetupVAFilterParams(filter_t *filter, filter_params_t *filter_params,
                         pf_init_va_filter_params_t pf_InitVAFilterParams)
{
    struct va_filter_desc *const        va_filter_desc =
        GET_VA_FILTER_DESC(filter);
    void *                              va_params;
    unsigned int                        sz_param;
    unsigned int                        num_params;
    int                                 ret_status;

    ret_status = pf_InitVAFilterParams(filter, filter_params, &va_params,
                                       &sz_param, &num_params)
        || vlc_vaapi_CreateBuffer(VLC_OBJECT(filter),
                                  va_filter_desc->dpy, va_filter_desc->ctx,
                                  VAProcFilterParameterBufferType,
                                  sz_param, num_params, va_params,
                                  &va_filter_desc->buf);
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    free(va_params);
    return !ret_status ? VLC_SUCCESS : VLC_EGENERIC;
}

static inline int
Open_SetupPipelineCaps
(filter_t *filter,
 pf_init_pipeline_caps_query_t pf_InitPipelineCapsQuery,
 pf_setup_specific_pipeline_caps_t pf_SetupSpecificPipelineCaps)
{
    struct filter_sys_common_members *const     base =
        GET_FILTER_SYS_COMMON_MEMBERS(filter);
    int                                         ret;
    VAProcPipelineCaps                          pipeline_caps;

    if (pf_InitPipelineCapsQuery
        && (ret = pf_InitPipelineCapsQuery(filter, &pipeline_caps)))
        return ret;

    if (vlc_vaapi_QueryVideoProcPipelineCaps(VLC_OBJECT(filter), base->va.dpy,
                                             base->va.ctx, &base->va.buf, 1,
                                             &pipeline_caps))
        return VLC_EGENERIC;

    base->b_pipeline_fast =
        pipeline_caps.pipeline_flags & VA_PROC_PIPELINE_FAST;

    return pf_SetupSpecificPipelineCaps
        ? pf_SetupSpecificPipelineCaps(filter, &pipeline_caps)
        : VLC_SUCCESS;
}

static inline void
Open_ReleaseInternalData
(vlc_object_t *obj,
 pf_release_specific_int_data_t pf_ReleaseSpecificInternalData)
{
    filter_t *const                             filter = (filter_t *)obj;
    struct filter_sys_common_members *const     base =
        GET_FILTER_SYS_COMMON_MEMBERS(filter);

    if (pf_ReleaseSpecificInternalData)
        pf_ReleaseSpecificInternalData(filter);
    if (base->dest_pics)
        picture_pool_Release(base->dest_pics);
    if (base->va.buf != VA_INVALID_ID)
        vlc_vaapi_DestroyBuffer(obj, base->va.dpy, base->va.buf);
    if (base->va.ctx != VA_INVALID_ID)
        vlc_vaapi_DestroyContext(obj, base->va.dpy, base->va.ctx);
    if (base->va.conf != VA_INVALID_ID)
        vlc_vaapi_DestroyConfig(obj, base->va.dpy, base->va.conf);
    if (base->va.dpy)
        vlc_vaapi_ReleaseInstance(base->va.dpy);
    free(filter->p_sys);
}

/*
** Common Close function
*/

static inline void
Close_ReleaseInternalData
(vlc_object_t *obj,
 pf_release_specific_int_data_t pf_ReleaseSpecificInternalData)
{
    filter_t *const                             filter = (filter_t *)obj;
    struct filter_sys_common_members *const     base =
        GET_FILTER_SYS_COMMON_MEMBERS(filter);

    if (pf_ReleaseSpecificInternalData)
        pf_ReleaseSpecificInternalData(filter);

    picture_pool_Release(base->dest_pics);

    vlc_vaapi_DestroyBuffer(obj, base->va.dpy, base->va.buf);
    vlc_vaapi_DestroyContext(obj, base->va.dpy, base->va.ctx);
    vlc_vaapi_DestroyConfig(obj, base->va.dpy, base->va.conf);
    vaDestroySurfaces(base->va.dpy, base->va.surface_ids, DEST_PICS_POOL_SZ);

    vlc_vaapi_ReleaseInstance(base->va.dpy);

    free(filter->p_sys);
}

/*
** Common Filter functions
*/

static inline picture_t *
Filter(filter_t *filter, picture_t *src,
       pf_upd_va_filter_params_t pf_UpdateVAFilterParams,
       pf_prep_rendsurf_t pf_PrepareRenderSurface,
       pf_upd_specific_pipeline_params_t pf_UpdateSpecificPipelineParams)
{
    picture_t * dest;

    if (!Filter_GetDestPic(filter, src, &dest))
    {
        if (!pf_UpdateVAFilterParams
            || !Filter_UpdateVAFilterParams(filter, pf_UpdateVAFilterParams))
            if (!Filter_RenderPicture(filter, vlc_vaapi_PicGetSurface(dest),
                                      vlc_vaapi_PicGetSurface(src),
                                      pf_PrepareRenderSurface,
                                      pf_UpdateSpecificPipelineParams))
                goto ret;
        picture_Release(dest);
        dest = NULL;
    }

ret:
    return dest;
}

static inline int
Filter_GetDestPic(filter_t *filter, picture_t *src, picture_t **p_dest)
{
    struct filter_sys_common_members *const     base =
        GET_FILTER_SYS_COMMON_MEMBERS(filter);

    if (!(*p_dest = picture_pool_Get(base->dest_pics)))
    {
        msg_Err(filter, "cannot retrieve picture from the dest pics pool");
        return VLC_EGENERIC;
    }
    picture_CopyProperties(*p_dest, src);

    return VLC_SUCCESS;
}

static inline int
Filter_UpdateVAFilterParams(filter_t *filter,
                            pf_upd_va_filter_params_t pf_UpdateVAFilterParams)
{
    struct va_filter_desc *const        va_filter_desc =
        GET_VA_FILTER_DESC(filter);
    void *                              va_params;

    return !vlc_vaapi_MapBuffer(VLC_OBJECT(filter), va_filter_desc->dpy,
                                va_filter_desc->buf, &va_params)
        && !pf_UpdateVAFilterParams(filter, va_params)
        && !vlc_vaapi_UnmapBuffer(VLC_OBJECT(filter),
                                  va_filter_desc->dpy, va_filter_desc->buf)
        ? VLC_SUCCESS : VLC_EGENERIC;
}

static inline int
Filter_RenderPicture
(filter_t *filter, VASurfaceID dest_surface, VASurfaceID src_surface,
 pf_prep_rendsurf_t pf_PrepareRenderSurface,
 pf_upd_specific_pipeline_params_t pf_UpdateSpecificPipelineParams)
{
    vlc_object_t *const                         obj = VLC_OBJECT(filter);
    struct filter_sys_common_members *const     base =
        GET_FILTER_SYS_COMMON_MEMBERS(filter);

    return !vlc_vaapi_BeginPicture(obj, base->va.dpy,
                                   base->va.ctx, dest_surface)
        && (!pf_PrepareRenderSurface || !pf_PrepareRenderSurface(filter))
        && !Filter_RenderSurface(filter, src_surface,
                                 pf_UpdateSpecificPipelineParams)
        && !vlc_vaapi_EndPicture(obj, base->va.dpy, base->va.ctx)
        ? VLC_SUCCESS : VLC_EGENERIC;
}

static inline int
Filter_RenderSurface
(filter_t *filter, VASurfaceID src_surface,
 pf_upd_specific_pipeline_params_t pf_UpdateSpecificPipelineParams)
{
    vlc_object_t *const                         obj = VLC_OBJECT(filter);
    struct filter_sys_common_members *const     base =
        GET_FILTER_SYS_COMMON_MEMBERS(filter);
    VABufferID                                  pipeline_buf;
    VAProcPipelineParameterBuffer *             pipeline_params;

    if (vlc_vaapi_CreateBuffer(obj, base->va.dpy, base->va.ctx,
                               VAProcPipelineParameterBufferType,
                               sizeof(*pipeline_params), 1, NULL,
                               &pipeline_buf)
        || vlc_vaapi_MapBuffer(obj, base->va.dpy, pipeline_buf,
                            (void **)&pipeline_params))
        return VLC_EGENERIC;

    Filter_UpdatePipelineParams(filter, pipeline_params, src_surface,
                                pf_UpdateSpecificPipelineParams);

    return !vlc_vaapi_UnmapBuffer(obj, base->va.dpy, pipeline_buf)
        && !vlc_vaapi_RenderPicture(obj, base->va.dpy, base->va.ctx,
                                    &pipeline_buf, 1)
        ? VLC_SUCCESS : VLC_EGENERIC;
}

static inline void
Filter_UpdatePipelineParams
(filter_t *filter,
 VAProcPipelineParameterBuffer *pipeline_params,
 VASurfaceID src_surface,
 pf_upd_specific_pipeline_params_t pf_UpdateSpecificPipelineParams)
{
    struct filter_sys_common_members *const     base =
        GET_FILTER_SYS_COMMON_MEMBERS(filter);

    *pipeline_params = (VAProcPipelineParameterBuffer){0};
    pipeline_params->surface = src_surface;
    if (base->b_pipeline_fast)
        pipeline_params->pipeline_flags = VA_PROC_PIPELINE_FAST;
    pipeline_params->filters = &base->va.buf;
    pipeline_params->num_filters = 1;

    if (pf_UpdateSpecificPipelineParams)
        pf_UpdateSpecificPipelineParams(filter, pipeline_params);
}

#endif /* VA_FILTER_H */
