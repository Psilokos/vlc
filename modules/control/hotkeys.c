/*****************************************************************************
 * hotkeys.c: Hotkey handling for vlc
 *****************************************************************************
 * Copyright (C) 2005-2009 the VideoLAN team
 * $Id$
 *
 * Authors: Sigmund Augdal Helberg <dnumgis@videolan.org>
 *          Jean-Paul Saman <jpsaman #_at_# m2x.nl>
 *          Victorien Le Couviour--Tuffet <victorien.lecouviour.tuffet@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#define VLC_MODULE_LICENSE VLC_LICENSE_GPL_2_PLUS
#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_interface.h>
#include <vlc_input_item.h>
#include <vlc_aout.h>
#include <vlc_mouse.h>
#include <vlc_viewpoint.h>
#include <vlc_vout_osd.h>
#include <vlc_player.h>
#include <vlc_playlist_new.h>
#include <vlc_actions.h>
#include "math.h"

#include <assert.h>

/*****************************************************************************
 * intf_sys_t: description and status of FB interface
 *****************************************************************************/
struct intf_sys_t
{
    struct
    {
        bool b_can_change;
        bool b_button_pressed;
        int x, y;
    } vrnav;

    vlc_playlist_t *playlist;
};

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int Open(vlc_object_t *);
static void Close(vlc_object_t *);

//static inline void
//ClearChannels(vout_thread_t **vouts, size_t count, int slider_chan)
//{
//    for (size_t i = 0; i < count; ++i)
//    {
//        vout_FlushSubpictureChannel(vouts[i], VOUT_SPU_CHANNEL_OSD);
//        vout_FlushSubpictureChannel(vouts[i], slider_chan);
//    }
//}

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin ()
    set_shortname(N_("Hotkeys"))
    set_description(N_("Hotkeys management interface"))
    set_capability("interface", 0)
    set_callbacks(Open, Close)
    set_category(CAT_INTERFACE)
    set_subcategory(SUBCAT_INTERFACE_HOTKEYS)
vlc_module_end ()

/*****************************************************************************
 * Action handling
 *****************************************************************************/

#define VLC_INTF_ACTION_HANDLER(name) \
static inline int action_handler_Intf##name(vlc_action_id_t action_id, \
                                            intf_thread_t *intf)
#define VLC_PLAYLIST_ACTION_HANDLER(name) \
static inline int action_handler_Playlist##name(vlc_action_id_t action_id, \
                                                vlc_playlist_t *playlist, \
                                                intf_thread_t *intf)
#define VLC_PLAYER_ACTION_HANDLER(name) \
static inline int action_handler_Player##name(vlc_action_id_t action_id, \
                                              vlc_player_t *player)

VLC_INTF_ACTION_HANDLER(Quit)
{
    VLC_UNUSED(action_id);

    libvlc_Quit(intf->obj.libvlc);
    return VLC_SUCCESS;
}

VLC_INTF_ACTION_HANDLER(Trigger)
{
    char const *varname;
    switch (action_id)
    {
        case ACTIONID_INTF_TOGGLE_FSC:
        case ACTIONID_INTF_HIDE:
            varname = "intf-toggle-fscontrol";
            break;
        case ACTIONID_INTF_BOSS:
            varname = "intf-boss";
            break;
        case ACTIONID_INTF_POPUP_MENU:
            varname = "intf-popupmenu";
            break;
        default:
            vlc_assert_unreachable();
    }
    var_TriggerCallback(intf->obj.libvlc, varname);
    return VLC_SUCCESS;
}

VLC_PLAYLIST_ACTION_HANDLER(Interact)
{
    VLC_UNUSED(intf);

    switch (action_id)
    {
        case ACTIONID_PLAY_CLEAR:
            vlc_playlist_Clear(playlist);
            break;
        case ACTIONID_PREV:
            vlc_playlist_Prev(playlist);
            break;
        case ACTIONID_NEXT:
            vlc_playlist_Next(playlist);
            break;
        default:
            vlc_assert_unreachable();
    }
    return VLC_SUCCESS;
}

VLC_PLAYLIST_ACTION_HANDLER(Playback)
{
    VLC_UNUSED(intf);

    switch (action_id)
    {
        case ACTIONID_LOOP:
        {
            enum vlc_playlist_playback_repeat repeat_mode =
                vlc_playlist_GetPlaybackRepeat(playlist);
            switch (repeat_mode)
            {
                case VLC_PLAYLIST_PLAYBACK_REPEAT_NONE:
                    repeat_mode = VLC_PLAYLIST_PLAYBACK_REPEAT_ALL;
                    break;
                case VLC_PLAYLIST_PLAYBACK_REPEAT_ALL:
                    repeat_mode = VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT;
                    break;
                case VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT:
                    repeat_mode = VLC_PLAYLIST_PLAYBACK_REPEAT_NONE;
                    break;
            }
            vlc_playlist_SetPlaybackRepeat(playlist, repeat_mode);
            break;
        }
        case ACTIONID_RANDOM:
        {
            enum vlc_playlist_playback_order order_mode =
                vlc_playlist_GetPlaybackOrder(playlist);
            order_mode =
                order_mode == VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL
                    ? VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM
                    : VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL;
            vlc_playlist_SetPlaybackOrder(playlist, order_mode);
            break;
        }
        default:
            vlc_assert_unreachable();
    }
    return VLC_SUCCESS;
}

