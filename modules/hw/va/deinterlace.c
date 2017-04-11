/*****************************************************************************
 * deinterlace.c: VAAPI deinterlacing filter
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
#include <stdlib.h>
#include <string.h>
#include <vlc_plugin.h>
#include "va_filter.h"

/*******************
 * Filter callback *
 *******************/

static picture_t *      Deinterlace(filter_t *, picture_t *);

/**********************************
 * VAAPI deinterlacing algorithms *
 **********************************/

typedef struct                  deint_mode
{
    char const *                name;
    VAProcDeinterlacingType     type;
}                               deint_mode_t;

static deint_mode_t const       deint_modes[VAProcDeinterlacingCount - 1] =
{
    { "x",      VAProcDeinterlacingMotionCompensated },
    { "ma",     VAProcDeinterlacingMotionAdaptive },
    { "bob",    VAProcDeinterlacingBob },
    { "weave",  VAProcDeinterlacingWeave }
};

/******************
 * Open functions *
 ******************/

static inline int       Open_SetupFilterParams(filter_t *filter,
                                               filter_params_t *filter_params);

static inline int       Open_GetDeinterlacingMode
(filter_t *filter,
 char const *deint_mode, VAProcDeinterlacingType *p_deint_mode,
 VAProcDeinterlacingType const caps[], unsigned int num_caps);

static inline bool      Open_IsValidType(filter_t *filter,
                                         VAProcDeinterlacingType const caps[],
                                         unsigned int num_caps,
                                         deint_mode_t const *deint_mode,
                                         bool const fallback);

static inline int       Open_InitVAFilterParams
(filter_t *filter, filter_params_t *filter_params, void **p_va_filter_params,
 unsigned int *p_va_filter_param_sz, unsigned int *p_num_va_filter_params);

static inline int       Open_InitHistory
(filter_t *filter, VAProcPipelineCaps const *pipeline_caps);

static inline void      Open_ReleaseSpecificInternalData(filter_t *filter);

/******************
 * Close function *
 ******************/

static inline void      Close_ReleaseSpecificInternalData(filter_t *filter);

/*************************
 * Deinterlace functions *
 *************************/

static inline picture_t *       Deinterlace_UpdateHistory
(filter_sys_t *filter_sys, picture_t *src);

static inline int               Deinterlace_UpdateReferenceFrames
(filter_t *filter);

static inline void              Deinterlace_UpdateSpecificPipelineParams
(filter_t *filter, VAProcPipelineParameterBuffer *pipeline_param);

/****************************
 * Filter parameters struct *
 ****************************/

struct                          filter_params
{
    VAProcDeinterlacingType     deint_mode;
};

/*********************
 * Filter sys struct *
 *********************/

struct  filter_sys_t
{
    FILTER_SYS_COMMON_MEMBERS

    struct
    {
        picture_t * *   pics;
        picture_t * *   p_cur;
        unsigned int    num_pics;
        unsigned int    sz;
    } history;

    struct
    {
        VASurfaceID *   surfaces;
        unsigned int    sz;
    } backward_refs, forward_refs;
};

/********************
 * Module desciptor *
 ********************/

vlc_module_begin()
    set_description(N_("VAAPI deinterlacing filter"))
    set_capability("video filter", 0)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_callbacks(Open, Close)
    add_shortcut ("deinterlace")
vlc_module_end()

/********************
 * Common callbacks *
 ********************/

static int
Open(vlc_object_t *obj)
{
    filter_t *const     filter = (filter_t *)obj;
    filter_params_t     filter_params;

    if (!Open_InitializeFilter(obj, VAProcFilterDeinterlacing, &filter_params,
                               NULL, Open_SetupFilterParams,
                               Open_InitVAFilterParams,
                               NULL, Open_InitHistory))
    {
        filter->fmt_out.video.i_frame_rate *= 2;
        filter->pf_video_filter = Deinterlace;
        return VLC_SUCCESS;
    }
    else
    {
        if (filter->p_sys)
            Open_ReleaseInternalData(obj, Open_ReleaseSpecificInternalData);
        return VLC_EGENERIC;
    }
}

static void
Close(vlc_object_t *obj)
{
    Close_ReleaseInternalData(obj, Close_ReleaseSpecificInternalData);
}

