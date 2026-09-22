#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define TRAVEL_TRIP_ID "western-australia-2026"
#define TRAVEL_CONTENT_VERSION "wa-child-v1"
#define TRAVEL_ACTIVITY_MAX 33u
#define TRAVEL_ACTIVITY_ID_SIZE 49u
#define TRAVEL_CONTENT_VERSION_SIZE 32u
#define TRAVEL_PROGRESS_CACHE_VERSION 1u

typedef enum { TRAVEL_ACTIVITY_PENDING, TRAVEL_ACTIVITY_COMPLETED, TRAVEL_ACTIVITY_SKIPPED } travel_activity_status_t;
typedef struct {
    char activity_id[TRAVEL_ACTIVITY_ID_SIZE];
    uint32_t revision;
    uint8_t status;
} travel_progress_item_t;
typedef struct {
    char content_version[TRAVEL_CONTENT_VERSION_SIZE];
    uint32_t progress_revision;
    uint8_t count;
    travel_progress_item_t activities[TRAVEL_ACTIVITY_MAX];
} travel_progress_snapshot_t;
typedef struct {
    char operation_id[65], activity_id[TRAVEL_ACTIVITY_ID_SIZE];
    char content_version[TRAVEL_CONTENT_VERSION_SIZE];
    uint32_t expected_activity_revision;
    uint8_t status;
} travel_progress_operation_t;
typedef struct {
    travel_progress_item_t server;
    uint32_t operation_counter, expected_revision;
    uint8_t desired_status;
    bool queued, conflict;
} travel_progress_entry_t;
typedef struct {
    uint32_t cache_version, nonce_hi, nonce_lo, next_counter, progress_revision;
    char content_version[TRAVEL_CONTENT_VERSION_SIZE];
    uint8_t count;
    bool writes_paused;
    travel_progress_entry_t entries[TRAVEL_ACTIVITY_MAX];
} travel_progress_t;
void travel_progress_init(travel_progress_t *state, uint32_t nonce_hi, uint32_t nonce_lo);
bool travel_progress_add(travel_progress_t *state, const char *activity_id);
bool travel_progress_import(travel_progress_t *state, const void *data, size_t size);
int travel_progress_find(const travel_progress_t *state, const char *activity_id);
bool travel_progress_mark(travel_progress_t *state, const char *activity_id, travel_activity_status_t status);
bool travel_progress_next(const travel_progress_t *state, travel_progress_operation_t *operation);
/* Full snapshots must exactly match the catalogue. Old snapshots never roll state back. */
bool travel_progress_apply(travel_progress_t *state, const travel_progress_snapshot_t *snapshot);
/* Acknowledge only the matching operation; conflicts keep the authoritative result visible. */
bool travel_progress_ack(travel_progress_t *state, const travel_progress_operation_t *operation,
                         const travel_progress_snapshot_t *snapshot, bool conflict);
travel_activity_status_t travel_progress_status(const travel_progress_t *state, const char *activity_id);
const char *travel_progress_status_name(unsigned status);
#ifdef __cplusplus
}
#endif
