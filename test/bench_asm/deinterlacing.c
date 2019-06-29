#include <libvlc.h>
#include <vlc_filter.h>
#include <vlc_modules.h>
#include "bench_asm.h"

static libvlc_instance_t *libvlc;

static int
init_deinterlacer(char const *mode)
{
    /* TODO:
     * - do sout without muxing if possible */
    libvlc = libvlc_new(2,
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

static void
bench_deinterlacer(void)
{
    libvlc_playlist_play(libvlc);
}

void
register_linear_deinterlacing(int id)
{
    bench_asm_register(id, "linear deinterlacing",
                       init_deinterlacer_linear,
                       destroy_deinterlacer,
                       check_feature_deinterlacer,
                       bench_deinterlacer);
}
