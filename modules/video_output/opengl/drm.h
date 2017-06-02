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

#ifndef DRM_H
# define DRM_H

# include <xf86drm.h>
# include <xf86drmMode.h>
# ifdef USE_GBM
#  include <gbm.h>
# endif

# include <dbus/dbus.h>
# include <libinput.h>

# include <vlc_common.h>
# include <vlc_threads.h>
# include <vlc_vout_window.h>

# include "drm_events.h"

struct  device
{
    char *      path;
    int         fd;
    uint32_t    minor;
    uint32_t    major;
};

struct  input
{
    struct libinput *           ctx;
    DBusConnection *            sysbus;
    char const *                logind_session_obj_path;
    char const *                session_seat;
    struct device *             devices;
    uint32_t                    num_devices;
    uint32_t                    sz_devices;
    uint32_t                    total_opened_dev;

    /* struct libinput_device *    kbd; */
    /* struct libinput_device *    mice; */

    struct
    {
        struct event_queue_item *       first;
        struct event_queue_item *       last;
        size_t                          size;
    }                           event_queue;
};

struct  drm_display
{
    drmModeConnector *          connector;
    drmModeModeInfo *           mode;
    uint32_t                    crtc_id;
    uint32_t                    fb;
    struct drm_display *        next;
};

struct  drm_device
{
    int                         fd;
    struct drm_display *        dpy;
};

struct vout_window_sys_t
{
    struct drm_device   drm;

#ifdef USE_GBM 
   struct
    {
        struct gbm_device *     device;
        struct gbm_surface *    surface;
        struct gbm_bo *         bo;
    }                   gbm;
#endif

    struct input        input;

    vlc_thread_t        thread;
};

int     create_input(vout_window_t * wnd);
void    destroy_input(vout_window_t * wnd);

int             poll_input_events(vout_window_t * wnd);
bool            is_input_event_queue_empty(vout_window_t * wnd);
struct event *  get_input_event(vout_window_t * wnd);

#endif /* include-guard */
