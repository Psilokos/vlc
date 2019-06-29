#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_picture.h>
#include <vlc_plugin.h>
#include "bench_asm.h"

struct filter_sys
{
    filter_t *deint;
    uint64_t total_delta;
    int num_delta;
};

static picture_t *
Filter(filter_t *filter, picture_t *pic)
{
    /* TODO record time */
    struct filter_sys *sys = filter->p_sys;
    uint64_t const *start_time = read_cycle_counter();
    pic = sys->deint->pf_video_filter(sys->deint, pic);
    uint64_t const *end_time = read_cycle_counter();
    return pic;
}

static int opened = 0;

static void
Close(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *)filter;
    struct filter_sys *sys = filter->p_sys;
    if (sys->deint)
    {
        if (sys->deint->p_module)
            module_unneed(filter, sys->deint->p_module);
        vlc_object_delete(deint);
    }
    free(sys);
    opened = 0;

    /* TODO report mean delta to bench tool */
}

static int
Open(vlc_object_t *obj)
{
    if (opened)
        return VLC_EGENERIC;

    filter_t *filter = (filter_t *)filter;
    struct filter_sys *sys = calloc(1, sizeof(*sys));
    if (!sys)
        return VLC_ENOMEM;

    sys->deint = vlc_custom_create(filter, sizeof(filter_t), "deinterlacer");
    if (!sys->deint)
        goto error;

    sys->deint->p_module =
        module_need(sys->deint, "video filter", "deinterlace", true);
    if (!sys->deint->p_module)
        goto error;

    filter->p_sys = sys;
    filter->pf_video_filter = Filter;
    opened = 1;
    return VLC_SUCCESS;

error:
    Close(obj);
    return VLC_EGENERIC;
}

vlc_module_begin()
    set_callbacks(Open, Close)
vlc_module_end()
