#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <vlc_common.h>
#include <vlc_cpu.h>
#include "bench_asm.h"
#include "../../modules/packetizer/startcode_helper.h"

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

static uint8_t const *(*startcode_FindAnnexB)(uint8_t const *, uint8_t const *);

static int
check_feature_startcode_FindAnnexB(int flag)
{
    startcode_FindAnnexB = startcode_FindAnnexB_helper();
    assert(startcode_FindAnnexB);
    if (flag == 0)
        return VLC_SUCCESS;
    vlc_CPU_mask(flag);
    int has_impl = startcode_FindAnnexB != startcode_FindAnnexB_helper();
    vlc_CPU_unmask(flag);
    return has_impl ? VLC_SUCCESS : VLC_EGENERIC;
}

static uint64_t
bench_startcode_FindAnnexB(void)
{
    assert(startcode_FindAnnexB);
    uint64_t cycles = 0;
    for (int i = 0; i < 4096; ++i)
    {
        buffer[i] = 1;
        uint64_t const cycles_start = read_cycle_counter();
        startcode_FindAnnexB(buffer, buffer + 4096);
        uint64_t const cycles_end = read_cycle_counter();
        cycles += cycles_end - cycles_start;
        buffer[i] = 0;
    }
    return (cycles + (1UL << 11)) >> 12;
}

void
subscribe_startcode_annexb(int id)
{
    bench_asm_subscribe(id, "startcode_FindAnnexB",
                        init_startcode_FindAnnexB,
                        destroy_startcode_FindAnnexB,
                        check_feature_startcode_FindAnnexB,
                        bench_startcode_FindAnnexB, true);
}
