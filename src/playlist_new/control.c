/*****************************************************************************
 * playlist_new/control.c
 *****************************************************************************
 * Copyright (C) 2018 VLC authors and VideoLAN
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

#include "control.h"

#include "item.h"
#include "notify.h"
#include "playlist.h"

static void
vlc_playlist_PlaybackOrderChanged(vlc_playlist_t *playlist)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    vlc_playlist_Notify(playlist, on_playback_order_changed, playlist->order);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

static void
vlc_playlist_PlaybackRepeatChanged(vlc_playlist_t *playlist)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    vlc_playlist_Notify(playlist, on_playback_repeat_changed, playlist->repeat);
    vlc_playlist_state_NotifyChanges(playlist, &state);
}

enum vlc_playlist_playback_repeat
vlc_playlist_GetPlaybackRepeat(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->repeat;
}

enum vlc_playlist_playback_order
vlc_playlist_GetPlaybackOrder(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->order;
}

void
vlc_playlist_SetPlaybackRepeat(vlc_playlist_t *playlist,
                               enum vlc_playlist_playback_repeat repeat)
{
    vlc_playlist_AssertLocked(playlist);

    if (playlist->repeat == repeat)
        return;

    playlist->repeat = repeat;
    vlc_playlist_PlaybackRepeatChanged(playlist);
}

void
vlc_playlist_SetPlaybackOrder(vlc_playlist_t *playlist,
                              enum vlc_playlist_playback_order order)
{
    vlc_playlist_AssertLocked(playlist);

    if (playlist->order == order)
        return;

    playlist->order = order;
    vlc_playlist_PlaybackOrderChanged(playlist);
}

int
vlc_playlist_SetCurrentMedia(vlc_playlist_t *playlist, ssize_t index)
{
    vlc_playlist_AssertLocked(playlist);

    input_item_t *media = index != -1
                        ? playlist->items.data[index]->media
                        : NULL;
    return vlc_player_SetCurrentMedia(playlist->player, media);
}

static inline bool
vlc_playlist_NormalOrderHasPrev(vlc_playlist_t *playlist)
{
    if (playlist->current == -1)
        return false;

    if (playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_ALL)
        return true;

    return playlist->current > 0;
}

static inline size_t
vlc_playlist_NormalOrderGetPrevIndex(vlc_playlist_t *playlist)
{
    switch (playlist->repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            return playlist->current - 1;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
            if (playlist->current == 0)
                return playlist->items.size - 1;
            return playlist->current - 1;
        default:
            vlc_assert_unreachable();
    }
}

static inline bool
vlc_playlist_NormalOrderHasNext(vlc_playlist_t *playlist)
{
    if (playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_ALL)
        return true;

    /* also works if current == -1 or playlist->items.size == 0 */
    return playlist->current < (ssize_t) playlist->items.size - 1;
}

static inline size_t
vlc_playlist_NormalOrderGetNextIndex(vlc_playlist_t *playlist)
{
    switch (playlist->repeat)
    {
        case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
        case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
            if (playlist->current >= (ssize_t) playlist->items.size - 1)
                return -1;
            return playlist->current + 1;
        case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
                if (playlist->items.size == 0)
                    return -1;
            return (playlist->current + 1) % playlist->items.size;
        default:
            vlc_assert_unreachable();
    }
}

static inline bool
vlc_playlist_RandomOrderHasPrev(vlc_playlist_t *playlist)
{
    VLC_UNUSED(playlist);
    /* TODO */
    return false;
}

static inline size_t
vlc_playlist_RandomOrderGetPrevIndex(vlc_playlist_t *playlist)
{
    VLC_UNUSED(playlist);
    /* TODO */
    return -0;
}

static inline bool
vlc_playlist_RandomOrderHasNext(vlc_playlist_t *playlist)
{
    VLC_UNUSED(playlist);
    /* TODO */
    return false;
}

static inline size_t
vlc_playlist_RandomOrderGetNextIndex(vlc_playlist_t *playlist)
{
    VLC_UNUSED(playlist);
    /* TODO */
    return 0;
}

