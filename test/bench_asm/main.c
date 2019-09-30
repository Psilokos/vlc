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

static struct bench
{
    void (*subscribe)(int id);
    char const *name;
    int (*init)(void);
    void (*destroy)(void);
    int (*check_feature)(int flag);
    uint64_t (*run)(void);
    bool need_warm_up;
} benchmarks[] =
{
    { 0 }
};

void
bench_asm_subscribe(int id, char const *name,
                    int (*init)(void), void (*destroy)(void),
                    int (*check_feature)(int),
                    uint64_t (*bench)(void), bool need_warm_up)
{
    benchmarks[id].name = name;
    benchmarks[id].init = init;
    benchmarks[id].destroy = destroy;
    benchmarks[id].check_feature = check_feature;
    benchmarks[id].run = bench;
    benchmarks[id].need_warm_up = need_warm_up;
}

static struct cpu_feature
{
    char const *name;
    int flag;
} const cpu_features[] =
{
    { "C",      0 },
    { "SSE2",   VLC_CPU_SSE2 },
    { "SSSE3",  VLC_CPU_SSSE3 },
    { "AVX2",   VLC_CPU_AVX2 },
    { 0 }
};

int
main(/* int argc, char **argv */)
{
    for (struct bench const *bench = benchmarks; bench->subscribe; ++bench)
    {
        bench->subscribe(bench - benchmarks);

        printf("%s:\n", bench->name);
        int ret = bench->init();
        if (ret != VLC_SUCCESS)
            goto error;
        vlc_CPU_mask(~0);
        for (struct cpu_feature const *feature = cpu_features;
             feature->name; ++feature)
        {
            vlc_CPU_unmask(feature->flag);
            if (bench->check_feature(feature->flag))
                continue;
            if (bench->need_warm_up && feature->flag)
                for (int i = 0; i < 5; ++i)
                    bench->run();
            printf(" - %-5s : %lu\n", feature->name, bench->run());
        }
        bench->destroy();
        continue;
error:
        if (ret == VLC_ENOMEM)
            printf("  allocation error, ");
        printf("skipping bench\n");
    }
}
