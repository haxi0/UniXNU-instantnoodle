// SPDX-License-Identifier: APSL-2.0
// cmd-db lookup adapted from HackiOS instantnoodle_touch.c, with bounded firmware reads.
#ifndef INSTANTNOODLE_TOUCH_POWER_H
#define INSTANTNOODLE_TOUCH_POWER_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct noodle_db_slave { uint16_t id, header_offset, data_offset, count, version, reserved[3]; };
struct noodle_db_header {
    uint32_t version;
    uint8_t magic[4];
    struct noodle_db_slave slave[8];
    uint32_t checksum, reserved;
};
struct noodle_db_entry { char id[8]; uint32_t priority[2], address; uint16_t length, offset; };

static inline uint32_t noodle_regulator_address(const void *bytes, size_t size, const char *name)
{
    struct noodle_db_header header;
    char id[8] = {0};
    size_t length = strlen(name);
    if (!length || length > sizeof(id) || size < sizeof(header)) return 0;
    memcpy(id, name, length);
    memcpy(&header, bytes, sizeof(header));
    if (header.version != 1 || memcmp(header.magic, "\xdb\x30\x03\x0c", 4)) return 0;
    const uint8_t *data = (const uint8_t *)bytes + sizeof(header);
    size -= sizeof(header);
    for (unsigned s = 0; s < 8; ++s) {
        struct noodle_db_slave slave = header.slave[s];
        if (!slave.id || slave.count > 512 || slave.header_offset > size ||
            slave.count > (size - slave.header_offset) / sizeof(struct noodle_db_entry)) continue;
        for (unsigned i = 0; i < slave.count; ++i) {
            struct noodle_db_entry entry;
            memcpy(&entry, data + slave.header_offset + i * sizeof(entry), sizeof(entry));
            if (!memcmp(entry.id, id, sizeof(id))) return entry.address;
        }
    }
    return 0;
}
#endif
