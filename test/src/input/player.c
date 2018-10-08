/*****************************************************************************
 * player.c: test vlc_player_t API
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

#include "../../libvlc/test.h"
#include "../lib/libvlc_internal.h"

#include <math.h>

#include <vlc_common.h>
#include <vlc_player.h>
#include <vlc_vector.h>

struct report_position
{
    vlc_tick_t time;
    float pos;
};

struct report_track_list
{
    enum vlc_player_list_action action;
    struct vlc_player_track *track;
};

struct report_track_selection
{
    vlc_es_id_t *unselected_id;
    vlc_es_id_t *selected_id;
};

struct report_program_list
{
    enum vlc_player_list_action action;
    struct vlc_player_program *prgm;
};

struct report_program_selection
{
    int unselected_id;
    int selected_id;
};

struct report_title_array
{
    input_title_t **array;
    size_t count;
};

struct report_chapter_selection
{
    size_t title_idx;
    size_t chapter_idx;
};

struct report_signal
{
    float quality;
    float strength;
};

struct report_vout_list
{
    enum vlc_player_list_action action;
    vout_thread_t *vout;
};

struct report_subitems
{
    size_t count;
    input_item_t **items;
};

#define REPORT_LIST \
    X(input_item_t *, on_current_media_changed) \
    X(enum vlc_player_state, on_state_changed) \
    X(enum vlc_player_error, on_error_changed) \
    X(float, on_buffering_changed) \
    X(float, on_rate_changed) \
    X(int, on_capabilities_changed) \
    X(struct report_position, on_position_changed) \
    X(vlc_tick_t, on_length_changed) \
    X(struct report_track_list, on_track_list_changed) \
    X(struct report_track_selection, on_track_selection_changed) \
    X(struct report_program_list, on_program_list_changed) \
    X(struct report_program_selection, on_program_selection_changed) \
    X(struct report_title_array, on_title_array_changed) \
    X(size_t, on_title_selection_changed) \
    X(struct report_chapter_selection, on_chapter_selection_changed) \
    X(vlc_tick_t, on_audio_delay_changed) \
    X(vlc_tick_t, on_subtitle_delay_changed) \
    X(bool, on_record_changed) \
    X(struct report_signal, on_signal_changed) \
    X(struct input_stats_t, on_stats_changed) \
    X(struct report_vout_list, on_vout_list_changed) \
    X(input_item_t *, on_media_meta_changed) \
    X(input_item_t *, on_media_epg_changed) \
    X(struct report_subitems, on_subitems_changed) \
    X(float, on_aout_volume_changed) \
    X(bool, on_aout_mute_changed) \

#define X(type, name) typedef struct VLC_VECTOR(type) vec_##name;
REPORT_LIST
#undef X

#define X(type, name) vec_##name name;
struct reports
{
REPORT_LIST
};
#undef X

static inline void
reports_init(struct reports *report)
{
#define X(type, name) vlc_vector_init(&report->name);
REPORT_LIST
#undef X
}

struct media_params
{
    size_t video_tracks;
    size_t audio_tracks;
    vlc_tick_t length;

    bool can_seek;
    bool can_pause;
    bool error;
};

#define DEFAULT_MEDIA_PARAMS(param_length) { \
    .video_tracks = 1, \
    .audio_tracks = 1, \
    .length = param_length, \
    .can_seek = true, \
    .can_pause = true, \
    .error = false, \
}

struct ctx
{
    vlc_player_t *player;
    struct VLC_VECTOR(input_item_t *) next_medias;

    size_t media_count;
    struct media_params params;
    float rate;

    vlc_cond_t wait;
    struct reports report;
};

static struct ctx *
get_ctx(vlc_player_t *player, void *data)
{
    assert(data);
    struct ctx *ctx = data;
    assert(player == ctx->player);
    return ctx;
}

static void
ctx_destroy(struct ctx *ctx)
{
#define X(type, name) vlc_vector_destroy(&ctx->report.name);
REPORT_LIST
#undef X
}

static input_item_t *
player_get_next(vlc_player_t *player, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    input_item_t *next_media;
    if (ctx->next_medias.size > 0)
    {
        next_media = ctx->next_medias.data[0];
        vlc_vector_remove(&ctx->next_medias, 0);
    }
    else
        next_media = NULL;
    return next_media;
}

#define VEC_PUSH(vec, item) do { \
    bool success = vlc_vector_push(&ctx->report.vec, item); \
    assert(success); \
    vlc_cond_signal(&ctx->wait); \
} while(0)

static void
player_on_current_media_changed(vlc_player_t *player,
                                input_item_t *new_media, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    if (new_media)
        input_item_Hold(new_media);
    VEC_PUSH(on_current_media_changed, new_media);
}

static void
player_on_state_changed(vlc_player_t *player, enum vlc_player_state state,
                        void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_state_changed, state);
}

static void
player_on_error_changed(vlc_player_t *player, enum vlc_player_error error,
                        void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_error_changed, error);
}

static void
player_on_buffering_changed(vlc_player_t *player, float new_buffering,
                            void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_buffering_changed, new_buffering);
}

static void
player_on_rate_changed(vlc_player_t *player, float new_rate, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_rate_changed, new_rate);
}

static void
player_on_capabilities_changed(vlc_player_t *player, int new_caps,
                               void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_capabilities_changed, new_caps);
}

static void
player_on_position_changed(vlc_player_t *player, vlc_tick_t time,
                           float pos, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_position report = {
        .time = time,
        .pos = pos,
    };
    VEC_PUSH(on_position_changed, report);
}

static void
player_on_length_changed(vlc_player_t *player, vlc_tick_t new_length,
                         void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_length_changed, new_length);
}

static void
player_on_track_list_changed(vlc_player_t *player,
                             enum vlc_player_list_action action,
                             const struct vlc_player_track *track,
                             void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_track_list report = {
        .action = action,
        .track = vlc_player_track_Dup(track),
    };
    assert(report.track);
    VEC_PUSH(on_track_list_changed, report);
}

static void
player_on_track_selection_changed(vlc_player_t *player,
                                  vlc_es_id_t *unselected_id,
                                  vlc_es_id_t *selected_id, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_track_selection report = {
        .unselected_id = unselected_id ? vlc_es_id_Hold(unselected_id) : NULL,
        .selected_id = selected_id ? vlc_es_id_Hold(selected_id) : NULL,
    };
    VEC_PUSH(on_track_selection_changed, report);
}

static void
player_on_program_list_changed(vlc_player_t *player,
                               enum vlc_player_list_action action,
                               const struct vlc_player_program *prgm,
                               void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_program_list report = {
        .action = action,
        .prgm = vlc_player_program_Dup(prgm)
    };
    assert(report.prgm);
    VEC_PUSH(on_program_list_changed, report);
}

static void
player_on_program_selection_changed(vlc_player_t *player,
                                    int unselected_id, int selected_id,
                                    void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_program_selection report = {
        .unselected_id = unselected_id,
        .selected_id = selected_id,
    };
    VEC_PUSH(on_program_selection_changed, report);
}

static void
player_on_title_array_changed(vlc_player_t *player,
                              const input_title_t * const *array,
                              size_t count, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_title_array report = {
        .count = count,
        .array = vlc_alloc(count, sizeof(input_title_t *)),
    };
    assert(report.array);
    for (size_t i = 0; i < count; ++i)
    {
        report.array[i] = vlc_input_title_Duplicate(array[i]);
        assert(report.array[i]);
    }
    VEC_PUSH(on_title_array_changed, report);
}

static void
player_on_title_selection_changed(vlc_player_t *player,
                                  const input_title_t *new_title,
                                  size_t new_idx, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_title_selection_changed, new_idx);
    (void) new_title;
}

static void
player_on_chapter_selection_changed(vlc_player_t *player,
                                    const input_title_t *title,
                                    size_t title_idx,
                                    const seekpoint_t *chapter,
                                    size_t chapter_idx, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_chapter_selection report = {
        .title_idx = title_idx,
        .chapter_idx = chapter_idx,
    };
    VEC_PUSH(on_chapter_selection_changed, report);
    (void) title;
    (void) chapter;
}

static void
player_on_audio_delay_changed(vlc_player_t *player, vlc_tick_t new_delay,
                              void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_audio_delay_changed, new_delay);
}

static void
player_on_subtitle_delay_changed(vlc_player_t *player, vlc_tick_t new_delay,
                                 void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_subtitle_delay_changed, new_delay);
}

static void
player_on_record_changed(vlc_player_t *player, bool recording, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_record_changed, recording);
}

static void
player_on_signal_changed(vlc_player_t *player,
                         float quality, float strength, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_signal report = {
        .quality = quality,
        .strength = strength,
    };
    VEC_PUSH(on_signal_changed, report);
}

static void
player_on_stats_changed(vlc_player_t *player,
                        const struct input_stats_t *stats, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct input_stats_t dup = *stats;
    VEC_PUSH(on_stats_changed, dup);
}

static void
player_on_vout_list_changed(vlc_player_t *player,
                            enum vlc_player_list_action action,
                            vout_thread_t *vout, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    struct report_vout_list report = {
        .action = action,
        .vout = vout,
    };
    vlc_object_hold(vout);
    VEC_PUSH(on_vout_list_changed, report);
}

static void
player_on_media_meta_changed(vlc_player_t *player, input_item_t *media,
                             void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    input_item_Hold(media);
    VEC_PUSH(on_media_meta_changed, media);
}

static void
player_on_media_epg_changed(vlc_player_t *player, input_item_t *media,
                            void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    input_item_Hold(media);
    VEC_PUSH(on_media_epg_changed, media);
}

static void
player_on_subitems_changed(vlc_player_t *player,
                           input_item_node_t *subitems, void *data)
{
    struct ctx *ctx = get_ctx(player, data);

    struct report_subitems report = {
        .count = subitems->i_children,
        .items = vlc_alloc(subitems->i_children, sizeof(input_item_t)),
    };
    assert(report.items);
    for (int i = 0; i < subitems->i_children; ++i)
        report.items[i] = input_item_Hold(subitems->pp_children[i]->p_item);
    VEC_PUSH(on_subitems_changed, report);
}

static void
player_on_aout_volume_changed(vlc_player_t *player, audio_output_t *aout,
                              float volume, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_aout_volume_changed, volume);
    (void) aout;
}

static void
player_on_aout_mute_changed(vlc_player_t *player, audio_output_t *aout,
                            bool muted, void *data)
{
    struct ctx *ctx = get_ctx(player, data);
    VEC_PUSH(on_aout_mute_changed, muted);
    (void) aout;
}

#define VEC_LAST(vec) vec->data[vec->size - 1]
#define assert_position(ctx, report) do { \
    assert(fabs((report)->pos - (report)->time / (float) ctx->params.length) < 0.001); \
} while (0)

#define wait_state(ctx, state) do { \
    vec_on_state_changed *vec = &ctx->report.on_state_changed; \
    while (vec->size == 0 || VEC_LAST(vec) != state) \
        vlc_player_CondWait(player, &ctx->wait); \
} while(0)

#define assert_state(ctx, state) do { \
    vec_on_state_changed *vec = &ctx->report.on_state_changed; \
    assert(VEC_LAST(vec) == state); \
} while(0)

#define assert_normal_state(ctx) do { \
    vec_on_state_changed *vec = &ctx->report.on_state_changed; \
    assert(vec->size == 3); \
    assert(vec->data[0] == VLC_PLAYER_STATE_STARTED); \
    assert(vec->data[1] == VLC_PLAYER_STATE_PLAYING); \
    assert(vec->data[2] == VLC_PLAYER_STATE_STOPPED); \
} while(0)

static void
ctx_reset(struct ctx *ctx)
{
#define FOREACH_VEC(item, vec) vlc_vector_foreach(item, &ctx->report.vec)
#define CLEAN_MEDIA_VEC(vec) do { \
    input_item_t *media; \
    FOREACH_VEC(media, vec) { \
        if (media) \
            input_item_Release(media); \
    } \
} while(0)


    CLEAN_MEDIA_VEC(on_current_media_changed);
    CLEAN_MEDIA_VEC(on_media_meta_changed);
    CLEAN_MEDIA_VEC(on_media_epg_changed);

    {
        struct report_track_list report;
        FOREACH_VEC(report, on_track_list_changed)
            vlc_player_track_Delete(report.track);
    }

    {
        struct report_track_selection report;
        FOREACH_VEC(report, on_track_selection_changed)
        {
            if (report.unselected_id)
                vlc_es_id_Release(report.unselected_id);
            if (report.selected_id)
                vlc_es_id_Release(report.selected_id);
        }
    }

    {
        struct report_program_list report;
        FOREACH_VEC(report, on_program_list_changed)
            vlc_player_program_Delete(report.prgm);
    }

    {
        struct report_title_array report;
        FOREACH_VEC(report, on_title_array_changed)
        {
            for (size_t i = 0; i < report.count; ++i)
                vlc_input_title_Delete(report.array[i]);
            free(report.array);
        }
    }

    {
        struct report_vout_list report;
        FOREACH_VEC(report, on_vout_list_changed)
            vlc_object_release(report.vout);
    }

    {
        struct report_subitems report;
        FOREACH_VEC(report, on_subitems_changed)
        {
            for (size_t i = 0; i < report.count; ++i)
                input_item_Release(report.items[i]);
            free(report.items);
        }
    }

#define X(type, name) vlc_vector_clear(&ctx->report.name);
REPORT_LIST
#undef X

    input_item_t *media;
    vlc_vector_foreach(media, &ctx->next_medias)
        input_item_Release(media);
    vlc_vector_clear(&ctx->next_medias);

    ctx->media_count = 0;
    ctx->rate = 1.f;
};

static input_item_t *
create_mock_media(const char *name, const struct media_params *params)
{
    assert(params);
    char *url;
    int ret = asprintf(&url,
        "mock://video_track_count=%zu;audio_track_count=%zu;"
        "length=%"PRId64";can_seek=%d;can_pause=%d;error=%d",
        params->video_tracks, params->audio_tracks, params->length,
        params->can_seek, params->can_pause, params->error);
    assert(ret != -1);

    input_item_t *item = input_item_New(url, name);
    assert(item);
    free(url);
    return item;
}

static void
player_set_next_mock_media(struct ctx *ctx, const char *name,
                           const struct media_params *params)
{
    input_item_t *media = create_mock_media(name, params);
    assert(media);
    if (vlc_player_GetCurrentMedia(ctx->player) == NULL)
    {
        assert(ctx->media_count == 0);
        ctx->params = *params;

        int ret = vlc_player_SetCurrentMedia(ctx->player, media);
        assert(ret == VLC_SUCCESS);
        input_item_Release(media);
    }
    else
    {
        assert(ctx->media_count > 0);
        bool success = vlc_vector_push(&ctx->next_medias, media);
        assert(success);
    }
    ctx->media_count++;
}

static void
player_set_rate(struct ctx *ctx, float rate)
{
    vlc_player_ChangeRate(ctx->player, rate);
    ctx->rate = rate;
}

static void
test_end(struct ctx *ctx)
{
    vlc_player_t *player = ctx->player;

    if (ctx->rate != 1.0f)
    {
        vec_on_rate_changed *vec = &ctx->report.on_rate_changed;
        assert(vec->size > 0);
        assert(VEC_LAST(vec) == ctx->rate);
    }

    {
        vec_on_length_changed *vec = &ctx->report.on_length_changed;
        assert(vec->size == ctx->media_count);
        for (size_t i = 0; i < vec->size; ++i)
            assert(vec->data[i] == ctx->params.length);
        assert(ctx->params.length == vlc_player_GetLength(player));
    }

    {
        vec_on_capabilities_changed *vec = &ctx->report.on_capabilities_changed;
        assert(vec->size > 0);
        assert(vlc_player_CanSeek(player) == ctx->params.can_seek
            && !!(VEC_LAST(vec) & VLC_PLAYER_CAP_SEEK) == ctx->params.can_seek);
        assert(vlc_player_CanPause(player) == ctx->params.can_pause
            && !!(VEC_LAST(vec) & VLC_PLAYER_CAP_PAUSE) == ctx->params.can_pause);
    }

    {
        vec_on_state_changed *vec = &ctx->report.on_state_changed;
        assert(vec-> size > 1);
        assert(vec->data[0] == VLC_PLAYER_STATE_STARTED);
    }

    vlc_player_Stop(player);
    assert(vlc_player_GetCurrentMedia(player) != NULL);

    vlc_player_SetCurrentMedia(player, NULL);
    assert(vlc_player_GetCurrentMedia(player) == NULL);

    {
        vec_on_current_media_changed *vec = &ctx->report.on_current_media_changed;
        assert(vec->size == ctx->media_count + 1);
        assert(VEC_LAST(vec) == NULL);
    }

    player_set_rate(ctx, 1.0f);

    ctx_reset(ctx);
}

static void
test_error(struct ctx *ctx)
{
    test_log("error\n");
    vlc_player_t *player = ctx->player;

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_SEC(1));
    params.error = true;
    player_set_next_mock_media(ctx, "media1", &params);

    vlc_player_Start(player);

    {
        vec_on_error_changed *vec = &ctx->report.on_error_changed;
        while (vec->size == 0 || VEC_LAST(vec) == VLC_PLAYER_ERROR_NONE)
            vlc_player_CondWait(player, &ctx->wait);
    }
    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);

    test_end(ctx);
}

static void
test_capabilities_seek(struct ctx *ctx)
{
    test_log("capabilites_seek\n");
    vlc_player_t *player = ctx->player;

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_SEC(1));
    params.can_seek = false;
    player_set_next_mock_media(ctx, "media1", &params);

    vlc_player_Start(player);

    {
        vec_on_capabilities_changed *vec = &ctx->report.on_capabilities_changed;
        while (vec->size == 0)
            vlc_player_CondWait(player, &ctx->wait);
    }

    vlc_player_ChangeRate(player, 4.f);

    /* Ensure that seek back to 0 doesn't work */
    {
        vlc_tick_t last_time = 0;
        vec_on_state_changed *vec = &ctx->report.on_state_changed;
        while (vec->size == 0 || VEC_LAST(vec) != VLC_PLAYER_STATE_STOPPED)
        {
            vec_on_position_changed *posvec = &ctx->report.on_position_changed;
            if (posvec->size > 0 && last_time != VEC_LAST(posvec).time)
            {
                last_time = VEC_LAST(posvec).time;
                vlc_player_SetTime(player, 0);
            }
            vlc_player_CondWait(player, &ctx->wait);
        }
    }

    assert_state(ctx, VLC_PLAYER_STATE_STOPPED);
    test_end(ctx);
}

