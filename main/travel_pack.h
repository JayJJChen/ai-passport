#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRAVEL_PACK_CAPACITY 0x100000u
#define TRAVEL_PACK_HEADER_SIZE 32u
#define TRAVEL_PACK_ENTRY_SIZE 40u
#define TRAVEL_PACK_MAX_FILES 12u
typedef struct { const uint8_t *data; size_t size; unsigned count; } travel_pack_t;
typedef struct { const uint8_t *data; size_t size; } travel_pack_file_t;
uint32_t travel_pack_crc32(const uint8_t *data, size_t size);
bool travel_pack_open(travel_pack_t *pack, const void *data, size_t capacity);
bool travel_pack_find(const travel_pack_t *pack, const char *name, travel_pack_file_t *file);
