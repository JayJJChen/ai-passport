#include "travel_progress.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static travel_progress_snapshot_t snapshot(const travel_progress_t *s) {
    travel_progress_snapshot_t v = {0};
    strcpy(v.content_version, s->content_version);
    v.count = s->count; v.progress_revision = s->progress_revision;
    for (unsigned i = 0; i < s->count; ++i) v.activities[i] = s->entries[i].server;
    return v;
}
int main(void) {
    travel_progress_t s, reboot;
    travel_progress_init(&s, 0x12345678, 0x90abcdef);
    assert(travel_progress_add(&s, "d08_quokka"));
    assert(travel_progress_add(&s, "d08_beach"));
    assert(!travel_progress_add(&s, "d08_quokka"));
    assert(!travel_progress_add(&s, "bad\"id"));
    assert(travel_progress_mark(&s, "d08_quokka", TRAVEL_ACTIVITY_COMPLETED));
    travel_progress_operation_t op, repeated;
    assert(travel_progress_next(&s, &op));
    assert(!strcmp(op.operation_id, "1234567890abcdef-00000001"));
    assert(travel_progress_mark(&s, "d08_quokka", TRAVEL_ACTIVITY_COMPLETED));
    assert(travel_progress_next(&s, &repeated));
    assert(!strcmp(op.operation_id, repeated.operation_id)); /* coalescing is idempotent */
    travel_progress_snapshot_t v = snapshot(&s);
    assert(travel_progress_apply(&s, &v)); /* old server pending cannot erase local mark */
    assert(travel_progress_status(&s, "d08_quokka") == TRAVEL_ACTIVITY_COMPLETED);
    travel_progress_init(&reboot, 0, 0);
    assert(travel_progress_add(&reboot, "d08_quokka"));
    assert(travel_progress_add(&reboot, "d08_beach"));
    assert(travel_progress_import(&reboot, &s, sizeof(s)));
    assert(travel_progress_next(&reboot, &repeated));
    assert(!strcmp(op.operation_id, repeated.operation_id)); /* restart replays identical op */
    v.progress_revision = 1; v.activities[0].status = TRAVEL_ACTIVITY_COMPLETED; v.activities[0].revision = 1;
    assert(travel_progress_ack(&reboot, &op, &v, false));
    assert(!travel_progress_next(&reboot, &repeated));
    assert(travel_progress_ack(&reboot, &op, &v, false)); /* duplicate ack is safe */
    v.progress_revision = 0;
    assert(!travel_progress_apply(&reboot, &v));
    assert(reboot.entries[0].server.revision == 1);
    v = snapshot(&reboot);
    assert(travel_progress_mark(&reboot, "d08_beach", TRAVEL_ACTIVITY_COMPLETED));
    assert(travel_progress_next(&reboot, &op));
    v.progress_revision = 2; v.activities[1].revision = 2; v.activities[1].status = TRAVEL_ACTIVITY_SKIPPED;
    assert(travel_progress_ack(&reboot, &op, &v, true));
    assert(reboot.entries[1].conflict && !reboot.entries[1].queued);
    assert(travel_progress_status(&reboot, "d08_beach") == TRAVEL_ACTIVITY_SKIPPED);
    assert(!travel_progress_mark(&reboot, "d08_beach", TRAVEL_ACTIVITY_COMPLETED));
    assert(travel_progress_apply(&reboot, &v) && reboot.entries[1].conflict);
    v.progress_revision = 3; v.activities[1].revision = 3;
    assert(travel_progress_apply(&reboot, &v) && !reboot.entries[1].conflict); /* explicit unchanged voice confirmation */
    assert(travel_progress_status(&reboot, "d08_beach") == TRAVEL_ACTIVITY_SKIPPED);
    v.progress_revision = 4; v.activities[1].revision = 4; v.activities[1].status = TRAVEL_ACTIVITY_COMPLETED;
    assert(travel_progress_apply(&reboot, &v)); /* later explicit correction */
    assert(travel_progress_mark(&reboot, "d08_quokka", TRAVEL_ACTIVITY_PENDING));
    assert(travel_progress_next(&reboot, &op));
    strcpy(v.content_version, "wa-child-v2");
    assert(!travel_progress_apply(&reboot, &v));
    assert(reboot.writes_paused && reboot.entries[0].queued);
    assert(!travel_progress_next(&reboot, &repeated));
    assert(!travel_progress_mark(&reboot, "d08_beach", TRAVEL_ACTIVITY_PENDING));
    strcpy(v.content_version, TRAVEL_CONTENT_VERSION);
    assert(travel_progress_apply(&reboot, &v));
    assert(travel_progress_next(&reboot, &repeated));
    assert(!strcmp(op.operation_id, repeated.operation_id));
    v.activities[1] = v.activities[0];
    assert(!travel_progress_apply(&reboot, &v)); /* duplicates cannot replace catalogue */
    strcpy(s.content_version, "wa-child-v2");
    assert(!travel_progress_import(&reboot, &s, sizeof(s)));
    travel_progress_init(&s, 1, 2);
    assert(travel_progress_add(&s, "one"));
    assert(travel_progress_mark(&s, "one", TRAVEL_ACTIVITY_COMPLETED));
    assert(travel_progress_next(&s, &op));
    assert(travel_progress_mark(&s, "one", TRAVEL_ACTIVITY_PENDING));
    assert(travel_progress_next(&s, &repeated));
    assert(strcmp(op.operation_id, repeated.operation_id));
    v = snapshot(&s); v.progress_revision = 1;
    v.activities[0].revision = 1; v.activities[0].status = TRAVEL_ACTIVITY_COMPLETED;
    assert(travel_progress_ack(&s, &op, &v, false));
    assert(s.entries[0].queued && travel_progress_status(&s, "one") == TRAVEL_ACTIVITY_PENDING);
    assert(travel_progress_next(&s, &op) && !strcmp(op.operation_id, repeated.operation_id));
    /* Lost receipt, later voice correction, then replay: duplicate must reveal latest authority. */
    travel_progress_init(&s, 3, 4);
    assert(travel_progress_add(&s, "one"));
    assert(travel_progress_mark(&s, "one", TRAVEL_ACTIVITY_COMPLETED));
    assert(travel_progress_next(&s, &op));
    v = snapshot(&s); v.progress_revision = 2;
    v.activities[0].revision = 2; v.activities[0].status = TRAVEL_ACTIVITY_PENDING;
    assert(travel_progress_ack(&s, &op, &v, false));
    assert(!s.entries[0].queued && travel_progress_status(&s, "one") == TRAVEL_ACTIVITY_PENDING);
    puts("Activity progress offline, restart, idempotency, snapshots and conflicts: PASS");
}
