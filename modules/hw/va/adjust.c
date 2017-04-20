/*****************************************************************************
 * adjust.c: VAAPI Contrast/Hue/Saturation/Brightness video plugin for vlc
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

/*******************
 * Filter callback *
 *******************/

static picture_t *      Adjust(filter_t *, picture_t *);

/******************
 * Open functions *
 ******************/

static inline int       Open_SetupFilterParams(filter_t *filter,
                                               filter_params_t *filter_params);

static inline int       Open_InitVAFilterParams
(filter_t *filter, filter_params_t *filter_params, void **p_va_filter_params,
 unsigned int *p_va_filter_param_sz, unsigned int *p_num_va_filter_params);

/*******************
 * Adjust function *
 *******************/

static inline int       Adjust_UpdateVAFilterParams(filter_t *filter,
                                                    void *va_filter_params);

/*******************
 * Adjust callback *
 *******************/

static int      AdjustCallback(vlc_object_t *this, char const *psz_var,
                               vlc_value_t oldval, vlc_value_t newval,
                               void *data);

/***********************
 * Color balance modes *
 ***********************/

enum { CONT, LUM, HUE, SAT, SZ };

static VAProcColorBalanceType const     color_balance_modes[SZ] =
{
    [CONT] = VAProcColorBalanceContrast,
    [LUM] = VAProcColorBalanceBrightness,
    [HUE] = VAProcColorBalanceHue,
    [SAT] = VAProcColorBalanceSaturation
};

static char const *const        color_balance_params[SZ] =
{
    [CONT] = "contrast",
    [LUM] = "brightness",
    [HUE] = "hue",
    [SAT] = "saturation"
};

/***************************
 * Sigma ranges (VLC side) *
 ***************************/

/*
** MUST DYNAMICALLY RETRIEVE THESE VALUES FROM THE CPU FILTER
** BUT THERE IS NO CORRECT WAY IMPLEMENTED AT THE MOMENT
*/

static struct
{
    float       min_value;
    float       max_value;
} const sigma_ranges[SZ] =
{
    [CONT] = {  .0f,    2.f     },
    [LUM]  = {  .0f,    2.f     },
    [HUE]  = {  -180.f, +180.f  },
    [SAT]  = {  .0f,    3.f     }
};

/****************************
 * Filter parameters struct *
 ****************************/

struct  filter_params
{
    struct
    {
        vlc_atomic_float        value;
        struct
        {
            float       min_value;
            float       max_value;
        }                       vlc_range;
        VAProcFilterValueRange  drv_range;
        bool                    is_available;
    } sigma[SZ];
};

/*********************
 * Filter sys struct *
 *********************/

struct  filter_sys_t
{
    FILTER_SYS_COMMON_MEMBERS

    filter_params_t     params;
    unsigned int        num_available_modes;
};

/**********
 * Macros *
 **********/

#define GET_DRV_SIGMA(vlc_sigma, vlc_range, drv_range)                  \
    ((vlc_sigma - vlc_range.min_value) *                                \
     (drv_range.max_value - drv_range.min_value) /                      \
     (vlc_range.max_value - vlc_range.min_value) + drv_range.min_value)

/*********************
 * Module descriptor *
 *********************/

vlc_module_begin()
    set_description(N_("VAAPI Image properties filter"))
    set_capability("video filter", 0)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    add_shortcut("adjust")
    set_callbacks(Open, Close)
vlc_module_end()

/********************
 * Common callbacks *
 ********************/

static int
Open(vlc_object_t *obj)
{
    filter_t *const     filter = (filter_t *)obj;

    if (Open_InitializeFilter(obj, VAProcFilterColorBalance, NULL,
                              NULL, &Open_SetupFilterParams,
                              &Open_InitVAFilterParams, NULL, NULL))
    {
        if (filter->p_sys)
            Open_ReleaseInternalData(obj, NULL);
        return VLC_EGENERIC;
    }

    for (unsigned int i = 0; i < SZ; ++i)
        var_AddCallback(filter, color_balance_params[i],
                        &AdjustCallback, filter->p_sys);

    filter->pf_video_filter = Adjust;

    return VLC_SUCCESS;
}

static void
Close(vlc_object_t *obj)
{
    for (unsigned int i = 0; i < SZ; ++i)
        var_DelCallback(obj, color_balance_params[i],
                        &AdjustCallback, ((filter_t *)obj)->p_sys);
    Close_ReleaseInternalData(obj, NULL);
}

