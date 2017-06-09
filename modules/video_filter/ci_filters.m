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
#include <vlc_picture.h>
#include <vlc_plugin.h>
#include "filter_picture.h"
#include "vt_utils.h"

#include <AppKit/NSOpenGL.h>
#include <CoreImage/CIContext.h>
#include <CoreImage/CIImage.h>
#include <CoreImage/CIFilter.h>
#include <CoreImage/CIVector.h>

// blur => multiply compositing over time?
enum    filter_type
{
    FILTER_NONE = -1,
    FILTER_ADJUST_HUE,
    FILTER_ADJUST_INTENSITIES,
    FILTER_ADJUST_GAMMA,
    FILTER_INVERT,
    FILTER_POSTERIZE,
    FILTER_PSYCHEDELIC,
    FILTER_SEPIA,
    FILTER_SHARPEN,
    NUM_FILTERS,
    NUM_MAX_EQUIVALENT_VLC_FILTERS = 3
};

#define NUM_FILTER_PARAM_MAX    4

struct  filter_chain
{
    enum filter_type            filter;
    CIFilter *                  ci_filter;
    struct
    {
        vlc_atomic_float    value;
        struct
        {
            float       min;
            float       max;
        }                   range;
    }                           ci_params[NUM_FILTER_PARAM_MAX];
    struct filter_chain *       next;
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
};

struct  range
{
    float       min;
    float       max;
};

struct  filter_desc
{
    struct
    {
        char const *            vlc;
        NSString *              ci;
    } const             name_desc;

    struct      filter_param_desc
    {
        char const *            vlc;
        NSString *              ci;
        struct
        {
            struct range    vlc;
            struct range    ci;
        }                       ranges[2];
        int                     vlc_type;
    } const             param_descs[NUM_FILTER_PARAM_MAX];
};

static struct filter_desc       filter_desc_table[] =
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
    [FILTER_PSYCHEDELIC] =
    {
        // { "psychedelic", @"CICrystallize" },
        // { "psychedelic", @"CIComicEffect" },
        // { "psychedelic", @"CICircleSplashDistortion" } //crash
        // { "psychedelic", @"CIBumpDistortion" }
        // { "psychedelic", @"CIThermal" }
        { "psychedelic", @"CIKaleidoscope" }
    },
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

#define GET_CI_VALUE(vlc_value, vlc_range, ci_range)               \
    ((vlc_value - vlc_range.min) * (ci_range.max - ci_range.min) / \
     (vlc_range.max - vlc_range.min) + ci_range.min)

static struct filter_chain *
filter_chain_AddFilter(struct filter_chain **fchain, enum filter_type filter)
{
    struct filter_chain *elem = calloc(1, sizeof(*elem));
    if (!elem)
        return NULL;
    elem->filter = filter;

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
                          enum filter_type filter)
{
    struct filter_chain *prev = NULL;
    struct filter_chain *to_del;

    for (to_del = *fchain; to_del && to_del->filter != filter;
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
filter_desc_table_GetFilterTypes
(char const *vlc_filter_name,
 enum filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS])
{
    int j = 0;
    for (int i = 0; i < NUM_FILTERS; ++i)
        if (!strcmp(filter_desc_table[i].name_desc.vlc, vlc_filter_name))
        {
            assert(j < NUM_MAX_EQUIVALENT_VLC_FILTERS);
            filter_types[j++] = i;
        }
    assert(j);
    while (j < NUM_MAX_EQUIVALENT_VLC_FILTERS)
        filter_types[j++] = FILTER_NONE;
}

static inline NSString *
filter_desc_table_GetFilterName(enum filter_type type)
{ assert(type < NUM_FILTERS);
    return filter_desc_table[type].name_desc.ci;
}

static float
filter_ConvertParam(float f_vlc_val,
                    struct filter_param_desc const *param_desc)
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
ParamsCallback(vlc_object_t *obj,
               char const *psz_var,
               vlc_value_t oldval, vlc_value_t newval,
               void *p_data)
{
    VLC_UNUSED(obj); VLC_UNUSED(oldval);
    struct filter_chain *filter = p_data;
    struct filter_param_desc const *filter_param_descs =
        filter_desc_table[filter->filter].param_descs;

    unsigned int i = 0;
    while (i < NUM_FILTER_PARAM_MAX &&
           strcmp(filter_param_descs[i].vlc, psz_var))
        ++i;
    assert(i < NUM_FILTER_PARAM_MAX);

    float new_vlc_val;
    if (filter_param_descs[i].vlc_type == VLC_VAR_FLOAT)
        new_vlc_val = newval.f_float;
    else if (filter_param_descs[i].vlc_type == VLC_VAR_INTEGER)
        new_vlc_val = newval.i_int;
    else
        assert(false);

    float new_ci_val =
        filter_ConvertParam(new_vlc_val, filter_param_descs + i); // rm var
    vlc_atomic_store_float(&filter->ci_params[i].value, new_ci_val);

    fprintf(stderr, "%s's %s: %f->%f\n",
            filter_desc_table[filter->filter].name_desc.vlc, filter_param_descs[i].vlc,
            new_vlc_val, new_ci_val);

    return VLC_SUCCESS;
}

static picture_t *
Filter(filter_t *filter, picture_t *src)
{
    struct ci_filters_ctx *ctx = filter->p_sys->ctx;
    enum filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS];

