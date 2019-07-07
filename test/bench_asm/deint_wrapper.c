#if HAVE_CONFIG_H
# include "config.h"
#endif

#define MODULE_STRING "deint_wrapper"
#define MODULE_NAME   deint_wrapper

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <vlc_common.h>
#include <vlc_filter.h>
#include <vlc_modules.h>
#include <vlc_picture.h>
#include <vlc_plugin.h>
#include "../src/libvlc.h"
#include "bench_asm.h"

struct filter_sys
{
    filter_chain_t *deint;
    uint64_t total_delta;
    int num_delta;
};

static picture_t *
Filter(filter_t *filter, picture_t *pic)
{
    struct filter_sys *sys = filter->p_sys;
    uint64_t const start_time = read_cycle_counter();
    pic = filter_chain_VideoFilter(sys->deint, pic);
    uint64_t const end_time = read_cycle_counter();
    sys->total_delta += end_time - start_time;
    ++sys->num_delta;
    return pic;
}

static void
Close(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *)obj;
    struct filter_sys *sys = filter->p_sys;
    int shm_id = shmget(ftok("deint_cycles", 0x2A), 64, 0600);
    uint64_t *cycles = shmat(shm_id, NULL, 0);
    *cycles = sys->total_delta / sys->num_delta;
    shmdt(cycles);
    if (sys->deint)
        filter_chain_Delete(sys->deint);
    free(sys);
}

static picture_t *
BufferNew(filter_t *filter)
{
    return filter_NewPicture((filter_t *)filter->owner.sys);
}

static struct filter_video_callbacks const cbs =
{
    .buffer_new = BufferNew
};

static int
Open(vlc_object_t *obj)
{
    filter_t *filter = (filter_t *)obj;

    if (filter->p_cfg && !strcmp(filter->p_cfg->psz_name, "wrapper-opened"))
        return VLC_EGENERIC;

    fprintf(stderr, "IT FUCKING WORKS!!!!\n");

    struct filter_sys *sys = calloc(1, sizeof(*sys));
    if (!sys)
        return VLC_ENOMEM;

    filter_owner_t owner =
    {
        .video = &cbs,
        .sys = filter
    };
    sys->deint = filter_chain_NewVideo(filter, false, &owner);
    if (!sys->deint)
        goto error;

    static struct config_chain_t cfg = { .psz_name = "wrapper-opened" };
    if (!filter_chain_AppendFilter(sys->deint, "deinterlace", &cfg,
                                   &filter->fmt_in, &filter->fmt_out))
        goto error;

    filter->p_sys = sys;
    filter->pf_video_filter = Filter;
    return VLC_SUCCESS;

error:
    Close(obj);
    return VLC_EGENERIC;
}

vlc_module_begin()
    add_shortcut("deinterlace")
    set_category(CAT_VIDEO)
    set_subcategory(SUBCAT_VIDEO_VFILTER)
    set_capability("video filter", 1)
    set_callbacks(Open, Close)
vlc_module_end()
