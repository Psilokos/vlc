/*****************************************************************************
 * ci_filters.m: Video filters for MacOSX OpenGL video output
 *****************************************************************************
 * Copyright © 2017 VLC authors, VideoLAN and VideoLabs
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

#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_filter.h>
#include <vlc_mouse.h>
#include <vlc_picture.h>
#include <vlc_plugin.h>
#include "filter_picture.h"
#include "vt_utils.h"

#include <AppKit/NSOpenGL.h>
#include <CoreImage/CIContext.h>
#include <CoreImage/CIImage.h>
#include <CoreImage/CIFilter.h>
#include <CoreImage/CIVector.h>

enum    common_filter_type
{
    FILTER_NONE = -1,
    FILTER_ADJUST_HUE,
    FILTER_ADJUST_INTENSITIES,
    FILTER_ADJUST_GAMMA,
    FILTER_INVERT,
    FILTER_POSTERIZE,
    FILTER_SEPIA,
    FILTER_SHARPEN,
    FILTER_PSYCHEDELIC,
    NUM_COMMON_FILTERS,
    NUM_MAX_EQUIVALENT_VLC_FILTERS = 3,
};

enum    psyche_filter_type
{
    PSYCHEDELIC_FILTER_BASE_KALEIDOSCOPE,
    NUM_PSYCHEDELIC_FILTER_BASES,
    NUM_PSYCHEDELIC_SUBFILTERS = NUM_PSYCHEDELIC_FILTER_BASES,
    PSYCHEDELIC_FILTER_BASE_SIXFOLD_REFLECTION, // disabled 'cause of crash in api
};

#define NUM_FILTER_PARAMS_MAX    4

struct  filter_chain
{
    struct
    {
        enum common_filter_type common;
        enum psyche_filter_type psyche;
    }                   type;

    CIFilter *          ci_filter;

    video_format_t *    vfmt;

    struct
    {
        union
        {
            vlc_atomic_float        f_atomic_val;
            float                   f_val;
            CIVector *              vector_val;
        }       value;
        bool    b_initialized;
    }                   ci_params[NUM_FILTER_PARAMS_MAX];

    struct filter_chain *       next;

    void        (*pf_update_filter_params)(filter_t *, struct filter_chain *);
};

struct  ci_filters_ctx
{
    CVPixelBufferPoolRef        dest_cvpx_pool;
    CIContext *                 ci_ctx;
    struct filter_chain *       fchain;
};

struct filter_sys_t
{
    char const *                psz_filter;
    struct ci_filters_ctx *     ctx;
    struct
    {
        atomic_int      x;
        atomic_int      y;
    }                           mouse;
};

struct  range
{
    float       min;
    float       max;
};

struct  common_filter_desc
{
    struct
    {
        char const *            vlc;
        NSString *              ci;
    } const             name_desc;

    struct      common_filter_param_desc
    {
        char const *            vlc;
        NSString *              ci;
        struct
        {
            struct range    vlc;
            struct range    ci;
        }                       ranges[2];
        int                     vlc_type;
    } const             param_descs[NUM_FILTER_PARAMS_MAX];
};

static struct common_filter_desc        common_filter_desc_table[] =
{
    [FILTER_ADJUST_HUE] =
    {
        { "adjust", @"CIHueAdjust" },
        {
            { "hue", @"inputAngle", {{{-180.f, +180.f}, {+3.f, -3.f}}}, VLC_VAR_FLOAT }
        }
    },
    [FILTER_ADJUST_INTENSITIES] =
    {
        { "adjust", @"CIColorControls" },
        {
            { "contrast",   @"inputContrast",   {{{.0f, 2.f}, {.0f, 2.f}}},   VLC_VAR_FLOAT },
            { "brightness", @"inputBrightness", {{{.0f, 2.f}, {-1.f, +1.f}}}, VLC_VAR_FLOAT },
            { "saturation", @"inputSaturation", {{{.0f, 3.f}, {.0f, 2.7f}}},  VLC_VAR_FLOAT }
        }
    },
    [FILTER_ADJUST_GAMMA] =
    {
        { "adjust", @"CIGammaAdjust" },
        {
            { "gamma", @"inputPower", {{{.01f, 1.f}, {10.f, 1.f}}, {{1.f, 10.f}, {1.f, .01f}}}, VLC_VAR_FLOAT }
        }
    },
    [FILTER_INVERT] =
    {
        { "invert", @"CIColorInvert" }
    },
    [FILTER_POSTERIZE] =
    {
        { "posterize", @"CIColorPosterize" },
        {
            { "posterize-level", @"inputLevels", {{{2.f, 256.f}, {2.f, 256.f}}}, VLC_VAR_INTEGER }
        }
    },
    [FILTER_PSYCHEDELIC] = {{ "psychedelic" }},
    [FILTER_SEPIA] =
    {
        { "sepia", @"CISepiaTone" },
        {
            { "sepia-intensity", @"inputIntensity", {{{.0f, 255.f}, {.0f, 1.f}}}, VLC_VAR_INTEGER }
        }
    },
    [FILTER_SHARPEN] =
    {
        { "sharpen", @"CISharpenLuminance" },
        {
            { "sharpen-sigma", @"inputSharpness", {{{.0f, 2.f}, {.0f, 5.f}}}, VLC_VAR_FLOAT }
        }
    }
};

struct  psyche_filter_desc
{
    NSString *const     name;

    struct      psyche_filter_param_desc
    {
        NSString *              name;
        enum    psyche_param_type
            {
                PSYCHEDELIC_PARAM_FLOAT,
                PSYCHEDELIC_PARAM_POSITION
            }                   type;
        enum    psyche_update_method
            {
                PSYCHEDELIC_PARAM_STATIC,
                PSYCHEDELIC_PARAM_PROGRESSIVE,
                PSYCHEDELIC_PARAM_PROGRESSIVE_LINEAR,
                PSYCHEDELIC_PARAM_PROGRESSIVE_FUNCTIONAL,
                PSYCHEDELIC_PARAM_PROGRESSIVE_FUNCTIONAL_RANDOM,
                PSYCHEDELIC_PARAM_DYNAMIC
            }                   update_method;
        float                   f_step;
        struct range            range;
        union
        {
            float       (*pf_get_float)(struct filter_chain *, unsigned int);
            CIVector *  (*pf_get_pos)(filter_t *, struct filter_chain *, unsigned int);
        }                       function;
    } const             param_descs[NUM_FILTER_PARAMS_MAX];
};

static CIVector *mousePos(filter_t *, struct filter_chain *, unsigned int);
static float upAndDown(struct filter_chain *, unsigned int);

static struct psyche_filter_desc        psyche_filter_desc_table[] =
{
    [PSYCHEDELIC_FILTER_BASE_KALEIDOSCOPE] =
    {
        @"CIKaleidoscope",
        {
            { @"inputAngle",  PSYCHEDELIC_PARAM_FLOAT,    PSYCHEDELIC_PARAM_PROGRESSIVE_LINEAR,     .1f, {.0f, 359.f}                             },
            { @"inputCenter", PSYCHEDELIC_PARAM_POSITION, PSYCHEDELIC_PARAM_PROGRESSIVE_FUNCTIONAL, 1.f, {},          {.pf_get_pos = mousePos}    },
            { @"inputCount",  PSYCHEDELIC_PARAM_FLOAT,    PSYCHEDELIC_PARAM_PROGRESSIVE_FUNCTIONAL, .3f, {4.f, 50.f}, {.pf_get_float = upAndDown} }
        }
    },
    [PSYCHEDELIC_FILTER_BASE_SIXFOLD_REFLECTION] =
    {
        @"CISixfoldReflectedTile",
        {
            { @"inputAngle",  PSYCHEDELIC_PARAM_FLOAT,    PSYCHEDELIC_PARAM_STATIC, .0f, {.0f, 3.f}    },
            { @"inputCenter", PSYCHEDELIC_PARAM_POSITION, PSYCHEDELIC_PARAM_STATIC, .0f,               },
            { @"inputWidth",  PSYCHEDELIC_PARAM_FLOAT,    PSYCHEDELIC_PARAM_STATIC, .0f, {50.f, 100.f} },
        }
    }
};

#define GET_CI_VALUE(vlc_value, vlc_range, ci_range)               \
    ((vlc_value - vlc_range.min) * (ci_range.max - ci_range.min) / \
     (vlc_range.max - vlc_range.min) + ci_range.min)

static CIVector *
mousePos(filter_t *filter, struct filter_chain *fchain, unsigned int param_idx)
{ VLC_UNUSED(fchain); VLC_UNUSED(param_idx);
    return [CIVector vectorWithX: (float)atomic_load(&filter->p_sys->mouse.x)
                               Y: (float)(fchain->vfmt->i_height - atomic_load(&filter->p_sys->mouse.y) - 1)];
}

static float
upAndDown(struct filter_chain *fchain, unsigned int param_idx)
{
    static bool b_up = true;
    struct psyche_filter_desc *psyche_filter_desc =
        psyche_filter_desc_table + fchain->type.psyche;
    float cur = fchain->ci_params[param_idx].value.f_val;

    if (b_up)
    {
        cur += psyche_filter_desc->param_descs[param_idx].f_step;
        if (cur >= psyche_filter_desc->param_descs[param_idx].range.max)
            b_up = false;
    }
    else
    {
        cur -= psyche_filter_desc->param_descs[param_idx].f_step;
        if (cur <= psyche_filter_desc->param_descs[param_idx].range.min)
            b_up = true;
    }

    return cur;
}

static struct filter_chain *
filter_chain_AddFilter(struct filter_chain **fchain,
                       enum common_filter_type type)
{
    struct filter_chain *elem = calloc(1, sizeof(*elem));
    if (!elem)
        return NULL;
    elem->type.common = type;

    if (!*fchain)
        *fchain = elem;
    else
    {
        struct filter_chain *it = *fchain;
        while (it->next) it = it->next;
        it->next = elem;
    }

    return elem;
}

static void
filter_chain_RemoveFilter(struct filter_chain **fchain,
                          enum common_filter_type type)
{
    struct filter_chain *prev = NULL;
    struct filter_chain *to_del;

    for (to_del = *fchain; to_del && to_del->type.common != type;
         to_del = to_del->next)
        prev = to_del;
    assert(to_del);
    if (!prev)
        *fchain = to_del->next;
    else
        prev->next = to_del->next;

    free(to_del);
}

static void
common_filter_desc_table_GetFilterTypes
(char const *vlc_filter_name,
 enum common_filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS])
{
    int j = 0;
    for (int i = 0; i < NUM_COMMON_FILTERS; ++i)
        if (!strcmp(common_filter_desc_table[i].name_desc.vlc, vlc_filter_name))
        {
            assert(j < NUM_MAX_EQUIVALENT_VLC_FILTERS);
            filter_types[j++] = i;
        }
    assert(j);
    while (j < NUM_MAX_EQUIVALENT_VLC_FILTERS)
        filter_types[j++] = FILTER_NONE;
}

static inline NSString *
common_filter_desc_table_GetFilterName(enum common_filter_type type)
{ assert(type < NUM_COMMON_FILTERS);
    return common_filter_desc_table[type].name_desc.ci;
}

static float
common_filter_ConvertParam(float f_vlc_val,
                           struct common_filter_param_desc const *param_desc)
{
    struct range clip_range = { param_desc->ranges[0].vlc.min,
                                param_desc->ranges[1].vlc.max
                                ? param_desc->ranges[1].vlc.max
                                : param_desc->ranges[0].vlc.max};
    f_vlc_val = VLC_CLIP(f_vlc_val, clip_range.min, clip_range.max);

    unsigned int range_idx;
    for (range_idx = 0; range_idx < 2; ++range_idx)
        if (f_vlc_val >= param_desc->ranges[range_idx].vlc.min &&
            f_vlc_val <= param_desc->ranges[range_idx].vlc.max)
            break;
    assert(range_idx < 2);

    return GET_CI_VALUE(f_vlc_val,
                        param_desc->ranges[range_idx].vlc,
                        param_desc->ranges[range_idx].ci);
}

static int
CommonFilter_ParamsCallback(vlc_object_t *obj,
                            char const *psz_var,
                            vlc_value_t oldval, vlc_value_t newval,
                            void *p_data)
{
    VLC_UNUSED(obj); VLC_UNUSED(oldval);
    struct filter_chain *filter = p_data;
    struct common_filter_param_desc const *filter_param_descs =
        common_filter_desc_table[filter->type.common].param_descs;

    unsigned int i = 0;
    while (i < NUM_FILTER_PARAMS_MAX &&
           strcmp(filter_param_descs[i].vlc, psz_var))
        ++i;
    assert(i < NUM_FILTER_PARAMS_MAX);

    float new_vlc_val;
    if (filter_param_descs[i].vlc_type == VLC_VAR_FLOAT)
        new_vlc_val = newval.f_float;
    else if (filter_param_descs[i].vlc_type == VLC_VAR_INTEGER)
        new_vlc_val = newval.i_int;
    else
        vlc_assert_unreachable();

    float new_ci_val =
        common_filter_ConvertParam(new_vlc_val, filter_param_descs + i); // rm var
    vlc_atomic_store_float(&filter->ci_params[i].value.f_atomic_val,
                           new_ci_val);

    fprintf(stderr, "%s's %s: %f->%f\n",
            common_filter_desc_table[filter->type.common].name_desc.vlc, filter_param_descs[i].vlc,
            new_vlc_val, new_ci_val);

    return VLC_SUCCESS;
}

static int
Filter_Mouse(filter_t *filter,
             struct vlc_mouse_t *p_mouse,
             struct vlc_mouse_t const *p_old,
             struct vlc_mouse_t const *p_new)
{ VLC_UNUSED(p_old);
    atomic_store(&filter->p_sys->mouse.x, p_new->i_x);
    atomic_store(&filter->p_sys->mouse.y, p_new->i_y);
    *p_mouse = *p_new;
    return VLC_SUCCESS;
}

static void
common_filter_UpdateParams(filter_t *filter, struct filter_chain *fchain)
{ VLC_UNUSED(filter);
    for (unsigned int i = 0;
         i < NUM_FILTER_PARAMS_MAX &&
             common_filter_desc_table[fchain->type.common].param_descs[i].vlc;
         ++i)
    {
        NSString *ci_param_name =
            common_filter_desc_table[fchain->type.common].param_descs[i].ci;
        float ci_value =
            vlc_atomic_load_float(&fchain->ci_params[i].value.f_atomic_val);

        [fchain->ci_filter setValue: [NSNumber numberWithFloat: ci_value]
                             forKey: ci_param_name];
    }
}

static void
psyche_filter_UpdateFloatParam(struct filter_chain *fchain,
                               NSString *ci_param_name, unsigned int param_idx)
{
    struct psyche_filter_desc *psyche_filter_desc =
        psyche_filter_desc_table + fchain->type.psyche;
    enum psyche_update_method meth =
        psyche_filter_desc->param_descs[param_idx].update_method;
    struct range const *range =
        &psyche_filter_desc->param_descs[param_idx].range;
    float ci_value;

    if (meth == PSYCHEDELIC_PARAM_DYNAMIC ||
        !fchain->ci_params[param_idx].b_initialized)
        ci_value = arc4random_uniform(range->max + 1) + range->min;
    else if (meth == PSYCHEDELIC_PARAM_PROGRESSIVE)
    {
        float f_step = (arc4random() % 2 ? 1 : -1)
            * psyche_filter_desc->param_descs[param_idx].f_step;
        float f_tmp = fchain->ci_params[param_idx].value.f_val + f_step;

        if (f_tmp < range->min || f_tmp > range->max)
        {
            f_step *= -1;
            f_tmp = fchain->ci_params[param_idx].value.f_val + f_step;
            if (f_tmp < range->min || f_tmp > range->max)
                return;
        }
        ci_value = f_tmp;
    }
    else if (meth == PSYCHEDELIC_PARAM_PROGRESSIVE_LINEAR)
    {
        ci_value = fchain->ci_params[param_idx].value.f_val +
            psyche_filter_desc->param_descs[param_idx].f_step;
        if (ci_value > psyche_filter_desc->param_descs[param_idx].range.max)
            ci_value = psyche_filter_desc->param_descs[param_idx].range.min;
    }
    else if (meth == PSYCHEDELIC_PARAM_PROGRESSIVE_FUNCTIONAL)
    {
        float (*pf_get_float)(struct filter_chain *, unsigned int) =
            psyche_filter_desc->param_descs[param_idx].function.pf_get_float;
        ci_value = pf_get_float(fchain, param_idx);
    }
    else if (meth == PSYCHEDELIC_PARAM_PROGRESSIVE_FUNCTIONAL_RANDOM)
        assert(!"Not implemented yet");
    else
        vlc_assert_unreachable();

    [fchain->ci_filter setValue: [NSNumber numberWithFloat: ci_value]
                         forKey: ci_param_name];
    fchain->ci_params[param_idx].value.f_val = ci_value;

    fprintf(stderr, "%f\n", fchain->ci_params[param_idx].value.f_val);
}

static void
psyche_filter_UpdatePosParam(filter_t *filter, struct filter_chain *fchain,
                             NSString *ci_param_name, unsigned int param_idx)
{
    struct psyche_filter_desc *psyche_filter_desc =
        psyche_filter_desc_table + fchain->type.psyche;
    enum psyche_update_method meth =
        psyche_filter_desc->param_descs[param_idx].update_method;
    CIVector *ci_value;

    if (meth == PSYCHEDELIC_PARAM_DYNAMIC ||
        !fchain->ci_params[param_idx].b_initialized)
    {
        float f_x =
            arc4random_uniform(fchain->vfmt->i_width);
        float f_y =
            arc4random_uniform(fchain->vfmt->i_height);
        ci_value = [CIVector vectorWithX: f_x
                                       Y: f_y];
    }
    else if (meth == PSYCHEDELIC_PARAM_PROGRESSIVE)
    {
        float f_step_x = (arc4random() % 2 ? 1 : -1)
            * psyche_filter_desc->param_descs[param_idx].f_step;
        float f_step_y = (arc4random() % 2 ? 1 : -1)
            * psyche_filter_desc->param_descs[param_idx].f_step;
        float f_x =
            [fchain->ci_params[param_idx].value.vector_val X] + f_step_x;
        float f_y =
            [fchain->ci_params[param_idx].value.vector_val Y] + f_step_y;

        if (f_x < 0 || (unsigned int)f_x > fchain->vfmt->i_width)
        {
            f_step_x *= -1;
            f_x = [fchain->ci_params[param_idx].value.vector_val X] + f_step_x;
            if (f_x < 0 || (unsigned int)f_x > fchain->vfmt->i_width)
                return;
        }
        if (f_y < 0 || (unsigned int)f_y >= fchain->vfmt->i_height)
        {
            f_step_y *= -1;
            f_y = [fchain->ci_params[param_idx].value.vector_val Y] + f_step_y;
            if (f_y < 0 || (unsigned int)f_y >= fchain->vfmt->i_height)
                return;
        }

        ci_value = [CIVector vectorWithX: f_x
                                       Y: f_y];
    }
    else if (meth == PSYCHEDELIC_PARAM_PROGRESSIVE_FUNCTIONAL)
    {
        CIVector *(*pf_get_pos)(filter_t *,
                                struct filter_chain *, unsigned int) =
            psyche_filter_desc->param_descs[param_idx].function.pf_get_pos;
        ci_value = pf_get_pos(filter, fchain, param_idx);
    }
    else if (meth == PSYCHEDELIC_PARAM_PROGRESSIVE_FUNCTIONAL_RANDOM)
        assert(!"Not implemented yet");
    else
        vlc_assert_unreachable();

    [fchain->ci_filter setValue: ci_value
                         forKey: ci_param_name];
    fchain->ci_params[param_idx].value.vector_val = ci_value;

    fprintf(stderr, "%f %f\n", [ci_value X], [ci_value Y]);
}

static void
psyche_filter_UpdateParams(filter_t *filter, struct filter_chain *fchain)
{
    struct psyche_filter_desc *psyche_filter_desc =
        psyche_filter_desc_table + fchain->type.psyche;

    for (unsigned int i = 0;
         i < NUM_FILTER_PARAMS_MAX && psyche_filter_desc->param_descs[i].name;
        ++i)
    {
        enum psyche_param_type type = psyche_filter_desc->param_descs[i].type;
        enum psyche_update_method meth = psyche_filter_desc->param_descs[i].update_method;

        NSString *ci_param_name = psyche_filter_desc->param_descs[i].name;

        if (meth == PSYCHEDELIC_PARAM_STATIC
            && fchain->ci_params[i].b_initialized)
            continue;
        if (type == PSYCHEDELIC_PARAM_FLOAT)
            psyche_filter_UpdateFloatParam(fchain, ci_param_name, i);
        else if (type == PSYCHEDELIC_PARAM_POSITION)
            psyche_filter_UpdatePosParam(filter, fchain, ci_param_name, i);
        else
            vlc_assert_unreachable();

        if (!fchain->ci_params[i].b_initialized)
            fchain->ci_params[i].b_initialized = true;
    }
}

static picture_t *
Filter(filter_t *filter, picture_t *src)
{
    struct ci_filters_ctx *ctx = filter->p_sys->ctx;
    enum common_filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS];

    common_filter_desc_table_GetFilterTypes(filter->p_sys->psz_filter,
                                            filter_types);
    if (ctx->fchain->type.common != filter_types[0])
        return src;

    picture_t *dst = picture_NewFromFormat(&filter->fmt_out.video);
    if (!dst)
        goto error;

    CVPixelBufferRef cvpx;
    cvpx = cvpxpool_get_cvpx(ctx->dest_cvpx_pool);
    if (!cvpx || cvpxpic_attach(dst, cvpx))
        goto error;

    NSDictionary<NSString *, id> *ci_img_opt_dict =
        [NSDictionary dictionaryWithObject: [NSNull null]
                                    forKey: kCIImageColorSpace];

    if (!ci_img_opt_dict)
        goto error;

    CIImage *ci_img;
    CVPixelBufferRef qwe = cvpxpic_get_ref(src);
    fprintf(stderr, "%p\n", qwe);
    ci_img = [CIImage imageWithCVImageBuffer: qwe
                                     options: ci_img_opt_dict];
    if (!ci_img)
        goto error;

    for (struct filter_chain *fchain = ctx->fchain;
         fchain; fchain = fchain->next)
    {
        [fchain->ci_filter setValue:ci_img forKey:kCIInputImageKey];
        fchain->pf_update_filter_params(filter, fchain);
        ci_img = [fchain->ci_filter valueForKey:kCIOutputImageKey];
    }

    [ctx->ci_ctx render: ci_img
            toIOSurface: CVPixelBufferGetIOSurface(cvpx)
                 bounds: [ci_img extent]
             colorSpace: nil];

    return CopyInfoAndRelease(dst, src);

error:
    if (dst)
        picture_Release(dst);
    picture_Release(src);
    return NULL;
}

static void
common_filter_Init(filter_t *filter, struct filter_chain *fchain)
{
    struct common_filter_param_desc const *filter_param_descs =
        common_filter_desc_table[fchain->type.common].param_descs;
    NSString *ci_filter_name =
        common_filter_desc_table_GetFilterName(fchain->type.common);

    fchain->ci_filter = [CIFilter filterWithName: ci_filter_name];

    for (NSString *param in [[fchain->ci_filter attributes]allKeys])
        fprintf(stderr, "%s\n", [param UTF8String]);

    for (unsigned int i = 0;
         i < NUM_FILTER_PARAMS_MAX && filter_param_descs[i].vlc;
         ++i)
    {
        NSString *ci_param_name = filter_param_descs[i].ci;
        NSDictionary<NSString *, id> *ci_param_attr_dict =
            [[fchain->ci_filter attributes] valueForKey:ci_param_name];

        NSNumber *ci_min = [ci_param_attr_dict valueForKey : kCIAttributeMin];
        NSNumber *ci_max = [ci_param_attr_dict valueForKey : kCIAttributeMax];
        fprintf(stderr, "%s's %s ci api range: [%f;%f]\n",
                [ci_filter_name UTF8String], [ci_param_name UTF8String],
                [ci_min floatValue], [ci_max floatValue]);

        char const *vlc_param_name = filter_param_descs[i].vlc;
        float vlc_param_val;
        if (filter_param_descs[i].vlc_type == VLC_VAR_FLOAT)
            vlc_param_val = var_CreateGetFloatCommand(filter, vlc_param_name);
        else if (filter_param_descs[i].vlc_type == VLC_VAR_INTEGER)
            vlc_param_val =
                (float)var_CreateGetIntegerCommand(filter, vlc_param_name);
        else
            vlc_assert_unreachable();

        float ci_param_val =
            common_filter_ConvertParam(vlc_param_val, filter_param_descs + i);
        vlc_atomic_init_float(&fchain->ci_params[i].value.f_atomic_val,
                              ci_param_val);

        var_AddCallback(filter, filter_param_descs[i].vlc,
                        CommonFilter_ParamsCallback, fchain);
        fprintf(stderr, "callback added on %s\n", filter_param_descs[i].vlc);
    }

    fchain->pf_update_filter_params = common_filter_UpdateParams;
}

static int
common_filter_CreateFilters
(filter_t *filter, struct filter_chain **p_last_filter,
 enum common_filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS])
{
    struct filter_chain *new_filter;

    for (unsigned int i = 0;
         i < NUM_MAX_EQUIVALENT_VLC_FILTERS
             && filter_types[i] != FILTER_NONE; ++i)
    {
        new_filter = filter_chain_AddFilter(p_last_filter, filter_types[i]);
        if (!new_filter)
            return VLC_EGENERIC;
        p_last_filter = &new_filter;
        common_filter_Init(filter, new_filter);
    }

    return VLC_SUCCESS;
}

static void
psyche_filter_Init(filter_t *filter, struct filter_chain *fchain,
                   enum psyche_filter_type psyche_filter_type)
{
    fchain->type.psyche = psyche_filter_type;
    struct psyche_filter_param_desc const *filter_param_descs =
        psyche_filter_desc_table[fchain->type.psyche].param_descs;
    NSString *ci_filter_name =
        psyche_filter_desc_table[fchain->type.psyche].name;

    fchain->ci_filter = [CIFilter filterWithName: ci_filter_name];

    fchain->vfmt = &filter->fmt_out.video;

    for (NSString *param in [[fchain->ci_filter attributes]allKeys])
        fprintf(stderr, "%s\n", [param UTF8String]);

    for (unsigned int i = 0;
         i < NUM_FILTER_PARAMS_MAX && filter_param_descs[i].name;
         ++i)
    {
        NSString *ci_param_name = filter_param_descs[i].name;
        NSDictionary<NSString *, id> *ci_param_attr_dict =
            [[fchain->ci_filter attributes] valueForKey:ci_param_name];

        if (filter_param_descs[i].type == PSYCHEDELIC_PARAM_FLOAT)
        {
            NSNumber *ci_min = [ci_param_attr_dict valueForKey : kCIAttributeMin];
            NSNumber *ci_max = [ci_param_attr_dict valueForKey : kCIAttributeMax];
            fprintf(stderr, "%s's %s ci api range: [%f;%f]\n",
                    [ci_filter_name UTF8String], [ci_param_name UTF8String],
                    [ci_min floatValue], [ci_max floatValue]);
        }
        else if (filter_param_descs[i].type == PSYCHEDELIC_PARAM_POSITION)
        {
            fprintf(stderr, "%s's %s is a vector\n",
                    [ci_filter_name UTF8String], [ci_param_name UTF8String]);
        }
        else
            vlc_assert_unreachable();
    }

    fchain->pf_update_filter_params = psyche_filter_UpdateParams;
    fchain->pf_update_filter_params(filter, fchain);
}

static int
psyche_filter_CreateFilters(filter_t *filter,
                            struct filter_chain **p_last_filter)
{
    struct filter_chain *new_filter;
    new_filter = filter_chain_AddFilter(p_last_filter, FILTER_PSYCHEDELIC);
    if (!new_filter)
        return VLC_EGENERIC;
    psyche_filter_Init(filter, new_filter,
                       arc4random_uniform(NUM_PSYCHEDELIC_FILTER_BASES));
    return VLC_SUCCESS;
}

static inline int
filter_CreateFilters
(filter_t *filter, struct filter_chain **p_last_filter,
 enum common_filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS])
{
#ifndef NDEBUG
    if (filter_types[0] == FILTER_PSYCHEDELIC)
        assert(filter_types[1] == FILTER_NONE);
#endif
    return filter_types[0] != FILTER_PSYCHEDELIC
        ? common_filter_CreateFilters(filter, p_last_filter, filter_types)
        : psyche_filter_CreateFilters(filter, p_last_filter);
}

static int
Open(vlc_object_t *obj, char const *psz_filter)
{
    filter_t *filter = (filter_t *)obj;
    filter->p_sys = calloc(1, sizeof(filter_sys_t));
    if (!filter->p_sys)
        return VLC_ENOMEM;

    enum common_filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS];
    common_filter_desc_table_GetFilterTypes(psz_filter, filter_types);

    struct ci_filters_ctx *ctx = var_InheritAddress(filter, "ci-filters-ctx");
    if (!ctx)
    {
        ctx = calloc(1, sizeof(*ctx));
        if (!ctx)
            goto error;

        ctx->dest_cvpx_pool = cvpxpool_create(&filter->fmt_in.video, 2);
        if (!ctx->dest_cvpx_pool)
            goto error;

        NSOpenGLContext *context =
            var_InheritAddress(filter, "macosx-ns-opengl-context");
        assert(context);

        ctx->ci_ctx = [CIContext contextWithCGLContext: [context CGLContextObj]
                                           pixelFormat: [[context pixelFormat] CGLPixelFormatObj]
                                            colorSpace: nil
                                               options: nil];
        if (!ctx->ci_ctx)
            goto error;

        if (filter_CreateFilters(filter, &ctx->fchain, filter_types))
            goto error;

        var_Create(filter->obj.parent, "ci-filters-ctx", VLC_VAR_ADDRESS);
        var_SetAddress(filter->obj.parent, "ci-filters-ctx", ctx);

        atomic_init(&filter->p_sys->mouse.x, filter->fmt_out.video.i_width / 2);
        atomic_init(&filter->p_sys->mouse.y, filter->fmt_out.video.i_height / 2);
        filter->pf_video_mouse = Filter_Mouse;
    }
    else if (filter_CreateFilters(filter, &ctx->fchain, filter_types))
        goto error;

    filter->p_sys->psz_filter = psz_filter;
    filter->p_sys->ctx = ctx;

    fprintf(stderr, "OPEN: %s filter added to chain\n", psz_filter);
    fprintf(stderr, "%s\n", [[[CIFilter filterNamesInCategory : kCICategoryGeometryAdjustment] componentsJoinedByString : @";"] UTF8String]);
    fprintf(stderr, "%s\n", [[[CIFilter filterNamesInCategory : kCICategoryDistortionEffect] componentsJoinedByString : @";"] UTF8String]);

    filter->pf_video_filter = Filter;

    return VLC_SUCCESS;

error:
    if (ctx)
    {
        if (ctx->ci_ctx)
            [ctx->ci_ctx release];
        if (ctx->dest_cvpx_pool)
            CVPixelBufferPoolRelease(ctx->dest_cvpx_pool);
        free(ctx);
    }
    free(filter->p_sys);
    return VLC_EGENERIC;
}

static int
OpenAdjust(vlc_object_t *obj)
{
    return Open(obj, "adjust");
}

static int
OpenGradient(vlc_object_t *obj)
{
    return Open(obj, "gradient");
}

static int
OpenInvert(vlc_object_t *obj)
{
    return Open(obj, "invert");
}

static int
OpenPosterize(vlc_object_t *obj)
{
    return Open(obj, "posterize");
}

static int
OpenPsychedelic(vlc_object_t *obj)
{
    return Open(obj, "psychedelic");
}

static int
OpenSepia(vlc_object_t *obj)
{
    return Open(obj, "sepia");
}

static int
OpenSharpen(vlc_object_t *obj)
{
    return Open(obj, "sharpen");
}

static void
Close(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *)obj;
    struct ci_filters_ctx *ctx = filter->p_sys->ctx;
    enum common_filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS];

    common_filter_desc_table_GetFilterTypes(filter->p_sys->psz_filter,
                                            filter_types);
    for (unsigned int i = 0;
         i < NUM_MAX_EQUIVALENT_VLC_FILTERS && filter_types[i] != FILTER_NONE;
         ++i)
        filter_chain_RemoveFilter(&ctx->fchain, filter_types[i]);

    if (!ctx->fchain)
    {
        [ctx->ci_ctx release];
        CVPixelBufferPoolRelease(ctx->dest_cvpx_pool);
        free(ctx);
        var_Destroy(filter->obj.parent, "ci-filters-ctx");
    }
    fprintf(stderr, "CLOSE: %s filter removed from chain\n", (char *)filter->p_sys->psz_filter);
    free(filter->p_sys);
}

vlc_module_begin()
    set_capability("video filter", 0)
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_description(N_("Mac OS X hardware video filters"))

    add_submodule()
    set_callbacks(OpenAdjust, Close)
    add_shortcut("adjust")

    add_submodule()
    set_callbacks(OpenGradient, Close)
    add_shortcut("gradient")

    add_submodule()
    set_callbacks(OpenInvert, Close)
    add_shortcut("invert")

    add_submodule()
    set_callbacks(OpenPosterize, Close)
    add_shortcut("posterize")

    add_submodule()
    set_callbacks(OpenPsychedelic, Close)
    add_shortcut("psychedelic")

    add_submodule()
    set_callbacks(OpenSepia, Close)
    add_shortcut("sepia")

    add_submodule()
    set_callbacks(OpenSharpen, Close)
    add_shortcut("sharpen")
vlc_module_end()
