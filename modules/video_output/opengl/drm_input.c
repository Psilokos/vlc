/**
 * @file drm_input.c
 * @brief DRM input events helper for VLC media player
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

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>

#include <dbus/dbus.h>

#include <vlc_common.h>
#include <vlc_fs.h>

#include "drm.h"

#define DBUS_PROPERTIES_IFACE   "org.freedesktop.DBus.Properties"
#define LOGIND_BUS_NAME         "org.freedesktop.login1"
#define LOGIND_MANAGER_PATH     "/org/freedesktop/login1"
#define LOGIND_MANAGER_IFACE    "org.freedesktop.login1.Manager"
#define LOGIND_SESSION_IFACE    "org.freedesktop.login1.Session"

static int
input_dbus_open_connection(struct input * input)
{
    DBusError           dbus_err;

    dbus_error_init(&dbus_err);

    input->sysbus = dbus_bus_get(DBUS_BUS_SYSTEM, &dbus_err);
    if (dbus_error_is_set(&dbus_err))
    {
        dbus_error_free(&dbus_err);
        return VLC_EGENERIC;
    }
    assert(input->sysbus);

    return VLC_SUCCESS;
}

static inline void
input_dbus_close_connection(struct input * input)
{
    dbus_connection_unref(input->sysbus);
}

static int
input_session_retrieve_object_path(struct input * input)
{
    DBusMessage *       dbus_msg;
    DBusMessage *       dbus_reply;
    DBusError           dbus_err;
    pid_t const         pid = getpid();
    char const *        logind_session_obj_path;

    dbus_msg = dbus_message_new_method_call(LOGIND_BUS_NAME,
                                            LOGIND_MANAGER_PATH,
                                            LOGIND_MANAGER_IFACE,
                                            "GetSessionByPID");
    if (!dbus_msg)
        return VLC_EGENERIC;

    if (!dbus_message_append_args(dbus_msg,
                                  DBUS_TYPE_UINT32, &pid,
                                  DBUS_TYPE_INVALID))
    {
        dbus_message_unref(dbus_msg);
        return VLC_EGENERIC;
    }

    dbus_error_init(&dbus_err);
    dbus_reply =
        dbus_connection_send_with_reply_and_block(input->sysbus, dbus_msg,
                                                  DBUS_TIMEOUT_USE_DEFAULT,
                                                  &dbus_err);
    dbus_message_unref(dbus_msg);
    if (dbus_error_is_set(&dbus_err))
    {
        dbus_error_free(&dbus_err);
        return VLC_EGENERIC;
    }
    assert(dbus_reply);

    dbus_message_get_args(dbus_reply, &dbus_err,
                          DBUS_TYPE_OBJECT_PATH,
                          &logind_session_obj_path,
                          DBUS_TYPE_INVALID);
    dbus_message_unref(dbus_reply);
    if (dbus_error_is_set(&dbus_err))
    {
        dbus_error_free(&dbus_err);
        return VLC_EGENERIC;
    }

    fprintf(stderr, "PID %d session object path: %s\n", pid, logind_session_obj_path);
    input->logind_session_obj_path = strdup(logind_session_obj_path);
    return input->logind_session_obj_path ? VLC_SUCCESS : VLC_EGENERIC;
}

static int
input_session_retrieve_seat(struct input * input)
{
    DBusMessage *       dbus_msg;
    DBusMessage *       dbus_reply;
    DBusMessageIter     dbus_reply_iter;
    DBusMessageIter     dbus_reply_iter_variant;
    DBusMessageIter     dbus_reply_iter_variant_struct;
    DBusError           dbus_err;
    char const *const   iface = LOGIND_SESSION_IFACE;
    char const *const   property = "Seat";
    char const *        session_seat;

    dbus_msg = dbus_message_new_method_call(LOGIND_BUS_NAME,
                                            input->logind_session_obj_path,
                                            DBUS_PROPERTIES_IFACE,
                                            "Get");
    if (!dbus_msg)
        return VLC_EGENERIC;

    if (!dbus_message_append_args(dbus_msg,
                                  DBUS_TYPE_STRING, &iface,
                                  DBUS_TYPE_STRING, &property,
                                  DBUS_TYPE_INVALID))
    {
        dbus_message_unref(dbus_msg);
        return VLC_EGENERIC;
    }

    dbus_error_init(&dbus_err);
    dbus_reply =
        dbus_connection_send_with_reply_and_block(input->sysbus, dbus_msg,
                                                  DBUS_TIMEOUT_USE_DEFAULT,
                                                  &dbus_err);
    dbus_message_unref(dbus_msg);
    if (dbus_error_is_set(&dbus_err))
    {
        dbus_error_free(&dbus_err);
        return VLC_EGENERIC;
    }
    assert(dbus_reply);

    if (!dbus_message_iter_init(dbus_reply, &dbus_reply_iter))
    {
        dbus_message_unref(dbus_reply);
        return VLC_EGENERIC;
    }

    assert(dbus_message_iter_get_arg_type(&dbus_reply_iter)
           == DBUS_TYPE_VARIANT);
    dbus_message_iter_recurse(&dbus_reply_iter, &dbus_reply_iter_variant);
    assert(dbus_message_iter_get_arg_type(&dbus_reply_iter_variant)
           == DBUS_TYPE_STRUCT);
    dbus_message_iter_recurse(&dbus_reply_iter_variant, &dbus_reply_iter_variant_struct);
    dbus_message_iter_get_basic(&dbus_reply_iter_variant_struct, &session_seat);

    fprintf(stderr, "using %s\n", session_seat);
    input->session_seat = strdup(session_seat);
    return input->session_seat ? VLC_SUCCESS : VLC_EGENERIC;
}

static int
input_session_take_control(struct input * input)
{
    DBusMessage * const dbus_msg =
        dbus_message_new_method_call(LOGIND_BUS_NAME,
                                     input->logind_session_obj_path,
                                     LOGIND_SESSION_IFACE, "TakeControl");
    DBusError           dbus_err;
    int                 force = 0;

    if (!dbus_msg)
        return VLC_EGENERIC;

    if (!dbus_message_append_args(dbus_msg,
                                  DBUS_TYPE_BOOLEAN, &force,
                                  DBUS_TYPE_INVALID))
    {
        dbus_message_unref(dbus_msg);
        return VLC_EGENERIC;
    }

    /* ret = dbus_connection_send(input->sysbus, dbus_msg, NULL); */
    dbus_error_init(&dbus_err);
    dbus_connection_send_with_reply_and_block(input->sysbus, dbus_msg, DBUS_TIMEOUT_USE_DEFAULT, &dbus_err);
    dbus_message_unref(dbus_msg);

    if (dbus_error_is_set(&dbus_err))
    {
        dbus_error_free(&dbus_err);
        return VLC_EGENERIC;
    }

    /* if (ret) */
    /*     fprintf(stderr, "I own u bitch!\n"); */

    /* return ret ? VLC_SUCCESS : VLC_EGENERIC; */
    fprintf(stderr, "session control taken\n");
    return VLC_SUCCESS;
}

