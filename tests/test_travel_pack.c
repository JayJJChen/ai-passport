#include "travel_pack.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}
static void crc(uint8_t *p, size_t n) { put32(p + 16, travel_pack_crc32(p + 32, n - 32)); }
int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *fp = fopen(argv[1], "rb"); assert(fp);
    fseek(fp, 0, SEEK_END); size_t n = (size_t)ftell(fp); rewind(fp);
    uint8_t *data = malloc(n), *copy = malloc(n); assert(data && copy);
    assert(fread(data, 1, n, fp) == n); fclose(fp);
    travel_pack_t pack; travel_pack_file_t file;
    assert(travel_pack_open(&pack, data, n));
    assert(travel_pack_find(&pack, "bg0.rgb565", &file) && file.size == 240 * 320 * 2);
    assert(travel_pack_find(&pack, "manifest.json", &file) && file.data[file.size - 1] == 0);
    assert(!travel_pack_find(&pack, "nonexistent", &file));
    for (size_t length = 0; length < n; length += 997) assert(!travel_pack_open(&pack, data, length));
    memcpy(copy, data, n); copy[n - 5] ^= 1; assert(!travel_pack_open(&pack, copy, n));
    memcpy(copy, data, n); put32(copy + 32 + 24, UINT32_MAX - 3); crc(copy, n); assert(!travel_pack_open(&pack, copy, n));
    memcpy(copy, data, n); put32(copy + 32 + 28, UINT32_MAX); crc(copy, n); assert(!travel_pack_open(&pack, copy, n));
    memcpy(copy, data, n); memcpy(copy + 72, copy + 32, 24); crc(copy, n); assert(!travel_pack_open(&pack, copy, n));
    memcpy(copy, data, n); memcpy(copy + 72 + 24, copy + 32 + 24, 4); crc(copy, n); assert(!travel_pack_open(&pack, copy, n));
    memcpy(copy, data, n); memset(copy + 32, 'x', 24); crc(copy, n); assert(!travel_pack_open(&pack, copy, n));
    free(copy); free(data);
    puts("Travel pack CRC/truncation/overlap/overflow tests: PASS");
}
