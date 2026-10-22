// SPDX-License-Identifier: BSD-2-Clause-Patent
// UniXNU-qemu / UniXNU-tb128fu TouchState.cpp, expressed as C for this board's archive.
#include <stdint.h>

volatile uint32_t native_touch_seq, native_touch_count, native_touch_hid_reports;
volatile int native_touch_xs[12], native_touch_ys[12], native_touch_ids[12];
static void (*volatile touch_sink)(void *);
static void *volatile touch_context;

void native_touch_set_sink(void (*sink)(void *), void *context);
void native_touch_published(void);

void native_touch_set_sink(void (*sink)(void *), void *context)
{
    touch_sink = 0;
    __asm__ volatile("dmb ish" ::: "memory");
    touch_context = context;
    __asm__ volatile("dmb ish" ::: "memory");
    touch_sink = sink;
}

void native_touch_published(void)
{
    void (*sink)(void *) = touch_sink;
    __asm__ volatile("dmb ish" ::: "memory");
    if (sink) sink(touch_context);
}