static void
test_capabilities_pause(struct ctx *ctx)
{
    test_log("capabilites_pause\n");
    vlc_player_t *player = ctx->player;

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_SEC(1));
    params.can_pause = false;
    player_set_next_mock_media(ctx, "media1", &params);

    vlc_player_Start(player);

    {
        vec_on_capabilities_changed *vec = &ctx->report.on_capabilities_changed;
        while (vec->size == 0)
            vlc_player_CondWait(player, &ctx->wait);
    }

    /* Ensure that pause doesn't work */
    vlc_player_Pause(player);
    vlc_player_ChangeRate(player, 32.f);

    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);
    assert_normal_state(ctx);

    test_end(ctx);
}

static void
test_pause(struct ctx *ctx)
{
    test_log("pause\n");
    vlc_player_t *player = ctx->player;

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_SEC(10));
    player_set_next_mock_media(ctx, "media1", &params);

    /* Start paused */
    vlc_player_Pause(player);
    vlc_player_Start(player);
    {
        vec_on_state_changed *vec = &ctx->report.on_state_changed;
        while (vec->size == 0 || VEC_LAST(vec) != VLC_PLAYER_STATE_PAUSED)
            vlc_player_CondWait(player, &ctx->wait);
        assert(vec->size == 3);
        assert(vec->data[0] == VLC_PLAYER_STATE_STARTED);
        assert(vec->data[1] == VLC_PLAYER_STATE_PLAYING);
        assert(vec->data[2] == VLC_PLAYER_STATE_PAUSED);
    }

    {
        vec_on_position_changed *vec = &ctx->report.on_position_changed;
        assert(vec->size == 0);
    }

    /* Resume */
    vlc_player_Resume(player);

    {
        vec_on_state_changed *vec = &ctx->report.on_state_changed;
        while (VEC_LAST(vec) != VLC_PLAYER_STATE_PLAYING)
            vlc_player_CondWait(player, &ctx->wait);
        assert(vec->size == 4);
    }

    {
        vec_on_position_changed *vec = &ctx->report.on_position_changed;
        while (vec->size == 0)
            vlc_player_CondWait(player, &ctx->wait);
    }

    /* Pause again (while playing) */
    vlc_player_Pause(player);

    {
        vec_on_state_changed *vec = &ctx->report.on_state_changed;
        while (VEC_LAST(vec) != VLC_PLAYER_STATE_PAUSED)
            vlc_player_CondWait(player, &ctx->wait);
        assert(vec->size == 5);
    }

    test_end(ctx);
}

