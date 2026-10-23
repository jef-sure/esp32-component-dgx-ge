/* Events at once from an actor's own say: in turn, each in the script the one before led to, as many as the build's
   DGX_GE_ANIM_NOW_EVENTS (built twice: as the Makefile says, and with 2); one more refused, the rest still taken; one
   at a branch point or at the end as before, a later in its place. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_pack.h"
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

/* The game: signal 1 answered with the events of answer, one after another, from inside the step; what each call
   gave back kept. The sounds the scripts said, in order. */
static int  answer[8], answers;
static bool took[8];
static int  heard[16], sounds;

static void say(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    if (what == DGX_GE_ANIM_SOUND && sounds < 16) heard[sounds++] = value;
    if (what == DGX_GE_ANIM_SIGNAL && value == 1)
        for (int i = 0; i < answers; ++i) took[i] = dgx_ge_actor_event(a, answer[i], at_ms, say, ctx);
}

/* The actor of s0 at 0, the game answering with these events; its first step made. */
static void begin(dgx_ge_actor_t *a, const dgx_ge_anim_t *anim, int n, const int *events)
{
    answers = n, sounds = 0;
    for (int i = 0; i < n; ++i) answer[i] = events[i], took[i] = false;
    dgx_ge_actor_start(a, anim, TEST_NOWS_SCRIPT_S0, 0, 0, 0);
    dgx_ge_actor_advance(a, 0, say, NULL);
}

static bool heard_as(int n, const int *want)
{
    if (sounds != n) return false;
    for (int i = 0; i < n; ++i)
        if (heard[i] != want[i]) return false;
    return true;
}

int main(int argc, char **argv)
{
    size_t        size = 0;
    uint8_t      *data = argc > 1 ? read_file(argv[1], &size) : NULL;
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t         n;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_NOWS, &n);
    dgx_ge_anim_t  anim;
    CHECK(blob && dgx_ge_anim_open(&anim, blob, n));
    enum { NEXT = TEST_NOWS_EVENT_NEXT, EAT = TEST_NOWS_EVENT_EAT, HALT = TEST_NOWS_EVENT_HALT, REST = TEST_NOWS_EVENT_REST,
           SLEEP = TEST_NOWS_EVENT_SLEEP, LATE = TEST_NOWS_EVENT_LATE, SPIN = TEST_NOWS_EVENT_SPIN };
    dgx_ge_actor_t a;

    /* one, as always: s1 from the moment of the step, the rest of s0's step (its sound 90, its frame) not made */
    begin(&a, &anim, 1, (const int[]){NEXT});
    CHECK(took[0] && a.script == TEST_NOWS_SCRIPT_S1 && a.entered_ms == 0 && a.image == TEST_IMG_ONE && a.due_ms == 100);
    CHECK(heard_as(2, (const int[]){1, 11}) && a.now[0] == 0);

    /* two: next taken in s1, where the first led: s2, all of s1 before its frame heard on the way, at the step's moment */
    begin(&a, &anim, 2, (const int[]){NEXT, NEXT});
    CHECK(took[0] && took[1] && a.script == TEST_NOWS_SCRIPT_S2 && a.entered_ms == 0 && a.due_ms == 100);
    CHECK(heard_as(3, (const int[]){1, 11, 2}) && a.now[0] == 0);

    /* eat means nothing in s0, something in s1: behind next it is taken there */
    begin(&a, &anim, 2, (const int[]){NEXT, EAT});
    CHECK(took[0] && took[1] && a.script == TEST_NOWS_SCRIPT_ATE && a.image == TEST_IMG_EIGHT);
    CHECK(heard_as(3, (const int[]){1, 11, 20}));
    /* alone it means nothing where the actor is: refused, as always */
    begin(&a, &anim, 1, (const int[]){EAT});
    CHECK(!took[0] && a.script == TEST_NOWS_SCRIPT_S0 && heard_as(1, (const int[]){90}));

    /* as many as the queue holds, all taken in turn; one more refused, those before still taken */
    const int depth = DGX_GE_ANIM_NOW_EVENTS;
    CHECK(depth == 4 || depth == 2);
    const int nexts[] = {NEXT, NEXT, NEXT, NEXT, NEXT}, ways[] = {1, 11, 2, 3, 4};
    begin(&a, &anim, depth, nexts);
    CHECK(took[depth - 1] && a.script == TEST_NOWS_SCRIPT_S0 + depth && heard_as(depth + 1, ways));
    begin(&a, &anim, depth + 1, nexts);
    bool all = true;
    for (int i = 0; i < depth; ++i) all = all && took[i];
    CHECK(all && !took[depth] && a.script == TEST_NOWS_SCRIPT_S0 + depth && heard_as(depth + 1, ways) && a.now[0] == 0);
    /* and after it events go on as ever: one from outside the step, at once */
    CHECK(dgx_ge_actor_event(&a, NEXT, 50, say, NULL) && a.script == TEST_NOWS_SCRIPT_S0 + depth + 1 && a.entered_ms == 50);
    if (depth >= 3) {
        begin(&a, &anim, 3, nexts);
        CHECK(took[2] && a.script == TEST_NOWS_SCRIPT_S3 && heard_as(4, ways));
    }

    /* two at a branch point from its own say: the later in place of the one waiting, as for any not at once */
    begin(&a, &anim, 2, (const int[]){HALT, REST});
    CHECK(took[0] && took[1] && a.script == TEST_NOWS_SCRIPT_S0 && a.pending == TEST_NOWS_SCRIPT_H2 + 1 && a.image == TEST_IMG_ONE);
    CHECK(heard_as(1, (const int[]){90}));
    dgx_ge_actor_advance(&a, 100, say, NULL);
    CHECK(a.script == TEST_NOWS_SCRIPT_H2 && heard_as(2, (const int[]){90, 32}));

    /* one at a branch point behind one at once: what it means is s1's, there it waits for s1's branch point */
    begin(&a, &anim, 2, (const int[]){NEXT, HALT});
    CHECK(took[0] && took[1] && a.script == TEST_NOWS_SCRIPT_S1 && a.pending == TEST_NOWS_SCRIPT_H1 + 1 && a.image == TEST_IMG_ONE);
    dgx_ge_actor_advance(&a, 100, say, NULL);
    CHECK(a.script == TEST_NOWS_SCRIPT_H1 && heard_as(3, (const int[]){1, 11, 31}));

    /* into a script that holds at once: the next, at its end there, taken as by one that holds */
    begin(&a, &anim, 2, (const int[]){SLEEP, LATE});
    CHECK(took[0] && took[1] && a.script == TEST_NOWS_SCRIPT_AFTER && !a.holding && a.image == TEST_IMG_ONE && a.entered_ms == 0);
    CHECK(heard_as(2, (const int[]){40, 41}));

    /* into a script that never shows a frame: it stops, the one behind passed by (not taken by the next step) */
    begin(&a, &anim, 2, (const int[]){SPIN, NEXT});
    CHECK(took[0] && took[1] && a.script == TEST_NOWS_SCRIPT_SPINNING && a.holding && a.now[0] == 0);
    CHECK(dgx_ge_actor_event(&a, NEXT, 10, say, NULL) && a.script == TEST_NOWS_SCRIPT_S1 && heard_as(2, (const int[]){1, 11}));

    free(data);
    CHECK_DONE();
}
