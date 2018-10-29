/*****************************************************************************
 * vlc_player.h: player interface
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

#ifndef VLC_PLAYER_H
#define VLC_PLAYER_H 1

#include <vlc_input.h>
#include <vlc_aout.h>

/**
 * @defgroup player Player
 * @ingroup input
 * VLC Player API
 * @brief
@dot
digraph player_states {
  label="Player state diagram";
  new [style="invis"];
  started [label="Started" URL="@ref VLC_PLAYER_STATE_STARTED"];
  playing [label="Playing" URL="@ref VLC_PLAYER_STATE_PLAYING"];
  paused [label="Paused" URL="@ref VLC_PLAYER_STATE_PAUSED"];
  stopping [label="Stopping" URL="@ref VLC_PLAYER_STATE_STOPPING"];
  stopped [label="Stopped" URL="@ref VLC_PLAYER_STATE_STOPPED"];
  new -> stopped [label="vlc_player_New()" URL="@ref vlc_player_New" fontcolor="green3"];
  started -> playing [style="dashed" label=<<i>internal transition</i>>];
  started -> stopping [label="vlc_player_Stop()" URL="@ref vlc_player_Stop" fontcolor="red"];
  playing -> paused [label="vlc_player_Pause()" URL="@ref vlc_player_Pause" fontcolor="blue"];
  paused -> playing [label="vlc_player_Resume()" URL="@ref vlc_player_Resume" fontcolor="blue3"];
  paused -> stopping [label="vlc_player_Stop()" URL="@ref vlc_player_Stop" fontcolor="red"];
  playing -> stopping [label="vlc_player_Stop()" URL="@ref vlc_player_Stop" fontcolor="red"];
  stopping -> stopped [style="dashed" label=<<i>internal transition</i>>];
  stopped -> started [label="vlc_player_Start()" URL="@ref vlc_player_Start" fontcolor="darkgreen"];
}
@enddot
 * @{
 * @file
 * VLC Player API
 */

/**
 * Player opaque structure.
 */
typedef struct vlc_player_t vlc_player_t;

/**
 * Player listener opaque structure.
 *
 * This opaque structure is returned by vlc_player_AddListener() and can be
 * used to remove the listener via vlc_player_RemoveListener().
 */
typedef struct vlc_player_listener_id vlc_player_listener_id;

/**
 * Player vout listener opaque structure.
 *
 * This opaque structure is returned by vlc_player_vout_AddListener() and can
 * be used to remove the listener via vlc_player_vout_RemoveListener().
 */
typedef struct vlc_player_vout_listener_id vlc_player_vout_listener_id;

/**
 * Player aout listener opaque structure.
 *
 * This opaque structure is returned by vlc_player_aout_AddListener() and can
 * be used to remove the listener via vlc_player_aout_RemoveListener().
 */
typedef struct vlc_player_aout_listener_id vlc_player_aout_listener_id;

/**
 * Player program structure.
 */
struct vlc_player_program
{
    /** Id used for vlc_player_SelectProgram() */
    int group_id;
    /** Program name, always valid */
    const char *name;
    /** True if the program is selected */
    bool selected;
    /** True if the program is scrambled */
    bool scrambled;
};

/**
 * Player track structure.
 */
struct vlc_player_track
{
    /** Id used for any player actions, like vlc_player_SelectTrack() */
    vlc_es_id_t *es_id;
    /** Track name, always valid */
    const char *name;
    /** Es format */
    es_format_t fmt;
    /** True if the track is selected */
    bool selected;
};

/**
 * Player chapter structure
 */
struct vlc_player_chapter
{
    /** Chapter name, always valid */
    const char *name;
    /** Position of this chapter */
    vlc_tick_t time;
};

/** vlc_player_title.flags: The title is a menu. */
#define VLC_PLAYER_TITLE_MENU         0x01
/** vlc_player_title.flags: The title is interactive. */
#define VLC_PLAYER_TITLE_INTERACTIVE  0x02

/** Player title structure */
struct vlc_player_title
{
    /** Title name, always valid */
    const char *name;
    /** Length of the title */
    vlc_tick_t length;
    /** Bit flag of @ref VLC_PLAYER_TITLE_MENU and @ref
     * VLC_PLAYER_TITLE_INTERACTIVE */
    unsigned flags;
    /** Number of chapters, can be 0 */
    size_t chapter_count;
    /** Array of chapters, can be NULL */
    const struct vlc_player_chapter *chapters;
};

/**
 * Opaque structure representing a list of @ref vlc_player_title.
 *
 * @see vlc_player_GetTitleList()
 * @see vlc_player_title_list_GetCount()
 * @see vlc_player_title_list_GetAt()
 */
typedef struct vlc_player_title_list vlc_player_title_list;

/**
 * Menu (VCD/DVD/BD) and viewpoint navigations
 *
 * @see vlc_player_Navigate()
 */
enum vlc_player_nav
{
    /** Activate the navigation item selected */
    VLC_PLAYER_NAV_ACTIVATE,
    /** Select a navigation item above or move the viewpoint up */
    VLC_PLAYER_NAV_UP,
    /** Select a navigation item under or move the viewpoint down */
    VLC_PLAYER_NAV_DOWN,
    /** Select a navigation item on the left or move the viewpoint left */
    VLC_PLAYER_NAV_LEFT,
    /** Select a navigation item on the right or move the viewpoint right */
    VLC_PLAYER_NAV_RIGHT,
    /** Activate the popup Menu (for BD) */
    VLC_PLAYER_NAV_POPUP,
    /** Activate disc Root Menu */
    VLC_PLAYER_NAV_MENU,
};

/**
 * Action of vlc_player_cbs.on_track_list_changed,
 * vlc_player_cbs.on_program_list_changed, and
 * vlc_player_cbs.on_vout_list_changed callbacks
 */
enum vlc_player_list_action
{
    VLC_PLAYER_LIST_ADDED,
    VLC_PLAYER_LIST_REMOVED,
    VLC_PLAYER_LIST_UPDATED,
};

/**
 * State of the player
 *
 * During a normal playback (no errors), the user is expected to receive all
 * events in the following order: STARTED, PLAYING, STOPPING, STOPPED.
 *
 * @note When playing more than one media in a row, the player stay at the
 * PLAYING state when doing the transition from the current media to the next
 * media (that can be gapless). This means that STOPPING, STOPPED states (for
 * the current media) and STARTED, PLAYING states (for the next one) won't be
 * sent. Nevertheless, the vlc_player_cbs.on_current_media_changed callback
 * will be called during this transition.
 */
enum vlc_player_state
{
    /**
     * The player is stopped
     *
     * Initial state, or triggered by an internal transition from the STOPPING
     * state.
     */
    VLC_PLAYER_STATE_STOPPED,

    /**
     * The player is started
     *
     * Triggered by vlc_player_Start()
     */
    VLC_PLAYER_STATE_STARTED,

    /**
     * The player is playing
     *
     * Triggered by vlc_player_Resume() or by an internal transition from the
     * STARTED state.
     */
    VLC_PLAYER_STATE_PLAYING,

    /**
     * The player is paused
     *
     * Triggered by vlc_player_Pause().
     */
    VLC_PLAYER_STATE_PAUSED,

    /**
     * The player is stopping
     *
     * Triggered by vlc_player_Stop(), vlc_player_SetCurrentMedia() or by an
     * internal transition (when the media reach the end of file for example).
     */
    VLC_PLAYER_STATE_STOPPING,
};

/**
 * Error of the player
 *
 * @see vlc_player_GetError()
 */
enum vlc_player_error
{
    VLC_PLAYER_ERROR_NONE,
    VLC_PLAYER_ERROR_GENERIC,
};

/**
 * Seek speed type
 *
 * @see vlc_player_SeekByPos()
 * @see vlc_player_SeekByTime()
 */
enum vlc_player_seek_speed
{
    /** Do a precise seek */
    VLC_PLAYER_SEEK_PRECISE,
    /** Do a fast seek */
    VLC_PLAYER_SEEK_FAST,
};

/**
 * Player seek/delay directive
 *
 * @see vlc_player_SeekByPos()
 * @see vlc_player_SeekByTime()
 * @see vlc_player_SetAudioDelay()
 * @see vlc_player_SetSubtitleDelay()
 */
enum vlc_player_whence
{
    /** Given time/position */
    VLC_PLAYER_WHENCE_ABSOLUTE,
    /** The current position +/- the given time/position */
    VLC_PLAYER_WHENCE_RELATIVE,
};

/**
 * Action when the player is stopped
 *
 * @see vlc_player_SetMediaStoppedAction()
 */
enum vlc_player_media_stopped_action {
    /** Continue (or stop if there is no next media), default behavior */
    VLC_PLAYER_MEDIA_STOPPED_CONTINUE,
    /** Pause when reaching the end of file */
    VLC_PLAYER_MEDIA_STOPPED_PAUSE,
    /** Stop, even if there is a next media to play */
    VLC_PLAYER_MEDIA_STOPPED_STOP,
    /** Exit VLC */
    VLC_PLAYER_MEDIA_STOPPED_EXIT,
};

/**
 * A to B loop state
 */
enum vlc_player_abloop
{
    VLC_PLAYER_ABLOOP_NONE,
    VLC_PLAYER_ABLOOP_A,
    VLC_PLAYER_ABLOOP_B,
};

/**
 * Track selection
 */
enum vlc_player_track_select
{
    VLC_PLAYER_TRACK_SELECT_PREV,
    VLC_PLAYER_TRACK_SELECT_NEXT,
};

