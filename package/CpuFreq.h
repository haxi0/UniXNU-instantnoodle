// SPDX-License-Identifier: APSL-2.0
// EPSS table selection adapted from OOSEX GANG HackiOS redwood_cpufreq.c.
#ifndef INSTANTNOODLE_CPUFREQ_H
#define INSTANTNOODLE_CPUFREQ_H
#include <stdint.h>

enum { NOODLE_EPSS_ROWS = 40 };
static inline unsigned noodle_epss_top(const uint32_t table[NOODLE_EPSS_ROWS], unsigned *khz)
{
    unsigned top = 0;
    *khz = 0;
    for (unsigned i = 0; i < NOODLE_EPSS_ROWS; ++i) {
        unsigned value = table[i] >> 30 ? (table[i] & 255) * 19200 : 300000;
        if (i && value <= *khz) break;
        *khz = value;
        top = i;
    }
    return top;
}
#endif
