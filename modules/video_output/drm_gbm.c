/**
 * @file drm_gbm.c
 * @brief DRM-GBM surface provider module for VLC media player
 */
/*****************************************************************************
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
# include <config.h>
#endif

#include <fcntl.h>
#include <errno.h>

#include <vlc_common.h>
#include <vlc_fs.h>
#include <vlc_plugin.h>
#include <vlc_vout_window.h>

#include "drm.h"

/* Background thread for event handling */
static void *
Thread(void *data)
{
    vout_window_t *const        wnd = data;

    while (1)
    {
        while (poll_input_events(wnd))
            msg_Err(wnd, "cannot poll input events");
        while (!is_input_event_queue_empty(wnd))
        {
            struct event *const event = get_input_event(wnd);

            switch (event->type)
            {
            case EVENT_KEYBOARD_KEY:
                if (event->key.value == KEY_ESCAPE)
                    vout_window_ReportClose(wnd);
                break;
            default:
                break;
            }

            free(event);
        }
    }

    return NULL;
}

#ifdef USE_GBM
static int
SwapFrameBuffersCallback(vlc_object_t * obj, char const * psz_name,
                         vlc_value_t oldval, vlc_value_t newval,
                         void * p_data)
{ VLC_UNUSED(obj); VLC_UNUSED(psz_name); VLC_UNUSED(oldval);
    VLC_UNUSED(newval);

    vout_window_sys_t *const    sys = p_data;
    struct gbm_bo *const        next_bo =
        gbm_surface_lock_front_buffer(sys->gbm.surface);
    uint32_t const              bo_handle = gbm_bo_get_handle(next_bo).u32;
    uint32_t const              bo_pitch = gbm_bo_get_stride(next_bo);

    for (struct drm_display * dpy = sys->drm.dpy; dpy; dpy = dpy->next)
    {
        uint32_t        next_fb;

        if (drmModeAddFB(sys->drm.fd,
                         dpy->mode->hdisplay, dpy->mode->vdisplay,
                         24, 32, bo_pitch, bo_handle, &next_fb) ||
            drmModeSetCrtc(sys->drm.fd, dpy->crtc_id, next_fb, 0, 0,
                           &dpy->connector->connector_id, 1, dpy->mode))
            return VLC_EGENERIC;

        if (dpy->fb)
            drmModeRmFB(sys->drm.fd, dpy->fb);
        dpy->fb = next_fb;
    }

    if (sys->gbm.bo)
        gbm_surface_release_buffer(sys->gbm.surface, sys->gbm.bo);
    sys->gbm.bo = next_bo;

    return VLC_SUCCESS;
}

static int
Control(vout_window_t * wnd, int cmd, va_list ap)
{ VLC_UNUSED(wnd); VLC_UNUSED(cmd); VLC_UNUSED(ap);
    return VLC_SUCCESS;
}

static void
Close(vout_window_t *wnd)
{
    vout_window_sys_t *const    sys = wnd->sys;

    msg_Err(wnd, "closing drm_gbm window");
    if (sys->thread.handle)
    {
        vlc_cancel(sys->thread);
        vlc_join(sys->thread, NULL);
    }
    destroy_input(wnd);
    if (sys->gbm.bo)
        gbm_surface_release_buffer(sys->gbm.surface, sys->gbm.bo);
    if (sys->drm.fb)
        drmModeRmFB(sys->drm.fd, sys->drm.fb);
    gbm_surface_destroy(sys->gbm.surface);
    gbm_device_destroy(sys->gbm.device);
    drmModeFreeModeInfo(sys->drm.mode);
    vlc_close(sys->drm.fd);
    free(sys);
}

static int
drm_open_device(vout_window_t * wnd)
{
    vout_window_sys_t *const    sys = wnd->sys;
    char *const                 drm_device_path =
        var_InheritString(wnd, "drm-device");
    int                         ret = VLC_SUCCESS;

    if (!drm_device_path)
        return VLC_EGENERIC;

    sys->drm.fd = vlc_open(drm_device_path, O_RDWR);
    if (sys->drm.fd != -1)
        msg_Info(wnd, "using %s as DRM device", drm_device_path);
    else
    {
        msg_Err(wnd, "opening %s failed: %s", drm_device_path,
                vlc_strerror(errno));
        ret = VLC_EGENERIC;
    }

    free(drm_device_path);
    return ret;
}

