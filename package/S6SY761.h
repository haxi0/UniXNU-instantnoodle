// SPDX-License-Identifier: APSL-2.0
// Event layout from HackiOS instantnoodle_touch.c (ce8ef8f), ios-sweet and ios-guacamole.
// Pure decoding for the OnePlus 8's 1080 x 2400 panel into NativeHidInput's logical axes.
#ifndef INSTANTNOODLE_S6SY761_H
#define INSTANTNOODLE_S6SY761_H
#include <stdint.h>
#include <stdbool.h>

enum { NOODLE_FINGERS = 10, NOODLE_TOUCH_X = 30480, NOODLE_TOUCH_Y = 20320 };
struct noodle_touch_frame {
    uint16_t x[NOODLE_FINGERS], y[NOODLE_FINGERS];
    bool down[NOODLE_FINGERS];
};

static inline uint16_t noodle_touch_scale(unsigned value, unsigned pixels, unsigned maximum)
{
    if (value >= pixels) value = pixels - 1;
    return (uint16_t)(value * maximum / (pixels - 1));
}

static inline bool noodle_touch_event(struct noodle_touch_frame *frame, const uint8_t event[8])
{
    unsigned id = (event[0] >> 2) & 15, state = event[0] >> 6;
    if (!event[0] || (event[0] & 3) || !id || id > NOODLE_FINGERS) return false;
    --id;
    if (state == 1 || state == 2) {
        frame->x[id] = noodle_touch_scale((unsigned)event[1] << 4 | event[3] >> 4, 1080, NOODLE_TOUCH_X);
        frame->y[id] = noodle_touch_scale((unsigned)event[2] << 4 | (event[3] & 15), 2400, NOODLE_TOUCH_Y);
        frame->down[id] = true;
        return true;
    }
    if (state == 3 && frame->down[id]) {
        frame->down[id] = false;
        return true;
    }
    return false;
}
#endif