/**
 * Subtitle position
 */
enum vlc_player_subtitle_direction
{
    VLC_PLAYER_SUBTITLE_DIR_DOWN,
    VLC_PLAYER_SUBTITLE_DIR_UP,
};

/**
 * Subtitle scale
 */
enum vlc_player_subtitle_scale
{
    VLC_PLAYER_SUBTITLE_SCALE_NORMAL,
    VLC_PLAYER_SUBTITLE_SCALE_DOWN,
    VLC_PLAYER_SUBTITLE_SCALE_UP,
};

/**
 * Subtitle sync es cat
 */

/**
 * Crop mode
 */
enum vlc_player_crop
{
    VLC_PLAYER_CROP_TOP,
    VLC_PLAYER_CROP_BOTTOM,
    VLC_PLAYER_CROP_LEFT,
    VLC_PLAYER_CROP_RIGHT,
};

/**
 * Zoom mode
 */
enum vlc_player_zoom_mode
{
    VLC_PLAYER_ZOOM_QUARTER,
    VLC_PLAYER_ZOOM_HALF,
    VLC_PLAYER_ZOOM_NORMAL,
    VLC_PLAYER_ZOOM_DOUBLE,
};

/**
 * Zoom direction
 */
enum vlc_player_zoom_direction
{
    VLC_PLAYER_ZOOM_IN,
    VLC_PLAYER_ZOOM_OUT,
};

/**
 * Cycle mode
 */
enum vlc_player_cycle
{
    VLC_PLAYER_CYCLE_PREV,
    VLC_PLAYER_CYCLE_NEXT,
};

/**
 * OSD mode
 */
enum vlc_player_osd
{
    VLC_PLAYER_OSD_TEXT,
    VLC_PLAYER_OSD_ICON,
    VLC_PLAYER_OSD_SLIDER,
};

/** Player capability: can seek */
#define VLC_PLAYER_CAP_SEEK (1<<0)
/** Player capability: can pause */
#define VLC_PLAYER_CAP_PAUSE (1<<1)
/** Player capability: can change the rate */
#define VLC_PLAYER_CAP_CHANGE_RATE (1<<2)
/** Player capability: can seek back */
#define VLC_PLAYER_CAP_REWIND (1<<3)

/** Player teletext key: Red */
#define VLC_PLAYER_TELETEXT_KEY_RED ('r' << 16)
/** Player teletext key: Green */
#define VLC_PLAYER_TELETEXT_KEY_GREEN ('g' << 16)
/** Player teletext key: Yellow */
#define VLC_PLAYER_TELETEXT_KEY_YELLOW ('y' << 16)
/** Player teletext key: Blue */
#define VLC_PLAYER_TELETEXT_KEY_BLUE ('b' << 16)
/** Player teletext key: Index */
#define VLC_PLAYER_TELETEXT_KEY_INDEX ('i' << 16)

/**
 * Callbacks for the owner of the player.
 *
 * These callbacks are needed to control the player flow (via the
 * vlc_playlist_t as a owner for example). It can only be set when creating the
 * player via vlc_player_New().
 *
 * All callbacks are called with the player locked (cf. vlc_player_Lock()), and
 * from any thread (even the current one).
 */
struct vlc_player_media_provider
{
    /**
     * Called when the player requires a new media
     *
     * @note The returned media must be already held with input_item_Hold()
     *
     * @param player locked player instance
     * @param data opaque pointer set from vlc_player_New()
     * @return the next media to play, held by the callee with input_item_Hold()
     */
    input_item_t *(*get_next)(vlc_player_t *player, void *data);
};

/**
 * Player callbacks
 *
 * Can be registered with vlc_player_AddListener().
 *
 * All callbacks are called with the player locked (cf. vlc_player_Lock()) and
 * from any threads (and even synchronously from a vlc_player function in some
 * cases). It is safe to call any vlc_player functions from these callbacks
 * except vlc_player_Delete().
 *
 * @warning To avoid deadlocks, users should never call vlc_player functions
 * with an external mutex locked and lock this same mutex from a player
 * callback.
 */
