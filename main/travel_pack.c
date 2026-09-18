#include "travel_pack.h"
#include <string.h>

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
uint32_t travel_pack_crc32(const uint8_t *data, size_t size) {
    uint32_t crc = UINT32_MAX;
    while (size--) {
        crc ^= *data++;
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
    return ~crc;
}
bool travel_pack_open(travel_pack_t *pack, const void *data, size_t capacity) {
    memset(pack, 0, sizeof(*pack));
    if (!data || capacity < TRAVEL_PACK_HEADER_SIZE) return false;
    const uint8_t *p = data;
    if (memcmp(p, "KTRVPK01", 8) || u32(p + 8) != 1 || u32(p + 24) != TRAVEL_PACK_ENTRY_SIZE || u32(p + 28)) return false;
    uint32_t size = u32(p + 12), count = u32(p + 20);
    if (!count || count > TRAVEL_PACK_MAX_FILES || size > capacity || size > TRAVEL_PACK_CAPACITY ||
        size < TRAVEL_PACK_HEADER_SIZE + count * TRAVEL_PACK_ENTRY_SIZE) return false;
    if (travel_pack_crc32(p + TRAVEL_PACK_HEADER_SIZE, size - TRAVEL_PACK_HEADER_SIZE) != u32(p + 16)) return false;
    size_t start = TRAVEL_PACK_HEADER_SIZE + count * TRAVEL_PACK_ENTRY_SIZE;
    for (unsigned i = 0; i < count; ++i) {
        const uint8_t *e = p + TRAVEL_PACK_HEADER_SIZE + i * TRAVEL_PACK_ENTRY_SIZE;
        const uint8_t *end = memchr(e, 0, 24);
        if (!end || end == e || u32(e + 32) || u32(e + 36)) return false;
        for (const uint8_t *c = e; c < end; ++c) {
            if (!((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '.')) return false;
        }
        uint32_t offset = u32(e + 24), length = u32(e + 28);
        if (offset % 4 || offset < start || !length || offset > size || length > size - offset) return false;
        for (unsigned j = 0; j < i; ++j) {
            const uint8_t *other = p + TRAVEL_PACK_HEADER_SIZE + j * TRAVEL_PACK_ENTRY_SIZE;
            uint32_t o = u32(other + 24), n = u32(other + 28);
            if (!strcmp((const char *)e, (const char *)other) || (offset < o + n && o < offset + length)) return false;
        }
    }
    *pack = (travel_pack_t){ p, size, count };
    return true;
}
bool travel_pack_find(const travel_pack_t *pack, const char *name, travel_pack_file_t *file) {
    if (!pack->data || !name) return false;
    for (unsigned i = 0; i < pack->count; ++i) {
        const uint8_t *e = pack->data + TRAVEL_PACK_HEADER_SIZE + i * TRAVEL_PACK_ENTRY_SIZE;
        if (!strcmp((const char *)e, name)) { *file = (travel_pack_file_t){pack->data + u32(e + 24), u32(e + 28)}; return true; }
    }
    return false;
}