static drmModeModeInfo *
drm_find_mode(drmModeConnector * connector)
{
    drmModeModeInfo *   mode = NULL;
    uint32_t            area = 0;

    for (int i = 0; i < connector->count_modes; ++i)
    {
        drmModeModeInfo *const  cur_mode = connector->modes + i;
        uint32_t const          cur_area =
            cur_mode->hdisplay * cur_mode->vdisplay;

        if (cur_mode->type & DRM_MODE_TYPE_PREFERRED)
            mode = cur_mode;
        if (cur_area > area)
        {
            mode = cur_mode;
            area = cur_area;
        }
    }

    return mode;
}

static struct drm_display *
drm_new_display(struct drm_device * drm, drmModeConnector * connector)
{
    struct drm_display *const   new = calloc(1, sizeof(*new));
    if (!new)
        return NULL;
    new->connector = connector;

    if (drm->dpy)
    {
        struct drm_display *    last = drm->dpy;
        while (last->next)
            last = last->next;
        last->next = new;
    }
    else
        drm->dpy = new;

    return new;
}

static drmModeEncoder *
drm_find_encoder(int drm_fd,
                 drmModeRes * resources,
                 drmModeConnector * connector)
{
    for (int i = 0; i < resources->count_encoders; ++i)
    {
        drmModeEncoder *const   encoder =
            drmModeGetEncoder(drm_fd, resources->encoders[i]);

        if (encoder->encoder_id == connector->encoder_id)
            return encoder;
        drmModeFreeEncoder(encoder);
    }
    return NULL;
}

static int
drm_find_crtc(int drm_fd, drmModeRes * resources,
              drmModeConnector * connector, uint32_t * p_crtc_id)
{
    int ret = VLC_EGENERIC;

    for (int i = 0; i < connector->count_encoders; ++i)
    {
        drmModeEncoder *const   encoder =
            drmModeGetEncoder(drm_fd, connector->encoders[i]);
        if (!encoder)
            continue;

        for (int j = 0; j < resources->count_crtcs; ++j)
            if (encoder->possible_crtcs & (1 << j))
            {
                *p_crtc_id = resources->crtcs[j];
                ret = VLC_SUCCESS;
                break;
            }
        drmModeFreeEncoder(encoder);

        if (!ret)
            break;
    }

    return ret;
}

static int
init_drm(vout_window_t * wnd)
{
    vout_window_sys_t *const    sys = wnd->sys;
    drmModeRes *                resources;

    if (drm_open_device(wnd))
        return VLC_EGENERIC;

    resources = drmModeGetResources(sys->drm.fd);
    if (!resources)
    {
        msg_Err(wnd, "drmModeGetResources failed\n");
        goto error;
    }

    for (int i = 0; i < resources->count_connectors; ++i)
    {
        drmModeConnector *const connector =
            drmModeGetConnector(sys->drm.fd, resources->connectors[i]);

        if (connector->connection == DRM_MODE_CONNECTED)
        {
            struct drm_display *const   dpy =
                drm_new_display(&sys->drm, connector);
            if (!dpy)
                goto error;

            dpy->mode = drm_find_mode(connector);
            if (!dpy->mode)
            {
                msg_Err(wnd, "could not detect display mode\n");
                goto error;
            }
            else
                msg_Dbg(wnd, "display mode is %ux%u",
                        dpy->mode->hdisplay, dpy->mode->vdisplay);

            drmModeEncoder *const       encoder =
                drm_find_encoder(sys->drm.fd, resources, connector);

            if (encoder)
            {
                dpy->crtc_id = encoder->crtc_id;
                drmModeFreeEncoder(encoder);
            }
            else if (drm_find_crtc(sys->drm.fd, resources,
                                   connector, &dpy->crtc_id))
            {
                msg_Err(wnd, "could not find a suitable CRT controller\n");
                goto error;
            }
        }
        else
            drmModeFreeConnector(connector);
    }

    if (!sys->drm.dpy)
    {
        msg_Err(wnd, "no display detected\n");
        goto error;
    }

    drmModeFreeResources(resources);
    return VLC_SUCCESS;

error:
    for (struct drm_display * dpy = sys->drm.dpy; dpy; )
    {
        struct drm_display *const       to_del = dpy;
        dpy = dpy->next;
        drmModeFreeConnector(to_del->connector);
        free(to_del);
    }
    if (resources)
        drmModeFreeResources(resources);
    return VLC_EGENERIC;
}

