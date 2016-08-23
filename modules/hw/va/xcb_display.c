/*****************************************************************************
 * xcb_display.c: VAAPI XCB display
 *****************************************************************************
 * Copyright (C) 2016 VLC authors and VideoLAN
 *
 * Authors: Petri Hintukainen <phintuka@gmail.com>
 *          Rémi Denis-Courmont
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
# include <config.h>
#endif

#include <stdlib.h>
#include <assert.h>

#include <xcb/xcb.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>
#include <vlc_picture_pool.h>
#include <vlc_xlib.h>

#include "events.h"

#include <va/va.h>
#include <va/va_x11.h>

#include "vlc_va.h"

static int Open(vlc_object_t *);
static void Close(vlc_object_t *);

vlc_module_begin()
    set_shortname(N_("VAAPI XCB"))
    set_description(N_("VA-API video output (XCB)"))
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("vout display", 3000)
    set_callbacks(Open, Close)

    add_shortcut("vaapi", "xid")
vlc_module_end()

struct vout_display_sys_t
{
    /* XCB */
    vout_window_t     *embed;  /**< parent window */
    xcb_connection_t  *conn;   /**< XCB connection */
    xcb_window_t       window; /**< target window */
    xcb_cursor_t       cursor; /**< blank cursor */

    Display           *dpy;    /**< X11 display */

    picture_pool_t    *pool;

    /* VAAPI */
    VADisplay          va_dpy;
    VAImageFormat      va_image_format;
    VAImageFormat      va_spu_format;
    unsigned int       va_rt_format;

    vlc_va_subpicture *spu;
};

static void DestroySubpicture(vout_display_sys_t *sys)
{
    if (sys->spu) {
        vlc_va_SubpictureDestroy(sys->va_dpy, sys->spu);
        sys->spu = NULL;
    }
}

static picture_pool_t *Pool(vout_display_t *vd, unsigned requested_count)
{
    vout_display_sys_t *sys = vd->sys;

    if (sys->pool == NULL) {
        sys->pool = vlc_va_PoolAlloc(VLC_OBJECT(vd), sys->va_dpy,
                                       requested_count, &vd->fmt, sys->va_rt_format);
    }
    return sys->pool;
}

static void Prepare(vout_display_t *vd, picture_t *pic, subpicture_t *subpicture)
{
    vout_display_sys_t *sys = vd->sys;
    picture_sys_t *picsys = pic->p_sys;
    VAStatus status;

    if (!subpicture || !subpicture->p_region) {
        DestroySubpicture(sys);
        return;
    }

    if (!sys->spu) {
        sys->spu = vlc_va_SubpictureNew();
    }

    if (sys->spu) {
        status = vlc_va_SubpictureUpdate(VLC_OBJECT(vd), sys->va_dpy,
                                         &sys->va_spu_format, sys->spu, subpicture);
        if (status != VA_STATUS_SUCCESS) {
            DestroySubpicture(sys);
        }
    }

    /* associate subpicture */
    if (sys->spu) {
        int d_x = sys->spu->place.x * pic->format.i_visible_width  / subpicture->i_original_picture_width;
        int d_y = sys->spu->place.y * pic->format.i_visible_height / subpicture->i_original_picture_height;
        int d_w = sys->spu->place.w * pic->format.i_visible_width  / subpicture->i_original_picture_width;
        int d_h = sys->spu->place.h * pic->format.i_visible_height / subpicture->i_original_picture_height;

        status = vaAssociateSubpicture(sys->va_dpy,
                                       sys->spu->va_subpicture_id,
                                       &picsys->va_surface_id, 1,

                                       0, 0,
                                       sys->spu->place.w, sys->spu->place.h,

                                       d_x, d_y, d_w, d_h,
                                       0);
        if (status != VA_STATUS_SUCCESS) {
            msg_Err(vd, "vaAassociateSubpicture failed: %d", status);
        }
    }
}

