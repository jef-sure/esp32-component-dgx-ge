/* The hits of the registry of bodies: two actors whose masks meet bite each other, damage, event and recoil, in the
   order of their handles; the game passes some by, stops the rest; one removed by its event is not touched again. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_anim.h"
#include "dgx_ge_mask.h"
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

static dgx_ge_pack_t pack;
static dgx_ge_anim_t anim;

/* what the brawlers told, in order: 10 * who (1 the left one, 2 the right one) + the signal (1 struck, 2 bounced) */
static dgx_ge_actor_t *left, *right;
static int             told[16], count;

/* the game that removes the left one's body when it is struck, and lets it go (frees it) when the next one speaks */
static dgx_ge_collisions_t *removing;
static bool                 doomed;

static void say(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)at_ms;
    CHECK(ctx == &told);
    if (what != DGX_GE_ANIM_SIGNAL) return;
    if (doomed) free(left), left = NULL, doomed = false;
    CHECK(count < 16);
    if (count < 16) told[count++] = (a == left ? 10 : a == right ? 20 : 90) + value;
    if (removing && a == left && value == 1) {
        dgx_ge_collisions_remove(removing, 0);
        doomed = true;
    }
}

/* The left one at 0, 0 facing right, the right one at 5, 0 facing left: the jaws of each on the body of the other. */
static dgx_ge_collisions_t *brawl(int left_handle, int right_handle)
{
    dgx_ge_collisions_t *c = dgx_ge_collisions_create(4, 4);
    dgx_ge_meets_t       meets;
    CHECK(c && dgx_ge_pack_meets(&pack, &meets));
    dgx_ge_collisions_meets(c, &meets);
    left  = malloc(sizeof(*left));
    right = malloc(sizeof(*right));
    dgx_ge_actor_start(left, &anim, TEST_BRAWLER_SCRIPT_STAND, 0, 0, 0);
    dgx_ge_actor_start(right, &anim, TEST_BRAWLER_SCRIPT_STAND, 5, 0, 0);
    right->flip = true;
    /* before its first step it shows nothing: no body */
    dgx_ge_collisions_set_actor(c, left_handle, &pack, left, 1, 1);
    CHECK(dgx_ge_collisions_actor_of(c, left_handle) == NULL);
    dgx_ge_actor_advance(left, 0, say, &told);
    dgx_ge_actor_advance(right, 0, say, &told);
    dgx_ge_collisions_set_actor(c, left_handle, &pack, left, 1, 1);
    dgx_ge_collisions_set_actor(c, right_handle, &pack, right, 2, 2);
    CHECK(dgx_ge_collisions_actor_of(c, left_handle) == left && dgx_ge_collisions_actor_of(c, right_handle) == right);
    count = 0;
    return c;
}

static void done(dgx_ge_collisions_t **c)
{
    dgx_ge_collisions_destroy(c);
    free(left), free(right);
    left = right = NULL;
}

/* the game: passes by the hits of one, stops at the hit of a number */
static int pass_by = -1, stop_at = -1, judged;

static dgx_ge_hit_verdict_t judge(int by, dgx_ge_actor_t *a, int on, dgx_ge_actor_t *b, dgx_ge_hit_t *hit, void *ctx)
{
    CHECK(ctx == &told && hit->damage == 3 && hit->event == TEST_BRAWLER_EVENT_STRUCK && hit->recoil == TEST_BRAWLER_EVENT_BOUNCED);
    CHECK(a == (by == 0 ? right : left) && b == (on == 0 ? right : left));
    if (judged++ == stop_at) return DGX_GE_HIT_STOP;
    return by == pass_by ? DGX_GE_HIT_PASS : DGX_GE_HIT_SEND;
}

