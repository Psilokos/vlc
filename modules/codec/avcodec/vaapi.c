/*****************************************************************************
 * vaapi.c: VAAPI helpers for the libavcodec decoder
 *****************************************************************************
 * Copyright (C) 2017 VLC authors and VideoLAN
 * Copyright (C) 2009-2010 Laurent Aimar
 * Copyright (C) 2012-2014 Rémi Denis-Courmont
 *
 * Authors: Laurent Aimar <fenrir_AT_ videolan _DOT_ org>
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
#include <vlc_plugin.h>
#include <vlc_fourcc.h>
#include <vlc_picture.h>

#ifdef VLC_VA_BACKEND_XLIB
# include <vlc_xlib.h>
# include <va/va_x11.h>
#endif
#ifdef VLC_VA_BACKEND_DRM
# include <sys/types.h>
# include <sys/stat.h>
# include <fcntl.h>
# include <vlc_fs.h>
# include <va/va_drm.h>
#endif
#include <libavcodec/avcodec.h>
#include <libavcodec/vaapi.h>

#include "avcodec.h"
#include "va.h"

#include "../../hw/vaapi/vlc_vaapi.h"

#ifndef VA_SURFACE_ATTRIB_SETTABLE
#define vaCreateSurfaces(d, f, w, h, s, ns, a, na) \
    vaCreateSurfaces(d, w, h, f, ns, s)
#endif

#ifndef VLC_VA_BACKEND_DR /* XLIB or DRM */
struct pic_ctx
{
    struct vlc_vaapi_pic_ctx s;
    void *priv;
    unsigned idx;
};
#endif

struct vlc_va_sys_t
{
#ifdef VLC_VA_BACKEND_XLIB
    Display  *p_display_x11;
#endif
#ifdef VLC_VA_BACKEND_DRM
    int       drm_fd;
#endif
    struct vaapi_context hw_ctx;

#ifndef VLC_VA_BACKEND_DR /* XLIB or DRM */
    /* */
    vlc_mutex_t  lock;
    vlc_cond_t   cond;

    uint8_t      count;
    uint32_t     available;
    bool         delete;
    VASurfaceID  surfaces[32];
    struct pic_ctx pic_ctxs[32];
#endif
};

static int GetVaProfile(AVCodecContext *ctx, VAProfile *va_profile,
                      unsigned *pic_count)
{
    VAProfile i_profile;
    unsigned count = 3;

    switch(ctx->codec_id)
    {
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        i_profile = VAProfileMPEG2Main;
        count = 4;
        break;
    case AV_CODEC_ID_MPEG4:
        i_profile = VAProfileMPEG4AdvancedSimple;
        break;
    case AV_CODEC_ID_WMV3:
        i_profile = VAProfileVC1Main;
        break;
    case AV_CODEC_ID_VC1:
        i_profile = VAProfileVC1Advanced;
        break;
    case AV_CODEC_ID_H264:
        i_profile = VAProfileH264High;
        count = 18;
        break;
    case AV_CODEC_ID_HEVC:
        if (ctx->profile == FF_PROFILE_HEVC_MAIN)
            i_profile = VAProfileHEVCMain;
        else if (ctx->profile == FF_PROFILE_HEVC_MAIN_10)
            i_profile = VAProfileHEVCMain10;
        else
            return VLC_EGENERIC;
        count = 18;
        break;
    case AV_CODEC_ID_VP8:
        i_profile = VAProfileVP8Version0_3;
        count = 5;
        break;
    case AV_CODEC_ID_VP9:
        if (ctx->profile == FF_PROFILE_VP9_0)
            i_profile = VAProfileVP9Profile0;
#if VA_CHECK_VERSION( 0, 39, 0 )
        else if (ctx->profile == FF_PROFILE_VP9_2)
            i_profile = VAProfileVP9Profile2;
#endif
        else
            return VLC_EGENERIC;
        count = 10;
        break;
    default:
        return VLC_EGENERIC;
    }

    *va_profile = i_profile;
    *pic_count = count + ctx->thread_count;
    return VLC_SUCCESS;
}

static bool IsVaProfileSupported(VADisplay dpy, VAProfile i_profile)
{
    /* Check if the selected profile is supported */
    int i_profiles_nb = vaMaxNumProfiles(dpy);
    if (i_profiles_nb < 0)
        return false;
    VAProfile *p_profiles_list = calloc(i_profiles_nb, sizeof(VAProfile));
    if (!p_profiles_list)
        return false;

    bool b_supported_profile = false;
    if (vaQueryConfigProfiles(dpy, p_profiles_list,
                              &i_profiles_nb) == VA_STATUS_SUCCESS)
    {
        for (int i = 0; i < i_profiles_nb; i++)
        {
            if (p_profiles_list[i] == i_profile)
            {
                b_supported_profile = true;
                break;
            }
        }
    }
    free(p_profiles_list);
    return b_supported_profile;
}