static void vaapiDisplay(vout_display_t *vd, picture_t *pic, subpicture_t *subpicture)
{
    vout_display_sys_t *sys = vd->sys;
    picture_sys_t *picsys = pic->p_sys;
    video_format_t *fmt = &vd->fmt;
    VAStatus status;
    unsigned int flags = 0;
    int va_rotation = VA_ROTATION_NONE;

    flags |= VA_CLEAR_DRAWABLE;
    flags |= VA_FRAME_PICTURE;
    flags |= VA_FILTER_SCALING_HQ;

    /* CSC */
    switch (fmt->space)
    {
        case COLOR_SPACE_BT601:
            flags |= VA_SRC_BT601;
            break;
        case COLOR_SPACE_BT709:
            flags |= VA_SRC_BT709;
            break;
        default:
            if (fmt->i_height >= 720) {
                flags |= VA_SRC_BT709;
            } else {
                flags |= VA_SRC_BT601;
            }
    }

    /* Set rotation. Ignore here (logged in Open) */
    vlc_va_OrientationToVaRotation(pic->format.orientation, &va_rotation);
    vlc_va_SetDisplayAttribute(sys->va_dpy, VADisplayAttribRotation, va_rotation);

    /* render picture to output */

    const video_format_t *src = &vd->source;
    vout_display_place_t place;

    vout_display_PlacePicture(&place, src, vd->cfg, false);

    int i_visible_width  = place.width;
    int i_visible_height = place.height;
    int i_x_offset = src->i_x_offset * place.width / src->i_visible_width;
    int i_y_offset = src->i_y_offset * place.height / src->i_visible_height;

    status = vaPutSurface(sys->va_dpy, picsys->va_surface_id,
                          sys->window,

                          0, 0,
                          pic->format.i_visible_width, pic->format.i_visible_height,

                          i_x_offset, i_y_offset,
                          i_visible_width, i_visible_height,

                          NULL, 0, flags);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(vd, "vaPutSurface failed: %d", status);
    }

    status = vaSyncSurface(sys->va_dpy, picsys->va_surface_id);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(vd, "vaSyncSurface failed: %d", status);
    }

    if (sys->spu) {
        vaDeassociateSubpicture(sys->va_dpy, sys->spu->va_subpicture_id,
                                &picsys->va_surface_id, 1);
    }

    if (subpicture) {
        subpicture_Delete(subpicture);
    }

    picture_Release(pic);
}

static void ConfigureWindow(vout_display_sys_t *sys,
                            const video_format_t *source,
                            const vout_display_cfg_t *cfg)
{
    vout_display_place_t place;
    vout_display_PlacePicture(&place, source, cfg, false);

    const uint32_t values[] = {place.x, place.y,
                               place.width, place.height};
    xcb_configure_window(sys->conn, sys->window,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y |
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                         values);
}

static int Control(vout_display_t *vd, int query, va_list ap)
{
    vout_display_sys_t *sys = vd->sys;

    switch (query)
    {
        case VOUT_DISPLAY_HIDE_MOUSE:
            xcb_change_window_attributes(sys->conn, sys->embed->handle.xid,
                                         XCB_CW_CURSOR, &(uint32_t){ sys->cursor });
            break;

        case VOUT_DISPLAY_CHANGE_DISPLAY_SIZE:
        case VOUT_DISPLAY_CHANGE_DISPLAY_FILLED:
        case VOUT_DISPLAY_CHANGE_ZOOM:
        {
            const vout_display_cfg_t *cfg = va_arg(ap, const vout_display_cfg_t *);
            ConfigureWindow(sys, &vd->source, cfg);
            break;
        }

        case VOUT_DISPLAY_CHANGE_SOURCE_ASPECT:
        case VOUT_DISPLAY_CHANGE_SOURCE_CROP:
        {
            const video_format_t *source = va_arg (ap, const video_format_t *);
            ConfigureWindow(sys, source, vd->cfg);
            break;
        }

        case VOUT_DISPLAY_RESET_PICTURES:
            vlc_assert_unreachable();
        default:
            msg_Err(vd, "unknown control request %d", query);
            return VLC_EGENERIC;
    }

    xcb_flush(sys->conn);
    return VLC_SUCCESS;
}

