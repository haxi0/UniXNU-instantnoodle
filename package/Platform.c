// SPDX-License-Identifier: APSL-2.0
// PM8150 key mapping and SPMI v5 transactions adapted from HackiOS redwood_buttons.c
// (haxi0 instantnoodle branch), with UniXNU NativeHidInput mask publication.
// Reboot reason and PSCI sequence: ios-instantnoodle patch 0104 (tested on this phone).
// One thread owns these observer channels; no other board component uses SPMI.
#include <kern/thread.h>
#include <kern/clock.h>
#include <machine/machine_routines.h>
#include <libkern/libkern.h>
#include <IOKit/IOLib.h>
#include <pexpert/pexpert.h>
#include "Buttons.h"

extern kern_return_t thread_terminate(thread_t);
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void instantnoodle_platform_init(void);
void instantnoodle_cpufreq_init(void);
void native_touch_probe(void);
void native_buttons_set_sink(void (*)(void *, uint32_t), void *);

static void (*volatile buttons_sink)(void *, uint32_t);
static void *volatile buttons_context;
static volatile uint32_t *core, *cnfg, *observer;
static unsigned channels[3];

static uint32_t rd(volatile uint32_t *base, unsigned offset) { return base[offset / 4]; }
static void wr(volatile uint32_t *base, unsigned offset, uint32_t value)
{
    base[offset / 4] = value;
    __asm__ volatile("dsb sy" ::: "memory");
}

void native_buttons_set_sink(void (*sink)(void *, uint32_t), void *context)
{
    buttons_sink = NULL;
    __asm__ volatile("dmb ish" ::: "memory");
    buttons_context = context;
    __asm__ volatile("dmb ish" ::: "memory");
    buttons_sink = sink;
}

static void publish_buttons(uint32_t mask)
{
    void (*sink)(void *, uint32_t) = buttons_sink;
    __asm__ volatile("dmb ish" ::: "memory");
    if (sink) sink(buttons_context, mask);
}

static int wait_channel(volatile uint32_t *base)
{
    for (unsigned i = 0; i < 2000; ++i) {
        uint32_t status = rd(base, 8);
        if (status & 1) return status & 14 ? -1 : 0;
        IODelay(1);
    }
    return -1;
}

static int read_byte(unsigned channel, uint8_t reg, uint8_t *value)
{
    volatile uint32_t *base = observer + (0x80 * channel) / 4;
    wr(base, 0, (1u << 27) | (uint32_t)reg << 4);
    if (wait_channel(base)) return -1;
    *value = (uint8_t)rd(base, 0x18);
    return 0;
}

static bool open_spmi(void)
{
    static const uint16_t ppids[3] = {0x008, 0x0c5, 0x0c6};
    core = (volatile uint32_t *)ml_io_map(0x0c440000, 0x2000);
    cnfg = (volatile uint32_t *)ml_io_map(0x0c40a000, 0x1000);
    observer = (volatile uint32_t *)ml_io_map(0x0e600000, 0x10000);
    if (!core || !cnfg || !observer) return false;
    unsigned version = rd(core, 0), count = rd(core, 4) & 0x7ff;
    if (version < 0x50000000 || version >= 0x70000000 || !count || count > 512) return false;
    for (unsigned key = 0; key < 3; ++key) {
        unsigned found = 512;
        for (unsigned i = 0; i < count; ++i) {
            unsigned entry = rd(core, 0x900 + 4 * i);
            if (!entry || ((entry >> 8) & 0xfff) != ppids[key]) continue;
            if (found == 512 || (rd(cnfg, 0x700 + 4 * i) & 7) == 0) found = i;
        }
        if (found == 512) return false;
        channels[key] = found;
        uint8_t type, subtype;
        if (read_byte(found, 4, &type) || read_byte(found, 5, &subtype)) return false;
        if (!key) {
            if (type != 1 || subtype != 4) return false;
        } else {
            uint8_t mode, pull, enable;
            if (type != 0x10 || subtype < 0x10 || subtype > 0x13 ||
                read_byte(found, 0x40, &mode) || read_byte(found, 0x42, &pull) || read_byte(found, 0x46, &enable) ||
                (mode & 3) != 0 || (pull & 7) >= 4 || !(enable & 0x80)) return false;
        }
    }
    printf("instantnoodle: SPMI v%08x, PM8150 channels %u/%u/%u\n", version, channels[0], channels[1], channels[2]);
    return true;
}