static size_t
vlc_playlist_GetPrevIndex(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    switch (playlist->order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return vlc_playlist_NormalOrderGetPrevIndex(playlist);
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return vlc_playlist_RandomOrderGetPrevIndex(playlist);
        default:
            vlc_assert_unreachable();
    }
}

static size_t
vlc_playlist_GetNextIndex(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    switch (playlist->order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return vlc_playlist_NormalOrderGetNextIndex(playlist);
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return vlc_playlist_RandomOrderGetNextIndex(playlist);
        default:
            vlc_assert_unreachable();
    }
}

bool
vlc_playlist_ComputeHasPrev(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    switch (playlist->order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return vlc_playlist_NormalOrderHasPrev(playlist);
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return vlc_playlist_RandomOrderHasPrev(playlist);
        default:
            vlc_assert_unreachable();
    }
}

bool
vlc_playlist_ComputeHasNext(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    switch (playlist->order)
    {
        case VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL:
            return vlc_playlist_NormalOrderHasNext(playlist);
        case VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM:
            return vlc_playlist_RandomOrderHasNext(playlist);
        default:
            vlc_assert_unreachable();
    }
}

ssize_t
vlc_playlist_GetCurrentIndex(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->current;
}

static void
vlc_playlist_SetCurrentIndex(vlc_playlist_t *playlist, ssize_t index)
{
    struct vlc_playlist_state state;
    vlc_playlist_state_Save(playlist, &state);

    playlist->current = index;
    playlist->has_prev = vlc_playlist_ComputeHasPrev(playlist);
    playlist->has_next = vlc_playlist_ComputeHasNext(playlist);

    vlc_playlist_state_NotifyChanges(playlist, &state);
}

bool
vlc_playlist_HasPrev(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->has_prev;
}

bool
vlc_playlist_HasNext(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    return playlist->has_next;
}

int
vlc_playlist_Prev(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);

    if (!vlc_playlist_ComputeHasPrev(playlist))
        return VLC_EGENERIC;

    ssize_t index = vlc_playlist_GetPrevIndex(playlist);
    assert(index != -1);

    int ret = vlc_playlist_SetCurrentMedia(playlist, index);
    if (ret != VLC_SUCCESS)
        return ret;

    vlc_playlist_SetCurrentIndex(playlist, index);
    return VLC_SUCCESS;
}

int
vlc_playlist_Next(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);

    if (!vlc_playlist_ComputeHasNext(playlist))
        return VLC_EGENERIC;

    ssize_t index = vlc_playlist_GetNextIndex(playlist);
    assert(index != -1);

    int ret = vlc_playlist_SetCurrentMedia(playlist, index);
    if (ret != VLC_SUCCESS)
        return ret;

    vlc_playlist_SetCurrentIndex(playlist, index);
    return VLC_SUCCESS;
}

int
vlc_playlist_GoTo(vlc_playlist_t *playlist, ssize_t index)
{
    vlc_playlist_AssertLocked(playlist);
    assert(index == -1 || (size_t) index < playlist->items.size);

    int ret = vlc_playlist_SetCurrentMedia(playlist, index);
    if (ret != VLC_SUCCESS)
        return ret;

    vlc_playlist_SetCurrentIndex(playlist, index);
    return VLC_SUCCESS;
}

static ssize_t
vlc_playlist_GetNextMediaIndex(vlc_playlist_t *playlist)
{
    vlc_playlist_AssertLocked(playlist);
    if (playlist->repeat == VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT)
        return playlist->current;
    if (!vlc_playlist_ComputeHasNext(playlist))
        return -1;
    return vlc_playlist_GetNextIndex(playlist);
}

input_item_t *
vlc_playlist_GetNextMedia(vlc_playlist_t *playlist)
{
    /* the playlist and the player share the lock */
    vlc_playlist_AssertLocked(playlist);

    ssize_t index = vlc_playlist_GetNextMediaIndex(playlist);
    if (index == -1)
        return NULL;

    input_item_t *media = playlist->items.data[index]->media;
    input_item_Hold(media);
    return media;
}