static void Manage(vout_display_t *vd)
{
    vout_display_sys_t *sys = vd->sys;
    bool visible;

    XCB_Manage(vd, sys->conn, &visible);
}

static int vlc_va_InitializeX11(vout_display_t *vd, vout_display_sys_t *sys)
{
    VAStatus status;

    sys->dpy = XOpenDisplay(sys->embed->display.x11);
    if (!sys->dpy) {
        return VA_STATUS_ERROR_UNKNOWN;
    }

    sys->va_dpy = vaGetDisplay(sys->dpy);
    if (!sys->va_dpy) {
        status = VA_STATUS_ERROR_UNKNOWN;
        goto error;
    }

    status = vlc_va_Initialize(VLC_OBJECT(vd), sys->va_dpy);
    if (status != VA_STATUS_SUCCESS) {
        goto error;
    }

    return VA_STATUS_SUCCESS;

 error:
    if (sys->va_dpy) {
        vlc_va_Terminate(sys->va_dpy);
    }
    XCloseDisplay(sys->dpy);
    sys->va_dpy = NULL;
    sys->dpy = NULL;
    return status;
}

static int Open(vlc_object_t *obj)
{
    vout_display_t *vd = (vout_display_t *)obj;
    video_format_t fmt = vd->fmt;
    vout_display_sys_t *sys;
    const xcb_screen_t *screen;
    VAStatus status;

    if (!vlc_xlib_init(obj)) {
        return VLC_EGENERIC;
    }

    sys = calloc(1, sizeof (*sys));
    if (!sys) {
        return VLC_ENOMEM;
    }

    sys->embed = XCB_parent_Create(vd, &sys->conn, &screen);
    if (!sys->embed) {
        free(sys);
        return VLC_EGENERIC;
    }

    status = vlc_va_InitializeX11(vd, sys);
    if (status != VA_STATUS_SUCCESS) {
        goto error;
    }

    /* Check source format */

    unsigned int va_fourcc;
    status = vlc_va_VaFourcc(fmt.i_chroma, &va_fourcc, &sys->va_rt_format);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(vd, "unsupported vlc fourcc: %4.4s", (const char *)&fmt.i_chroma);
        goto error;
    }
    msg_Dbg(vd, "VLC %4.4s mapped to VAAPI %4.4s (rt %d)",
            (const char *)&fmt.i_chroma, (const char *)&va_fourcc, sys->va_rt_format);

    status = vlc_va_FindImageFormat(sys->va_dpy, &sys->va_image_format, va_fourcc, 0);
    if (status != VA_STATUS_SUCCESS) {
        msg_Err(vd, "VAAPI image format for %4.4s not found", (const char *)&fmt.i_chroma);
        goto error;
    }

    /* accept only VAAPI surfaces */
    fmt.i_chroma = VLC_CODEC_VAAPI_OPAQUE;

    /* create a window dedicated to the back-end */
    {
        xcb_pixmap_t pix = xcb_generate_id(sys->conn);
        xcb_create_pixmap(sys->conn, screen->root_depth, pix,
                          screen->root, 1, 1);

        uint32_t mask =
            XCB_CW_BACK_PIXMAP | XCB_CW_BACK_PIXEL |
            XCB_CW_BORDER_PIXMAP | XCB_CW_BORDER_PIXEL |
            XCB_CW_EVENT_MASK | XCB_CW_COLORMAP;
        const uint32_t values[] = {
            pix, screen->black_pixel, pix, screen->black_pixel,
            XCB_EVENT_MASK_VISIBILITY_CHANGE, screen->default_colormap
        };
        vout_display_place_t place;

        vout_display_PlacePicture (&place, &vd->source, vd->cfg, false);
        sys->window = xcb_generate_id(sys->conn);

        xcb_void_cookie_t c =
            xcb_create_window_checked(sys->conn, screen->root_depth,
                sys->window, sys->embed->handle.xid, place.x, place.y,
                place.width, place.height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                screen->root_visual, mask, values);
        if (XCB_error_Check(vd, sys->conn, "window creation failure", c))
            goto error;
        msg_Dbg(vd, "using X11 window 0x%08"PRIx32, sys->window);
        xcb_map_window(sys->conn, sys->window);
    }

    /* Check subpicture format */
    const vlc_fourcc_t *spu_chromas = NULL;