    filter_desc_table_GetFilterTypes(filter->p_sys->psz_filter, filter_types);
    if (ctx->fchain->filter != filter_types[0])
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
    ci_img = [CIImage imageWithCVImageBuffer : cvpxpic_get_ref(src)
                                     options : ci_img_opt_dict];
    if (!ci_img)
        goto error;

    for (struct filter_chain *fchain = ctx->fchain;
         fchain; fchain = fchain->next)
    {
        [fchain->ci_filter setValue: ci_img
                             forKey: kCIInputImageKey];

        if (fchain->filter == FILTER_PSYCHEDELIC)
        {
            [fchain->ci_filter setValue: [NSNumber numberWithFloat: .0f]
                                 forKey: @"inputAngle"];
            [fchain->ci_filter setValue: [CIVector vectorWithX: (CGFloat)(src->format.i_width / 2)
                                                             Y: (CGFloat)(src->format.i_height / 2)]
                                 forKey: @"inputCenter"];
            [fchain->ci_filter setValue: [NSNumber numberWithFloat: 12.f]
                                 forKey: @"inputCount"];
        }

        for (unsigned int i = 0; i < NUM_FILTER_PARAM_MAX &&
                 filter_desc_table[fchain->filter].param_descs[i].vlc; ++i)
        {
            NSString *ci_param_name =
                filter_desc_table[fchain->filter].param_descs[i].ci;
            float ci_value =
                vlc_atomic_load_float(&fchain->ci_params[i].value);

            [fchain->ci_filter setValue: [NSNumber numberWithFloat: ci_value]
                                 forKey: ci_param_name];
        }

        ci_img = [fchain->ci_filter valueForKey: kCIOutputImageKey];

        // fprintf(stderr, "FILTER: %s filter added to CoreImage filter chain\n",
        //         [filter_desc_table_GetFilterName(fchain->filter) UTF8String]);
    }

    [ctx->ci_ctx render: ci_img
            toIOSurface: CVPixelBufferGetIOSurface(cvpx)
                 bounds: [ci_img extent]
             colorSpace: nil];
    // fprintf(stderr, "FILTER: rendering CoreImage filter chain\n");

    return CopyInfoAndRelease(dst, src);

error:
    if (dst)
        picture_Release(dst);
    picture_Release(src);
    return NULL;
}

static void
filter_Init(vlc_object_t *obj, struct filter_chain *filter)
{
    struct filter_param_desc const *filter_param_descs =
        filter_desc_table[filter->filter].param_descs;
    NSString *ci_filter_name = filter_desc_table_GetFilterName(filter->filter);

    filter->ci_filter = [CIFilter filterWithName: ci_filter_name];

    for (NSString *param in [[filter->ci_filter attributes]allKeys])
        fprintf(stderr, "%s\n", [param UTF8String]);

    for (int i = 0; i < NUM_FILTER_PARAM_MAX && filter_param_descs[i].vlc; ++i)
    {
        NSString *ci_param_name = filter_param_descs[i].ci;
        NSDictionary<NSString *, id> *ci_param_attr_dict =
            [[filter->ci_filter attributes] valueForKey:ci_param_name];

        NSNumber *ci_min = [ci_param_attr_dict valueForKey : kCIAttributeMin];
        NSNumber *ci_max = [ci_param_attr_dict valueForKey : kCIAttributeMax];
        fprintf(stderr, "%s's %s ci api range: [%f;%f]\n",
                [ci_filter_name UTF8String], [ci_param_name UTF8String],
                [ci_min floatValue], [ci_max floatValue]);

        char const *vlc_param_name = filter_param_descs[i].vlc;
        float vlc_param_val;
        if (filter_param_descs[i].vlc_type == VLC_VAR_FLOAT)
            vlc_param_val = var_CreateGetFloatCommand(obj, vlc_param_name);
        else if (filter_param_descs[i].vlc_type == VLC_VAR_INTEGER)
            vlc_param_val =
                (float)var_CreateGetIntegerCommand(obj, vlc_param_name);
        else if (filter_param_descs[i].vlc_type == VLC_VAR_VOID)
            vlc_param_val = 1.f;
        else
            assert(false);

        vlc_atomic_init_float(&filter->ci_params[i].value,
                              filter_ConvertParam(vlc_param_val,
                                                  filter_param_descs + i));

        if (filter_param_descs[i].vlc_type != VLC_VAR_VOID)
        {
            var_AddCallback(obj, filter_param_descs[i].vlc,
                            ParamsCallback, filter);
            fprintf(stderr, "callback added on %s\n", filter_param_descs[i].vlc);
        }
    }
}

