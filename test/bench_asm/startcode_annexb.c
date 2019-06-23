#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_cpu.h>
#include "bench_asm.h"

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
# include "../../modules/packetizer/startcode_helper.h"
#undef vlc_CPU

static uint8_t *buffer;

static int
init_startcode_FindAnnexB(void)
{
    buffer = calloc(4096, 1);
    if (!buffer) return VLC_ENOMEM;
    return VLC_SUCCESS;
}

static void
destroy_startcode_FindAnnexB(void)
{
    free(buffer);
}

static int
check_feature_startcode_FindAnnexB(int flag)
{
    uint8_t const *(*fct)(uint8_t const *ptr, uint8_t const *end)
        = startcode_FindAnnexB_helper();
    assert(fct);
    vlc_CPU_mask(flag);
    int has_impl = fct != startcode_FindAnnexB_helper();
    vlc_CPU_unmask(flag);
    return has_impl;
}

static unsigned int
bench_startcode_FindAnnexB(char const *feature)
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
    return cycles >> 12;
}

void
register_startcode_annexb(int id)
{
    bench_asm_register(id, "startcode_FindAnnexB",
                       init_startcode_FindAnnexB,
                       destroy_startcode_FindAnnexB,
                       check_feature_startcode_FindAnnexB,
                       bench_startcode_FindAnnexB);
}