static void
test_seeks(struct ctx *ctx)
{
    test_log("seeks\n");
    vlc_player_t *player = ctx->player;

    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_SEC(10));
    player_set_next_mock_media(ctx, "media1", &params);

    /* only the last one will be taken into account before start */
    vlc_player_SetTimeFast(player, 0);
    vlc_player_SetTimeFast(player, VLC_TICK_FROM_SEC(100));
    vlc_player_SetTimeFast(player, 10);

    vlc_tick_t seek_time = VLC_TICK_FROM_SEC(5);
    vlc_player_SetTimeFast(player, seek_time);
    vlc_player_Start(player);

    {
        vec_on_position_changed *vec = &ctx->report.on_position_changed;
        while (vec->size == 0)
            vlc_player_CondWait(player, &ctx->wait);

        assert(VEC_LAST(vec).time >= seek_time);
        assert_position(ctx, &VEC_LAST(vec));

        vlc_tick_t last_time = VEC_LAST(vec).time;

        vlc_tick_t jump_time = -VLC_TICK_FROM_SEC(2);
        vlc_player_JumpTime(player, jump_time);

        while (VEC_LAST(vec).time >= last_time)
            vlc_player_CondWait(player, &ctx->wait);

        assert(VEC_LAST(vec).time >= last_time + jump_time);
        assert_position(ctx, &VEC_LAST(vec));
    }

    vlc_player_SetPosition(player, 2.0f);

    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);
    assert_normal_state(ctx);

    test_end(ctx);
}

