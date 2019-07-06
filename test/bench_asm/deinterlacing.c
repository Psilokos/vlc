#include <assert.h>
#include <vlc/vlc.h>
#include <vlc_filter.h>
#include <vlc_modules.h>
#include "bench_asm.h"

static libvlc_instance_t *libvlc;

static int
init_deinterlacer(char const *mode)
{
    /* TODO:
     * - do sout without muxing if possible */
    libvlc = libvlc_new(2, (char const *[])
            {
                "http://streams.videolan.org/streams/ts/bbc_news_24-239.35.2.0_dvbsub.ts",
                "--deinterlace-mode", mode
            });
    assert(libvlc);
}

static int
init_deinterlacer_linear(void)
{
    return init_deinterlacer("linear");
}

static void
destroy_deinterlacer(void)
{
    libvlc_release(libvlc);
}

static int
check_feature_deinterlacer(int flag)
{
}

static int
bench_deinterlacer(void)
{
    libvlc_playlist_play(libvlc);
    return 0;
}

void
subscribe_linear_deinterlacer(int id)
{
    bench_asm_subscribe(id, "linear deinterlacer",
                        init_deinterlacer_linear,
                        destroy_deinterlacer,
                        check_feature_deinterlacer,
                        bench_deinterlacer);
}