#ifdef WORDS_BIGENDIAN
    static const vlc_fourcc_t subpicture_chromas[] = { VLC_CODEC_ARGB, 0 };
    status = vlc_va_FindImageFormat(sys->va_dpy, &sys->va_spu_format, VA_FOURCC_ARGB, 1);
    if (status == VA_STATUS_SUCCESS) {
        spu_chromas = subpicture_chromas;
    }
#else
    static const vlc_fourcc_t subpicture_chromas[] = { VLC_CODEC_RGBA, 0 };
    status = vlc_va_FindImageFormat(sys->va_dpy, &sys->va_spu_format, VA_FOURCC_RGBA, 1);
    if (status == VA_STATUS_SUCCESS) {
        spu_chromas = subpicture_chromas;
    }
#endif

    /*
      TODO: add HW SPU format (or add b_changed flag to subpicture_t ?)
      => avoid uploading unchanged overlay every time frame is rendered

      static const vlc_fourcc_t subpicture_chromas_GPU[] = { VLC_CODEC_VAAPI_SPU, 0 };
      spu_chromas = subpicture_chromas_GPU;
    */

    /* check rotation support */
    if (fmt.orientation != ORIENT_NORMAL) {
        int va_rotation = VA_ROTATION_NONE;
        status = vlc_va_OrientationToVaRotation(fmt.orientation, &va_rotation);
        if (status != VA_STATUS_SUCCESS) {
            msg_Err(vd, "Unsupported video orientation %d", fmt.orientation);
            fmt.orientation = ORIENT_NORMAL;
        } else if (vlc_va_SetDisplayAttribute(sys->va_dpy, VADisplayAttribRotation, va_rotation)
                   != VA_STATUS_SUCCESS) {
            msg_Err(vd, "HW does not support video orientation %d", fmt.orientation);
            fmt.orientation = ORIENT_NORMAL;
        }
    }

    sys->cursor = XCB_cursor_Create(sys->conn, screen);
    sys->pool = NULL;

    /* */
    vd->sys = sys;
    vd->info.has_pictures_invalid = true;
    vd->info.has_event_thread = true;
    vd->info.subpicture_chromas = spu_chromas;
    vd->fmt = fmt;

    vd->pool = Pool;
    vd->prepare = Prepare;
    vd->display = vaapiDisplay;
    vd->control = Control;
    vd->manage = Manage;

    return VLC_SUCCESS;

error:
    vlc_va_Terminate(sys->va_dpy);
    xcb_disconnect(sys->conn);
    vout_display_DeleteWindow(vd, sys->embed);
    free(sys);
    return VLC_EGENERIC;
}

static void Close(vlc_object_t *obj)
{
    vout_display_t *vd = (vout_display_t *)obj;
    vout_display_sys_t *sys = vd->sys;

    DestroySubpicture(sys);

    /* Restore cursor explicitly (parent window connection will survive) */
    xcb_change_window_attributes(sys->conn, sys->embed->handle.xid,
                                 XCB_CW_CURSOR, &(uint32_t) { XCB_CURSOR_NONE });
    xcb_flush(sys->conn);

    if (sys->pool) {
        picture_pool_Release(sys->pool);
    }

    if (sys->va_dpy) {
        vlc_va_Terminate(sys->va_dpy);
    }

    if (sys->dpy) {
        XCloseDisplay(sys->dpy);
    }

    xcb_disconnect(sys->conn);
    vout_display_DeleteWindow(vd, sys->embed);
    free(sys);
}