struct vlc_player_cbs
{
    /**
     * Called when the current media has changed
     *
     * @note This can be called from the PLAYING state (when the player plays
     * the next media internally) or from the STOPPED state (from
     * vlc_player_SetCurrentMedia() or from an internal transition).
     *
     * @see vlc_player_SetCurrentMedia()
     * @see vlc_player_InvalidateNextMedia()
     *
     * @param player locked player instance
     * @param new_media new media currently played or NULL (when there is no
     * more media to play)
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_current_media_changed)(vlc_player_t *player,
        input_item_t *new_media, void *data);

    /**
     * Called when the player state has changed
     *
     * @see vlc_player_state
     *
     * @param player locked player instance
     * @param new_state new player state
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_state_changed)(vlc_player_t *player,
        enum vlc_player_state new_state, void *data);

    /**
     * Called when a media triggered an error
     *
     * Can be called from any states. When it happens the player will stop
     * itself. It is safe to play an other media or event restart the player
     * (This will reset the error state).
     *
     * @param player locked player instance
     * @param error player error
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_error_changed)(vlc_player_t *player,
        enum vlc_player_error error, void *data);

    /**
     * Called when the player buffering (or cache) has changed
     *
     * This event is always called with the 0 and 1 values before a playback
     * (in case of success).  Values in between depends on the media type.
     *
     * @param player locked player instance
     * @param new_buffering buffering in the range [0:1]
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_buffering_changed)(vlc_player_t *player,
        float new_buffering, void *data);

    /**
     * Called when the player rate has changed
     *
     * Triggered by vlc_player_ChangeRate(), not sent when the media starts
     * with the default rate (1.f)
     *
     * @param player locked player instance
     * @param new_rate player
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_rate_changed)(vlc_player_t *player,
        float new_rate, void *data);

    /**
     * Called when the media capabilities has changed
     *
     * Always called when the media is opening. Can be called during playback.
     *
     * @param player locked player instance
     * @param new_caps player capabilities
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_capabilities_changed)(vlc_player_t *player,
        int new_caps, void *data);

    /**
     * Called when the player position has changed
     *
     * @note A started and playing media doesn't have necessarily a valid time.
     *
     * @param player locked player instance
     * @param new_time a valid time or VLC_TICK_INVALID
     * @param new_pos a valid position
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_position_changed)(vlc_player_t *player,
        vlc_tick_t new_time, float new_pos, void *data);

    /**
     * Called when the media length has changed
     *
     * May be called when the media is opening or during playback.
     *
     * @note A started and playing media doesn't have necessarily a valid length.
     *
     * @param player locked player instance
     * @param new_length a valid time or VLC_TICK_INVALID
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_length_changed)(vlc_player_t *player,
        vlc_tick_t new_length, void *data);

    /**
     * Called when a track is added, removed, or updated
     *
     * @note The track is only valid from this callback context. Users should
     * duplicate this track via vlc_player_track_Dup() if they want to use it
     * from an other context.
     *
     * @param player locked player instance
     * @param action added, removed or updated
     * @param track valid track
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_track_list_changed)(vlc_player_t *player,
        enum vlc_player_list_action action,
        const struct vlc_player_track *track, void *data);

    /**
     * Called when a new track is selected and/or unselected
     *
     * @note This event can be called with both unselected_id and selected_id
     * valid. This mean that a new track is replacing the old one.
     *
     * @param player locked player instance
     * @param unselected_id valid track id or NULL (when nothing is unselected)
     * @param selected_id valid track id or NULL (when nothing is selected)
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_track_selection_changed)(vlc_player_t *player,
        vlc_es_id_t *unselected_id, vlc_es_id_t *selected_id, void *data);

    /**
     * Called when a new program is added, removed or updated
     *
     * @note The program is only valid from this callback context. Users should
     * duplicate this program via vlc_player_program_Dup() if they want to use
     * it from an other context.
     *
     * @param player locked player instance
     * @param action added, removed or updated
     * @param prgm valid program
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_program_list_changed)(vlc_player_t *player,
        enum vlc_player_list_action action,
        const struct vlc_player_program *prgm, void *data);

    /**
     * Called when a new program is selected and/or unselected
     *
     * @note This event can be called with both unselected_id and selected_id
     * valid. This mean that a new program is replacing the old one.
     *
     * @param player locked player instance
     * @param unselected_id valid program id or -1 (when nothing is unselected)
     * @param selected_id valid program id or -1 (when nothing is selected)
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_program_selection_changed)(vlc_player_t *player,
        int unselected_id, int selected_id, void *data);

    /**
     * Called when the media titles has changed
     *
     * This event is not called when the opening media doesn't have any titles.
     * This title list and all its elements are constant. If an element is to
     * be updated, a new list will be sent from this callback.
     *
     * @note Users should hold this list with vlc_player_title_list_Hold() if
     * they want to use it from an other context.
     *
     * @param player locked player instance
     * @param titles valid title list or NULL
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_titles_changed)(vlc_player_t *player,
        vlc_player_title_list *titles, void *data);

    /**
     * Called when a new title is selected
     *
     * There are no events when a title is unselected. Titles are automatically
     * unselected when the title list changes. Titles and indexes are always
     * valid inside the vlc_player_title_list sent by
     * vlc_player_cbs.on_titles_changed.
     *
     * @param player locked player instance
     * @param new_title new selected title
     * @param new_idx index of this title
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_title_selection_changed)(vlc_player_t *player,
        const struct vlc_player_title *new_title, size_t new_idx, void *data);

    /**
     * Called when a new chapter is selected
     *
     * There are no events when a chapter is unselected. Chapters are
     * automatically unselected when the title list changes. Titles, chapters
     * and indexes are always valid inside the vlc_player_title_list sent by
     * vlc_player_cbs.on_titles_changed.
     *
     * @param player locked player instance
     * @param title selected title
     * @param title_idx selected title index
     * @param chapter new selected chapter
     * @param chapter_idx new selected chapter index
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_chapter_selection_changed)(vlc_player_t *player,
        const struct vlc_player_title *title, size_t title_idx,
        const struct vlc_player_chapter *new_chapter, size_t new_chapter_idx,
        void *data);

    /**
     * Called when the media has a teletext menu
     *
     * @param player locked player instance
     * @param has_teletext_menu true if the media has a teletext menu
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_teletext_menu_changed)(vlc_player_t *player,
        bool has_teletext_menu, void *data);

    /**
     * Called when teletext is enabled or disabled
     *
     * @see vlc_player_SetTeletextEnabled()
     *
     * @param player locked player instance
     * @param enabled true if teletext is enabled
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_teletext_enabled_changed)(vlc_player_t *player,
        bool enabled, void *data);

    /**
     * Called when the teletext page has changed
     *
     * @see vlc_player_SelectTeletextPage()
     *
     * @param player locked player instance
     * @param new_page page in the range ]0;888]
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_teletext_page_changed)(vlc_player_t *player,
        unsigned new_page, void *data);

    /**
     * Called when the teletext transparency has changed
     *
     * @see vlc_player_SetTeletextTransparency()
     *
     * @param player locked player instance
     * @param enabled true is the teletext overlay is transparent
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_teletext_transparency_changed)(vlc_player_t *player,
        bool enabled, void *data);

    /**
     * Called when the player audio delay has changed
     *
     * @see vlc_player_SetAudioDelay()
     *
     * @param player locked player instance
     * @param new_delay audio delay
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_audio_delay_changed)(vlc_player_t *player,
        vlc_tick_t new_delay, void *data);

    /**
     * Called when the player subtitle delay has changed
     *
     * @see vlc_player_SetSubtitleDelay()
     *
     * @param player locked player instance
     * @param new_delay subtitle delay
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_subtitle_delay_changed)(vlc_player_t *player,
        vlc_tick_t new_delay, void *data);

    /**
     * Called when associated subtitle has changed
     *
     * @see vlc_player_SetAssociatedSubsFPS()
     *
     * @param player locked player instance
     * @param sub_fps subtitle fps
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_associated_subs_fps_changed)(vlc_player_t *player,
        float subs_fps, void *data);

    /**
     * Called when a new renderer item is set
     *
     * @see vlc_player_SetRenderer()
     *
     * @param player locked player instance
     * @param new_item a valid renderer item or NULL (if unset)
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_renderer_changed)(vlc_player_t *player,
        vlc_renderer_item_t *new_item, void *data);

    /**
     * Called when the player recording state has changed
     *
     * @see vlc_player_SetRecordingEnabled()
     *
     * @param player locked player instance
     * @param recording true if recording is enabled
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_recording_changed)(vlc_player_t *player,
        bool recording, void *data);

    /**
     * Called when the media signal has changed
     *
     * @param player locked player instance
     * @param new_quality signal quality
     * @param new_strength signal strength,
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_signal_changed)(vlc_player_t *player,
        float quality, float strength, void *data);

    /**
     * Called when the player has new statisics
     *
     * @note The stats structure is only valid from this callback context. It
     * can be copied in order to use it from an other context.
     *
     * @param player locked player instance
     * @param stats valid stats, only valid from this context
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_statistics_changed)(vlc_player_t *player,
        const struct input_stats_t *stats, void *data);

    /**
     * Called when the A to B loop has changed
     *
     * @see vlc_player_SetAtoBLoop()
     *
     * @param player locked player instance
     * @param state A, when only A is set, B when both A and B are set, None by
     * default
     * @param time valid time or VLC_TICK_INVALID of the current state
     * @param pos valid pos of the current state
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_atobloop_changed)(vlc_player_t *player,
        enum vlc_player_abloop new_state, vlc_tick_t time, float pos,
        void *data);

    /**
     * Called when media stopped action has changed
     *
     * @see vlc_player_SetMediaStoppedAction()
     *
     * @param player locked player instance
     * @param new_action action to execute when a media is stopped
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_media_stopped_action_changed)(vlc_player_t *player,
        enum vlc_player_media_stopped_action new_action, void *data);

    /**
     * Called when the media meta has changed
     *
     * @param player locked player instance
     * @param media current media
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_media_meta_changed)(vlc_player_t *player,
        input_item_t *media, void *data);

    /**
     * Called when media epg has changed
     *
     * @param player locked player instance
     * @param media current media
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_media_epg_changed)(vlc_player_t *player,
        input_item_t *media, void *data);

    /**
     * Called when the media has new subitems
     *
     * @param player locked player instance
     * @param media current media
     * @param new_subitems node representing all media subitems
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_media_subitems_changed)(vlc_player_t *player,
        input_item_t *media, input_item_node_t *new_subitems, void *data);

    /**
     * Called when a new vout is added or removed
     *
     * @param player locked player instance
     * @param action added or removed
     * @param vout new vout
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_vout_list_changed)(vlc_player_t *player,
        enum vlc_player_list_action action, vout_thread_t *vout, void *data);

    /**
     * Called when the player is corked
     *
     * The player can be corked when the audio output loose focus or when a
     * renderer was paused from the outside.
     *
     * @note called only if pause on cork was not set to true (by
     * vlc_player_SetPauseOnCork())
     * @note a cork_count higher than 0 means the player is corked. In that
     * case, the user should pause the player and release all external resource
     * needed by the player. A value higher than 1 mean that the player was
     * corked more than one time (for different reasons). A value of 0 means
     * the player is no longer corked. In that case, the user could resume the
     * player.
     *
     * @param player locked player instance
     * @param cork_count 0 for uncorked, > 0 for corked
     * @param data opaque pointer set by vlc_player_AddListener()
     */
    void (*on_cork_changed)(vlc_player_t *player, unsigned cork_count,
                            void *data);
};

/**
 * Player vout callbacks
 *
 * Can be registered with vlc_player_vout_AddListener().
 *
 * These callbacks are *not* called with the player locked. It is safe to lock
 * the player and call any vlc_player functions from these callbacks.
 *
 * @note The state changed from the callbacks can be either applied on the
 * player (and all future video outputs), or on a specified video output. The
 * state is applied on the player when the vout argument is NULL.
 *
 * @warning To avoid deadlocks, users should never call vout_thread_t functions
 * from these callbacks.
 */
struct vlc_player_vout_cbs
{
    /**
     * Called when the player and/or vout fullscreen state has changed
     *
     * @see vlc_player_vout_SetFullscreen()
     *
     * @param player unlocked player instance
     * @param vout cf. vlc_player_vout_cbs note
     * @param enabled true when fullscreen is enabled
     * @param data opaque pointer set by vlc_player_vout_AddListener()
     */
    void (*on_fullscreen_changed)(vlc_player_t *player,
        vout_thread_t *vout, bool enabled, void *data);

    /**
     * Called when the player and/or vout wallpaper mode has changed
     *
     * @see vlc_player_vout_SetWallpaperModeEnabled()
     *
     * @param player unlocked player instance
     * @param vout cf. vlc_player_vout_cbs note
     * @param enabled true when wallpaper mode is enabled
     * @param data opaque pointer set by vlc_player_vout_AddListener()
     */
    void (*on_wallpaper_mode_changed)(vlc_player_t *player,
        vout_thread_t *vout, bool enabled, void *data);

    void (*on_aspect_ratio_selection_changed)(vlc_player_t *player,
        vout_thread_t *vout, char const *aspect_ratio_text, void *data);

    void (*on_crop_selection_changed)(vlc_player_t *player,
        vout_thread_t *vout, char const *crop_text, void *data);
};

/**
 * Player aout callbacks
 *
 * Can be registered with vlc_player_aout_AddListener().
 *
 * These callbacks are *not* called with the player locked. It is safe to lock
 * the player and call any vlc_player functions from these callbacks.
 *
 * @warning To avoid deadlocks, users should never call audio_output_t
 * functions from these callbacks.
 */
struct vlc_player_aout_cbs
{
    /**
     * Called when the volume has changed
     *
     * @see vlc_player_aout_SetVolume()
     *
     * @param player unlocked player instance
     * @param new_volume volume in the range [0;2.f]
     * @param data opaque pointer set by vlc_player_vout_AddListener()
     */
    void (*on_volume_changed)(vlc_player_t *player,
        float new_volume, void *data);

    /**
     * Called when the mute state has changed
     *
     * @see vlc_player_aout_Mute()
     *
     * @param player unlocked player instance
     * @param new_mute true if muted
     * @param data opaque pointer set by vlc_player_vout_AddListener()
     */
    void (*on_mute_changed)(vlc_player_t *player,
        bool new_muted, void *data);
};

/**
 * Duplicate a track
 *
 * This function can be used to pass a track from a callback to an other
 * context. The es_id will be held by the duplicated track.
 *
 * @see vlc_player_cbs.on_track_list_changed
 *
 * @return a duplicated track or NULL on allocation error
 */
