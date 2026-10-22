// SPDX-License-Identifier: APSL-2.0
#include <assert.h>
#include <stdio.h>
#include "S6SY761.h"
#include "TouchPower.h"
#include "CpuFreq.h"
#include "Buttons.h"

static void event(uint8_t out[8], unsigned id, unsigned state, unsigned x, unsigned y)
{
    memset(out, 0, 8);
    out[0] = (uint8_t)(state << 6 | id << 2);
    out[1] = (uint8_t)(x >> 4);
    out[2] = (uint8_t)(y >> 4);
    out[3] = (uint8_t)((x & 15) << 4 | (y & 15));
}

int main(void)
{
    struct noodle_touch_frame frame = {0};
    uint8_t e[8];
    event(e, 1, 1, 0, 0);
    assert(noodle_touch_event(&frame, e) && frame.down[0] && !frame.x[0] && !frame.y[0]);
    event(e, 1, 2, 1079, 2399);
    assert(noodle_touch_event(&frame, e) && frame.x[0] == NOODLE_TOUCH_X && frame.y[0] == NOODLE_TOUCH_Y);
    event(e, 10, 2, 4095, 4095);
    assert(noodle_touch_event(&frame, e) && frame.down[9] && frame.x[9] == NOODLE_TOUCH_X && frame.y[9] == NOODLE_TOUCH_Y);
    event(e, 1, 3, 0, 0);
    assert(noodle_touch_event(&frame, e) && !frame.down[0] && frame.down[9]);
    assert(!noodle_touch_event(&frame, e));
    event(e, 0, 1, 0, 0); assert(!noodle_touch_event(&frame, e));
    event(e, 11, 1, 0, 0); assert(!noodle_touch_event(&frame, e));
    event(e, 1, 0, 0, 0); assert(!noodle_touch_event(&frame, e));
    event(e, 1, 1, 0, 0); e[0] |= 1; assert(!noodle_touch_event(&frame, e));
    unsigned previous = 0;
    for (unsigned x = 0; x < 1080; ++x) {
        unsigned scaled = noodle_touch_scale(x, 1080, NOODLE_TOUCH_X);
        assert(scaled >= previous && scaled <= NOODLE_TOUCH_X); previous = scaled;
    }
    uint32_t table[NOODLE_EPSS_ROWS] = {0};
    unsigned khz;
    assert(noodle_epss_top(table, &khz) == 0 && khz == 300000);
    table[0] = (1u << 30) | 16; table[1] = (1u << 30) | 94; table[2] = table[1];
    assert(noodle_epss_top(table, &khz) == 1 && khz == 1804800);
    table[2] = (1u << 30) | 90; assert(noodle_epss_top(table, &khz) == 1);
    for (unsigned i = 0; i < NOODLE_EPSS_ROWS; ++i) table[i] = (1u << 30) | (i + 16);
    assert(noodle_epss_top(table, &khz) == NOODLE_EPSS_ROWS - 1 && khz == 55 * 19200);
    for (unsigned mask = 0; mask < 8; ++mask)
        assert(noodle_buttons_mask(mask & 1, mask & 2 ? 0 : 1, mask & 4 ? 0 : 1) == mask << 1);
    uint8_t db[1024] = {0};
    struct noodle_db_header header = {.version = 1, .magic = {0xdb, 0x30, 3, 0x0c}};
    header.slave[0].id = 4; header.slave[0].count = 1;
    struct noodle_db_entry entry = {.id = "ldoc1", .address = 0x42e00};
    memcpy(db, &header, sizeof(header)); memcpy(db + sizeof(header), &entry, sizeof(entry));
    assert(noodle_regulator_address(db, sizeof(db), "ldoc1") == 0x42e00);
    assert(!noodle_regulator_address(db, sizeof(db), "ldoa13"));
    assert(!noodle_regulator_address(db, sizeof(header) - 1, "ldoc1"));
    assert(!noodle_regulator_address(db, sizeof(header) + sizeof(entry) - 1, "ldoc1"));
    assert(!noodle_regulator_address(db, sizeof(db), "ldoc1-too-long"));
    header.slave[0].header_offset = 0xffff; memcpy(db, &header, sizeof(header));
    assert(!noodle_regulator_address(db, sizeof(db), "ldoc1"));
    header.slave[0].header_offset = 0; header.slave[0].count = 513; memcpy(db, &header, sizeof(header));
    assert(!noodle_regulator_address(db, sizeof(db), "ldoc1"));
    header.slave[0].count = 1; header.magic[0] = 0; memcpy(db, &header, sizeof(header));
    assert(!noodle_regulator_address(db, sizeof(db), "ldoc1"));
    puts("PASS: touch bounds/multitouch/releases, EPSS LUTs, all key masks and malformed cmd-db refusal");
}
