/*
 * The game's variables: one store made from the pack, each as it begins; scripts read and write them (game_var.<name>:
 * set, add, within 32 bits, the game told), by a step of the game heard or not, never by a rehearsal; none given, read 0
 * and written nowhere; a
 * dialog's condition reads them, its control edits one, its line shows them; saved and loaded, an old save without them
 * refused and nothing changed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_dialog.h"
#include "dgx_ge_game.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
#include "dgx_ge_save.h"
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

/* What the game was told of variables: how many times, the last one. */
static int told, told_var;

static void say(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)a, (void)at_ms, (void)ctx;
    if (what == DGX_GE_ANIM_VAR) ++told, told_var = value;
}

/* The answers shown, their strings joined by '|'. */
static const char *answers(const dgx_ge_talk_t *t, const dgx_ge_strings_t *s)
{
    static char out[256];
    uint16_t    id;
    out[0] = 0;
    for (int i = 0; dgx_ge_talk_answer(t, i, &id); ++i) {
        if (i) strcat(out, "|");
        strcat(out, dgx_ge_string(s, id));
    }
    return out;
}

int main(int argc, char **argv)
{
    if (argc < 2) return fprintf(stderr, "usage: %s test.pack\n", argv[0]), 2;
    size_t        size;
    uint8_t      *data = read_file(argv[1], &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 64));

    /* the store of the pack's blob: each as it begins, by the header's numbers */
    dgx_ge_vars_t  vars;
    size_t         blob_size;
    const uint8_t *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_VARIABLES, &blob_size);
    CHECK(dgx_ge_vars_open(&vars, blob, blob_size) && vars.count == TEST_GAME_VAR_COUNT && TEST_GAME_VAR_COUNT == 3);
    CHECK(vars.values[TEST_GAME_VAR_SCORE] == 5 && vars.values[TEST_GAME_VAR_LIGHT] == 2 && vars.values[TEST_GAME_VAR_BIG] == 2147483000);
    CHECK(dgx_ge_var(&vars, TEST_GAME_VAR_SCORE) == 5 && dgx_ge_var(&vars, 3) == 0 && dgx_ge_var(&vars, -1) == 0 && dgx_ge_var(NULL, 0) == 0);
    dgx_ge_var_set(&vars, TEST_GAME_VAR_LIGHT, 7);
    dgx_ge_var_set(&vars, TEST_GAME_VAR_BIG, (int64_t)1 << 40); /* within 32 bits: the top */
    dgx_ge_var_set(&vars, 3, 1);                                /* none such: nothing */
    CHECK(vars.values[TEST_GAME_VAR_LIGHT] == 7 && vars.values[TEST_GAME_VAR_BIG] == INT32_MAX);
    dgx_ge_vars_reset(&vars);
    CHECK(vars.values[TEST_GAME_VAR_LIGHT] == 2 && vars.values[TEST_GAME_VAR_BIG] == 2147483000);
    /* a blob that is none; no blob at all: no variables */
    dgx_ge_vars_t other;
    CHECK(!dgx_ge_vars_open(&other, data, 16));
    CHECK(dgx_ge_vars_open(&other, NULL, 0) && other.count == 0 && !other.values);
    dgx_ge_vars_free(&other);

    /* a script: 10 more each time, what it is then into its param, the light a step down from 30 on; the game told */
    dgx_ge_game_vars          = &vars;
    const dgx_ge_anim_t *anim = dgx_ge_kind(&kinds, TEST_BLOB_TALLY);
    CHECK(anim != NULL);
    dgx_ge_actor_t a;
    dgx_ge_actor_start(&a, anim, TEST_TALLY_SCRIPT_REST, 0, 0, 0);
    dgx_ge_actor_advance(&a, 0, say, NULL);
    dgx_ge_actor_event(&a, TEST_EVENT_TALLY, 100, say, NULL);
    dgx_ge_actor_advance(&a, 100, say, NULL);
    CHECK(vars.values[TEST_GAME_VAR_SCORE] == 15 && a.params[TEST_TALLY_PARAM_SEEN] == 15 && vars.values[TEST_GAME_VAR_LIGHT] == 2);
    CHECK(told == 1 && told_var == TEST_GAME_VAR_SCORE);
    dgx_ge_actor_event(&a, TEST_EVENT_TALLY, 200, say, NULL);
    dgx_ge_actor_event(&a, TEST_EVENT_TALLY, 300, say, NULL);
    CHECK(vars.values[TEST_GAME_VAR_SCORE] == 35 && a.params[TEST_TALLY_PARAM_SEEN] == 35 && vars.values[TEST_GAME_VAR_LIGHT] == 1);
    CHECK(told == 4 && told_var == TEST_GAME_VAR_LIGHT);
    /* past the top of 32 bits: the top, as a param stops there; at the top already, no change, nothing told */
    dgx_ge_actor_event(&a, TEST_EVENT_TOP, 400, say, NULL);
    CHECK(vars.values[TEST_GAME_VAR_BIG] == INT32_MAX && told == 5 && told_var == TEST_GAME_VAR_BIG);
    dgx_ge_actor_event(&a, TEST_EVENT_TOP, 500, say, NULL);
    CHECK(vars.values[TEST_GAME_VAR_BIG] == INT32_MAX && told == 5);
    /* a rehearsal (a thing made again from its record, its script played once already): it reads them, writes none */
    dgx_ge_actor_rehearse_event(&a, TEST_EVENT_TALLY, 600);
    dgx_ge_actor_rehearse(&a, 600);
    CHECK(vars.values[TEST_GAME_VAR_SCORE] == 35 && a.params[TEST_TALLY_PARAM_SEEN] == 35 && vars.values[TEST_GAME_VAR_LIGHT] == 1 && told == 5);
    /* a step of the game with no say (no one listens): it writes, tells no one */
    dgx_ge_actor_event(&a, TEST_EVENT_TALLY, 650, NULL, NULL);
    CHECK(vars.values[TEST_GAME_VAR_SCORE] == 45 && a.params[TEST_TALLY_PARAM_SEEN] == 45 && vars.values[TEST_GAME_VAR_LIGHT] == 0 && told == 5);
    /* no store given: read 0, written nowhere */
    dgx_ge_game_vars = NULL;
    dgx_ge_actor_event(&a, TEST_EVENT_TALLY, 700, say, NULL);
    CHECK(a.params[TEST_TALLY_PARAM_SEEN] == 0 && vars.values[TEST_GAME_VAR_SCORE] == 45 && told == 5);
    dgx_ge_game_vars = &vars;
    dgx_ge_var_set(&vars, TEST_GAME_VAR_LIGHT, 1); /* the light as the page below takes it */

    /* a page: its control edits the variable (the slider 0..3), the answer of a condition of one shown as it says, the line of two */
    size_t         strings_size;
    const uint8_t *strings_blob = dgx_ge_pack_blob(&pack, TEST_BLOB_STRINGS, &strings_size);
    dgx_ge_strings_t strings;
    CHECK(dgx_ge_strings_open(&strings, strings_blob, strings_size));
    dgx_ge_dialog_t page;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_TALLY_PAGE, &blob_size);
    CHECK(dgx_ge_dialog_open(&page, blob, blob_size));
    dgx_ge_talk_t t;
    vars.values[TEST_GAME_VAR_SCORE] = 20;
    dgx_ge_talk_start(&t, &page, &(dgx_ge_cast_t){.kinds = &kinds}, 0, 0, 0, NULL, NULL);
    CHECK(t.state == DGX_GE_TALK_CHOOSING && !strcmp(answers(&t, &strings), "Light|Back"));
    int value, least, most, step;
    CHECK(dgx_ge_talk_control_range(&t, 0, &value, &least, &most, &step) && value == 1 && least == 0 && most == 3 && step == 1);
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 10, 10);
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 20, 20);
    CHECK(vars.values[TEST_GAME_VAR_LIGHT] == 3 && t.changes == 2 && t.state == DGX_GE_TALK_CHOOSING);
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 30, 30); /* at its most: no change */
    dgx_ge_talk_event(&t, DGX_GE_UI_LESS, 40, 40);
    CHECK(vars.values[TEST_GAME_VAR_LIGHT] == 2 && t.changes == 3);
    int image;
    CHECK(dgx_ge_talk_control(&t, 0, &image) && image == TEST_IMG_ONE + 2); /* its picture by the variable: ONE + min(value, 2) */
    /* a change of the variable from elsewhere: the answers as the conditions say once the talk looks again */
    vars.values[TEST_GAME_VAR_SCORE] = 30;
    dgx_ge_talk_start(&t, &page, &(dgx_ge_cast_t){.kinds = &kinds}, 0, 0, 0, NULL, NULL);
    CHECK(!strcmp(answers(&t, &strings), "Light|Rich|Back"));
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 50, 50);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 60, 60);
    CHECK(t.state == DGX_GE_TALK_SAYING && t.forms == 1 && t.count == 2 && t.number == 30 && t.more[0] == 2);
    CHECK(!t.important); /* a line of variables is not an important one for that */
    char line[64];
    CHECK(!strcmp(dgx_ge_strings_numbers(&strings, t.string, t.forms, (int32_t[]){t.number, t.more[0]}, t.count, line, sizeof line), "30 of 2"));
    /* no store given: the control edits nothing (none shown), the condition reads 0 */
    dgx_ge_game_vars = NULL;
    dgx_ge_talk_start(&t, &page, &(dgx_ge_cast_t){.kinds = &kinds}, 0, 0, 0, NULL, NULL);
    CHECK(!strcmp(answers(&t, &strings), "Light|Back") && !dgx_ge_talk_control(&t, 0, &image));
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 70, 70);
    CHECK(t.changes == 0 && t.pick == 0); /* its control has no value to edit: more changes nothing */
    dgx_ge_game_vars = &vars;

    /* saved and loaded: the values back; a save without them (made before the game had any): refused, nothing changed */
    uint8_t       buf[256];
    dgx_ge_save_t s;
    dgx_ge_save_begin(&s, buf, sizeof buf, 0xDA7A, 1000);
    dgx_ge_vars_save(&vars, &s);
    const size_t n = dgx_ge_save_end(&s);
    CHECK(n > 0);
    const int32_t score = vars.values[TEST_GAME_VAR_SCORE], light = vars.values[TEST_GAME_VAR_LIGHT];
    dgx_ge_vars_reset(&vars);
    dgx_ge_load_t l;
    uint32_t      saved_ms;
    CHECK(dgx_ge_load_open(&l, buf, n, 0xDA7A, &saved_ms) && dgx_ge_vars_load_check(&vars, &l));
    CHECK(vars.values[TEST_GAME_VAR_SCORE] == 5); /* the check alone changes nothing */
    CHECK(dgx_ge_vars_load(&vars, &l) && vars.values[TEST_GAME_VAR_SCORE] == score && vars.values[TEST_GAME_VAR_LIGHT] == light);
    dgx_ge_save_begin(&s, buf, sizeof buf, 0xDA7A, 1000);
    dgx_ge_save_chunk(&s, "POPG");
    dgx_ge_save_u8(&s, 3);
    dgx_ge_save_chunk_end(&s);
    const size_t old = dgx_ge_save_end(&s);
    CHECK(old > 0 && dgx_ge_load_open(&l, buf, old, 0xDA7A, &saved_ms));
    CHECK(!dgx_ge_vars_load_check(&vars, &l) && !dgx_ge_vars_load(&vars, &l) && vars.values[TEST_GAME_VAR_SCORE] == score);
    /* of another count (more variables than the store): refused */
    dgx_ge_save_begin(&s, buf, sizeof buf, 0xDA7A, 1000);
    dgx_ge_save_chunk(&s, "VARS");
    dgx_ge_save_u16(&s, 4);
    for (int i = 0; i < 4; ++i) dgx_ge_save_i32(&s, 99);
    dgx_ge_save_chunk_end(&s);
    const size_t more = dgx_ge_save_end(&s);
    CHECK(more > 0 && dgx_ge_load_open(&l, buf, more, 0xDA7A, &saved_ms) && !dgx_ge_vars_load(&vars, &l) && vars.values[TEST_GAME_VAR_SCORE] == score);

    dgx_ge_vars_free(&vars);
    CHECK(!dgx_ge_game_vars && !vars.values && vars.count == 0); /* the store freed: expressions point at it no more */
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
