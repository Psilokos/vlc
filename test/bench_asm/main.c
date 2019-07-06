/* FIXME add licence */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <vlc_common.h>
#include <vlc_cpu.h>
#include "bench_asm.h"

static int logging = 1;

static struct bench
{
    void (*subscribe)(int id);
    char const *name;
    int (*init)(void);
    void (*destroy)(void);
    int (*check_feature)(int flag);
    void (*run)(void);
} const benchmarks[] =
{
    { .subscribe = subscribe_startcode_annexb },
    { .subscribe = subscribe_linear_deinterlacer },
    { 0 }
};

static struct cpu_feature
{
    char const *name;
    int flag;
} const cpu_features[] =
{
    { "C", 0 },
    { "SSE2",   VLC_CPU_SSE2 },
    { "SSSE3",  VLC_CPU_SSSE3 },
    { "AVX2",   VLC_CPU_AVX2 },
//    { "AVX512", VLC_CPU_AVX512 },
    { 0 }
};

static inline void
toggle_log(void)
{
    logging ^= 1;
}

int
main(/* int argc, char **argv */)
{
    for (struct bench const *bench = benchmarks; bench->name; ++bench)
    {
        bench->subscribe((bench - benchmarks) / sizeof(*bench));

        printf("%s:\n", bench->name);
        int ret = bench->init();
        if (ret != VLC_SUCCESS)
            goto error;
        vlc_CPU_mask(~0);
        for (struct cpu_feature const *feature = cpu_features;
             feature->name; ++feature)
        {
            vlc_CPU_unmask(feature->flag);
            if (feature->flag && !bench->check_feature(feature->flag))
                continue;
            toggle_log();
            for (int i = 0; i < 5; ++i) /* warm up phase */
                bench->run();
            toggle_log();
            bench->run();
        }
        bench->destroy();
        continue;
error:
        if (ret == VLC_ENOMEM)
            printf("  allocation error, ");
        printf("skipping bench\n");
    }
}
