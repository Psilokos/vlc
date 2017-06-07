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

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <vlc_common.h>
#include <vlc_fs.h>
#include <vlc_plugin.h>
#include <vlc_vout_window.h>

struct vout_window_sys_t
{
    struct
    {
        int                     fd;
        drmModeConnector *      connector;
        drmModeModeInfo *       mode;
        uint32_t                crtc_id;
        uint32_t                fb;
    } drm;

    struct
    {
        struct gbm_device *     device;
        struct gbm_surface *    surface;
        struct gbm_bo *         bo;
    } gbm;
};

static int
SwapFrameBuffersCallback(vlc_object_t * obj, char const * name,
                         vlc_value_t prev, vlc_value_t cur,
                         void * data)
{ VLC_UNUSED(obj); VLC_UNUSED(name); VLC_UNUSED(prev); VLC_UNUSED(cur);
    vout_window_sys_t *const    sys = data;
    struct gbm_bo *const        next_bo =
        gbm_surface_lock_front_buffer(sys->gbm.surface);
    uint32_t const              bo_handle = gbm_bo_get_handle(next_bo).u32;
    uint32_t const              bo_pitch = gbm_bo_get_stride(next_bo);
    uint32_t                    next_fb;

    if (drmModeAddFB(sys->drm.fd,
                     sys->drm.mode->hdisplay, sys->drm.mode->vdisplay,
                     24, 32, bo_pitch, bo_handle, &next_fb) ||
        drmModeSetCrtc(sys->drm.fd, sys->drm.crtc_id, next_fb, 0, 0,
                       &sys->drm.connector->connector_id, 1, sys->drm.mode))
        return VLC_EGENERIC;

    if (sys->gbm.bo)
    {
        drmModeRmFB(sys->drm.fd, sys->drm.fb);
        gbm_surface_release_buffer(sys->gbm.surface, sys->gbm.bo);
    }
    sys->drm.fb = next_fb;
    sys->gbm.bo = next_bo;

    return VLC_SUCCESS;
}

static int
Control(vout_window_t * wnd, int cmd, va_list ap)
{ VLC_UNUSED(wnd); VLC_UNUSED(cmd); VLC_UNUSED(ap);
    return VLC_SUCCESS;
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

static drmModeConnector *
drm_find_connector(int drm_fd, drmModeRes * resources)
{
    for (int i = 0; i < resources->count_connectors; ++i)
    {
        drmModeConnector *const connector =
            drmModeGetConnector(drm_fd, resources->connectors[i]);

        if (connector->connection == DRM_MODE_CONNECTED)
            return connector;
        drmModeFreeConnector(connector);
    }
    return NULL;
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
    drmModeEncoder *            encoder = NULL;

    if (drm_open_device(wnd))
        return VLC_EGENERIC;

    resources = drmModeGetResources(sys->drm.fd);
    if (!resources)
    {
        msg_Err(wnd, "drmModeGetResources failed\n");
        goto error;
    }

    sys->drm.connector = drm_find_connector(sys->drm.fd, resources);
    if (!sys->drm.connector)
    {
        msg_Err(wnd, "drm_find_connector failed\n");
        goto error;
    }

    sys->drm.mode = drm_find_mode(sys->drm.connector);
    if (!sys->drm.mode)
    {
        msg_Err(wnd, "drm_find_mode failed\n");
        goto error;
    }
    else
        msg_Dbg(wnd, "display mode is %ux%u",
                sys->drm.mode->hdisplay, sys->drm.mode->vdisplay);

    encoder = drm_find_encoder(sys->drm.fd, resources, sys->drm.connector);
    if (encoder)
        sys->drm.crtc_id = encoder->crtc_id;
    else if (drm_find_crtc(sys->drm.fd, resources,
                           sys->drm.connector, &sys->drm.crtc_id))
    {
        msg_Err(wnd, "drm_find_crtc failed\n");
        goto error;
    }

    drmModeFreeEncoder(encoder);
    drmModeFreeResources(resources);
    return VLC_SUCCESS;

error:
    if (encoder)
        drmModeFreeEncoder(encoder);
    if (resources)
        drmModeFreeResources(resources);
    return VLC_EGENERIC;
}

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

static int
Open(vout_window_t * wnd, vout_window_cfg_t const * cfg)
{ VLC_UNUSED(cfg);
    if (cfg->type != VOUT_WINDOW_TYPE_INVALID &&
        cfg->type != VOUT_WINDOW_TYPE_DRM_GBM)
        return VLC_EGENERIC;

    wnd->sys = calloc(1, sizeof(vout_window_sys_t));
    if (!wnd->sys)
        return VLC_ENOMEM;

    if (init_drm(wnd) || init_gbm(wnd))
        goto error;

    vout_window_ReportSize(wnd, wnd->sys->drm.mode->hdisplay,
                           wnd->sys->drm.mode->vdisplay);

    var_Create(wnd, "drm-gbm-swap_frame_buffers", VLC_VAR_VOID);

    /* This callback is to be triggered rigth after a call to
       eglSwapBuffers. */
    var_AddCallback(wnd, "drm-gbm-swap_frame_buffers",
                    SwapFrameBuffersCallback, wnd->sys);

    wnd->type = VOUT_WINDOW_TYPE_DRM_GBM;
    wnd->handle.gbm = wnd->sys->gbm.surface;
    wnd->display.gbm = wnd->sys->gbm.device;
    wnd->control = Control;

    return VLC_SUCCESS;

error:
    if (wnd->sys->drm.connector)
        drmModeFreeConnector(wnd->sys->drm.connector);
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
    if (sys->gbm.bo)
        gbm_surface_release_buffer(sys->gbm.surface, sys->gbm.bo);
    if (sys->drm.fb)
        drmModeRmFB(sys->drm.fd, sys->drm.fb);
    gbm_surface_destroy(sys->gbm.surface);
    gbm_device_destroy(sys->gbm.device);
    drmModeFreeConnector(sys->drm.connector);
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
