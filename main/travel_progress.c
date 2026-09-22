#include "travel_progress.h"
#include <stdio.h>
#include <string.h>

static bool valid_id(const char *id) {
    if (!id || !*id || strlen(id) >= TRAVEL_ACTIVITY_ID_SIZE) return false;
    for (const char *p = id; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
    return true;
}
void travel_progress_init(travel_progress_t *s, uint32_t hi, uint32_t lo) {
    memset(s, 0, sizeof(*s));
    s->cache_version = TRAVEL_PROGRESS_CACHE_VERSION;
    s->nonce_hi = hi; s->nonce_lo = lo; s->next_counter = 1;
    strcpy(s->content_version, TRAVEL_CONTENT_VERSION);
}
int travel_progress_find(const travel_progress_t *s, const char *id) {
    if (!s || !id) return -1;
    for (unsigned i = 0; i < s->count; ++i) if (!strcmp(s->entries[i].server.activity_id, id)) return (int)i;
    return -1;
}
bool travel_progress_add(travel_progress_t *s, const char *id) {
    if (!s || s->count >= TRAVEL_ACTIVITY_MAX || !valid_id(id) || travel_progress_find(s, id) >= 0) return false;
    strcpy(s->entries[s->count++].server.activity_id, id);
    return true;
}
bool travel_progress_import(travel_progress_t *s, const void *data, size_t size) {
    if (!s || !data || size != sizeof(*s)) return false;
    /* NVS validates blob CRC; validate all fields before accepting persisted state. */
    const travel_progress_t *v = data;
    if (v->cache_version != TRAVEL_PROGRESS_CACHE_VERSION || v->count != s->count ||
        !memchr(v->content_version, 0, sizeof(v->content_version)) ||
        strcmp(v->content_version, s->content_version) || !v->next_counter) return false;
    for (unsigned i = 0; i < s->count; ++i) {
        const travel_progress_entry_t *e = &v->entries[i];
        if (!memchr(e->server.activity_id, 0, sizeof(e->server.activity_id)) ||
            strcmp(e->server.activity_id, s->entries[i].server.activity_id) ||
            e->server.status > TRAVEL_ACTIVITY_SKIPPED || e->desired_status > TRAVEL_ACTIVITY_SKIPPED ||
            (e->queued && (!e->operation_counter || e->operation_counter >= v->next_counter))) return false;
    }
    memcpy(s, data, size); return true;
}
bool travel_progress_mark(travel_progress_t *s, const char *id, travel_activity_status_t status) {
    int i = travel_progress_find(s, id);
    if (i < 0 || status > TRAVEL_ACTIVITY_SKIPPED || s->writes_paused) return false;
    travel_progress_entry_t *e = &s->entries[i];
    if (e->conflict) return false; /* resolve conflicting intentions in the conversation */
    if ((e->queued && e->desired_status == status) || (!e->queued && e->server.status == status)) return true;
    if (s->next_counter == UINT32_MAX) return false;
    e->operation_counter = s->next_counter++;
    e->expected_revision = e->server.revision;
    e->desired_status = (uint8_t)status; e->queued = true;
    return true;
}
bool travel_progress_next(const travel_progress_t *s, travel_progress_operation_t *op) {
    if (!s || !op || s->writes_paused) return false;
    for (unsigned i = 0; i < s->count; ++i) {
        const travel_progress_entry_t *e = &s->entries[i];
        if (!e->queued) continue;
        memset(op, 0, sizeof(*op));
        snprintf(op->operation_id, sizeof(op->operation_id), "%08lx%08lx-%08lx",
                 (unsigned long)s->nonce_hi, (unsigned long)s->nonce_lo, (unsigned long)e->operation_counter);
        strcpy(op->activity_id, e->server.activity_id); strcpy(op->content_version, s->content_version);
        op->status = e->desired_status; op->expected_activity_revision = e->expected_revision;
        return true;
    }
    return false;
}
bool travel_progress_apply(travel_progress_t *s, const travel_progress_snapshot_t *v) {
    if (!s || !v || !memchr(v->content_version, 0, sizeof(v->content_version))) return false;
    if (strcmp(s->content_version, v->content_version)) { s->writes_paused = true; return false; }
    if (v->count != s->count || v->count > TRAVEL_ACTIVITY_MAX || v->progress_revision < s->progress_revision) return false;
    uint64_t seen = 0;
    for (unsigned i = 0; i < v->count; ++i) {
        const travel_progress_item_t *item = &v->activities[i];
        if (!memchr(item->activity_id, 0, sizeof(item->activity_id))) return false;
        int idx = travel_progress_find(s, item->activity_id);
        if (idx < 0 || (seen & (UINT64_C(1) << idx)) || item->status > TRAVEL_ACTIVITY_SKIPPED ||
            item->revision < s->entries[idx].server.revision || item->revision > v->progress_revision) return false;
        if (item->revision == s->entries[idx].server.revision && item->status != s->entries[idx].server.status) return false;
        seen |= UINT64_C(1) << idx;
    }
    for (unsigned i = 0; i < v->count; ++i) {
        int idx = travel_progress_find(s, v->activities[i].activity_id);
        travel_progress_entry_t *e = &s->entries[idx];
        if (v->activities[i].revision > e->server.revision) e->conflict = false;
        e->server = v->activities[i]; /* queued overlay intentionally survives a refresh */
    }
    s->progress_revision = v->progress_revision; s->writes_paused = false;
    return true;
}
bool travel_progress_ack(travel_progress_t *s, const travel_progress_operation_t *op,
                         const travel_progress_snapshot_t *v, bool conflict) {
    if (!travel_progress_apply(s, v)) return false;
    int idx = travel_progress_find(s, op->activity_id);
    if (idx < 0) return false;
    travel_progress_entry_t *e = &s->entries[idx];
    char id[65];
    snprintf(id, sizeof(id), "%08lx%08lx-%08lx", (unsigned long)s->nonce_hi,
             (unsigned long)s->nonce_lo, (unsigned long)e->operation_counter);
    if (e->queued && !strcmp(id, op->operation_id)) {
        /* A duplicate receipt can legitimately contain a later voice correction.
         * Receipt proves this operation was accepted, not that its old status is still current. */
        e->queued = false; e->conflict = conflict;
    }
    return true;
}
travel_activity_status_t travel_progress_status(const travel_progress_t *s, const char *id) {
    int idx = travel_progress_find(s, id);
    if (idx < 0) return TRAVEL_ACTIVITY_PENDING;
    const travel_progress_entry_t *e = &s->entries[idx];
    return (travel_activity_status_t)(e->queued && !e->conflict ? e->desired_status : e->server.status);
}
const char *travel_progress_status_name(unsigned status) {
    static const char *names[] = {"pending", "completed", "skipped"};
    return status <= TRAVEL_ACTIVITY_SKIPPED ? names[status] : NULL;
}