VLC_API struct vlc_player_track *
vlc_player_track_Dup(const struct vlc_player_track *track);

/**
 * Delete a duplicated track
 */
VLC_API void
vlc_player_track_Delete(struct vlc_player_track *track);

/**
 * Duplicate a program
 *
 * This function can be used to pass a program from a callback to an other
 * context.
 *
 * @see vlc_player_cbs.on_program_list_changed
 *
 * @return a duplicated program or NULL on allocation error
 */
VLC_API struct vlc_player_program *
vlc_player_program_Dup(const struct vlc_player_program *prgm);

/**
 * Delete a duplicated program
 */
VLC_API void
vlc_player_program_Delete(struct vlc_player_program *prgm);

/**
 * Hold the title list of the player
 *
 * This function can be used to pass this title list from a callback to an
 * other thread.
 *
 * @see vlc_player_cbs.on_titles_changed
 *
 * @return the same instance
 */
VLC_API vlc_player_title_list *
vlc_player_title_list_Hold(vlc_player_title_list *titles);

/**
 * Release of previously held title list
 */
VLC_API void
vlc_player_title_list_Release(vlc_player_title_list *titles);

/**
 * Get the number of title of a list
 */
VLC_API size_t
vlc_player_title_list_GetCount(vlc_player_title_list *titles);

/**
 * Get the title at a given index
 *
 * @param idx index in the range [0; count[
 * @return a valid title (can't be NULL)
 */
VLC_API const struct vlc_player_title *
vlc_player_title_list_GetAt(vlc_player_title_list *titles, size_t idx);

/**
 * Create a new player instance
 *
 * @param parent parent VLC object
 * @param media_provider pointer to a media_provider structure or NULL, the
 * structure must be valid during the lifetime of the player
 * @param media_provider_data opaque data used by provider callbacks
 * @return a pointer to a valid player instance or NULL in case of error
 */
VLC_API vlc_player_t *
vlc_player_New(vlc_object_t *parent,
               const struct vlc_player_media_provider *media_provider,
               void *media_provider_data);

/**
 * Delete a player instance
 *
 * This function stop any playback previously started and wait for their
 * termination.
 *
 * @warning Blocking function if the player state is not STOPPED, don't call it
 * from an UI thread in that case.
 *
 * @param player unlocked player instance created by vlc_player_New()
 */
VLC_API void
vlc_player_Delete(vlc_player_t *player);

/**
 * Lock the player.
 *
 * All player functions (except vlc_player_Delete()) need to be called while
 * the player lock is held.
 *
 * @param player unlocked player instance
 */
VLC_API void
vlc_player_Lock(vlc_player_t *player);

/**
 * Unlock the player
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_Unlock(vlc_player_t *player);

/**
 * Wait on a condition variable
 *
 * This call allow users to use their own condition with the player mutex.
 *
 * @param player locked player instance
 * @param cond external condition
 */
VLC_API void
vlc_player_CondWait(vlc_player_t *player, vlc_cond_t *cond);

/**
 * Add a listener callback
 *
 * @note Every registered callbacks need to be removed by the caller with
 * vlc_player_RemoveListener().
 *
 * @param player locked player instance
 * @param cbs pointer to a vlc_player_cbs structure, the structure must be
 * valid during the lifetime of the player
 * @param cbs_data opaque pointer used by the callbacks
 * @return a valid listener id, or NULL in case of allocation error
 */
VLC_API vlc_player_listener_id *
vlc_player_AddListener(vlc_player_t *player,
                       const struct vlc_player_cbs *cbs, void *cbs_data);

/**
 * Remove a listener callback
 *
 * @param player locked player instance
 * @param listener_id listener id returned by vlc_player_AddListener()
 */
VLC_API void
vlc_player_RemoveListener(vlc_player_t *player,
                          vlc_player_listener_id *listener_id);

/**
 * Set the current media
 *
 * This function replaces the current and next medias.
 *
 * @note A successful call will always result of
 * vlc_player_cbs.on_current_media_changed being called. This function is not
 * blocking. If a media is currently being played, this media will be stopped
 * and the requested media will be set after.
 *
 * @warning This function is either synchronous (if the player state is
 * STOPPED) or asynchronous. In the later case, vlc_player_GetCurrentMedia()
 * will return the old media, even after this call, and until the
 * vlc_player_cbs.on_current_media_changed is called.
 *
 * @param player locked player instance
 * @param media new media to play (will be held by the player)
 * @return VLC_SUCCESS or a VLC error code
 */
VLC_API int
vlc_player_SetCurrentMedia(vlc_player_t *player, input_item_t *media);

/**
 * Get the current played media.
 *
 * @see vlc_player_cbs.on_current_media_changed
 *
 * @param player locked player instance
 * @return a valid media or NULL (if no media is set)
 */
VLC_API input_item_t *
vlc_player_GetCurrentMedia(vlc_player_t *player);

/**
 * Helper that hold the current media
 */
static inline input_item_t *
vlc_player_HoldCurrentMedia(vlc_player_t *player)
{
    input_item_t *item = vlc_player_GetCurrentMedia(player);
    return item ? input_item_Hold(item) : NULL;
}

/**
 * Invalidate the next media.
 *
 * This function can be used to invalidate the media returned by the
 * vlc_player_media_provider.get_next callback. This can be used when the next
 * item from a playlist was changed by the user.
 *
 * Calling this function will trigger the
 * vlc_player_media_provider.get_next callback to be called again.
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_InvalidateNextMedia(vlc_player_t *player);

/**
 * Ask to started in a paused state
 *
 * This function can be used before vlc_player_Start()
 *
 * @param player locked player instance
 * @param start_paused true to start in a paused state, false to cancel it
 */
VLC_API void
vlc_player_SetStartPaused(vlc_player_t *player, bool start_paused);

/**
 * Setup an action when a media is stopped
 *
 * @param player locked player instance
 * @param action action to do when a media is stopped
 */
VLC_API void
vlc_player_SetMediaStoppedAction(vlc_player_t *player,
                                 enum vlc_player_media_stopped_action action);

/**
 * Start the playback of the current media.
 *
 * @param player locked player instance
 * @return VLC_SUCCESS or a VLC error code
 */
VLC_API int
vlc_player_Start(vlc_player_t *player);

/**
 * Stop the playback of the current media
 *
 * @note This function is asynchronous. Users should wait on
 * STOPPED state event to know when the stop is finished.
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_Stop(vlc_player_t *player);

/**
 * Pause the playback
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_Pause(vlc_player_t *player);

/**
 * Resume the playback from a pause
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_Resume(vlc_player_t *player);

/**
 * Pause and display the next video frame
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_NextVideoFrame(vlc_player_t *player);

/**
 * Get the state of the player
 *
 * @note Since all players actions are asynchronous, this function won't
 * reflect the new state immediately. Wait for the
 * vlc_players_cbs.on_state_changed event to be notified.
 *
 * @see vlc_player_state
 * @see vlc_player_cbs.on_state_changed
 *
 * @param player locked player instance
 * @return the current player state
 */
VLC_API enum vlc_player_state
vlc_player_GetState(vlc_player_t *player);

/**
 * Get the error state of the player
 *
 * @see vlc_player_cbs.on_capabilities_changed
 *
 * @param player locked player instance
 * @return the current error state
 */
VLC_API enum vlc_player_error
vlc_player_GetError(vlc_player_t *player);

/**
 * Helper to get the started state
 */
static inline bool
vlc_player_IsStarted(vlc_player_t *player)
{
    switch (vlc_player_GetState(player))
    {
        case VLC_PLAYER_STATE_STARTED:
        case VLC_PLAYER_STATE_PLAYING:
        case VLC_PLAYER_STATE_PAUSED:
            return true;
        default:
            return false;
    }
}

/**
 * Helper to get the paused state
 */
static inline bool
vlc_player_IsPaused(vlc_player_t *player)
{
    return vlc_player_GetState(player) == VLC_PLAYER_STATE_PAUSED;
}

/**
 * Helper to toggle the pause state
 */
static inline void
vlc_player_TogglePause(vlc_player_t *player)
{
    if (vlc_player_IsStarted(player))
    {
        if (vlc_player_IsPaused(player))
            vlc_player_Resume(player);
        else
            vlc_player_Pause(player);
    }
}

/**
 * Get the player capabilities
 *
 * @see vlc_player_cbs.on_capabilities_changed
 *
 * @param player locked player instance
 * @return the player capabilities, a bitwise mask of @ref VLC_PLAYER_CAP_SEEK,
 * @ref VLC_PLAYER_CAP_PAUSE, @ref VLC_PLAYER_CAP_CHANGE_RATE, @ref
 * VLC_PLAYER_CAP_REWIND
 */
VLC_API int
vlc_player_GetCapabilities(vlc_player_t *player);

/**
 * Helper to get the seek capability
 */
static inline bool
vlc_player_CanSeek(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_SEEK;
}

/**
 * Helper to get the pause capability
 */
static inline bool
vlc_player_CanPause(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_PAUSE;
}

/**
 * Helper to get the change-rate capability
 */
static inline bool
vlc_player_CanChangeRate(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_CHANGE_RATE;
}

/**
 * Helper to get the rewindable capability
 */
static inline bool
vlc_player_CanRewind(vlc_player_t *player)
{
    return vlc_player_GetCapabilities(player) & VLC_PLAYER_CAP_REWIND;
}