static VAConfigID CreateVaConfig(VADisplay dpy, VAProfile i_profile)
{
    /* Create a VA configuration */
    VAConfigAttrib attrib = {
        .type = VAConfigAttribRTFormat,
    };
    if (vaGetConfigAttributes(dpy, i_profile, VAEntrypointVLD, &attrib, 1))
        return VA_INVALID_ID;

    /* Not sure what to do if not, I don't have a way to test */
    if ((attrib.value & VA_RT_FORMAT_YUV420) == 0)
        return VA_INVALID_ID;

    VAConfigID va_config_id;
    if (vaCreateConfig(dpy, i_profile, VAEntrypointVLD, &attrib, 1,
                       &va_config_id))
        return VA_INVALID_ID;
    return va_config_id;
}

static int Extract(vlc_va_t *va, picture_t *pic, uint8_t *data)
{
    (void) va; (void) pic; (void) data;
    return VLC_SUCCESS;
}

#ifdef VLC_VA_BACKEND_DR

static int GetDR(vlc_va_t *va, picture_t *pic, uint8_t **data)
{
    (void) va;
    *data = (void *) (uintptr_t) vlc_vaapi_PicGetSurface(pic);

    return VLC_SUCCESS;
}

static void DeleteDR(vlc_va_t *va, AVCodecContext *avctx)
{
    vlc_va_sys_t *sys = va->sys;

    (void) avctx;

    vaDestroyContext(sys->hw_ctx.display, sys->hw_ctx.context_id);
    vaDestroyConfig(sys->hw_ctx.display, sys->hw_ctx.config_id);
    vlc_vaapi_ReleaseInstance(sys->hw_ctx.display);
    free(sys);
}

static int CreateDR(vlc_va_t *va, AVCodecContext *ctx, enum PixelFormat pix_fmt,
                        const es_format_t *fmt, picture_sys_t *p_sys)
{
    if (pix_fmt != AV_PIX_FMT_VAAPI_VLD)
        return VLC_EGENERIC;

    (void) fmt;
    (void) p_sys;

    int ret = VLC_EGENERIC;
    vlc_va_sys_t *sys = NULL;

    /* The picture must be allocated by the vout */
    VADisplay *va_dpy = vlc_vaapi_GetInstance();
    if (va_dpy == NULL)
        return VLC_EGENERIC;

    VASurfaceID *render_targets;
    unsigned num_render_targets =
        vlc_vaapi_GetRenderTargets(va_dpy, &render_targets);
    if (num_render_targets == 0)
        goto error;

    VAProfile i_profile;
    unsigned count;
    if (GetVaProfile(ctx, &i_profile, &count) != VLC_SUCCESS)
        goto error;

    sys = malloc(sizeof *sys);
    if (unlikely(sys == NULL))
    {
        ret = VLC_ENOMEM;
        goto error;
    }
    memset(sys, 0, sizeof (*sys));

    /* */
    sys->hw_ctx.display = va_dpy;
    sys->hw_ctx.config_id = VA_INVALID_ID;
    sys->hw_ctx.context_id = VA_INVALID_ID;

    if (!IsVaProfileSupported(sys->hw_ctx.display, i_profile))
    {
        msg_Dbg(va, "Codec and profile not supported by the hardware");
        goto error;
    }

    sys->hw_ctx.config_id = CreateVaConfig(sys->hw_ctx.display, i_profile);
    if (sys->hw_ctx.config_id == VA_INVALID_ID)
        goto error;

    /* Create a context */
    if (vaCreateContext(sys->hw_ctx.display, sys->hw_ctx.config_id,
                        ctx->coded_width, ctx->coded_height, VA_PROGRESSIVE,
                        render_targets, num_render_targets,
                        &sys->hw_ctx.context_id))
    {
        sys->hw_ctx.context_id = VA_INVALID_ID;
        goto error;
    }

    ctx->hwaccel_context = &sys->hw_ctx;
    va->sys = sys;
    va->description = vaQueryVendorString(sys->hw_ctx.display);
    va->get = GetDR;
    va->release = NULL;
    va->extract = Extract;
    return VLC_SUCCESS;

error:
    if (sys != NULL)
    {
        if (sys->hw_ctx.context_id != VA_INVALID_ID)
            vaDestroyContext(sys->hw_ctx.display, sys->hw_ctx.context_id);
        if (sys->hw_ctx.config_id != VA_INVALID_ID)
            vaDestroyConfig(sys->hw_ctx.display, sys->hw_ctx.config_id);
        free(sys);
    }
    vlc_vaapi_ReleaseInstance(va_dpy);
    return ret;
}