/*******************
 * Filter callback *
 *******************/

static picture_t *
Deinterlace(filter_t *filter, picture_t *src)
{
    filter_sys_t *const filter_sys = filter->p_sys;
    picture_t *         dest;

    src = Deinterlace_UpdateHistory(filter_sys, src);
    if (filter_sys->history.num_pics < filter_sys->history.sz)
        return NULL;

    dest = Filter(filter, src, NULL, Deinterlace_UpdateReferenceFrames,
                  Deinterlace_UpdateSpecificPipelineParams);
    if (dest)
        dest->b_progressive = true;

    return dest;
}

/******************
 * Open functions *
 ******************/

static inline int
Open_SetupFilterParams(filter_t *filter, filter_params_t *filter_params)
{
    VAProcDeinterlacingType     caps[VAProcDeinterlacingCount];
    unsigned int                num_caps = VAProcDeinterlacingCount;
    char *                      deint_mode;
    int                         ret;

    if (Open_QueryVideoProcFilterCaps(filter, VAProcFilterDeinterlacing,
                                      caps, &num_caps))
        return VLC_EGENERIC;

    deint_mode = var_InheritString(filter, "deinterlace-mode");
    ret = Open_GetDeinterlacingMode(filter, deint_mode,
                                    &filter_params->deint_mode,
                                    caps, num_caps);
    free(deint_mode);
    return ret;
}

static inline int
Open_GetDeinterlacingMode(filter_t *filter, char const *deint_mode,
                          VAProcDeinterlacingType *p_deint_mode,
                          VAProcDeinterlacingType const caps[],
                          unsigned int num_caps)
{
    if (deint_mode)
    {
        for (unsigned int i = 0; i < VAProcDeinterlacingCount - 1; ++i)
            if (!strcmp(deint_mode, deint_modes[i].name))
            {
                if (Open_IsValidType(filter, caps, num_caps,
                                     deint_modes + i, false))
                {
                    *p_deint_mode = deint_modes[i].type;
                    msg_Dbg(filter, "using %s deinterlace method",
                            deint_modes[i].name);
                    return VLC_SUCCESS;
                }
                break;
            }
        msg_Err(filter, "%s algorithm not available", deint_mode);
        return VLC_EGENERIC;
    }

    for (unsigned int i = 0; i < VAProcDeinterlacingCount - 1; ++i)
        if (Open_IsValidType(filter, caps, num_caps,
                             deint_modes + i, true))
        {
            *p_deint_mode = deint_modes[i].type;
            msg_Dbg(filter, "using %s deinterlace method",
                    deint_modes[i].name);
            return VLC_SUCCESS;
        }

    /* We shouldn't be able to reach this, 'cause if there is no deinterlacing
       algorithm available, then the driver would have told us before the
       deinterlace filtering is not supported at all. */

    msg_Err(filter, "no algorithm available");
    return VLC_EGENERIC;
}

static inline bool
Open_IsValidType(filter_t *filter,
                 VAProcDeinterlacingType const caps[], unsigned int num_caps,
                 deint_mode_t const *deint_mode, bool const fallback)
{
    for (unsigned int j = 0; j < num_caps; ++j)
        if (caps[j] == deint_mode->type)
        {
            if (fallback)
                msg_Info(filter, "falling back to %s algorithm",
                         deint_mode->name);
            return true;
        }
    return false;
}

static inline int
Open_InitVAFilterParams(filter_t *filter, filter_params_t *filter_params,
                        void **p_va_filter_params,
                        unsigned int *p_va_filter_param_sz,
                        unsigned int *p_num_va_filter_params)
{
    VAProcFilterParameterBufferDeinterlacing const      param =
        {
            .type = VAProcFilterDeinterlacing,
            .algorithm = filter_params->deint_mode
        };

    *p_va_filter_param_sz = sizeof(param);
    *p_num_va_filter_params = 1;

    if (!(*p_va_filter_params = malloc(sizeof(param))))
    {
        msg_Err(filter, "unable to allocate memory");
        return VLC_ENOMEM;
    }

    *(VAProcFilterParameterBufferDeinterlacing *)*p_va_filter_params = param;

    return VLC_SUCCESS;
}