/**
 * Get the rate of the player
 *
 * @see vlc_player_cbs.on_rate_changed
 *
 * @param player locked player instance
 * @return rate of the player (< 1.f is slower, > 1.f is faster)
 */
VLC_API float
vlc_player_GetRate(vlc_player_t *player);

/**
 * Change the rate of the player
 *
 * @note The rate is saved across several medias
 *
 * @param player locked player instance
 * @param rate new rate (< 1.f is slower, > 1.f is faster)
 */
VLC_API void
vlc_player_ChangeRate(vlc_player_t *player, float rate);

/**
 * Increment the rate of the player (faster)
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_IncrementRate(vlc_player_t *player);

/**
 * Decrement the rate of the player (Slower)
 *
 * @param player locked player instance
 */
VLC_API void
vlc_player_DecrementRate(vlc_player_t *player);

/**
 * Get the length of the current media
 *
 * @note A started and playing media doesn't have necessarily a valid length.
 *
 * @see vlc_player_cbs.on_length_changed
 *
 * @param player locked player instance
 * @return a valid length or VLC_TICK_INVALID (if no media is set,
 * playback is not yet started or in case of error)
 */
VLC_API vlc_tick_t
vlc_player_GetLength(vlc_player_t *player);

/**
 * Get the time of the current media
 *
 * @note A started and playing media doesn't have necessarily a valid time.
 *
 * @see vlc_player_cbs.vlc_player_cbs.on_position_changed
 *
 * @param player locked player instance
 * @return a valid time or VLC_TICK_INVALID (if no media is set, the media
 * doesn't have any time, if playback is not yet started or in case of error)
 */
VLC_API vlc_tick_t
vlc_player_GetTime(vlc_player_t *player);

/**
 * Get the position of the current media
 *
 * @see vlc_player_cbs.on_position_changed
 *
 * @param player locked player instance
 * @return a valid position in the range [0.f;1.f] or -1.f (if no media is
 * set,if playback is not yet started or in case of error)
 */
VLC_API float
vlc_player_GetPosition(vlc_player_t *player);

/**
 * Seek the current media by position
 *
 * @note This function can be called before vlc_player_Start() in order to set
 * a starting position.
 *
 * @param player locked player instance
 * @param position position in the range [0.f;1.f]
 * @param speed precise of fast
 * @param whence absolute or relative
 */
VLC_API void
vlc_player_SeekByPos(vlc_player_t *player, float position,
                     enum vlc_player_seek_speed speed,
                     enum vlc_player_whence whence);

/**
 * Seek the current media by time
 *
 * @note This function can be called before vlc_player_Start() in order to set
 * a starting position.
 *
 * @warning This function has an effect only if the media has a valid length.
 *
 * @param player locked player instance
 * @param time a time in the range [0;length]
 * @param speed precise of fast
 * @param whence absolute or relative
 */
VLC_API void
vlc_player_SeekByTime(vlc_player_t *player, vlc_tick_t time,
                      enum vlc_player_seek_speed speed,
                      enum vlc_player_whence whence);

/**
 * Helper to set the absolute position precisely
 */
static inline void
vlc_player_SetPosition(vlc_player_t *player, float position)
{
    vlc_player_SeekByPos(player, position, VLC_PLAYER_SEEK_PRECISE,
                         VLC_PLAYER_WHENCE_ABSOLUTE);
}

/**
 * Helper to set the absolute position fast
 */
static inline void
vlc_player_SetPositionFast(vlc_player_t *player, float position)
{
    vlc_player_SeekByPos(player, position, VLC_PLAYER_SEEK_FAST,
                         VLC_PLAYER_WHENCE_ABSOLUTE);
}

/**
 * Helper to jump the position precisely
 */
static inline void
vlc_player_JumpPos(vlc_player_t *player, float jumppos)
{
    /* No fask seek for jumps. Indeed, jumps can seek to the current position
     * if not precise enough or if the jump value is too small. */
    vlc_player_SeekByPos(player, jumppos, VLC_PLAYER_SEEK_PRECISE,
                         VLC_PLAYER_WHENCE_RELATIVE);
}

/**
 * Helper to set the absolute time precisely
 */
static inline void
vlc_player_SetTime(vlc_player_t *player, vlc_tick_t time)
{
    vlc_player_SeekByTime(player, time, VLC_PLAYER_SEEK_PRECISE,
                          VLC_PLAYER_WHENCE_ABSOLUTE);
}

/**
 * Helper to set the absolute time fast
 */
static inline void
vlc_player_SetTimeFast(vlc_player_t *player, vlc_tick_t time)
{
    vlc_player_SeekByTime(player, time, VLC_PLAYER_SEEK_FAST,
                          VLC_PLAYER_WHENCE_ABSOLUTE);
}

/**
 * Helper to jump the time precisely
 */
static inline void
vlc_player_JumpTime(vlc_player_t *player, vlc_tick_t jumptime)
{
    /* No fask seek for jumps. Indeed, jumps can seek to the current position
     * if not precise enough or if the jump value is too small. */
    vlc_player_SeekByTime(player, jumptime, VLC_PLAYER_SEEK_PRECISE,
                          VLC_PLAYER_WHENCE_RELATIVE);
}

/**
 * Enable A to B loop of the current media
 *
 * This function need to be called 2 times with VLC_PLAYER_ABLOOP_A and
 * VLC_PLAYER_ABLOOP_B to setup an A to B loop. It current the current
 * time/position when called. The B time must be higher than the A time.
 *
 * @param player locked player instance
 * @return VLC_SUCCESS or a VLC error code
 */
VLC_API int
vlc_player_SetAtoBLoop(vlc_player_t *player, enum vlc_player_abloop abloop);

/**
 * Get the A to B loop status
 *
 * @note If the returned status is VLC_PLAYER_ABLOOP_A, then a_time and a_pos
 * will be valid. If the returned status is VLC_PLAYER_ABLOOP_B, then all
 * output parameters are valid. If the returned status is
 * VLC_PLAYER_ABLOOP_NONE, then all output parameters are invalid.
 *
 * @see vlc_player_cbs.on_atobloop_changed
 *
 * @param player locked player instance
 * @param a_time A time or VLC_TICK_INVALID (if the media doesn't have valid
 * times)
 * @param a_pos A position
 * @param b_time B time or VLC_TICK_INVALID (if the media doesn't have valid
 * times)
 * @param b_pos B position
 * @return A to B loop status
 */
VLC_API enum vlc_player_abloop
vlc_player_GetAtoBLoop(vlc_player_t *player, vlc_tick_t *a_time, float *a_pos,
                       vlc_tick_t *b_time, float *b_pos);

/**
 * Get the number of tracks for an ES category
 *
 * @warning The returned size becomes invalid when the player is unlocked.
 *
 * @param player locked player instance
 * @param cat VIDEO_ES, AUDIO_ES or SPU_ES
 * @return number of tracks, or 0 (in case of error, or if the media is not
 * started)
 */
VLC_API size_t
vlc_player_GetTrackCount(vlc_player_t *player, enum es_format_category_e cat);

/**
 * Get the track at a specific index for an ES category
 *
 * @warning The behaviour is undefined if the index is not valid.
 *
 * @warning The returned pointer becomes invalid when the player is unlocked.
 * The referenced structure can be safely copied with vlc_player_track_Dup().
 *
 * @param player locked player instance
 * @param cat VIDEO_ES, AUDIO_ES or SPU_ES
 * @param index valid index in the range [0; count[
 * @return a valid track (can't be NULL if vlc_player_GetTrackCount() returned
 * a valid count)
 */
VLC_API const struct vlc_player_track *
vlc_player_GetTrackAt(vlc_player_t *player, enum es_format_category_e cat,
                      size_t index);

/**
 * Helper to get the video track count
 */
static inline size_t
vlc_player_GetVideoTrackCount(vlc_player_t *player)
{
    return vlc_player_GetTrackCount(player, VIDEO_ES);
}

/**
 * Helper to get a video track at a specific index
 */
static inline const struct vlc_player_track *
vlc_player_GetVideoTrackAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetTrackAt(player, VIDEO_ES, index);
}

/**
 * Helper to get the audio track count
 */
static inline size_t
vlc_player_GetAudioTrackCount(vlc_player_t *player)
{
    return vlc_player_GetTrackCount(player, AUDIO_ES);
}

/**
 * Helper to get an audio track at a specific index
 */
static inline const struct vlc_player_track *
vlc_player_GetAudioTrackAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetTrackAt(player, AUDIO_ES, index);
}

/**
 * Helper to get the subtitle track count
 */
static inline size_t
vlc_player_GetSubtitleTrackCount(vlc_player_t *player)
{
    return vlc_player_GetTrackCount(player, SPU_ES);
}

/**
 * Helper to get a subtitle track at a specific index
 */
static inline const struct vlc_player_track *
vlc_player_GetSubtitleTrackAt(vlc_player_t *player, size_t index)
{
    return vlc_player_GetTrackAt(player, SPU_ES, index);
}

/**
 * Get a track from an ES identifier
 *
 * @warning The returned pointer becomes invalid when the player is unlocked.
 * The referenced structure can be safely copied with vlc_player_track_Dup().
 *
 * @param player locked player instance
 * @param id an ES ID (retrieved from vlc_player_cbs.on_track_list_changed or
 * vlc_player_GetTrackAt())
 * @return a valid player track or NULL (if the track was terminated by the
 * playback thread)
 */
VLC_API const struct vlc_player_track *
vlc_player_GetTrack(vlc_player_t *player, vlc_es_id_t *es_id);

