// SPDX-License-Identifier: BSD-2-Clause-Patent
// NativeHidInput report 2: Home bit 0, Lock bit 1, VolumeIncrement bit 2, VolumeDecrement bit 3.
#ifndef INSTANTNOODLE_BUTTONS_H
#define INSTANTNOODLE_BUTTONS_H
#include <stdint.h>
static inline uint32_t noodle_buttons_mask(uint8_t pon, uint8_t up, uint8_t down)
{
    return (pon & 1 ? 2u : 0) | (up & 1 ? 0 : 4u) | (down & 1 ? 0 : 8u);
}
#endif