static void
input_session_release_control(struct input * input)
{
    DBusMessage * const dbus_msg =
        dbus_message_new_method_call(LOGIND_BUS_NAME,
                                     input->logind_session_obj_path,
                                     LOGIND_SESSION_IFACE, "ReleaseControl");
    if (!dbus_msg)
        return;

    dbus_connection_send(input->sysbus, dbus_msg, NULL);
    dbus_message_unref(dbus_msg);
    dbus_connection_flush(input->sysbus);
    fprintf(stderr, "session control released\n");
}

static int
input_device_retrieve_minor_major(char const * path,
                                  uint32_t * p_minor, uint32_t * p_major)
{
    char const *const   min_maj_input_dir = "/sys/class/input";
    char const *const   min_maj_filename = "/uevent";
    char *              min_maj_path;
    char const *const   dev_name = strrchr(path, '/');
    assert(dev_name);
    int                 fd;

    min_maj_path = calloc(strlen(min_maj_input_dir) +
                          strlen(dev_name) +
                          strlen(min_maj_filename) + 1,
                          sizeof(char));
    if (!min_maj_path)
        return VLC_EGENERIC;

    strcpy(min_maj_path, min_maj_input_dir);
    strcat(min_maj_path, dev_name);
    strcat(min_maj_path, min_maj_filename);

    fd = vlc_open(min_maj_path, O_RDONLY);
    free(min_maj_path);
    if (fd == -1)
        return VLC_EGENERIC;

    *p_minor = 0;
    *p_major = 0;
    errno = 0;

    FILE *      stream = fdopen(fd, "r");
    char *      line = NULL;
    size_t      n = 0;
    while (getline(&line, &n, stream) != -1)
    {
        if (!strncmp(line, "MINOR=", 6))
        {
            uintmax_t   minor = strtoumax(line + 6, NULL, 10);

            if (!errno)
                *p_minor = minor;
        }
        else if (!strncmp(line, "MAJOR=", 6))
        {
            uintmax_t   major = strtoumax(line + 6, NULL, 10);

            if (!errno)
                *p_major = major;
        }
    }
    free(line);

    return *p_minor && *p_major ? VLC_SUCCESS : VLC_EGENERIC;
}

#include <sys/stat.h>
#include <linux/kdev_t.h>