// static void
// filter_InitPsychedelic(vlc_object_t *obj, struct filter_chain *filter)
// {
//     struct filter_param_desc const *filter_param_descs =
//         filter_desc_table[filter->filter].param_descs;
//     NSString *ci_filter_name = filter_desc_table_GetFilterName(filter->filter);

//     filter->ci_filter = [CIFilter filterWithName: ci_filter_name];

//     for (NSString *param in [[filter->ci_filter attributes]allKeys])
//         fprintf(stderr, "%s\n", [param UTF8String]);

//     for (int i = 0; i < NUM_FILTER_PARAM_MAX && filter_param_descs[i].vlc; ++i)
//     {
//         NSString *ci_param_name = filter_param_descs[i].ci;
//         NSDictionary<NSString *, id> *ci_param_attr_dict =
//             [[filter->ci_filter attributes] valueForKey:ci_param_name];

//         NSNumber *ci_min = [ci_param_attr_dict valueForKey : kCIAttributeMin];
//         NSNumber *ci_max = [ci_param_attr_dict valueForKey : kCIAttributeMax];
//         fprintf(stderr, "%s's %s ci api range: [%f;%f]\n",
//                 [ci_filter_name UTF8String], [ci_param_name UTF8String],
//                 [ci_min floatValue], [ci_max floatValue]);

//         char const *vlc_param_name = filter_param_descs[i].vlc;
//         float vlc_param_val;
//         if (filter_param_descs[i].vlc_type == VLC_VAR_FLOAT)
//             vlc_param_val = var_CreateGetFloatCommand(obj, vlc_param_name);
//         else if (filter_param_descs[i].vlc_type == VLC_VAR_INTEGER)
//             vlc_param_val =
//                 (float)var_CreateGetIntegerCommand(obj, vlc_param_name);
//         else if (filter_param_descs[i].vlc_type == VLC_VAR_VOID)
//             vlc_param_val = 1.f;
//         else
//             assert(false);

//         vlc_atomic_init_float(&filter->ci_params[i].value,
//                               filter_ConvertParam(vlc_param_val,
//                                                   filter_param_descs + i));

//         if (filter_param_descs[i].vlc_type != VLC_VAR_VOID)
//         {
//             var_AddCallback(obj, filter_param_descs[i].vlc,
//                             ParamsCallback, filter);
//             fprintf(stderr, "callback added on %s\n", filter_param_descs[i].vlc);
//         }
//     }
// }

static int
filter_CreateFilters
(vlc_object_t *obj, struct filter_chain **p_last_filter,
 enum filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS])
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
        filter_Init(obj, new_filter);
    }

    return VLC_SUCCESS;
}

static int
Open(vlc_object_t *obj, char const *psz_filter)
{
    filter_t *filter = (filter_t *)obj;
    filter->p_sys = calloc(1, sizeof(filter_sys_t));
    if (!filter->p_sys)
        return VLC_ENOMEM;

    enum filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS];
    filter_desc_table_GetFilterTypes(psz_filter, filter_types);

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

        ctx->ci_ctx = [CIContext contextWithCGLContext : [context CGLContextObj]
                                           pixelFormat : [[context pixelFormat] CGLPixelFormatObj]
                                            colorSpace : nil
                                               options : nil];
        if (!ctx->ci_ctx)
            goto error;

        if (filter_CreateFilters(obj, &ctx->fchain, filter_types))
            goto error;

        var_Create(filter->obj.parent, "ci-filters-ctx", VLC_VAR_ADDRESS);
        var_SetAddress(filter->obj.parent, "ci-filters-ctx", ctx);
    }
    else if (filter_CreateFilters(obj, &ctx->fchain, filter_types))
        goto error;

    filter->p_sys->psz_filter = psz_filter;
    filter->p_sys->ctx = ctx;

    fprintf(stderr, "OPEN: %s filter added to chain\n", psz_filter);
    fprintf(stderr, "%s\n", [[[CIFilter filterNamesInCategory : kCICategoryGradient] componentsJoinedByString : @";"] UTF8String]);

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
OpenSepia(vlc_object_t *obj)
{
    return Open(obj, "sepia");
}

static int
OpenSharpen(vlc_object_t *obj)
{
    return Open(obj, "sharpen");
}

static int
OpenPsychedelic(vlc_object_t *obj)
{
    return Open(obj, "psychedelic");
}

static void
Close(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *)obj;
    struct ci_filters_ctx *ctx = filter->p_sys->ctx;
    enum filter_type filter_types[NUM_MAX_EQUIVALENT_VLC_FILTERS];

    filter_desc_table_GetFilterTypes(filter->p_sys->psz_filter, filter_types);
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