/**
 * Get the selected track from an ES category
 *
 * @param player locked player instance
 * @param cat VIDEO_ES, AUDIO_ES or SPU_ES
 * @return a valid player track or NULL (if none was selected or if the track
 * was terminated by the playback thread)
 */
VLC_API const struct vlc_player_track *
vlc_player_GetSelectedTrack(vlc_player_t *player,
                            enum es_format_category_e cat);

/**
 * Helper to get the selected video track
 */
static inline const struct vlc_player_track *
vlc_player_GetSelectedVideoTrack(vlc_player_t *player)
{
    return vlc_player_GetSelectedTrack(player, VIDEO_ES);
}

/**
 * Helper to get the selected audio track
 */
static inline const struct vlc_player_track *
vlc_player_GetSelectedAudioTrack(vlc_player_t *player)
{
    return vlc_player_GetSelectedTrack(player, AUDIO_ES);
}

/**
 * Helper to get the selected subtitle track
 */
static inline const struct vlc_player_track *
vlc_player_GetSelectedSubtitleTrack(vlc_player_t *player)
{
    return vlc_player_GetSelectedTrack(player, SPU_ES);
}

/**
 * Select a track from an ES identifier
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_track_selection_changed event.
 *
 * @param player locked player instance
 * @param id an ES ID (retrieved from vlc_player_cbs.on_track_list_changed or
 * vlc_player_GetTrackAt())
 */
VLC_API void
vlc_player_SelectTrack(vlc_player_t *player, vlc_es_id_t *es_id);

VLC_API void
vlc_player_SelectPrevTrack(vlc_player_t *player,
                           enum es_format_category_e cat);

VLC_API void
vlc_player_SelectNextTrack(vlc_player_t *player,
                           enum es_format_category_e cat);

/**
 * Unselect a track from an ES identifier
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_track_selection_changed event.
 *
 * @param player locked player instance
 * @param id an ES ID (retrieved from vlc_player_cbs.on_track_list_changed or
 * vlc_player_GetTrackAt())
 */
VLC_API void
vlc_player_UnselectTrack(vlc_player_t *player, vlc_es_id_t *es_id);

/**
 * Helper to unselect all tracks from an ES category
 */
static inline void
vlc_player_UnselectTrackCategory(vlc_player_t *player,
                                 enum es_format_category_e cat)
{
    size_t count = vlc_player_GetTrackCount(player, cat);
    for (size_t i = 0; i < count; ++i)
    {
        const struct vlc_player_track *track =
            vlc_player_GetTrackAt(player, cat, i);
        assert(track);
        if (track->selected)
            vlc_player_UnselectTrack(player, track->es_id);
    }
}

VLC_API void
vlc_player_ToggleSubtitle(vlc_player_t *player);

/**
 * Restart a track from an ES identifier
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_track_selection_changed event.
 *
 * @param player locked player instance
 * @param id an ES ID (retrieved from vlc_player_cbs.on_track_list_changed or
 * vlc_player_GetTrackAt())
 */
VLC_API void
vlc_player_RestartTrack(vlc_player_t *player, vlc_es_id_t *es_id);

/**
  * Helper to restart all selected tracks from an ES category
  */
static inline void
vlc_player_RestartTrackCategory(vlc_player_t *player,
                                enum es_format_category_e cat)
{
    size_t count = vlc_player_GetTrackCount(player, cat);
    for (size_t i = 0; i < count; ++i)
    {
        const struct vlc_player_track *track =
            vlc_player_GetTrackAt(player, cat, i);
        assert(track);
        if (track->selected)
            vlc_player_RestartTrack(player, track->es_id);
    }
}

/**
 * Select the default track for an ES category.
 *
 * Tracks for this category will be automatically chosen according to the
 * language for all future played media.
 *
 * @param player locked player instance
 * @param cat VIDEO_ES, AUDIO_ES or SPU_ES
 * @param lang language (TODO: define it) or NULL to reset the default state
 */
VLC_API void
vlc_player_SelectDefaultTrack(vlc_player_t *player,
                              enum es_format_category_e cat, const char *lang);

/**
 * Helper to select the default video track
 */
static inline void
vlc_player_SelectDefaultVideoTrack(vlc_player_t *player, const char *lang)
{
    vlc_player_SelectDefaultTrack(player, VIDEO_ES, lang);
}

/**
 * Helper to select the default audio track
 */
static inline void
vlc_player_SelectDefaultAudioTrack(vlc_player_t *player, const char *lang)
{
    vlc_player_SelectDefaultTrack(player, AUDIO_ES, lang);
}

/**
 * Helper to select the default spu track
 */
static inline void
vlc_player_SelectDefaultSubtitleTrack(vlc_player_t *player, const char *lang)
{
    vlc_player_SelectDefaultTrack(player, SPU_ES, lang);
}

/**
 * Get the number of programs
 *
 * @warning The returned size becomes invalid when the player is unlocked.
 *
 * @param player locked player instance
 * @return number of programs, or 0 (in case of error, or if the media is not
 * started)
 */
VLC_API size_t
vlc_player_GetProgramCount(vlc_player_t *player);

/**
 * Get the program at a specific index
 *
 * @warning The behaviour is undefined if the index is not valid.
 *
 * @warning The returned pointer becomes invalid when the player is unlocked.
 * The referenced structure can be safely copied with vlc_player_program_Dup().
 *
 * @param player locked player instance
 * @param index valid index in the range [0; count[
 * @return a valid program (can't be NULL if vlc_player_GetProgramCount()
 * returned a valid count)
 */
VLC_API const struct vlc_player_program *
vlc_player_GetProgramAt(vlc_player_t *player, size_t index);

/**
 * Get a program from an ES group identifier
 *
 * @param player locked player instance
 * @param group_id a program ID (retrieved from
 * vlc_player_cbs.on_program_list_changed or vlc_player_GetProgramAt())
 * @return a valid program or NULL (if the program was terminated by the
 * playback thread)
 */
VLC_API const struct vlc_player_program *
vlc_player_GetProgram(vlc_player_t *player, int group_id);

/**
 * Select a program from an ES group identifier
 *
 * @param player locked player instance
 * @param group_id a program ID (retrieved from
 * vlc_player_cbs.on_program_list_changed or vlc_player_GetProgramAt())
 */
VLC_API void
vlc_player_SelectProgram(vlc_player_t *player, int group_id);

VLC_API void
vlc_player_CycleProgram(vlc_player_t *player, enum vlc_player_cycle cycle);

/**
 * Check if the media has a teletext menu
 *
 * @see vlc_player_cbs.on_teletext_menu_changed
 *
 * @param player locked player instance
 * @return true if the media has a teletext menu
 */
VLC_API bool
vlc_player_HasTeletextMenu(vlc_player_t *player);

/**
 * Enable or disable teletext
 *
 * This function has an effect only if the player has a teletext menu.
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_teletext_enabled_changed event.
 * 
 * @param player locked player instance
 * @param enabled true to enable
 */
VLC_API void
vlc_player_SetTeletextEnabled(vlc_player_t *player, bool enabled);

/**
 * Check if teletext is enabled
 *
 * @see vlc_player_cbs.on_teletext_enabled_changed
 *
 * @param player locked player instance
 */
VLC_API bool
vlc_player_IsTeletextEnabled(vlc_player_t *player);

/**
 * Select a teletext page or do an action from a key
 *
 * This function has an effect only if the player has a teletext menu.
 *
 * @note Page keys can be the following: @ref VLC_PLAYER_TELETEXT_KEY_RED,
 * @ref VLC_PLAYER_TELETEXT_KEY_GREEN, @ref VLC_PLAYER_TELETEXT_KEY_YELLOW,
 * @ref VLC_PLAYER_TELETEXT_KEY_BLUE or @ref VLC_PLAYER_TELETEXT_KEY_INDEX.

 * @note A successful call will trigger the
 * vlc_player_cbs.on_teletext_page_changed event.
 *
 * @param player locked player instance
 * @param page a page in the range ]0;888] or a valid key
 */
VLC_API void
vlc_player_SelectTeletextPage(vlc_player_t *player, unsigned page);

/**
 * Get the current teletext page
 *
 * @see vlc_player_cbs.on_teletext_page_changed
 *
 * @param player locked player instance
 */
VLC_API unsigned
vlc_player_GetTeletextPage(vlc_player_t *player);

/**
 * Enable or disable teletext transparency
 *
 * This function has an effect only if the player has a teletext menu.

 * @note A successful call will trigger the
 * vlc_player_cbs.on_teletext_transparency_changed event.
 *
 * @param player locked player instance
 * @param enabled true to enable
 */
VLC_API void
vlc_player_SetTeletextTransparency(vlc_player_t *player, bool enabled);

/**
 * Check if teletext is transparent
 *
 * @param player locked player instance
 */
VLC_API bool
vlc_player_IsTeletextTransparent(vlc_player_t *player);

/**
 * Get the title list of the current media
 *
 * @see vlc_player_cbs.on_titles_changed
 *
 * @param player locked player instance
 */
VLC_API vlc_player_title_list *
vlc_player_GetTitleList(vlc_player_t *player);

/**
 * Get the selected title index for the current media
 *
 * @see vlc_player_cbs.on_title_selection_changed
 *
 * @param player locked player instance
 */
VLC_API ssize_t
vlc_player_GetSelectedTitleIdx(vlc_player_t *player);

/**
 * Helper to get the current selected title
 */