static void
test_next_medias(struct ctx *ctx)
{
    test_log("next_medias\n");
    const char *media_names[] = { "media1", "media2", "media3" };
    const size_t media_count = ARRAY_SIZE(media_names);

    vlc_player_t *player = ctx->player;
    struct media_params params = DEFAULT_MEDIA_PARAMS(VLC_TICK_FROM_MS(100));

    for (size_t i = 0; i < media_count; ++i)
        player_set_next_mock_media(ctx, media_names[i], &params);
    player_set_rate(ctx, 4.f);
    vlc_player_Start(player);

    wait_state(ctx, VLC_PLAYER_STATE_STOPPED);
    assert_normal_state(ctx);

    {
        vec_on_current_media_changed *vec = &ctx->report.on_current_media_changed;
        assert(vec->size == media_count);
        assert(ctx->next_medias.size == 0);
        for (size_t i = 0; i < ctx->media_count; ++i)
        {
            assert(vec->data[i]);
            char *name = input_item_GetName(vec->data[i]);
            assert(name && strcmp(name, media_names[i]) == 0);
            free(name);
        }
    }

    assert_normal_state(ctx);

    test_end(ctx);
}

int
main(void)
{
    test_init();

    static const char * argv[] = {
        "-v",
        "--ignore-config",
        "-Idummy",
        "--no-media-library",
        /* Avoid leaks from various dlopen... */
        "--codec=araw,rawvideo,none",
        "--vout=dummy",
        "--aout=dummy",
    };
    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(argv), argv);
    assert(vlc);

    static const struct vlc_player_media_provider provider = {
        .get_next = player_get_next,
    };

