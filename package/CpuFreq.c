// SPDX-License-Identifier: APSL-2.0
// EPSS port from OOSEX GANG HackiOS redwood_cpufreq.c and ios-instantnoodle patch 0107.
// SM8250 domains: CPUs 0-3, 4-6, 7. Linux qcom-cpufreq-hw.c supplies the EPSS register layout.
// No RPMh bandwidth vote: touch owns active TCS0 until a shared RPMh arbiter exists.
#include <kern/thread.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <libkern/libkern.h>
#include <pexpert/pexpert.h>
#include "CpuFreq.h"

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void instantnoodle_cpufreq_init(void);
void instantnoodle_cpufreq_init(void)
{
    unsigned enabled = 0;
    if (!PE_parse_boot_argn("instantnoodle-cpufreq", &enabled, sizeof(enabled)) || enabled != 1) return;
    for (unsigned domain = 0; domain < 3; ++domain) {
        volatile uint32_t *regs = (volatile uint32_t *)ml_io_map(0x18591000UL + domain * 0x1000, 0x1000);
        if (!regs || !(regs[0] & 1)) {
            printf("instantnoodle cpufreq: domain %u unavailable/disabled\n", domain);
            continue;
        }
        uint32_t table[NOODLE_EPSS_ROWS];
        for (unsigned row = 0; row < NOODLE_EPSS_ROWS; ++row) table[row] = regs[0x100 / 4 + row];
        unsigned khz, top = noodle_epss_top(table, &khz);
        if (!top) {
            printf("instantnoodle cpufreq: domain %u has no increasing LUT\n", domain);
            continue;
        }
        unsigned before = regs[0x320 / 4];
        regs[0x320 / 4] = top;
        __asm__ volatile("dsb sy" ::: "memory");
        delay_for_interval(20, NSEC_PER_MSEC);
        printf("instantnoodle cpufreq: domain %u row %u -> %u (%u MHz), readback %u\n",
               domain, before, top, khz / 1000, regs[0x320 / 4]);
    }
}