static inline const struct vlc_player_title *
vlc_player_GetSelectedTitle(vlc_player_t *player)
{
    vlc_player_title_list *titles = vlc_player_GetTitleList(player);
    if (!titles)
        return NULL;
    ssize_t selected_idx = vlc_player_GetSelectedTitleIdx(player);
    if (selected_idx < 0)
        return NULL;
    return vlc_player_title_list_GetAt(titles, selected_idx);
}

/**
 * Select a title index for the current media
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_title_selection_changed event.
 *
 * @see vlc_player_title_list_GetAt()
 * @see vlc_player_title_list_GetCount()
 *
 * @param player locked player instance
 * @param index valid index in the range [0;count[
 */
VLC_API void
vlc_player_SelectTitleIdx(vlc_player_t *player, size_t index);

/**
 * Select a title for the current media
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_title_selection_changed event.
 *
 * @see vlc_player_title_list_GetAt()
 * @see vlc_player_title_list_GetCount()
 *
 * @param player locked player instance
 * @param title a valid title coming from the vlc_player_title_list
 */
VLC_API void
vlc_player_SelectTitle(vlc_player_t *player,
                       const struct vlc_player_title *title);

/**
 * Select a chapter for the current media
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_chapter_selection_changed event.
 *
 * @param player locked player instance
 * @param title the selected title
 * @param chapter_idx index from vlc_player_title.chapters
 */
VLC_API void
vlc_player_SelectChapter(vlc_player_t *player,
                         const struct vlc_player_title *title,
                         size_t chapter_idx);

/**
 * Select the next title for the current media
 *
 * @see vlc_player_SelectTitleIdx()
 */
VLC_API void
vlc_player_SelectNextTitle(vlc_player_t *player);

/**
 * Select the previous title for the current media
 *
 * @see vlc_player_SelectTitleIdx()
 */
VLC_API void
vlc_player_SelectPrevTitle(vlc_player_t *player);

/**
 * Get the selected chapter index for the current media
 *
 * @see vlc_player_cbs.on_chapter_selection_changed
 *
 * @param player locked player instance
 */
VLC_API ssize_t
vlc_player_GetSelectedChapterIdx(vlc_player_t *player);

/**
 * Helper to get the current selected chapter
 */
static inline const struct vlc_player_chapter *
vlc_player_GetSelectedChapter(vlc_player_t *player)
{
    const struct vlc_player_title *title = vlc_player_GetSelectedTitle(player);
    if (!title || !title->chapter_count)
        return NULL;
    ssize_t chapter_idx = vlc_player_GetSelectedChapterIdx(player);
    return chapter_idx >= 0 ? &title->chapters[chapter_idx] : NULL;
}

/**
 * Select a chapter index for the current media
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_chaper_selection_changed event.
 *
 * @see vlc_player_title.chapters
 *
 * @param player locked player instance
 * @param index valid index in the range [0;vlc_player_title.chapter_count[
 */
VLC_API void
vlc_player_SelectChapterIdx(vlc_player_t *player, size_t index);

/**
 * Select the next chapter for the current media
 *
 * @see vlc_player_SelectChapterIdx()
 */
VLC_API void
vlc_player_SelectNextChapter(vlc_player_t *player);

/**
 * Select the previous chapter for the current media
 *
 * @see vlc_player_SelectChapterIdx()
 */
VLC_API void
vlc_player_SelectPrevChapter(vlc_player_t *player);

/**
 * Add an associated (or external) media to the current media
 *
 * @param player locked player instance
 * @param cat AUDIO_ES or SPU_ES
 * @param uri absolute uri of the external media
 * @param select true to select the track of this external media
 * @param notify true to notify the OSD
 * @param check_ext true to check subtitles extension
 */
VLC_API int
vlc_player_AddAssociatedMedia(vlc_player_t *player,
                              enum es_format_category_e cat, const char *uri,
                              bool select, bool notify, bool check_ext);

/**
 * Set the associated subtitle FPS
 *
 * In order to correct the rate of the associated media according to this FPS
 * and the media video FPS.
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_associated_subs_fps_changed event.
 *
 * @warning this function will change the rate of all external subtitle files
 * associated with the current media.
 *
 * @param player locked player instance
 * @param fps FPS of the subtitle file
 */
VLC_API void
vlc_player_SetAssociatedSubsFPS(vlc_player_t *player, float fps);

/**
 * Get the associated subtitle FPS
 *
 * @param player locked player instance
 * @return fps
 */
VLC_API float
vlc_player_GetAssociatedSubsFPS(vlc_player_t *player);

/**
 * Set the renderer
 *
 * Valid for the current media and all future ones.
 *
 * @note A successful call will trigger the vlc_player_cbs.on_renderer_changed
 * event.
 *
 * @param player locked player instance
 * @param renderer a valid renderer item or NULL (to disable it), the item will
 * be held by the player
 */
VLC_API void
vlc_player_SetRenderer(vlc_player_t *player, vlc_renderer_item_t *renderer);

/**
 * Get the renderer
 *
 * @see vlc_player_cbs.on_renderer_changed
 *
 * @param player locked player instance
 * @return the renderer item set by vlc_player_SetRenderer()
 */
VLC_API vlc_renderer_item_t *
vlc_player_GetRenderer(vlc_player_t *player);

/**
 * Navigate (for DVD/Bluray menus or viewpoint)
 *
 * @param player locked player instance
 * @param nav navigation key
 */
VLC_API void
vlc_player_Navigate(vlc_player_t *player, enum vlc_player_nav nav);

/**
  * Update the viewpoint
  *
  * @param player locked player instance
  * @param viewpoint the viewpoint value
  * @param whence absolute or relative
  * @return VLC_SUCCESS or a VLC error code
  */
VLC_API void
vlc_player_UpdateViewpoint(vlc_player_t *player,
                           const vlc_viewpoint_t *viewpoint,
                           enum vlc_player_whence whence);

/**
 * Check if the playing is recording
 *
 * @see vlc_player_cbs.on_recording_changed
 *
 * @param player locked player instance
 * @return true if the player is recording
 */
VLC_API bool
vlc_player_IsRecording(vlc_player_t *player);

/**
 * Enable or disable recording for the current media
 *
 * @note A successful call will trigger the vlc_player_cbs.on_recording_changed
 * event.
 *
 * @param player locked player instance
 * @param enabled true to enable recording
 */
VLC_API void
vlc_player_SetRecordingEnabled(vlc_player_t *player, bool enabled);

/**
 * Helper to toggle the recording state
 */
static inline void
vlc_player_ToggleRecording(vlc_player_t *player)
{
    vlc_player_SetRecordingEnabled(player, !vlc_player_IsRecording(player));
}

/**
 * Get the audio delay for the current media
 *
 * @see vlc_player_cbs.on_audio_delay_changed
 *
 * @param player locked player instance
 */
VLC_API vlc_tick_t
vlc_player_GetAudioDelay(vlc_player_t *player);

/**
 * Set the audio delay for the current media
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_audio_delay_changed event.
 *
 * @param player locked player instance
 * @param delay a valid time
 * @param whence absolute or relative
 */
VLC_API void
vlc_player_SetAudioDelay(vlc_player_t *player, vlc_tick_t delay,
                         enum vlc_player_whence whence);

/**
 * Get the subtitle delay for the current media
 *
 * @see vlc_player_cbs.on_subtitle_delay_changed
 *
 * @param player locked player instance
 */
VLC_API vlc_tick_t
vlc_player_GetSubtitleDelay(vlc_player_t *player);

VLC_API void
vlc_player_SubtitleSyncMarkAudio(vlc_player_t *player);

VLC_API void
vlc_player_SubtitleSyncMarkSubtitle(vlc_player_t *player);

VLC_API void
vlc_player_SubtitleSyncApply(vlc_player_t *player);

VLC_API void
vlc_player_SubtitleSyncReset(vlc_player_t *player);

/**
 * Set the subtitle delay for the current media
 *
 * @note A successful call will trigger the
 * vlc_player_cbs.on_subtitle_delay_changed event.
 *
 * @param player locked player instance
 * @param delay a valid time
 * @param whence absolute or relative
 */
VLC_API void
vlc_player_SetSubtitleDelay(vlc_player_t *player, vlc_tick_t delay,
                            enum vlc_player_whence whence);

/**
 * Get the signal quality and strength of the current media
 *
 * @param player locked player instance
 */
VLC_API int
vlc_player_GetSignal(vlc_player_t *player, float *quality, float *strength);

/**
 * Get the statistics of the current media
 *
 * @warning The returned pointer becomes invalid when the player is unlocked.
 * The referenced structure can be safely copied.
 *
 * @see vlc_player_cbs.on_statistics_changed
 *
 * @param player locked player instance
 * @return pointer to the player stats structure or NULL
 */
VLC_API const struct input_stats_t *
vlc_player_GetStatistics(vlc_player_t *player);

/**
 * Enable or disable pause on cork event
 *
 * If enabled, the player will automatically pause and resume on cork events.
 * In that case, cork events won't be propagated via callbacks.
 * @see vlc_player_cbs.on_cork_changed
 *
 * @param player locked player instance
 * @param enabled true to enable
 */
VLC_API void
vlc_player_SetPauseOnCork(vlc_player_t *player, bool enabled);

/**
 * Get the audio output
 *
 * @warning The returned pointer must be released with vlc_object_release().
 *
 * @param player player instance
 * @return a valid audio_output_t * or NULL (if there is no aouts)
 */
