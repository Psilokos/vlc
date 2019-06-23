#include <libvlc.h>
#include <vlc_filter.h>
#include <vlc_modules.h>
#include "bench_asm.h"

static libvlc_instance_t *libvlc;
static filter_t *deinterlacer;

/* FIXME for cpu features cycling, need to create / destroy filter in
 * bench function
 * init and destroy must handle everything but the deinterlacer module
 */

static int
init_deinterlacer(int vlc_argc, char const **vlc_argv)
{
    libvlc = libvlc_new(vlc_argc, vlc_argv);
    assert(libvlc);

    deinterlacer = vlc_custom_create(libvlc->p_libvlc_int,
                                     sizeof(filter_t), "deinterlacer");
    assert(deinterlacer);

    deinterlacer->p_module =
        module_need(deinterlacer, "video filter", "deinterlace", true);
    assert(deinterlacer->p_module);
}

static int
init_deinterlacer_linear(void)
{
    return init_deinterlacer(1, { "--deinterlace-mode=linear" });
}

static void
destroy_deinterlacer(void)
{
    module_unneed(libvlc->p_libvlc_int, deinterlacer->p_module);
    vlc_object_delete(deinterlacer);
    libvlc_release(libvlc);
}

static int
check_feature_deinterlacer(int flag)
{
}

static void
bench_deinterlacer(void)
{
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