#else /* XLIB or DRM */

static int Get( vlc_va_t *va, picture_t *pic, uint8_t **data )
{
    vlc_va_sys_t *sys = va->sys;

    vlc_mutex_lock( &sys->lock );
    while (!sys->available)
        vlc_cond_wait(&sys->cond, &sys->lock);

    unsigned i = ctz(sys->available);
    sys->available &= ~(1 << i);

    vlc_mutex_unlock( &sys->lock );

    pic->context = &sys->pic_ctxs[i];
    *data = (void *)(uintptr_t)sys->pic_ctxs[i].s.surface;
    return VLC_SUCCESS;
}

static void DeleteSysLocked(vlc_va_sys_t *sys)
{
    vlc_mutex_unlock(&sys->lock);

    vlc_mutex_destroy(&sys->lock);
    vlc_cond_destroy(&sys->cond);

    vaDestroyContext(sys->hw_ctx.display, sys->hw_ctx.context_id);
    vaDestroySurfaces(sys->hw_ctx.display, sys->surfaces, sys->count);
    vaDestroyConfig(sys->hw_ctx.display, sys->hw_ctx.config_id);
    vlc_vaapi_ReleaseInstance(sys->hw_ctx.display);
#ifdef VLC_VA_BACKEND_XLIB
    XCloseDisplay(sys->p_display_x11);
#endif
#ifdef VLC_VA_BACKEND_DRM
    vlc_close(sys->drm_fd);
#endif
    free(sys);
}

static void PicContextDestroyCb(void *opaque)
{
    struct pic_ctx *ctx = opaque;
    vlc_va_sys_t *sys = ctx->priv;

    unsigned i = ctx->idx;
    vlc_mutex_lock(&sys->lock);
    assert(((sys->available >> i) & 1) == 0);
    sys->available |= 1 << i;
    vlc_cond_signal(&sys->cond);
    if (sys->available == (uint32_t)((1 << sys->count) - 1) && sys->delete)
        DeleteSysLocked(sys);
    else
        vlc_mutex_unlock( &sys->lock );
}

static void Delete( vlc_va_t *va, AVCodecContext *avctx )
{
    vlc_va_sys_t *sys = va->sys;

    (void) avctx;
    vlc_mutex_lock(&sys->lock);
    if (sys->available == (uint32_t)((1 << sys->count) - 1))
        DeleteSysLocked(sys);
    else
    {
        /* Really Delete when all pictures are released */
        sys->delete = true;
        vlc_mutex_unlock(&sys->lock);
    }
}