static int
input_session_take_device(struct input * input,
                          int * p_fd,
                          uint32_t const * p_minor,
                          uint32_t const * p_major)
{
    DBusMessage *       dbus_msg;
    DBusMessage *       dbus_reply;
    DBusError           dbus_err;

    dbus_msg = dbus_message_new_method_call(LOGIND_BUS_NAME,
                                            input->logind_session_obj_path,
                                            LOGIND_SESSION_IFACE,
                                            "TakeDevice");
    if (!dbus_msg)
        return VLC_EGENERIC;

    if (!dbus_message_append_args(dbus_msg,
                                  DBUS_TYPE_UINT32, p_major,
                                  DBUS_TYPE_UINT32, p_minor,
                                  DBUS_TYPE_INVALID))
    {
        dbus_message_unref(dbus_msg);
        return VLC_EGENERIC;
    }

    dbus_error_init(&dbus_err);
    dbus_reply =
        dbus_connection_send_with_reply_and_block(input->sysbus, dbus_msg,
                                                  DBUS_TIMEOUT_USE_DEFAULT,
                                                  &dbus_err);
    dbus_message_unref(dbus_msg);
    if (dbus_error_is_set(&dbus_err))
    {
        dbus_error_free(&dbus_err);
        return VLC_EGENERIC;
    }
    assert(dbus_reply);

    dbus_message_get_args(dbus_reply, &dbus_err,
                          DBUS_TYPE_UNIX_FD, &p_fd,
                          DBUS_TYPE_INVALID);
    dbus_message_unref(dbus_reply);
    if (dbus_error_is_set(&dbus_err))
    {
        dbus_error_free(&dbus_err);
        return VLC_EGENERIC;
    }

    struct stat st;
    int fd = *p_fd;
    if (fstat(fd, &st) != -1)
        fprintf(stderr, "device %lu:%lu taken (%u:%u)\n",
                MAJOR(st.st_rdev), MINOR(st.st_rdev), *p_major, *p_minor);

    return VLC_SUCCESS;
}

static void
input_session_release_device(struct input * input,
                             uint32_t const * p_minor,
                             uint32_t const * p_major)
{
    DBusMessage *       dbus_msg;

    dbus_msg = dbus_message_new_method_call(LOGIND_BUS_NAME,
                                            input->logind_session_obj_path,
                                            LOGIND_SESSION_IFACE,
                                            "ReleaseDevice");
    if (!dbus_msg)
        return;

    if (!dbus_message_append_args(dbus_msg,
                                  DBUS_TYPE_UINT32, p_major,
                                  DBUS_TYPE_UINT32, p_minor,
                                  DBUS_TYPE_INVALID))
        return dbus_message_unref(dbus_msg);

    dbus_connection_send(input->sysbus, dbus_msg, NULL);
    dbus_message_unref(dbus_msg);
    /* fprintf(stderr, "device %u:%u released\n", *p_major, *p_minor); */
}

static int
open_device(char const * path, int flags, void * data)
{ VLC_UNUSED(flags);
    struct input *const input = data;
    uint32_t            minor;
    uint32_t            major;
    int                 fd;

    if (input_device_retrieve_minor_major(path, &minor, &major) ||
        input_session_take_device(input, &fd, &minor, &major))
        return -1;

    if (input->num_devices == input->sz_devices)
    {
        input->sz_devices += 16;
        input->devices = realloc(input->devices,
                                 input->sz_devices * sizeof(struct device));
        if (!input->devices)
        {
            input_session_release_device(input, &minor, &major);
            return -1;
        }
        memset(input->devices + input->num_devices, 0,
               input->sz_devices - input->num_devices);
    }

    unsigned int        i;
    for (i = input->num_devices; i < input->sz_devices; ++i)
        if (!input->devices[i].fd)
        {
            input->devices[i].fd = fd;
            input->devices[i].minor = minor;
            input->devices[i].major = major;
            input->devices[i].path = strdup(path);
            /* fprintf(stderr, "device %s opened\n", path); */
            ++input->num_devices;
            ++input->total_opened_dev;
            return fd;
        }
    assert(false);
}

static void
close_device(int fd, void * data)
{
    struct input *const input = data;

    for (unsigned int i = 0; i < input->num_devices; ++i)
        if (input->devices[i].fd == fd)
        {
            input_session_release_device(input,
                                         &input->devices[i].minor,
                                         &input->devices[i].major);
            input->devices[i].fd = 0;
            /* fprintf(stderr, "device %s closed\n", input->devices[i].path); */
            --input->num_devices;
            return;
        }
    assert(false);
}

