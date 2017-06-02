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

#ifndef DRM_EVENTS_H
# define DRM_EVENTS_H

# include <stdint.h>

enum    event_type
{
    EVENT_KEYBOARD_KEY,
    EVENT_MOUSE_MOTION,
    EVENT_MOUSE_BUTTON
};

enum    event_kdb_key_type
{
    KEY_PRESSED,
    KEY_RELEASED
};

enum    event_kbd_key_value
{
    KEY_ESCAPE = 0x1
};

struct  event_kbd_key
{
    enum event_kdb_key_type     type;
    enum event_kbd_key_value    value;
};

struct  event_mouse_motion
{
    uint32_t    x;
    uint32_t    y;
};

enum    event_mouse_button_type
{
    BUTTON_PRESSED,
    BUTTON_RELEASED
};

enum    event_mouse_button_value
{
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_MIDDLE,
    BUTTON_WHEELUP,
    BUTTON_WHEELDOWN,
    BUTTON_X1,
    BUTTON_X2
};

struct  event_mouse_button
{
    enum event_mouse_button_type        type;
    enum event_mouse_button_value       value;
};

struct  event
{
    enum event_type             type;
    struct event_kbd_key        key;
    struct event_mouse_motion   motion;
    struct event_mouse_button   button;
};

struct  event_queue_item
{
    struct event *              event;
    struct event_queue_item *   next;
};

#endif /* include-guard */