int main(int argc, char **argv)
{
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    size_t         n;
    const uint8_t *b = dgx_ge_pack_blob(&pack, TEST_BLOB_BRAWLER, &n);
    CHECK(b && dgx_ge_anim_open(&anim, b, n));

    /*
     * The right one is body 0, the left one 1: the meetings go by the handles, the lower first, then by masks. Body 0's
     * body (mask 1) on body 1's jaws (2): the left one bites, the right one struck (3 off its hp), the left one bounced;
     * then 0's jaws on 1's body: the other way round. Found by the look the left one's first (it is further left).
     */
    dgx_ge_collisions_t *c = brawl(1, 0);
    CHECK(dgx_ge_collisions_damage_of(c, 0) == -1);
    dgx_ge_collisions_damage(c, 0, TEST_BRAWLER_PARAM_HP);
    dgx_ge_collisions_damage(c, 1, TEST_BRAWLER_PARAM_HP);
    CHECK(dgx_ge_collisions_damage_of(c, 1) == TEST_BRAWLER_PARAM_HP);
    CHECK(dgx_ge_collisions_hits(c, 10, NULL, say, &told) == 2);
    CHECK(count == 4 && told[0] == 21 && told[1] == 12 && told[2] == 11 && told[3] == 22);
    CHECK(left->params[TEST_BRAWLER_PARAM_HP] == 7 && right->params[TEST_BRAWLER_PARAM_HP] == 7);
    /* nothing changed: nothing met; a body removed forgets its damage */
    CHECK(dgx_ge_collisions_hits(c, 20, NULL, say, &told) == 0 && count == 4);
    dgx_ge_collisions_remove(c, 1);
    CHECK(dgx_ge_collisions_damage_of(c, 1) == -1);
    done(&c);

    /* no damage param: the event and the recoil all the same */
    c = brawl(1, 0);
    CHECK(dgx_ge_collisions_hits(c, 10, NULL, say, &told) == 2 && count == 4);
    CHECK(left->params[TEST_BRAWLER_PARAM_HP] == 10 && right->params[TEST_BRAWLER_PARAM_HP] == 10);
    done(&c);

    /* the game passes the left one's hits by: only the right one bites */
    c = brawl(1, 0);
    pass_by = 1, stop_at = -1, judged = 0;
    CHECK(dgx_ge_collisions_hits(c, 10, judge, say, &told) == 1 && judged == 2);
    CHECK(count == 2 && told[0] == 11 && told[1] == 22);
    done(&c);

    /* the round is over: the game stops at the first hit, none more is judged or sent */
    c = brawl(1, 0);
    dgx_ge_collisions_damage(c, 0, TEST_BRAWLER_PARAM_HP);
    dgx_ge_collisions_damage(c, 1, TEST_BRAWLER_PARAM_HP);
    pass_by = -1, stop_at = 0, judged = 0;
    CHECK(dgx_ge_collisions_hits(c, 10, judge, say, &told) == 0 && judged == 1 && count == 0);
    CHECK(left->params[TEST_BRAWLER_PARAM_HP] == 10 && right->params[TEST_BRAWLER_PARAM_HP] == 10);
    done(&c);
    /* it stops at the second: the first is sent */
    c = brawl(1, 0);
    pass_by = -1, stop_at = 1, judged = 0;
    CHECK(dgx_ge_collisions_hits(c, 10, judge, say, &told) == 1 && judged == 2);
    CHECK(count == 2 && told[0] == 21 && told[1] == 12);
    done(&c);

    /*
     * The left one is body 0: the right one bites it first; struck, the game removes its body, and frees it when the
     * right one speaks of its recoil. Its own bite, the next meeting, is not sent: it is touched no more.
     */
    c        = brawl(0, 1);
    removing = c;
    CHECK(dgx_ge_collisions_hits(c, 10, NULL, say, &told) == 1);
    CHECK(count == 2 && told[0] == 11 && told[1] == 22 && !left && dgx_ge_collisions_actor_of(c, 0) == NULL);
    CHECK(right->params[TEST_BRAWLER_PARAM_HP] == 10);
    removing = NULL;
    done(&c);

    free(data);
    CHECK_DONE();
}