static int Create( vlc_va_t *va, AVCodecContext *ctx, enum PixelFormat pix_fmt,
                   const es_format_t *fmt, picture_sys_t *p_sys )
{
    if( pix_fmt != AV_PIX_FMT_VAAPI_VLD )
        return VLC_EGENERIC;

    (void) fmt;
    (void) p_sys;

    VADisplay vout_dpy = vlc_vaapi_GetInstance();
    if (vout_dpy != NULL)
    {
        /* There is a vout handling vaapi surfaces but the dr va failed, we
         * need to fail in order to fallback to a sw decoder */
        vlc_vaapi_ReleaseInstance(vout_dpy);
        return VLC_EGENERIC;
    }

#ifdef VLC_VA_BACKEND_XLIB
    if( !vlc_xlib_init( VLC_OBJECT(va) ) )
    {
        msg_Warn( va, "Ignoring VA-X11 API" );
        return VLC_EGENERIC;
    }
#endif

    VAProfile i_profile;
    unsigned count;
    if (GetVaProfile(ctx, &i_profile, &count) != VLC_SUCCESS)
        return VLC_EGENERIC;

    vlc_va_sys_t *sys;

    sys = malloc(sizeof(vlc_va_sys_t));
    if (!sys)
       return VLC_ENOMEM;
    memset(sys, 0, sizeof (*sys));

    /* */
    sys->hw_ctx.display = NULL;
    sys->hw_ctx.config_id = VA_INVALID_ID;
    sys->hw_ctx.context_id = VA_INVALID_ID;
    sys->count = count;
    sys->available = (1 << sys->count) - 1;
    assert(count < sizeof (sys->available) * CHAR_BIT);

    /* Create a VA display */
#ifdef VLC_VA_BACKEND_XLIB
    sys->p_display_x11 = XOpenDisplay(NULL);
    if( !sys->p_display_x11 )
    {
        msg_Err( va, "Could not connect to X server" );
        goto error;
    }

    sys->hw_ctx.display = vaGetDisplay(sys->p_display_x11);
#endif
#ifdef VLC_VA_BACKEND_DRM
    static const char const *drm_device_paths[] = {
        "/dev/dri/renderD128",
        "/dev/dri/card0"
    };

    for (int i = 0; ARRAY_SIZE(drm_device_paths); i++) {
        sys->drm_fd = vlc_open(drm_device_paths[i], O_RDWR);
        if (sys->drm_fd < 0)
            continue;

        sys->hw_ctx.display = vaGetDisplayDRM(sys->drm_fd);
        if (sys->hw_ctx.display)
            break;

        vlc_close(sys->drm_fd);
        sys->drm_fd = -1;
    }
#endif
    if (sys->hw_ctx.display == NULL)
    {
        msg_Err( va, "Could not get a VAAPI device" );
        goto error;
    }

    int major, minor;
    if (vaInitialize(sys->hw_ctx.display, &major, &minor))
    {
        msg_Err( va, "Failed to initialize the VAAPI device" );
        goto error;
    }

    if (!IsVaProfileSupported(sys->hw_ctx.display, i_profile))
    {
        msg_Dbg( va, "Codec and profile not supported by the hardware" );
        goto error;
    }

    sys->hw_ctx.config_id = CreateVaConfig(sys->hw_ctx.display, i_profile);
    if (sys->hw_ctx.config_id == VA_INVALID_ID)
        goto error;

    /* Create surfaces */
    assert(ctx->coded_width > 0 && ctx->coded_height > 0);
    if (vaCreateSurfaces(sys->hw_ctx.display, VA_RT_FORMAT_YUV420,
                         ctx->coded_width, ctx->coded_height,
                         sys->surfaces, sys->count, NULL, 0))
        goto error;

    /* Create a context */
    if (vaCreateContext(sys->hw_ctx.display, sys->hw_ctx.config_id,
                        ctx->coded_width, ctx->coded_height, VA_PROGRESSIVE,
                        sys->surfaces, sys->count, &sys->hw_ctx.context_id))
    {
        sys->hw_ctx.context_id = VA_INVALID_ID;
        vaDestroySurfaces(sys->hw_ctx.display, sys->surfaces, sys->count);
        goto error;
    }

    for (unsigned i = 0; i < count; ++i)
    {
        sys->pic_ctxs[i].s.destroy = PicContextDestroyCb;
        sys->pic_ctxs[i].s.surface = sys->surfaces[i];
        sys->pic_ctxs[i].priv = sys;
        sys->pic_ctxs[i].idx = i;
    }

    if (vlc_vaapi_SetInstance(sys->hw_ctx.display))
    {
        msg_Err(va, "VAAPI instance already in use");
        goto error;
    }

    vlc_mutex_init(&sys->lock);
    vlc_cond_init(&sys->cond);

    ctx->hwaccel_context = &sys->hw_ctx;
    va->sys = sys;
    va->description = vaQueryVendorString(sys->hw_ctx.display);
    va->get = Get;
    va->release = NULL;
    va->extract = Extract;
    return VLC_SUCCESS;

error:
    if (sys->hw_ctx.context_id != VA_INVALID_ID)
    {
        vaDestroyContext(sys->hw_ctx.display, sys->hw_ctx.context_id);
        vaDestroySurfaces(sys->hw_ctx.display, sys->surfaces, sys->count);
    }
    if (sys->hw_ctx.config_id != VA_INVALID_ID)
        vaDestroyConfig(sys->hw_ctx.display, sys->hw_ctx.config_id);
    if (sys->hw_ctx.display != NULL)
        vaTerminate(sys->hw_ctx.display);
#ifdef VLC_VA_BACKEND_XLIB
    if( sys->p_display_x11 != NULL )
        XCloseDisplay( sys->p_display_x11 );
#endif
#ifdef VLC_VA_BACKEND_DRM
    if( sys->drm_fd != -1 )
        vlc_close( sys->drm_fd );
#endif
    free( sys );
    return VLC_EGENERIC;
}
#endif

vlc_module_begin ()
#if defined (VLC_VA_BACKEND_XLIB)
    set_description( N_("VA-API video decoder via X11") )
    set_capability( "hw decoder", 0 )
    set_callbacks( Create, Delete )
#elif defined (VLC_VA_BACKEND_DRM)
    set_description( N_("VA-API video decoder via DRM") )
    set_capability( "hw decoder", 0 )
    set_callbacks( Create, Delete )
#elif defined (VLC_VA_BACKEND_DR)
    set_description( N_("VA-API direct video decoder") )
    set_capability( "hw decoder", 100 )
    set_callbacks( CreateDR, DeleteDR )
#endif
    set_category( CAT_INPUT )
    set_subcategory( SUBCAT_INPUT_VCODEC )
    add_shortcut( "vaapi" )
vlc_module_end ()