#ifdef USE_GBM
static int
init_gbm(vout_window_t * wnd)
{
    vout_window_sys_t *const    sys = wnd->sys;

    sys->gbm.device = gbm_create_device(sys->drm.fd);
    if (!sys->gbm.device)
    {
        msg_Err(wnd, "gbm_create_create failed\n");
        return VLC_EGENERIC;
    }

    sys->gbm.surface =
        gbm_surface_create(sys->gbm.device,
                           sys->drm.mode->hdisplay, sys->drm.mode->vdisplay,
                           GBM_FORMAT_XRGB8888,
                           GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!sys->gbm.surface)
    {
        msg_Err(wnd, "gbm_surface_create failed\n");
        gbm_device_destroy(sys->gbm.device);
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}
#endif

static int
Open(vout_window_t * wnd, vout_window_cfg_t const * cfg)
{ VLC_UNUSED(cfg);
    msg_Err(wnd, "opening drm_gbm window");
    if (cfg->type != VOUT_WINDOW_TYPE_INVALID &&
        cfg->type != VOUT_WINDOW_TYPE_DRM_GBM)
        return VLC_EGENERIC;

    wnd->sys = calloc(1, sizeof(vout_window_sys_t));
    if (!wnd->sys)
        return VLC_ENOMEM;

    if (init_drm(wnd) || create_input(wnd))
        goto error;
#ifdef USE_GBM
    if (init_gbm(wnd))
        goto error;
#endif

    vout_window_ReportSize(wnd, wnd->sys->drm.mode->hdisplay,
                           wnd->sys->drm.mode->vdisplay);

    var_Create(wnd, "drm-gbm-swap_frame_buffers", VLC_VAR_VOID);

    /* This callback is to be triggered rigth after a call to
       eglSwapBuffers. */
    var_AddCallback(wnd, "drm-gbm-swap_frame_buffers",
                    SwapFrameBuffersCallback, wnd->sys);

#ifdef USE_GBM
    wnd->type = VOUT_WINDOW_TYPE_DRM_GBM;
    wnd->handle.gbm = wnd->sys->gbm.surface;
    wnd->display.gbm = wnd->sys->gbm.device;
#endif
    wnd->control = Control;

    if (vlc_clone(&wnd->sys->thread, Thread, wnd, VLC_THREAD_PRIORITY_LOW))
    {
        wnd->sys->thread.handle = 0;
        Close(wnd);
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;

error:
    destroy_input(wnd);
    if (wnd->sys->drm.fd)
        vlc_close(wnd->sys->drm.fd);
    free(wnd->sys);
    return VLC_EGENERIC;
}

static void
Close(vout_window_t * wnd)
{
    vout_window_sys_t *const    sys = wnd->sys;

    var_DelCallback(wnd, "drm-gbm-swap_frame_buffers",
                    SwapFrameBuffersCallback, wnd->sys);
    var_Destroy(wnd, "drm-gbm-swap_frame_buffers");
    destroy_input(wnd);
    if (sys->gbm.bo)
        gbm_surface_release_buffer(sys->gbm.surface, sys->gbm.bo);
    gbm_surface_destroy(sys->gbm.surface);
    gbm_device_destroy(sys->gbm.device);
    for (struct drm_display * dpy = sys->drm.dpy; dpy; )
    {
        struct drm_display *const       to_del = dpy;
        dpy = dpy->next;
        if (to_del->fb)
            drmModeRmFB(sys->drm.fd, to_del->fb);
        drmModeFreeConnector(to_del->connector);
        free(to_del);
    }
    vlc_close(sys->drm.fd);
    free(sys);
}

#define DEVICE_TEXT     N_("DRM device")
#define DEVICE_LONGTEXT N_(                                     \
        "Video will be rendered to this device. "               \
        "(" DRM_DIR_NAME "/card0, " DRM_DIR_NAME "/renderD128)" \
        "If empty, this module won't be used.")

vlc_module_begin()
    set_shortname(N_("DRM-GBM window"))
    set_description(N_("DRM-GBM window surface"))
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("vout window", 1)
    set_callbacks(Open, Close)

    add_string("drm-device", NULL, DEVICE_TEXT, DEVICE_LONGTEXT, true)
vlc_module_end()
