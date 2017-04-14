/*****************************************************************************
 * denoise_sharpen.c: VAAPI noise reduction & sharpening filters
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

#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_plugin.h>

#include "va_filter.h"

/**********
 * Macros *
 **********/

#if defined (MODULE_NAME_IS_denoise)

# define DENOISE_HELP   N_("Reduces video noise")
# define FILTER_PREFIX  "denoise-"

# define SIG_TEXT       N_("Denoise strength (0-2)")
# define SIG_LONGTEXT   N_("Set the Denoise strength, "         \
                           "between 0 and 2. Defaults to 0.")

# define SIG_MIN        .0f
# define SIG_MAX        2.f

# define FILTER_NAME    Denoise
# define VA_FILTER_TYPE VAProcFilterNoiseReduction

#elif defined (MODULE_NAME_IS_sharpen)

# define FILTER_PREFIX  "sharpen-"

/*
** MUST DYNAMICALLY RETRIEVE THESE VALUES FROM THE CPU FILTER
** BUT THERE IS NO CORRECT WAY IMPLEMENTED AT THE MOMENT
**
*/
# define SIG_MIN        .0f
# define SIG_MAX        2.f

# define FILTER_NAME    Sharpen
# define VA_FILTER_TYPE VAProcFilterSharpening

#endif

#define GET_DRV_SIGMA(vlc_sigma, drv_range)                             \
    ((vlc_sigma - SIG_MIN) * (drv_range.max_value - drv_range.min_value) / \
     (SIG_MAX - SIG_MIN) + drv_range.min_value)

/*********************
 * module descriptor *
 *********************/

vlc_module_begin()
    set_capability("video filter", 0)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
#if defined (MODULE_NAME_IS_denoise)
    set_description(N_("VAAPI noise reduction filter"))
    set_shortname(N_("Denoise"))
    set_help(DENOISE_HELP)
    add_float_with_range(FILTER_PREFIX "sigma", SIG_MIN, SIG_MIN, SIG_MAX,
                         SIG_TEXT, SIG_LONGTEXT, false)
    add_shortcut("denoise")
#elif defined (MODULE_NAME_IS_sharpen)
    set_description(N_("VAAPI sharpening filter"))
    add_shortcut("sharpen")
#endif
    set_callbacks(Open, Close)
vlc_module_end()

/*******************
 * Filter callback *
 *******************/

static picture_t *      FILTER_NAME(filter_t *, picture_t *);

/******************
 * Open functions *
 ******************/

static inline int       Open_SetupFilterParams(filter_t *filter,
                                               filter_params_t *filter_params);

static inline int       Open_InitVAFilterParams
(filter_t *filter, filter_params_t *filter_params, void **p_va_filter_params,
 unsigned int *p_va_filter_param_sz, unsigned int *p_num_va_filter_params);

/************************
 * FILTER_NAME function *
 ************************/

static inline int       FILTER_NAME_UpdateVAFilterParams
(filter_t *filter, void *va_filter_params);

/********************
 * Sigma callback *
 ********************/

static int      SigmaCallback(vlc_object_t *this, char const *psz_var,
                              vlc_value_t oldval, vlc_value_t newval,
                              void *data);

/****************************
 * Filter parameters struct *
 ****************************/

struct  filter_params
{
    struct
    {
        vlc_atomic_float        value;
        VAProcFilterValueRange  drv_range;
    } sigma;
};

/*********************
 * Filter sys struct *
 *********************/

struct  filter_sys_t
{
    FILTER_SYS_COMMON_MEMBERS

    filter_params_t     params;
};

/********************
 * Common callbacks *
 ********************/

static int
Open(vlc_object_t *obj)
{
    filter_t *const     filter = (filter_t *)obj;

    if (Open_InitializeFilter(obj, VA_FILTER_TYPE, NULL,
                              NULL, Open_SetupFilterParams,
                              Open_InitVAFilterParams, NULL, NULL))
    {
        if (filter->p_sys)
            Open_ReleaseInternalData(obj, NULL);
        return VLC_EGENERIC;
    }

    var_AddCallback(filter, FILTER_PREFIX "sigma",
                    SigmaCallback, filter->p_sys);

    filter->pf_video_filter = FILTER_NAME;

    return VLC_SUCCESS;
}

static void
Close(vlc_object_t *obj)
{
    var_DelCallback(obj, FILTER_PREFIX "sigma",
                    SigmaCallback, ((filter_t *)obj)->p_sys);
    Close_ReleaseInternalData(obj, NULL);
}

/*******************
 * Filter callback *
 *******************/

static picture_t *
FILTER_NAME(filter_t *filter, picture_t *src)
{
    picture_t * dest;

    dest = Filter(filter, src, FILTER_NAME_UpdateVAFilterParams, NULL, NULL);
    picture_Release(src);

    return dest;
}

/******************
 * Open functions *
 ******************/

static inline int
Open_SetupFilterParams(filter_t *filter, filter_params_t *filter_params)
{
    VAProcFilterCap     caps;
    unsigned int        num_caps = 1;

    if (Open_QueryVideoProcFilterCaps(filter, VA_FILTER_TYPE,
                                      &caps, &num_caps))
        return VLC_EGENERIC;

    float const sigma =
        var_CreateGetFloatCommand(filter, FILTER_PREFIX "sigma");

    filter_params = &filter->p_sys->params;
    vlc_atomic_init_float(&filter_params->sigma.value, sigma);
    filter_params->sigma.drv_range = caps.range;

    return VLC_SUCCESS;
}

static inline int
Open_InitVAFilterParams(filter_t *filter, filter_params_t *filter_params,
                        void **p_va_filter_params,
                        unsigned int *p_va_filter_param_sz,
                        unsigned int *p_num_va_filter_params)
{ VLC_UNUSED(filter_params);
    VAProcFilterParameterBuffer *       p_params;

    *p_va_filter_param_sz = sizeof(*p_params);
    *p_num_va_filter_params = 1;

    if (!(p_params = calloc(1, sizeof(*p_params))))
    {
        msg_Err(filter, "unable to allocate memory");
        return VLC_ENOMEM;
    }
    p_params->type = VA_FILTER_TYPE;
    *p_va_filter_params = p_params;

    return VLC_SUCCESS;
}

/************************
 * FILTER_NAME function *
 ************************/

static inline int
FILTER_NAME_UpdateVAFilterParams(filter_t *filter, void *va_filter_params)
{
    filter_params_t *const              filter_params = &filter->p_sys->params;
    VAProcFilterParameterBuffer *const  p_param =
        (typeof(p_param))va_filter_params;

    p_param->value =
        GET_DRV_SIGMA(vlc_atomic_load_float(&filter_params->sigma.value),
                      filter_params->sigma.drv_range);

    return VLC_SUCCESS;
}

/******************
 * Sigma callback *
 ******************/

static int
SigmaCallback(vlc_object_t *this, char const *psz_var,
              vlc_value_t oldval, vlc_value_t newval, void *data)
{ VLC_UNUSED(this); VLC_UNUSED(psz_var); VLC_UNUSED(oldval);
    filter_sys_t *const filter_sys = (filter_sys_t *)data;

    vlc_atomic_store_float(&filter_sys->params.sigma.value,
                           VLC_CLIP(newval.f_float, SIG_MIN, SIG_MAX));

    return VLC_SUCCESS;
}