#define X(type, name) .name = player_##name,
    static const struct vlc_player_cbs cbs = {
REPORT_LIST
    };
#undef X

    struct ctx ctx = {
        .next_medias = VLC_VECTOR_INITIALIZER,
        .media_count = 0,
        .rate = 1.f,
        .wait = VLC_STATIC_COND,
    };
    reports_init(&ctx.report);

    /* Force wdummy window */
    int ret = var_Create(vlc->p_libvlc_int, "window", VLC_VAR_STRING);
    assert(ret == VLC_SUCCESS);
    ret = var_SetString(vlc->p_libvlc_int, "window", "wdummy");
    assert(ret == VLC_SUCCESS);

    ctx.player = vlc_player_New(VLC_OBJECT(vlc->p_libvlc_int), &provider, &ctx);
    vlc_player_t *player = ctx.player;
    assert(player);

    vlc_player_Lock(player);
    struct vlc_player_listener_id *listener =
        vlc_player_AddListener(player, &cbs, &ctx);
    assert(listener);

    test_next_medias(&ctx);
    test_seeks(&ctx);
    test_pause(&ctx);
    test_capabilities_pause(&ctx);
    test_capabilities_seek(&ctx);
    test_error(&ctx);

    vlc_player_RemoveListener(player, listener);
    vlc_player_Unlock(player);

    vlc_player_Delete(player);
    libvlc_release(vlc);

    ctx_destroy(&ctx);
    return 0;
}