VLC_PLAYLIST_ACTION_HANDLER(Bookmark)
{
    char *psz_bookmark_name;
    if (action_id >= ACTIONID_SET_BOOKMARK1 &&
        action_id <= ACTIONID_SET_BOOKMARK10)
    {
        int bookmark_id = action_id - ACTIONID_SET_BOOKMARK1 + 1;
        if (asprintf(&psz_bookmark_name, "bookmark%i", bookmark_id) == -1)
            return VLC_EGENERIC;
        var_Create(intf, psz_bookmark_name, VLC_VAR_STRING | VLC_VAR_DOINHERIT);
        vlc_player_t *player = vlc_playlist_GetPlayer(playlist);
        input_item_t *item = vlc_player_GetCurrentMedia(player);
        if (item)
        {
            char *psz_uri = input_item_GetURI(item);
            config_PutPsz(psz_bookmark_name, psz_uri);
            msg_Info(intf, "setting playlist bookmark %i to %s",
                     bookmark_id, psz_uri);
            free(psz_uri);
        }
    }
    else if (action_id >= ACTIONID_PLAY_BOOKMARK1 &&
             action_id <= ACTIONID_PLAY_BOOKMARK10)
    {
        int bookmark_id = action_id - ACTIONID_PLAY_BOOKMARK1 + 1;
        if (asprintf(&psz_bookmark_name, "bookmark%i", bookmark_id) == -1)
            return VLC_EGENERIC;
        char *psz_bookmark = var_CreateGetString(intf, psz_bookmark_name);
        size_t count = vlc_playlist_Count(playlist);
        size_t i;
        for (i = 0; i < count; ++i)
        {
            vlc_playlist_item_t *plitem = vlc_playlist_Get(playlist, i);
            input_item_t *item = vlc_playlist_item_GetMedia(plitem);
            char *psz_uri = input_item_GetURI(item);
            if (!strcmp(psz_bookmark, psz_uri))
                break;
            free(psz_uri);
        }
        if (i != count)
            vlc_playlist_PlayAt(playlist, i);
        free(psz_bookmark);
    }
    free(psz_bookmark_name);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(State)
{
    switch (action_id)
    {
        case ACTIONID_PLAY_PAUSE:
        {
            enum vlc_player_state state = vlc_player_GetState(player);
            if (state == VLC_PLAYER_STATE_PAUSED)
                vlc_player_Resume(player);
            else
                vlc_player_Pause(player);
            break;
        }
        case ACTIONID_PLAY:
            vlc_player_Start(player);
            break;
        case ACTIONID_PAUSE:
            vlc_player_Pause(player);
            break;
        case ACTIONID_STOP:
            vlc_player_Stop(player);
            break;
        default:
            vlc_assert_unreachable();
    }
    return VLC_SUCCESS;
}

// hacky shit: should be player callback but needs intf for vars
VLC_PLAYLIST_ACTION_HANDLER(Seek)
{
    vlc_player_t *player = vlc_playlist_GetPlayer(playlist);
    if (!vlc_player_CanSeek(player))
        return VLC_SUCCESS;

    char const *varname;
    int sign = +1;
    switch (action_id)
    {
        case ACTIONID_JUMP_BACKWARD_EXTRASHORT:
            sign = -1;
            /* fall through */
        case ACTIONID_JUMP_FORWARD_EXTRASHORT:
            varname = "extrashort-jump-size";
            break;
        case ACTIONID_JUMP_BACKWARD_SHORT:
            sign = -1;
            /* fall through */
        case ACTIONID_JUMP_FORWARD_SHORT:
            varname = "short-jump-size";
            break;
        case ACTIONID_JUMP_BACKWARD_MEDIUM:
            sign = -1;
            /* fall through */
        case ACTIONID_JUMP_FORWARD_MEDIUM:
            varname = "medium-jump-size";
            break;
        case ACTIONID_JUMP_BACKWARD_LONG:
            sign = -1;
            /* fall through */
        case ACTIONID_JUMP_FORWARD_LONG:
            varname = "long-jump-size";
            break;
        default:
            vlc_assert_unreachable();
    }
    int jmpsz = var_InheritInteger(intf->obj.libvlc, varname);
    if (jmpsz < 0)
        return VLC_EGENERIC;
    vlc_player_JumpTime(player, vlc_tick_from_sec(jmpsz * sign));
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(NextFrame)
{
    VLC_UNUSED(action_id);

    vlc_player_NextVideoFrame(player);
    return VLC_SUCCESS;
}

static inline float AdjustRateFine(float rate, int const dir)
{
    float const rate_min = (float)INPUT_RATE_DEFAULT / INPUT_RATE_MAX;
    float const rate_max = (float)INPUT_RATE_DEFAULT / INPUT_RATE_MIN;
    int const sign = rate < 0 ? -1 : 1;

    rate = floor(fabs(rate) / 0.1 + dir + 0.05) * 0.1;
    if (rate < rate_min)
       rate = rate_min;
    else if (rate > rate_max )
        rate = rate_max;
    return rate * sign;
}

VLC_PLAYER_ACTION_HANDLER(Rate)
{
    switch (action_id)
    {
        case ACTIONID_RATE_SLOWER:
            vlc_player_DecrementRate(player);
            break;
        case ACTIONID_RATE_FASTER:
            vlc_player_IncrementRate(player);
            break;
        default:
        {
            float rate;
            switch (action_id)
            {
                case ACTIONID_RATE_NORMAL:
                    rate = 1.f;
                    break;
                case ACTIONID_RATE_SLOWER_FINE:
                case ACTIONID_RATE_FASTER_FINE:
                {
                    int const dir = action_id == ACTIONID_RATE_SLOWER_FINE ?
                        -1 : +1;
                    rate = vlc_player_GetRate(player);
                    rate = AdjustRateFine(rate, dir);
                    break;
                }
                default:
                    vlc_assert_unreachable();
            }
            vlc_player_ChangeRate(player, rate);
            break;
        }
    }
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Record)
{
    VLC_UNUSED(action_id);

    vlc_player_ToggleRecording(player);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Volume)
{
    switch (action_id)
    {
        case ACTIONID_VOL_DOWN:
            vlc_player_aout_DecrementVolume(player, 1, NULL);
            break;
        case ACTIONID_VOL_UP:
            vlc_player_aout_IncrementVolume(player, 1, NULL);
            break;
        case ACTIONID_VOL_MUTE:
            vlc_player_aout_ToggleMute(player);
            break;
        default:
            vlc_assert_unreachable();
    }
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(CycleAudioDevice)
{
    VLC_UNUSED(action_id);

    return vlc_player_aout_NextDevice(player);
}

VLC_PLAYER_ACTION_HANDLER(ToggleSubtitle)
{
    VLC_UNUSED(action_id);

    vlc_player_ToggleSubtitle(player);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(SubtitleSync)
{
    switch (action_id)
    {
        case ACTIONID_SUBSYNC_MARKAUDIO:
            vlc_player_SubtitleSyncMarkAudio(player);
            break;
        case ACTIONID_SUBSYNC_MARKSUB:
            vlc_player_SubtitleSyncMarkSubtitle(player);
            break;
        case ACTIONID_SUBSYNC_APPLY:
            // FIXME is that still the case?
            /* Warning! Can yield a pause in the playback.
             * For example, the following succession of actions will yield a 5 second delay :
             * - Pressing Shift-H (ACTIONID_SUBSYNC_MARKAUDIO)
             * - wait 5 second
             * - Press Shift-J (ACTIONID_SUBSYNC_MARKSUB)
             * - Press Shift-K (ACTIONID_SUBSYNC_APPLY)
             * --> 5 seconds pause
             * This is due to var_SetTime() (and ultimately UpdatePtsDelay())
             * which causes the video to pause for an equivalent duration
             * (This problem is also present in the "Track synchronization" window) */
            vlc_player_SubtitleSyncApply(player);
            break;
        case ACTIONID_SUBSYNC_RESET:
            vlc_player_SubtitleSyncReset(player);
            break;
        default:
            vlc_assert_unreachable();
    }
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Delay)
{
    enum { AUDIODELAY, SUBDELAY } type;
    int delta = 50;
    switch (action_id)
    {
        case ACTIONID_AUDIODELAY_DOWN:
            delta = -50;
            /* fall-through */
        case ACTIONID_AUDIODELAY_UP:
            type = AUDIODELAY;
            break;
        case ACTIONID_SUBDELAY_DOWN:
            delta = -50;
            /* fall-through */
        case ACTIONID_SUBDELAY_UP:
            type = SUBDELAY;
            break;
        default:
            vlc_assert_unreachable();
    }
    enum vlc_player_whence whence = VLC_PLAYER_WHENCE_RELATIVE;
    delta = VLC_TICK_FROM_MS(delta);
    if (type == AUDIODELAY)
        vlc_player_SetAudioDelay(player, delta, whence);
    else
        vlc_player_SetSubtitleDelay(player, delta, whence);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Track)
{
    switch (action_id)
    {
        case ACTIONID_AUDIO_TRACK:
            vlc_player_SelectNextTrack(player, AUDIO_ES);
            break;
        case ACTIONID_SUBTITLE_TRACK:
            vlc_player_SelectPrevTrack(player, SPU_ES);
            break;
        case ACTIONID_SUBTITLE_REVERSE_TRACK:
            vlc_player_SelectNextTrack(player, SPU_ES);
            break;
        default:
            vlc_assert_unreachable();
    }
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Program)
{
    if (action_id == ACTIONID_PROGRAM_SID_PREV)
        vlc_player_SelectPrevProgram(player);
    else
        vlc_player_SelectNextProgram(player);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(MediaNav)
{
    switch (action_id)
    {
        case ACTIONID_TITLE_PREV:
            vlc_player_SelectPrevTitle(player);
            break;
        case ACTIONID_TITLE_NEXT:
            vlc_player_SelectNextTitle(player);
            break;
        case ACTIONID_CHAPTER_PREV:
            vlc_player_SelectPrevChapter(player);
            break;
        case ACTIONID_CHAPTER_NEXT:
            vlc_player_SelectNextChapter(player);
            break;
        default:
            vlc_assert_unreachable();
    }
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Navigate)
{
    enum vlc_player_nav nav;
    switch (action_id)
    {
#define PLAYER_NAV_FROM_ACTION(navval) \
        case ACTIONID_NAV_##navval: \
            nav = VLC_PLAYER_NAV_##navval; \
            break;
        PLAYER_NAV_FROM_ACTION(ACTIVATE)
        PLAYER_NAV_FROM_ACTION(UP)
        PLAYER_NAV_FROM_ACTION(DOWN)
        PLAYER_NAV_FROM_ACTION(LEFT)
        PLAYER_NAV_FROM_ACTION(RIGHT)
#undef PLAYER_NAV_FROM_ACTION
        default:
            vlc_assert_unreachable();
    }
    vlc_player_Navigate(player, nav);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Viewpoint)
{
    vlc_viewpoint_t viewpoint;
    switch (action_id)
    {
        case ACTIONID_VIEWPOINT_FOV_IN:
            viewpoint.fov = -1.f;
            break;
        case ACTIONID_VIEWPOINT_FOV_OUT:
            viewpoint.fov = +1.f;
            break;
        case ACTIONID_VIEWPOINT_ROLL_CLOCK:
            viewpoint.roll = -1.f;
            break;
        case ACTIONID_VIEWPOINT_ROLL_ANTICLOCK:
            viewpoint.roll = +1.f;
            break;
        default:
            vlc_assert_unreachable();
    }
    vlc_player_UpdateViewpoint(player, &viewpoint,
                               VLC_PLAYER_WHENCE_RELATIVE);
    return VLC_SUCCESS;
}


/*****************************************************************************
 * Video output actions
 *****************************************************************************/

VLC_PLAYER_ACTION_HANDLER(Fullscreen)
{
    switch (action_id)
    {
        case ACTIONID_TOGGLE_FULLSCREEN:
            vlc_player_vout_ToggleFullscreen(player);
            break;
        case ACTIONID_LEAVE_FULLSCREEN:
            vlc_player_vout_SetFullscreen(player, false);
            break;
        default:
            vlc_assert_unreachable();
    }
    return VLC_SUCCESS;
}

static inline void
vout_CycleVariable(vout_thread_t *vout,
                   char const *varname, int vartype, bool next)
{
    vlc_value_t val;
    var_Get(vout, varname, &val);
    size_t num_choices;
    vlc_value_t *choices;
    var_Change(vout, varname, VLC_VAR_GETCHOICES,
               &num_choices, &choices, NULL);

    vlc_value_t *choice = choices;
    for (size_t curidx = 0; curidx < num_choices; ++curidx, ++choice)
        if ((vartype == VLC_VAR_FLOAT &&
             choice->f_float == val.f_float) ||
            (vartype == VLC_VAR_STRING &&
             !strcmp(choice->psz_string, val.psz_string)))
        {
            curidx += next ? +1 : -1;
            if (next && curidx == num_choices)
                curidx = 0;
            else if (!next && curidx == (size_t)-1)
                curidx = num_choices - 1;
            choice = choices + curidx;
            break;
        }
    if (vartype == VLC_VAR_FLOAT)
        var_SetFloat(vout, varname, choice->f_float);
    else if (vartype == VLC_VAR_STRING)
        var_SetString(vout, varname, choice->psz_string);

    if (vartype == VLC_VAR_STRING)
    {
        free(val.psz_string);
        for (size_t i = 0; i < num_choices; ++i)
            free(choices[i].psz_string);
    }
    free(choices);
}

#define vout_CycleVariable(vout, varname, vartype, next) \
    do \
    { \
        static_assert(vartype == VLC_VAR_FLOAT || \
                      vartype == VLC_VAR_STRING, \
                      "vartype must be either VLC_VAR_FLOAT or VLC_VAR_STRING"); \
        vout_CycleVariable(vout, varname, vartype, next); \
    } while (0)


VLC_PLAYER_ACTION_HANDLER(AspectRatio)
{
    VLC_UNUSED(action_id);
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    vout_CycleVariable(vout, "aspect-ratio", VLC_VAR_STRING, true);
    vlc_object_release(vout);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Crop)
{
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    if (action_id == ACTIONID_CROP)
        vout_CycleVariable(vout, "crop", VLC_VAR_STRING, true);
    else
    {
        char const *varname;
        int delta;
        switch (action_id)
        {
#define CASE_CROP(crop, var) \
    case ACTIONID_CROP_##crop: \
    case ACTIONID_UNCROP_##crop: \
        varname = "crop-"#var; \
        delta = action_id == ACTIONID_CROP_##crop? +1 : -1; \
        break;
            CASE_CROP(TOP, top)
            CASE_CROP(BOTTOM, bottom)
            CASE_CROP(LEFT, left)
            CASE_CROP(RIGHT, right)
#undef CASE_CROP
            default:
                vlc_assert_unreachable();
        }
        int crop = var_GetInteger(vout, varname);
        var_SetInteger(vout, varname, crop + delta);
    }
    vlc_object_release(vout);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Zoom)
{
    char const *varname = "zoom";
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    switch (action_id)
    {
        case ACTIONID_TOGGLE_AUTOSCALE:
            if (var_GetFloat(vout, varname) != 1.f)
                var_SetFloat(vout, varname, 1.f);
            else
                var_ToggleBool(vout, "autoscale");
            break;
        case ACTIONID_SCALE_DOWN:
        case ACTIONID_SCALE_UP:
        {
            float zoom = var_GetFloat(vout, varname);
            if (zoom >= .3f && zoom <= 10.f)
                var_SetFloat(vout, varname,
                             zoom + ACTIONID_SCALE_DOWN ? -1.f : +1.f);
            break;
        }
        case ACTIONID_ZOOM:
        case ACTIONID_UNZOOM:
            vout_CycleVariable(vout, varname, VLC_VAR_FLOAT,
                               action_id == ACTIONID_ZOOM);
            break;
        default:
        {
            static float const zoom_modes[] = { .25f, .5f, 1.f, 2.f };
            var_SetFloat(vout, varname,
                         zoom_modes[action_id - ACTIONID_ZOOM_QUARTER]);
            break;
        }
    }
    vlc_object_release(vout);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Deinterlace)
{
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    if (action_id == ACTIONID_DEINTERLACE)
        var_SetInteger(vout, "deinterlace",
                       var_GetInteger(vout, "deinterlace") ? 0 : 1);
    else if (action_id == ACTIONID_DEINTERLACE_MODE)
        vout_CycleVariable(vout, "deinterlace-mode", VLC_VAR_STRING, true);
    vlc_object_release(vout);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(SubtitleDisplay)
{
    vout_thread_t *vout = vlc_player_vout_Hold(player);
    switch (action_id)
    {
        case ACTIONID_SUBPOS_DOWN:
            var_DecInteger(vout, "sub-margin");
            break;
        case ACTIONID_SUBPOS_UP:
            var_IncInteger(vout, "sub-margin");
            break;
        default:
        {
            char const *varname = "sub-text-scale";
            int scale;
            if (action_id == ACTIONID_SUBTITLE_TEXT_SCALE_NORMAL)
                scale = 100;
            else
            {
                scale = var_GetInteger(vout, varname);
                scale += action_id == ACTIONID_SUBTITLE_TEXT_SCALE_DOWN ?
                    -25 : +25;
                scale = VLC_CLIP(scale, 25, 500);
            }
            var_SetInteger(vout, varname, scale);
            break;
        }
    }
    vlc_object_release(vout);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(WallpaperMode)
{
    VLC_UNUSED(action_id);
    vlc_player_vout_ToggleWallpaperMode(player);
    return VLC_SUCCESS;
}

VLC_PLAYER_ACTION_HANDLER(Snapshot)
{
    VLC_UNUSED(action_id);
    vlc_player_vout_Snapshot(player);
    return VLC_SUCCESS;
}

//static int PutAction(intf_thread_t *intf,
//                     int slider_chan, bool b_vrnav, int action)
//{
//#define DO_ACTION(x) PutAction(intf, slider_chan, b_vrnav, x)
//
//    switch (action)
//    {
//        case ACTIONID_DISC_MENU:
//            // TODO what the fuck is this??? oO
//            //if( p_input )
//            //    var_SetInteger( p_input, "title  0", 2 );
//            DisplayMessage(vouts, vout_count, _("No fucking idea what this is supposed to do"));
//            break;
//
//        /* Input + video output */
//        case ACTIONID_POSITION:
//            // TODO
//            // if( p_vout && vout_OSDEpg( p_vout, input_GetItem( p_input ) ) )
//            //     DisplayPosition(p_vout, slider_chan, p_input );
//            break;
//
//        case ACTIONID_COMBO_VOL_FOV_UP:
//            if (b_vrnav)
//                DO_ACTION(ACTIONID_VIEWPOINT_FOV_IN);
//            else
//                DO_ACTION(ACTIONID_VOL_UP);
//            break;
//        case ACTIONID_COMBO_VOL_FOV_DOWN:
//            if (b_vrnav)
//                DO_ACTION(ACTIONID_VIEWPOINT_FOV_OUT);
//            else
//                DO_ACTION(ACTIONID_VOL_DOWN);
//            break;
//    }
//}

struct vlc_action
{
    enum
    {
        NULL_ACTION = -1,
        INTF_ACTION,
        PLAYLIST_ACTION,
        PLAYER_ACTION,
    } type;

    struct
    {
        vlc_action_id_t first;
        vlc_action_id_t last;
    } range;

    union
    {
        void *fptr;
        int (*pf_intf)(vlc_action_id_t, intf_thread_t *);
        int (*pf_playlist)(vlc_action_id_t, vlc_playlist_t *, intf_thread_t *);
        int (*pf_player)(vlc_action_id_t, vlc_player_t *);
    } handler;

    bool pl_need_lock;
};

#define VLC_ACTIONS(first_action, last_action, name, lock) \
    { \
        .type = ACTION_TYPE, \
        .range = { ACTIONID_##first_action, ACTIONID_##last_action }, \
        .handler.fptr = action_handler_##name, \
        .pl_need_lock = lock \
    },
#define VLC_ACTION(action, handler_name, pl_need_lock) \
    VLC_ACTIONS(action, action, handler_name, pl_need_lock)

static struct vlc_action const actions[] =
{
    /* libVLC / interface actions */
#define ACTION_TYPE INTF_ACTION
    VLC_ACTION(QUIT, IntfQuit, false)
    VLC_ACTIONS(INTF_TOGGLE_FSC, INTF_POPUP_MENU, IntfTrigger, false)
#undef ACTION_TYPE

    /* playlist actions */
#define ACTION_TYPE PLAYLIST_ACTION
    VLC_ACTIONS(PLAY_CLEAR, NEXT, PlaylistInteract, true)
    VLC_ACTIONS(LOOP, RANDOM, PlaylistPlayback, true)
    VLC_ACTIONS(SET_BOOKMARK1, PLAY_BOOKMARK10, PlaylistBookmark, true)
    VLC_ACTIONS(JUMP_BACKWARD_EXTRASHORT, JUMP_FORWARD_LONG, PlaylistSeek, true) // FIXME hack, should be player action
#undef ACTION_TYPE

    /* player actions */
#define ACTION_TYPE PLAYER_ACTION
    VLC_ACTIONS(RATE_SLOWER, RATE_FASTER, PlayerRate, true)
    VLC_ACTIONS(RATE_NORMAL, RATE_FASTER_FINE, PlayerRate, true)

    VLC_ACTIONS(VOL_DOWN, VOL_MUTE, PlayerVolume, false)
    VLC_ACTION(AUDIODEVICE_CYCLE, PlayerCycleAudioDevice, false)

    VLC_ACTIONS(PLAY_PAUSE, STOP, PlayerState, true)
    VLC_ACTION(FRAME_NEXT, PlayerNextFrame, true)

    VLC_ACTION(RECORD, PlayerRecord, true)

    VLC_ACTION(SUBTITLE_TOGGLE, PlayerToggleSubtitle, true)
    VLC_ACTIONS(SUBSYNC_MARKAUDIO, SUBSYNC_RESET, PlayerSubtitleSync, true)

    VLC_ACTIONS(SUBDELAY_UP, SUBDELAY_DOWN, PlayerDelay, true)
    VLC_ACTIONS(AUDIODELAY_UP, AUDIODELAY_DOWN, PlayerDelay, true)

    VLC_ACTIONS(AUDIO_TRACK, SUBTITLE_TRACK, PlayerTrack, true)

    VLC_ACTIONS(PROGRAM_SID_NEXT, PROGRAM_SID_PREV, PlayerProgram, true)

    VLC_ACTIONS(TITLE_PREV, CHAPTER_NEXT, PlayerMediaNav, true)
    VLC_ACTIONS(NAV_ACTIVATE, NAV_RIGHT, PlayerNavigate, true)

    VLC_ACTION(WALLPAPER, PlayerWallpaperMode, false)
    VLC_ACTION(SNAPSHOT, PlayerSnapshot, false)
    VLC_ACTIONS(TOGGLE_FULLSCREEN, LEAVE_FULLSCREEN, PlayerFullscreen, false)
    VLC_ACTION(ASPECT_RATIO, PlayerAspectRatio, false)
    VLC_ACTIONS(CROP, UNCROP_RIGHT, PlayerCrop, false)
    VLC_ACTIONS(VIEWPOINT_FOV_IN, VIEWPOINT_ROLL_ANTICLOCK, PlayerViewpoint, false)
    VLC_ACTIONS(TOGGLE_AUTOSCALE, ZOOM_DOUBLE, PlayerZoom, false)
    VLC_ACTIONS(DEINTERLACE, DEINTERLACE_MODE, PlayerDeinterlace, false)

    VLC_ACTIONS(SUBPOS_DOWN, SUBTITLE_TEXT_SCALE_UP, PlayerSubtitleDisplay, false)
#undef ACTION_TYPE

    { .type = NULL_ACTION }
};

#undef VLC_ACTION_ATOMIC
#undef VLC_ACTION

/*****************************************************************************
 * Hotkey actions callback
 *****************************************************************************/
static int ActionCallback(vlc_object_t *libvlc, char const *psz_var,
                          vlc_value_t oldval, vlc_value_t newval, void *data)
{
    VLC_UNUSED(libvlc); VLC_UNUSED(psz_var); VLC_UNUSED(oldval);

    intf_thread_t *intf = (intf_thread_t *)data;
    vlc_action_id_t action_id = newval.i_int;

    size_t action_idx;
    for (action_idx = 0; actions[action_idx].type != NULL_ACTION; ++action_idx)
        if (actions[action_idx].range.first <= action_id &&
            actions[action_idx].range.last >= action_id)
            break;
    if (actions[action_idx].type == NULL_ACTION)
    {
        msg_Warn(intf, "no handler for action %d", action_id);
        return VLC_EGENERIC;
    }

    struct vlc_action const *action = actions + action_idx;
    int ret;
    switch (action->type)
    {
        case INTF_ACTION:
            ret = action->handler.pf_intf(action_id, intf);
            break;
        case PLAYLIST_ACTION:
        case PLAYER_ACTION:
        {
            vlc_playlist_t *playlist = intf->p_sys->playlist;
            if (action->pl_need_lock)
                vlc_playlist_Lock(playlist);
            if (action->type == PLAYLIST_ACTION)
                ret = action->handler.pf_playlist(action_id, playlist, intf);
            else
            {
                vlc_player_t *player = vlc_playlist_GetPlayer(playlist);
                ret = action->handler.pf_player(action_id, player);
            }
            if (action->pl_need_lock)
                vlc_playlist_Unlock(playlist);
            break;
        }
        default:
            vlc_assert_unreachable();
    }
    return ret;
//
//    vlc_mutex_lock(&sys->lock);
//    int slider_chan = sys->slider_chan;
//    bool b_vrnav = sys->vrnav.b_can_change;
//    vlc_mutex_unlock(&sys->lock);
//
//    return PutAction(intf, slider_chan, b_vrnav, newval.i_int);
}

static int MovedEvent(vlc_object_t *this, char const *psz_var,
                      vlc_value_t oldval, vlc_value_t newval, void *data)
{
    VLC_UNUSED(this); VLC_UNUSED(psz_var); VLC_UNUSED(oldval);

    intf_sys_t *sys = ((intf_thread_t *)data)->p_sys;
    vlc_player_t *player = vlc_playlist_GetPlayer(sys->playlist);
    int ret = VLC_SUCCESS;

    if (sys->vrnav.b_button_pressed)
    {
        int i_horizontal = newval.coords.x - sys->vrnav.x;
        int i_vertical   = newval.coords.y - sys->vrnav.y;

        vlc_viewpoint_t viewpoint = {
            .yaw   = -i_horizontal * 0.05f,
            .pitch = -i_vertical   * 0.05f,
        };

        vlc_player_Lock(player);
        vlc_player_UpdateViewpoint(player, &viewpoint,
                                   VLC_PLAYER_WHENCE_RELATIVE);
        vlc_player_Unlock(player);

        if (ret == VLC_SUCCESS)
        {
            sys->vrnav.x = newval.coords.x;
            sys->vrnav.y = newval.coords.y;
        }
    }

    return ret;
}

static int ViewpointMovedEvent(vlc_object_t *this, char const *psz_var,
                               vlc_value_t oldval, vlc_value_t newval,
                               void *data)
{
    VLC_UNUSED(this); VLC_UNUSED(psz_var); VLC_UNUSED(oldval);

    vlc_playlist_t *playlist = ((intf_thread_t *)data)->p_sys->playlist;
    vlc_player_t *player = vlc_playlist_GetPlayer(playlist);
    vlc_player_Lock(player);
    vlc_player_UpdateViewpoint(player, (vlc_viewpoint_t *)newval.p_address,
		    	       VLC_PLAYER_WHENCE_RELATIVE);
    vlc_player_Unlock(player);

    return VLC_SUCCESS;
}

static int ButtonEvent(vlc_object_t *this, char const *psz_var,
                       vlc_value_t oldval, vlc_value_t newval, void *data)
{
    VLC_UNUSED(psz_var);

    intf_thread_t *intf = data;
    intf_sys_t *sys = intf->p_sys;

    if ((newval.i_int & (1 << MOUSE_BUTTON_LEFT)) && sys->vrnav.b_can_change)
    {
        if (!sys->vrnav.b_button_pressed)
        {
            sys->vrnav.b_button_pressed = true;
            var_GetCoords(this, "mouse-moved", &sys->vrnav.x, &sys->vrnav.y);
        }
    }
    else
        sys->vrnav.b_button_pressed = false;

    unsigned pressed = newval.i_int & ~oldval.i_int;

    if (pressed & (1 << MOUSE_BUTTON_LEFT))
        var_SetBool(intf->obj.libvlc, "intf-popupmenu", false);
    if (pressed & (1 << MOUSE_BUTTON_CENTER))
        var_TriggerCallback(intf->obj.libvlc, "intf-toggle-fscontrol");
#ifndef _WIN32
    if (pressed & (1 << MOUSE_BUTTON_RIGHT))
#else
    if ((oldval.i_int & (1 << MOUSE_BUTTON_RIGHT))
     && !(newval.i_int & (1 << MOUSE_BUTTON_RIGHT)))
#endif
        var_SetBool(intf->obj.libvlc, "intf-popupmenu", true);

    for (int i = MOUSE_BUTTON_WHEEL_UP; i <= MOUSE_BUTTON_WHEEL_RIGHT; i++)
        if (pressed & (1 << i))
            var_SetInteger(intf->obj.libvlc, "key-pressed",
                           i - MOUSE_BUTTON_WHEEL_UP + KEY_MOUSEWHEELUP);

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Open: initialize interface
 *****************************************************************************/
static int Open(vlc_object_t *this)
{
    intf_thread_t *intf = (intf_thread_t *)this;
    intf_sys_t *sys;
    sys = malloc(sizeof(intf_sys_t));
    if (!sys)
        return VLC_ENOMEM;
    intf->p_sys = sys;
    // sys->vrnav.b_can_change = false;
    // sys->vrnav.b_button_pressed = false;
    sys->playlist = vlc_intf_GetMainPlaylist(intf);
    var_AddCallback(intf->obj.libvlc, "key-action", ActionCallback, intf);
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: destroy interface
 *****************************************************************************/
static void Close(vlc_object_t *this)
{
    intf_thread_t *intf = (intf_thread_t *)this;
    var_DelCallback(intf->obj.libvlc, "key-action", ActionCallback, intf);
    free(intf->p_sys);
}

//static void
//DisplayPosition(vout_thread_t **vouts, size_t vout_count,
//                int slider_chan, vlc_player_t *player)
//{
//    char psz_duration[MSTRTIME_MAX_SIZE];
//    char psz_time[MSTRTIME_MAX_SIZE];
//
//    int64_t t = SEC_FROM_VLC_TICK(vlc_player_GetTime(player));
//    int64_t l = SEC_FROM_VLC_TICK(vlc_player_GetLength(player));
//
//    secstotimestr( psz_time, t );
//
//    ClearChannels(vouts, vout_count, slider_chan);
//
//    if (l > 0)
//    {
//        secstotimestr(psz_duration, l);
//        DisplayMessage(vouts, vout_count, "%s / %s", psz_time, psz_duration);
//    }
//    else if (t > 0)
//        DisplayMessage(vouts, vout_count, "%s", psz_time);
//
//    if (vlc_player_vout_IsFullscreen(player))
//    {
//        int pos = vlc_player_GetPosition(player) * 100;
//        for (size_t i = 0; i < vout_count; ++i)
//            vout_OSDSlider(vouts[i], slider_chan, pos, OSD_HOR_SLIDER);
//    }
//}