static inline int
Open_InitHistory(filter_t *filter, VAProcPipelineCaps const *pipeline_caps)
{
    filter_sys_t *const filter_sys = filter->p_sys;
    unsigned int const  sz_backward_refs =
        pipeline_caps->num_backward_references;
    unsigned int const  sz_forward_refs =
        pipeline_caps->num_forward_references;
    unsigned int const  history_sz = sz_backward_refs + 1 + sz_forward_refs;

    if (!(filter_sys->history.pics = calloc(history_sz, sizeof(picture_t *))))
        goto no_mem;

    filter_sys->history.p_cur = filter_sys->history.pics + sz_backward_refs;
    filter_sys->history.num_pics = 0;
    filter_sys->history.sz = history_sz;

    if (history_sz - 1 &&
        !(filter_sys->backward_refs.surfaces =
          malloc((history_sz - 1) * sizeof(VASurfaceID))))
        goto no_mem;

    filter_sys->forward_refs.surfaces =
        filter_sys->backward_refs.surfaces + sz_backward_refs;

    filter_sys->backward_refs.sz = sz_backward_refs;
    filter_sys->forward_refs.sz = sz_forward_refs;

    return VLC_SUCCESS;

no_mem:
    msg_Err(filter, "cannot allocate memory");
    return VLC_ENOMEM;
}

static inline void
Open_ReleaseSpecificInternalData(filter_t *filter)
{
    filter_sys_t *const filter_sys = filter->p_sys;

    if (filter_sys->backward_refs.surfaces)
        free(filter_sys->backward_refs.surfaces);
    if (filter_sys->history.pics)
        free(filter_sys->history.pics);
}

/******************
 * Close function *
 ******************/

static inline void
Close_ReleaseSpecificInternalData(filter_t *filter)
{
    filter_sys_t *const filter_sys = filter->p_sys;

    if (filter_sys->backward_refs.surfaces)
        free(filter_sys->backward_refs.surfaces);
    if (filter_sys->history.pics)
    {
        while (filter_sys->history.num_pics)
            picture_Release(filter_sys->history.pics
                            [--filter_sys->history.num_pics]);
        free(filter_sys->history.pics);
    }
}

/*************************
 * Deinterlace functions *
 *************************/

static inline picture_t *
Deinterlace_UpdateHistory(filter_sys_t *filter_sys, picture_t *src)
{
    if (filter_sys->history.num_pics == filter_sys->history.sz)
    {
        picture_Release(*filter_sys->history.pics);
        memmove(filter_sys->history.pics, filter_sys->history.pics + 1,
                --filter_sys->history.num_pics * sizeof(picture_t *));
    }
    filter_sys->history.pics[filter_sys->history.num_pics++] = src;

    return *filter_sys->history.p_cur;
}

static inline int
Deinterlace_UpdateReferenceFrames(filter_t *filter)
{
    filter_sys_t *const filter_sys = filter->p_sys;
    picture_t * *const  pics = filter_sys->history.pics;

    if (filter_sys->backward_refs.sz)
        for (unsigned int i = 0; i < filter_sys->backward_refs.sz; ++i)
            filter_sys->backward_refs.surfaces[i] =
                pics[i]->p_sys->va_surface_id;

    if (filter_sys->forward_refs.sz)
        for (unsigned int i = 0; i < filter_sys->forward_refs.sz; ++i)
        {
            unsigned int const  idx = filter_sys->backward_refs.sz + 1 + i;

            filter_sys->forward_refs.surfaces[i] =
                pics[idx]->p_sys->va_surface_id;
        }

    return VLC_SUCCESS;
}

static inline void
Deinterlace_UpdateSpecificPipelineParams
(filter_t *filter, VAProcPipelineParameterBuffer *pipeline_param)
{
    filter_sys_t *const filter_sys = filter->p_sys;

    pipeline_param->filter_flags =
        (*filter_sys->history.p_cur)->b_top_field_first ?
        0 : VA_DEINTERLACING_BOTTOM_FIELD_FIRST;

    pipeline_param->backward_references = filter_sys->backward_refs.surfaces;
    pipeline_param->forward_references = filter_sys->forward_refs.surfaces;
    pipeline_param->num_backward_references = filter_sys->backward_refs.sz;
    pipeline_param->num_forward_references = filter_sys->forward_refs.sz;
}