VLC_API audio_output_t *
vlc_player_aout_Hold(vlc_player_t *player);

/**
 * Add a listener callback for audio output events
 *
 * @note The player instance doesn't need to be locked for vlc_player_aout_*()
 * functions.
 * @note Every registered callbacks need to be removed by the caller with
 * vlc_player_aout_RemoveListener().
 *
 * @param player player instance
 * @param cbs pointer to a vlc_player_aout_cbs structure, the structure must be
 * valid during the lifetime of the player
 * @param cbs_data opaque pointer used by the callbacks
 * @return a valid listener id, or NULL in case of allocation error
 */
VLC_API vlc_player_aout_listener_id *
vlc_player_aout_AddListener(vlc_player_t *player,
                            const struct vlc_player_aout_cbs *cbs,
                            void *cbs_data);

/**
 * Remove a aout listener callback
 *
 * @param player player instance
 * @param listener_id listener id returned by vlc_player_aout_AddListener()
 */
VLC_API void
vlc_player_aout_RemoveListener(vlc_player_t *player,
                               vlc_player_aout_listener_id *listener_id);

/**
 * Get the audio volume
 *
 * @note The player instance doesn't need to be locked for vlc_player_aout_*()
 * functions.
 *
 * @see vlc_player_aout_cbs.on_volume_changed
 *
 * @param player player instance
 * @return volume in the range [0;2.f] or -1.f if there is no audio outputs
 * (independent of mute)
 */
VLC_API float
vlc_player_aout_GetVolume(vlc_player_t *player);

/**
 * Set the audio volume
 *
 * @note The player instance doesn't need to be locked for vlc_player_aout_*()
 * functions.
 *
 * @note A successful call will trigger the
 * vlc_player_vout_cbs.on_volume_changed event.
 *
 * @param player player instance
 * @param volume volume in the range [0;2.f]
 * @return VLC_SUCCESS or VLC_EGENERIC if there is no audio outputs
 */
VLC_API int
vlc_player_aout_SetVolume(vlc_player_t *player, float volume);

/**
 * Increment the audio volume
 *
 * @see vlc_player_aout_SetVolume()
 *
 * @param player player instance
 * @param steps number of "volume-step"
 * @param result pointer to store the resulting volume (can be NULL)
 * @return VLC_SUCCESS or VLC_EGENERIC if there is no audio outputs
 */
VLC_API int
vlc_player_aout_IncrementVolume(vlc_player_t *player, int steps, float *result);

/**
 * Helper to decrement the audio volume
 */
static inline int
vlc_player_aout_DecrementVolume(vlc_player_t *player, int steps, float *result)
{
    return vlc_player_aout_IncrementVolume(player, -steps, result);
}

/**
 * Check if the audio output is muted
 *
 * @note The player instance doesn't need to be locked for vlc_player_aout_*()
 * functions.
 *
 * @see vlc_player_aout_cbs.on_mute_changed
 *
 * @param player player instance
 * @return 0 if not muted, 1 if muted, -1 if there is no audio outputs
 */
VLC_API int
vlc_player_aout_IsMuted(vlc_player_t *player);

/**
 * Mute or unmute the audio output
 *
 * @note The player instance doesn't need to be locked for vlc_player_aout_*()
 * functions.
 *
 * @note A successful call will trigger the
 * vlc_player_aout_cbs.on_mute_changed event.
 *
 * @param player player instance
 * @param mute true to mute
 * @return VLC_SUCCESS or VLC_EGENERIC if there is no audio outputs
 */
VLC_API int
vlc_player_aout_Mute(vlc_player_t *player, bool mute);

static inline int
vlc_player_aout_ToggleMute(vlc_player_t *player)
{
    return vlc_player_aout_Mute(player,
                                !vlc_player_aout_IsMuted(player));
}

/**
 * Enable or disable an audio filter
 *
 * @see aout_EnableFilter()
 *
 * @return VLC_SUCCESS or VLC_EGENERIC if there is no audio outputs
 */
VLC_API int
vlc_player_aout_EnableFilter(vlc_player_t *player, const char *name, bool add);

VLC_API int
vlc_player_aout_NextDevice(vlc_player_t *player);

/**
 * Get and hold the main video output
 *
 * @warning the returned vout_thread_t * must be released with
 * vlc_object_release().
 * @see vlc_players_cbs.on_vout_list_changed
 *
 * @param player player instance
 * @return a valid vout_thread_t * or NULL, cf. warning
 */
VLC_API vout_thread_t *
vlc_player_vout_Hold(vlc_player_t *player);

/**
 * Get and hold the list of video output
 *
 * @warning All vout_thread_t * element of the array must be released with
 * vlc_object_release(). The returned array must be freed.
 *
 * @see vlc_players_cbs.on_vout_list_changed
 *
 * @param player player instance
 * @param count valid pointer to store the array count
 * @return a array of vout_thread_t * or NULL, cf. warning
 */
VLC_API vout_thread_t **
vlc_player_vout_HoldAll(vlc_player_t *player, size_t *count);

/**
 * Add a listener callback for video output events
 *
 * @note The player instance doesn't need to be locked for vlc_player_vout_*()
 * functions.
 * @note Every registered callbacks need to be removed by the caller with
 * vlc_player_vout_RemoveListener().
 *
 * @param player player instance
 * @param cbs pointer to a vlc_player_vout_cbs structure, the structure must be
 * valid during the lifetime of the player
 * @param cbs_data opaque pointer used by the callbacks
 * @return a valid listener id, or NULL in case of allocation error
 */
VLC_API vlc_player_vout_listener_id *
vlc_player_vout_AddListener(vlc_player_t *player,
                            const struct vlc_player_vout_cbs *cbs,
                            void *cbs_data);

/**
 * Remove a vout listener callback
 *
 * @param player player instance
 * @param listener_id listener id returned by vlc_player_vout_AddListener()
 */
VLC_API void
vlc_player_vout_RemoveListener(vlc_player_t *player,
                               vlc_player_vout_listener_id *listener_id);

/**
 * Check if the player is fullscreen
 *
 * @warning The fullscreen state of the player and all vouts can be different.
 *
 * @note The player instance doesn't need to be locked for vlc_player_vout_*()
 * functions.
 *
 * @see vlc_player_vout_cbs.on_fullscreen_changed
 *
 * @param player player instance
 * @return true if the player is fullscreen
 */
VLC_API bool
vlc_player_vout_IsFullscreen(vlc_player_t *player);

/**
 * Enable or disable the player fullscreen state
 *
 * This will have an effect on all current and future vouts.
 *
 * @note The player instance doesn't need to be locked for vlc_player_vout_*()
 * functions.
 * @note A successful call will trigger the
 * vlc_player_vout_cbs.on_fullscreen_changed event.
 *
 * @param player player instance
 * @param enabled true to enable fullscreen
 */
VLC_API void
vlc_player_vout_SetFullscreen(vlc_player_t *player, bool enabled);

/**
 * Helper to toggle the player fullscreen state
 */
static inline void
vlc_player_vout_ToggleFullscreen(vlc_player_t *player)
{
    vlc_player_vout_SetFullscreen(player,
                                  !vlc_player_vout_IsFullscreen(player));
}

/**
 * Check if the player has wallpaper-mode enaled
 *
 * @warning The wallpaper-mode state of the player and all vouts can be
 * different.
 *
 * @note The player instance doesn't need to be locked for vlc_player_vout_*()
 * functions.
 *
 * @see vlc_player_vout_cbs.on_wallpaper_mode_changed
 *
 * @param player player instance
 * @return true if the player is fullscreen
 */
VLC_API bool
vlc_player_vout_IsWallpaperModeEnabled(vlc_player_t *player);

/**
 * Enable or disable the player wallpaper-mode
 *
 * This will have an effect on all current and future vouts.
 *
 * @note The player instance doesn't need to be locked for vlc_player_vout_*()
 * functions.
 * @note A successful call will trigger the
 * vlc_player_vout_cbs.on_wallpaper_mode_changed event.
 *
 * @param player player instance
 * @param enabled true to enable wallpaper-mode
 */
VLC_API void
vlc_player_vout_SetWallpaperModeEnabled(vlc_player_t *player, bool enabled);

/**
 * Helper to toggle the player wallpaper-mode state
 */
static inline void
vlc_player_vout_ToggleWallpaperMode(vlc_player_t *player)
{
    vlc_player_vout_SetWallpaperModeEnabled(player,
        !vlc_player_vout_IsWallpaperModeEnabled(player));
}

VLC_API void
vlc_player_vout_Snapshot(vlc_player_t *player);

VLC_API void
vlc_player_vout_SetOSDChannel(vlc_player_t *player,
                              enum vlc_player_osd osd_mode, int channel);

VLC_API void
vlc_player_vout_OSDAction(vlc_player_t *player,
                          enum vlc_player_osd osd_mode, ...);

#define vlc_player_vout_OSDMessage(player, fmt...) \
    vlc_player_vout_OSDAction(player, VLC_PLAYER_OSD_TEXT, fmt)

static inline void
vlc_player_vout_OSDIcon(vlc_player_t *player, short type)
{
    vlc_player_vout_OSDAction(player, VLC_PLAYER_OSD_ICON, type);
}

static inline void
vlc_player_vout_OSDSlider(vlc_player_t *player, int position, short type)
{
    vlc_player_vout_OSDAction(player, VLC_PLAYER_OSD_SLIDER, position, type);
}

/** @} */
#endif