static int
input_create_context(struct input * input)
{
    struct udev *const                  udev = udev_new();
    if (!udev)
        return VLC_EGENERIC;
    struct libinput_interface const     intf =
        {
            .open_restricted = open_device,
            .close_restricted = close_device
        };

    input->ctx = libinput_udev_create_context(&intf, input, udev);
    if (!input->ctx)
        return VLC_EGENERIC;

    if (libinput_udev_assign_seat(input->ctx, input->session_seat))
        return VLC_EGENERIC;
    fprintf(stderr, "udev assign seat done: %d/%d\n", input->num_devices, input->total_opened_dev);
    input->total_opened_dev = 0;
    libinput_resume(input->ctx); // remove this shit

    // udev_unref(udev);
    return VLC_SUCCESS;
}

static inline void
input_destroy_context(struct input * input)
{
    libinput_unref(input->ctx);
}

int
create_input(vout_window_t * wnd)
{
    struct input *const input = &wnd->sys->input;

    if (input_dbus_open_connection(input) ||
        input_session_retrieve_object_path(input) ||
        input_session_retrieve_seat(input) ||
        input_session_take_control(input) ||
        input_create_context(input))
        return VLC_EGENERIC;

    /* input->kbd = libinput_path_add_device(input->ctx, "/dev/input/event0"); */
    /* input->mice = NULL; // TODO */
    /* if (!input->kbd) */
    /*     return VLC_EGENERIC; */

    return VLC_SUCCESS;
}

void
destroy_input(vout_window_t * wnd)
{
    struct input *const input = &wnd->sys->input;

    /* if (input->mice) */
    /*     libinput_path_remove_device(input->mice); */
    /* if (input->kbd) */
    /*     libinput_path_remove_device(input->kbd); */
    if (input->ctx)
        input_destroy_context(input);
    if (input->sysbus)
    {
        memset(input->devices, 0, input->num_devices * sizeof(struct device));
        input->num_devices = 0;
        input_session_release_control(input);
        free((char *)input->logind_session_obj_path);
        input_dbus_close_connection(input);
    }
}

static struct event *
input_event_queue_push_event(struct input * input)
{
    struct event_queue_item *const      item = calloc(1, sizeof(*item));
    struct event *const                 event = calloc(1, sizeof(*event));

    if (!item || !event)
        return NULL;
    item->event = event;

    if (!input->event_queue.size)
    {
        input->event_queue.first = item;
        input->event_queue.last = item;
        input->event_queue.size = 1;
    }
    else
    {
        input->event_queue.last->next = item;
        input->event_queue.last = item;
        ++input->event_queue.size;
    }

    return event;
}

static struct event *
input_event_queue_pop_event(struct input * input)
{
    assert(input->event_queue.size);
    struct event_queue_item *const      item = input->event_queue.first;
    struct event *const                 event = item->event;

    input->event_queue.first = input->event_queue.first->next;
    if (--input->event_queue.size)
        input->event_queue.last = input->event_queue.first->next;
    else
        input->event_queue.last = NULL;

    free(item);

    return event;
}

static void
input_fill_keyboard_event(struct event * event,
                          struct libinput_event * generic_event)
{
    struct libinput_event_keyboard *const       kbd_event =
        libinput_event_get_keyboard_event(generic_event);
    enum libinput_key_state const               state =
        libinput_event_keyboard_get_key_state(kbd_event);

    event->type = EVENT_KEYBOARD_KEY;
    event->key.type = state == LIBINPUT_KEY_STATE_PRESSED ?
        KEY_PRESSED : KEY_RELEASED;
    event->key.value = libinput_event_keyboard_get_key(kbd_event);
}

int
poll_input_events(vout_window_t * wnd)
{
    struct input *const input = &wnd->sys->input;
    struct pollfd       pollfd =
        {
            .fd = libinput_get_fd(input->ctx),
            .events = POLLIN
        };

    while (poll(&pollfd, 1, -1) < 0);
    if (libinput_dispatch(input->ctx))
        return VLC_EGENERIC;

    struct libinput_event *     generic_event;
    while ((generic_event = libinput_get_event(input->ctx)))
    {
        enum libinput_event_type const  type =
            libinput_event_get_type(generic_event);
        assert(type != LIBINPUT_EVENT_NONE);
        struct event *const             event =
            input_event_queue_push_event(input);

        switch (type)
        {
        case LIBINPUT_EVENT_KEYBOARD_KEY:
            input_fill_keyboard_event(event, generic_event);
            break;
        default:
            break;
        }
    }

    return VLC_SUCCESS;
}

bool
is_input_event_queue_empty(vout_window_t * wnd)
{
    return !wnd->sys->input.event_queue.size;
}

struct event *
get_input_event(vout_window_t * wnd)
{
    return wnd->sys->input.event_queue.size
        ? input_event_queue_pop_event(&wnd->sys->input)
        : NULL;
}