/*******************
 * Filter callback *
 *******************/

static picture_t *
Adjust(filter_t *filter, picture_t *src)
{
    picture_t * dest;

    dest = Filter(filter, src, &Adjust_UpdateVAFilterParams, NULL, NULL);
    picture_Release(src);

    return dest;
}

/******************
 * Open functions *
 ******************/

static inline int
Open_SetupFilterParams(filter_t *filter, filter_params_t *filter_params)
{
    filter_sys_t *const         filter_sys = filter->p_sys;
    VAProcFilterCapColorBalance caps[VAProcColorBalanceCount];
    unsigned int                num_caps = VAProcColorBalanceCount;

    if (Open_QueryVideoProcFilterCaps(filter, VAProcFilterColorBalance,
                                      &caps, &num_caps))
        return VLC_EGENERIC;

    filter_params = &filter->p_sys->params;
    for (unsigned int i = 0; i < SZ; ++i)
    {
        unsigned int    j;

        for (j = 0; j < num_caps; ++j)
            if (caps[j].type == color_balance_modes[i])
            {
                float const     sigma =
                    var_InheritFloat(filter, color_balance_params[i]);

                vlc_atomic_init_float(&filter_params->sigma[i].value, sigma);
                filter_sys->params.sigma[i].drv_range = caps[j].range;
                filter_sys->params.sigma[i].is_available = true;
                ++filter_sys->num_available_modes;
                break;
            }
        if (j == num_caps)
            filter_sys->params.sigma[i].is_available = false;
    }

    return VLC_SUCCESS;
}

static inline int
Open_InitVAFilterParams(filter_t *filter, filter_params_t *filter_params,
                        void **p_va_filter_params,
                        unsigned int *p_va_filter_param_sz,
                        unsigned int *p_num_va_filter_params)
{ VLC_UNUSED(filter_params);
    VAProcFilterParameterBufferColorBalance *   p_params;
    unsigned int                                i;

    *p_va_filter_param_sz = sizeof(typeof(*p_params));
    *p_num_va_filter_params = filter->p_sys->num_available_modes;

    if (!(p_params = calloc(*p_num_va_filter_params, *p_va_filter_param_sz)))
    {
        msg_Err(filter, "unable to allocate memory");
        return VLC_ENOMEM;
    }

    filter_params = &filter->p_sys->params;
    i = 0;
    for (unsigned int j = 0; j < SZ; ++j)
        if (filter_params->sigma[j].is_available)
        {
            p_params[i].type = VAProcFilterColorBalance;
            p_params[i++].attrib = color_balance_modes[j];
        }

    *p_va_filter_params = p_params;

    return VLC_SUCCESS;
}

/*******************
 * Adjust function *
 *******************/

static inline int
Adjust_UpdateVAFilterParams(filter_t *filter, void *va_filter_params)
{
    filter_params_t *const                              filter_params =
        &filter->p_sys->params;
    VAProcFilterParameterBufferColorBalance *const      p_params =
        (typeof(p_params))va_filter_params;
    unsigned int                                        i = 0;

    for (unsigned int j = 0; j < SZ; ++j)
        if (filter_params->sigma[j].is_available)
        {
            p_params[i].value = GET_DRV_SIGMA(
                vlc_atomic_load_float(&filter_params->sigma[j].value),
                sigma_ranges[i], filter_params->sigma[j].drv_range);
            ++i;
        }

    return VLC_SUCCESS;
}

/*******************
 * Adjust callback *
 *******************/

static int
AdjustCallback(vlc_object_t *this, char const *psz_var,
               vlc_value_t oldval, vlc_value_t newval, void *data)
{ VLC_UNUSED(this); VLC_UNUSED(oldval);
    filter_sys_t *      filter_sys = (filter_sys_t *)data;

    for (unsigned int i = 0; i < SZ; ++i)
        if (!strcmp(psz_var, color_balance_params[i]))
        {
            float const sigma = VLC_CLIP(newval.f_float,
                                         sigma_ranges[i].min_value,
                                         sigma_ranges[i].max_value);
            vlc_atomic_store_float(&filter_sys->params.sigma[i].value, sigma);
            break;
        }

    return VLC_SUCCESS;
}
