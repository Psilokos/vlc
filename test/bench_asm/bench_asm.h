#ifndef BENCH_ASM_H
# define BENCH_ASM_H

#include <stdint.h>

void bench_asm_subscribe(int id, char const *name,
                         int (*init)(void), void (*destroy)(void),
                         int (*check_feature)(int),
                         uint64_t (*bench)(void), bool need_warm_up);

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

#endif /* BENCH_ASM_H */