static void reset_bootloader(void)
{
    volatile uint32_t *reason = (volatile uint32_t *)ml_io_map(0x146bf65c, 4);
    if (!reason) {
        printf("instantnoodle: reset refused, IMEM mapping failed\n");
        return;
    }
    *reason = 0x77665500;
    __asm__ volatile("dsb sy" ::: "memory");
    if (*reason != 0x77665500) {
        printf("instantnoodle: reset refused, IMEM readback failed\n");
        return;
    }
    printf("instantnoodle: rebooting to bootloader\n");
    register uint64_t x0 __asm__("x0") = 0x84000009, x1 __asm__("x1") = 0, x2 __asm__("x2") = 0, x3 __asm__("x3") = 0;
    __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) :: "memory", "x4", "x5", "x6", "x7", "x8", "x9",
                     "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17");
    printf("instantnoodle: PSCI reset returned %lld\n", (long long)x0);
}

static void platform_thread(void *arg __unused, wait_result_t result __unused)
{
    unsigned buttons = 0, reset = 0;
    PE_parse_boot_argn("instantnoodle-buttons", &buttons, sizeof(buttons));
    PE_parse_boot_argn("instantnoodle-autoreset", &reset, sizeof(reset));
    instantnoodle_cpufreq_init();
    native_touch_probe();
    if (reset && (reset < 30 || reset > 3600)) {
        printf("instantnoodle: autoreset must be 30..3600 seconds; disabled\n");
        reset = 0;
    }
    if (buttons == 1 && !open_spmi()) {
        printf("instantnoodle: SPMI/PON/GPIO identity check failed; keys disabled\n");
        buttons = 0;
    }
    uint64_t start = mach_absolute_time();
    uint32_t last = 0, candidate = 0;
    unsigned stable = 0, errors = 0;
    bool armed = false;
    for (;;) {
        if (buttons == 1) {
            uint8_t pon, up, down;
            if (!read_byte(channels[0], 0x10, &pon) && !read_byte(channels[1], 0x10, &up) && !read_byte(channels[2], 0x10, &down)) {
                uint32_t mask = noodle_buttons_mask(pon, up, down);
                if (mask != candidate) {candidate = mask; stable = 0;}
                if (stable < 2) ++stable;
                if (stable == 2) {
                    if (!mask) armed = true; // Bootloader key holds must be released before reporting any press.
                    if (armed && mask != last) {
                        printf("instantnoodle buttons: mask %x\n", mask);
                        publish_buttons(mask);
                        last = mask;
                    }
                }
                errors = 0;
            } else if (++errors == 10) {
                publish_buttons(0); last = candidate = stable = 0; armed = false;
                printf("instantnoodle buttons: bus errors, released keys\n");
            }
        }
        uint64_t ns;
        absolutetime_to_nanoseconds(mach_absolute_time() - start, &ns);
        if (reset && ns / NSEC_PER_SEC >= reset) {reset_bootloader(); reset = 0;}
        if (buttons != 1 && !reset) break;
        IOSleep(10);
    }
    thread_terminate(current_thread());
}

void instantnoodle_platform_init(void)
{
    static bool started;
    thread_t thread;
    if (started) return;
    if (kernel_thread_start(platform_thread, NULL, &thread) == KERN_SUCCESS) {
        started = true;
        thread_deallocate(thread);
    } else printf("instantnoodle: platform thread creation failed\n");
}
