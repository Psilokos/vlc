#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <vlc_common.h>
#include <vlc_cpu.h>

static uint32_t cpu_mask = 0;

static inline void
vlc_CPU_mask(unsigned cap)
{
    cpu_mask |= cap;
}

static inline void
vlc_CPU_unmask(unsigned cap)
{
    cpu_mask &= ~cap;
}

static inline unsigned
vlc_CPU_masked(void)
{
    return vlc_CPU() & ~cpu_mask;
}

#define vlc_CPU vlc_CPU_masked
# include "../modules/packetizer/startcode_helper.h"
#undef vlc_CPU

static int logging = 1;

static inline uint64_t
read_cycle_counter(void)
{
    register uint32_t eax;
    register uint32_t edx;
    __asm__ volatile ("lfence\n"
                      "rdtsc\n"
                      : "=a"(eax), "=d"(edx));
    return ((uint64_t)edx << 32) | eax;
}

static uint8_t *buffer;

static int
init_packetizer_startcode_FindAnnexB(void)
{
    buffer = calloc(4096, 1);
    if (!buffer) return VLC_ENOMEM;
    return VLC_SUCCESS;
}

static void
destroy_packetizer_startcode_FindAnnexB(void)
{
    free(buffer);
}

static int
check_feature_packetizer_startcode_FindAnnexB(int flag)
{
    uint8_t const *(*fct)(uint8_t const *ptr, uint8_t const *end)
        = startcode_FindAnnexB_helper();
    assert(fct);
    vlc_CPU_mask(flag);
    int has_impl = fct != startcode_FindAnnexB_helper();
    vlc_CPU_unmask(flag);
    return has_impl;
}

static void
bench_packetizer_startcode_FindAnnexB(char const *feature)
{
    uint8_t const *(*fct)(uint8_t const *ptr, uint8_t const *end)
        = startcode_FindAnnexB_helper();
    uint64_t cycles = 0;
    for (int j = 0; j < 4096; ++j)
    {
        buffer[j] = 1;
        uint64_t const cycles_start = read_cycle_counter();
        fct(buffer, buffer + 4096);
        uint64_t const cycles_end = read_cycle_counter();
        cycles += cycles_end - cycles_start;
        buffer[j] = 0;
    }
    if (logging)
        printf(" - %s: %lu\n", feature, cycles >> 12);
}

static struct bench
{
    char const *name;
    int (*init)(void);
    void (*destroy)(void);
    int (*check_feature)(int flag);
    void (*run)(char const *feature);
} const benchmarks[] =
{
    {
        .name          = "packetizer/startcode_FindAnnexB",
        .init          = init_packetizer_startcode_FindAnnexB,
        .destroy       = destroy_packetizer_startcode_FindAnnexB,
        .check_feature = check_feature_packetizer_startcode_FindAnnexB,
        .run           = bench_packetizer_startcode_FindAnnexB
    },
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
                bench->run(feature->name);
            toggle_log();
            bench->run(feature->name);
        }
        bench->destroy();
        continue;
error:
        if (ret == VLC_ENOMEM)
            printf("  allocation error, ");
        printf("skipping bench\n");
    }
}
