/* Timers: once, every, every until; their resets; each goes off at its own moment; a script reads one at the moment of its step. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_game.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_timers.h"
#include "test_pack.h"

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = malloc(*size);
    if (data && fread(data, 1, *size, f) != *size) free(data), data = NULL;
    fclose(f);
    return data;
}

/* what went off, in order */
typedef struct {
    int      name[16], event[16];
    uint32_t at[16];
    int      count;
    bool     again; /* the bomb set again as it goes off */
} heard_t;

static void heard(const dgx_ge_timer_t *timer, uint32_t at_ms, void *ctx)
{
    heard_t *h = ctx;
    if (h->count < 16) h->name[h->count] = timer->name, h->event[h->count] = timer->event, h->at[h->count] = at_ms, ++h->count;
    if (h->again && timer->name == TEST_TIMER_BOMB) {
        h->again = false;
        dgx_ge_timers_t *t = (dgx_ge_timers_t *)dgx_ge_timers_seen;
        CHECK(dgx_ge_timers_start_named(t, TEST_TIMER_BOMB, DGX_GE_OWNER_ACTOR, 7, 0, 1, at_ms));
    }
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));

    /* names: read by the watch first, then described; as many as the game says */
    CHECK(TEST_TIMER_CLOCK == 0 && TEST_TIMER_ALARM == 1 && TEST_TIMER_BOMB == 2);
    size_t         n;
    dgx_ge_game_t  game;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_GAME, &n);
    CHECK(blob && dgx_ge_game_open(&game, blob, n));
    heard_t         h = {0};
    dgx_ge_timers_t t;
    CHECK(dgx_ge_timers_init(&t, dgx_ge_game_value(&game, TEST_GAME_TIMERS, 0), heard, &h) && t.capacity == 3);
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_TIMERS, &n);
    CHECK(!dgx_ge_timers_define(&t, data, 8) && t.def_count == 0);
    /* more than 255, the tools' most (a timer's name is a byte) */
    {
        static uint8_t many[8 + 256 * 12];
        const uint32_t magic = DGX_GE_TIMERS_MAGIC;
        memcpy(many, &magic, 4), many[4] = 1, many[6] = 0, many[7] = 1; /* 256 */
        CHECK(!dgx_ge_timers_define(&t, many, sizeof many) && t.def_count == 0);
        many[6] = 255, many[7] = 0;
        CHECK(dgx_ge_timers_define(&t, many, sizeof many) && t.def_count == 255);
    }
    CHECK(blob && dgx_ge_timers_define(&t, blob, n) && t.def_count == 4); /* clock, alarm, bomb; down of the button (test_cells) */
    dgx_ge_timers_seen = &t;

    /* the clock goes from the start of the game; the others are started */
    CHECK(dgx_ge_timers_start_game(&t, 1000) == 1 && t.count == 1);
    CHECK(dgx_ge_timers_elapsed(&t, TEST_TIMER_CLOCK, 1500) == 500 && dgx_ge_timers_elapsed(&t, TEST_TIMER_BOMB, 1500) == -1);
    CHECK(dgx_ge_timers_start_named(&t, TEST_TIMER_BOMB, DGX_GE_OWNER_ACTOR, 7, 0, 1, 1200));
    CHECK(dgx_ge_timers_start_named(&t, TEST_TIMER_ALARM, DGX_GE_OWNER_SCENE, 1, 0, 1, 1300));
    CHECK(!dgx_ge_timers_start_named(&t, TEST_TIMER_BOMB, DGX_GE_OWNER_ACTOR, 8, 0, 1, 1300)); /* a fourth: no room, told */
    CHECK(!dgx_ge_timers_start_named(&t, 9, DGX_GE_OWNER_GAME, 0, 0, 0, 1300) && t.count == 3);  /* no such name */

    /* on to 3400 at one call: each at its own moment, in order; the alarm 2 s / 500 ms = 4 times, then gone */
    dgx_ge_timers_play(&t, 3400);
    const int      names[] = {TEST_TIMER_ALARM, TEST_TIMER_CLOCK, TEST_TIMER_ALARM, TEST_TIMER_ALARM, TEST_TIMER_CLOCK, TEST_TIMER_ALARM};
    const uint32_t ats[]   = {1800, 2000, 2300, 2800, 3000, 3300};
    CHECK(h.count == 6);
    for (int i = 0; i < 6 && i < h.count; ++i) CHECK(h.name[i] == names[i] && h.at[i] == ats[i]);
    CHECK(h.event[0] == TEST_EVENT_RING && h.event[1] == 0xFF);
    CHECK(t.count == 2 && dgx_ge_timers_elapsed(&t, TEST_TIMER_ALARM, 3400) == -1);

    /* leaving the scene stops the alarm (started again), not the bomb (never) nor the clock; leaving the map, one of the map */
    CHECK(dgx_ge_timers_start_named(&t, TEST_TIMER_ALARM, DGX_GE_OWNER_SCENE, 1, 0, 1, 3400) && t.count == 3);
    dgx_ge_timers_leave(&t, 0, 2);
    CHECK(t.count == 3); /* another scene: nothing */
    dgx_ge_timers_leave(&t, 0, 1);
    CHECK(t.count == 2 && dgx_ge_timers_elapsed(&t, TEST_TIMER_ALARM, 3400) == -1 && dgx_ge_timers_elapsed(&t, TEST_TIMER_BOMB, 3400) == 2200);
    dgx_ge_timer_t room = {.name = TEST_TIMER_ALARM, .kind = DGX_GE_TIMER_EVERY, .reset = DGX_GE_RESET_MAP, .event = 0xFF, .map = 3, .period_ms = 100};
    CHECK(dgx_ge_timers_start(&t, &room, 3400) && t.count == 3);
    dgx_ge_timers_leave(&t, 2, -1);
    CHECK(t.count == 3);
    dgx_ge_timers_leave(&t, 3, -1);
    CHECK(t.count == 2);
    room.kind = DGX_GE_TIMER_EVERY, room.period_ms = 0;
    CHECK(!dgx_ge_timers_start(&t, &room, 3400)); /* every 0 ms: never */

    /* the bomb at 6200, its event; set again as it goes off (the game's doing inside the call): at 11200 */
    h.count = 0, h.again = true;
    dgx_ge_timers_play(&t, 6200);
    CHECK(h.count == 4 && h.at[0] == 4000 && h.at[1] == 5000 && h.at[2] == 6000);
    CHECK(h.name[3] == TEST_TIMER_BOMB && h.at[3] == 6200 && h.event[3] == TEST_EVENT_BOOM);
    CHECK(t.count == 2 && dgx_ge_timers_elapsed(&t, TEST_TIMER_BOMB, 6300) == 100);
    /* late to call: it still goes off at its moment */
    h.count = 0;
    dgx_ge_timers_play(&t, 20000);
    CHECK(h.count == 15 && h.name[5] == TEST_TIMER_BOMB && h.at[5] == 11200 && h.at[14] == 20000);

    /* held: its time stands, nothing goes off; let go, on from where it stood (a bomb set meanwhile goes off) */
    CHECK(dgx_ge_timers_start_named(&t, TEST_TIMER_BOMB, DGX_GE_OWNER_ACTOR, 7, 0, 1, 16200));
    CHECK(dgx_ge_timers_hold(&t, TEST_TIMER_CLOCK, DGX_GE_OWNER_GAME, 0, true, 20300));
    CHECK(dgx_ge_timers_elapsed(&t, TEST_TIMER_CLOCK, 20300) == 19300 && dgx_ge_timers_elapsed(&t, TEST_TIMER_CLOCK, 25000) == 19300);
    CHECK(dgx_ge_timers_hold(&t, TEST_TIMER_CLOCK, DGX_GE_OWNER_GAME, 0, true, 21000)); /* held again: still from 20300 */
    h.count = 0;
    dgx_ge_timers_play(&t, 25000);
    CHECK(h.count == 1 && h.name[0] == TEST_TIMER_BOMB && h.at[0] == 21200); /* the bomb goes on, the clock stands */
    CHECK(dgx_ge_timers_start_named(&t, TEST_TIMER_BOMB, DGX_GE_OWNER_ACTOR, 7, 0, 1, 25000));
    CHECK(dgx_ge_timers_hold(&t, TEST_TIMER_CLOCK, DGX_GE_OWNER_GAME, 0, false, 25000));
    CHECK(dgx_ge_timers_elapsed(&t, TEST_TIMER_CLOCK, 25000) == 19300);
    h.count = 0;
    dgx_ge_timers_play(&t, 25700);
    CHECK(h.count == 1 && h.name[0] == TEST_TIMER_CLOCK && h.at[0] == 25700); /* its 20th second, 4.7 s late */
    CHECK(!dgx_ge_timers_hold(&t, TEST_TIMER_ALARM, DGX_GE_OWNER_GAME, 0, true, 25700));

    /* as if it had gone so long (a saved game): no seconds made up for, the next when it would come */
    CHECK(dgx_ge_timers_set_elapsed(&t, TEST_TIMER_CLOCK, DGX_GE_OWNER_GAME, 0, 3595500, 26000));
    CHECK(dgx_ge_timers_elapsed(&t, TEST_TIMER_CLOCK, 26000) == 3595500);
    h.count = 0;
    dgx_ge_timers_play(&t, 26499);
    CHECK(h.count == 0);
    dgx_ge_timers_play(&t, 26500);
    CHECK(h.count == 1 && h.at[0] == 26500 && dgx_ge_timers_elapsed(&t, TEST_TIMER_CLOCK, 26500) == 3596000);
    CHECK(dgx_ge_timers_set_elapsed(&t, TEST_TIMER_BOMB, DGX_GE_OWNER_ACTOR, 7, 9000, 26500)); /* once, past its 5 s: at once */
    h.count = 0;
    dgx_ge_timers_play(&t, 26500);
    CHECK(h.count == 1 && h.name[0] == TEST_TIMER_BOMB && h.at[0] == 26500);
    CHECK(dgx_ge_timers_start_named(&t, TEST_TIMER_ALARM, DGX_GE_OWNER_SCENE, 1, 0, 1, 26500));
    CHECK(dgx_ge_timers_set_elapsed(&t, TEST_TIMER_ALARM, DGX_GE_OWNER_SCENE, 1, 1700, 27000) && dgx_ge_timers_elapsed(&t, TEST_TIMER_ALARM, 27000) == 1700);
    h.count = 0;
    dgx_ge_timers_play(&t, 28000);
    CHECK(h.count == 2 && h.name[0] == TEST_TIMER_ALARM && h.at[0] == 27300 && h.at[1] == 27500); /* its last, at 2 s; the clock's second */
    CHECK(dgx_ge_timers_start_named(&t, TEST_TIMER_ALARM, DGX_GE_OWNER_SCENE, 1, 0, 1, 28000));
    CHECK(dgx_ge_timers_set_elapsed(&t, TEST_TIMER_ALARM, DGX_GE_OWNER_SCENE, 1, 2000, 28000) && t.count == 1); /* past its limit: gone */

    /* stopped by name and owner */
    CHECK(!dgx_ge_timers_stop(&t, TEST_TIMER_CLOCK, DGX_GE_OWNER_ACTOR, 0));
    CHECK(dgx_ge_timers_stop(&t, TEST_TIMER_CLOCK, DGX_GE_OWNER_GAME, 0) && !dgx_ge_timers_stop(&t, TEST_TIMER_CLOCK, DGX_GE_OWNER_GAME, 0));
    CHECK(t.count == 0);

    /* a script reads a timer at the moment of its step: started at 2000 (called at 5000), the clock 1 s old; at 4100, over 3 s: lost */
    dgx_ge_anim_t anim;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_WATCH, &n);
    CHECK(blob && dgx_ge_anim_open(&anim, blob, n));
    CHECK(dgx_ge_timers_start_game(&t, 1000) == 1);
    dgx_ge_actor_t w;
    dgx_ge_actor_start(&w, &anim, TEST_WATCH_SCRIPT_LOOK, 0, 0, 2000);
    dgx_ge_actor_advance(&w, 5000, NULL, NULL);
    CHECK(w.params[TEST_WATCH_PARAM_LEFT] == 59 && w.params[TEST_WATCH_PARAM_LOST] == 0);
    dgx_ge_actor_start(&w, &anim, TEST_WATCH_SCRIPT_LOOK, 0, 0, 4100);
    dgx_ge_actor_advance(&w, 4100, NULL, NULL);
    CHECK(w.params[TEST_WATCH_PARAM_LEFT] == 57 && w.params[TEST_WATCH_PARAM_LOST] == 1);
    /* an event at its moment reads the alarm then, not at the call; none going: -1 */
    CHECK(dgx_ge_timers_start_named(&t, TEST_TIMER_ALARM, DGX_GE_OWNER_ACTOR, 0, 0, 0, 2000));
    dgx_ge_actor_start(&w, &anim, TEST_WATCH_SCRIPT_LOOK, 0, 0, 2000);
    dgx_ge_actor_advance(&w, 2000, NULL, NULL);
    CHECK(dgx_ge_actor_event(&w, TEST_EVENT_RING, 2700, NULL, NULL));
    dgx_ge_actor_advance(&w, 9000, NULL, NULL);
    CHECK(w.params[TEST_WATCH_PARAM_LEFT] == 700);
    dgx_ge_timers_seen = NULL;
    dgx_ge_actor_start(&w, &anim, TEST_WATCH_SCRIPT_RANG, 0, 0, 2000);
    dgx_ge_actor_advance(&w, 2000, NULL, NULL);
    CHECK(w.params[TEST_WATCH_PARAM_LEFT] == -1);

    dgx_ge_timers_free(&t);
    CHECK(t.timers == NULL && t.capacity == 0);
    CHECK(!dgx_ge_timers_init(&t, 0, NULL, NULL));
    free(data);
    CHECK_DONE();
}
