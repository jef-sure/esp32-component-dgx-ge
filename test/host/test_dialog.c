/* Dialogs: strings in the language of the pack by context, lines, answers by their conditions, events and pockets, styles. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "dgx_ge_dialog.h"
#include "dgx_ge_pack.h"
#include "dgx_ge_pockets.h"
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

static int said_events;
static void said(dgx_ge_actor_t *a, dgx_ge_anim_say_t what, uint8_t value, uint32_t at_ms, void *ctx)
{
    (void)a, (void)what, (void)value, (void)at_ms, (void)ctx;
    ++said_events;
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
    size_t   size = 0;
    uint8_t *data = read_file(argc > 1 ? argv[1] : "build/test.pack", &size);
    dgx_ge_pack_t pack;
    CHECK(data && dgx_ge_pack_open(&pack, data, size));

    /* the strings: translated in the context of post, the same msgid in other as it is; equal strings once */
    size_t           n;
    const uint8_t   *blob = dgx_ge_pack_blob(&pack, TEST_BLOB_STRINGS, &n);
    dgx_ge_strings_t strings;
    CHECK(blob && dgx_ge_strings_open(&strings, blob, n));
    dgx_ge_dialog_t post, other;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_POST, &n);
    CHECK(blob && dgx_ge_dialog_open(&post, blob, n) && post.roles == 3);
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_OTHER, &n);
    CHECK(blob && dgx_ge_dialog_open(&other, blob, n) && other.roles == 0);
    CHECK(!strcmp(dgx_ge_string(&strings, 9999), "") && strings.count == 26); /* 7 of post, 2 names, 4 of other (Thanks. there), 6 of page, 2 of faces (Hi., Bye.), 4 of clock */

    /* times: a line on by itself, an important one only once seen whole, a choice unanswered where silence leads */
    dgx_ge_talk_t t;
    dgx_ge_talk_start(&t, &other, NULL, 0, 0, NULL, NULL);
    CHECK(t.state == DGX_GE_TALK_SAYING && t.role == DGX_GE_ROLE_GAME && !strcmp(dgx_ge_string(&strings, t.string), "Thanks.") && t.wait_ms == 2000);
    dgx_ge_talk_play(&t, 0, true);
    dgx_ge_talk_play(&t, 1999, true);
    CHECK(!strcmp(dgx_ge_string(&strings, t.string), "Thanks."));
    dgx_ge_talk_play(&t, 2000, true);
    CHECK(t.important && !strcmp(dgx_ge_string(&strings, t.string), "Long news."));
    dgx_ge_talk_play(&t, 2500, false);
    dgx_ge_talk_play(&t, 5000, false); /* not read yet: it waits */
    CHECK(!strcmp(dgx_ge_string(&strings, t.string), "Long news."));
    dgx_ge_talk_play(&t, 5100, true); /* seen whole now */
    dgx_ge_talk_play(&t, 6099, true);
    CHECK(t.state == DGX_GE_TALK_SAYING);
    dgx_ge_talk_play(&t, 6100, true);
    CHECK(t.state == DGX_GE_TALK_CHOOSING && t.shown == 1 && !strcmp(answers(&t, &strings), "Again."));
    dgx_ge_talk_play(&t, 9099, true);
    CHECK(t.state == DGX_GE_TALK_CHOOSING);
    dgx_ge_talk_play(&t, 9100, true);
    CHECK(t.state == DGX_GE_TALK_SAYING && !strcmp(dgx_ge_string(&strings, t.string), "Too late.") && !t.wait_ms);
    dgx_ge_talk_play(&t, 60000, true); /* no time: it waits for a button */
    CHECK(t.state == DGX_GE_TALK_SAYING);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 60010);
    CHECK(t.state == DGX_GE_TALK_OVER);
    /* a button goes on before the time */
    dgx_ge_talk_start(&t, &other, NULL, 0, 0, NULL, NULL);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 10);
    CHECK(!strcmp(dgx_ge_string(&strings, t.string), "Long news."));
    /* a dialog that stops the game (the flags byte after the roles): its talk pauses while it goes on, not when over */
    {
        CHECK(!(other.flags & DGX_GE_DIALOG_PAUSE) && !dgx_ge_talk_pauses(&t));
        static uint8_t copy[4096];
        const uint8_t *o = dgx_ge_pack_blob(&pack, TEST_BLOB_OTHER, &n);
        CHECK(n <= sizeof copy);
        memcpy(copy, o, n), copy[7] = DGX_GE_DIALOG_PAUSE;
        dgx_ge_dialog_t paused;
        CHECK(dgx_ge_dialog_open(&paused, copy, n) && (paused.flags & DGX_GE_DIALOG_PAUSE));
        dgx_ge_talk_t p;
        dgx_ge_talk_start(&p, &paused, NULL, 0, 0, NULL, NULL);
        CHECK(dgx_ge_talk_pauses(&p) && p.ahead_ms == 0);
        dgx_ge_talk_play(&p, 0, true), dgx_ge_talk_play(&p, 2000, true); /* its own time goes on: the line goes by its time */
        CHECK(dgx_ge_talk_pauses(&p) && !strcmp(dgx_ge_string(&strings, p.string), "Long news."));
        p.state = DGX_GE_TALK_OVER;
        CHECK(!dgx_ge_talk_pauses(&p));
        dgx_ge_talk_t none = {0};
        CHECK(!dgx_ge_talk_pauses(&none)); /* no dialog: nothing pauses */
    }
    /* the post's lines have no time: they wait */
    dgx_ge_talk_start(&t, &post, NULL, 0, 0, NULL, NULL);
    dgx_ge_talk_play(&t, 100000, true);
    CHECK(t.state == DGX_GE_TALK_SAYING && !strcmp(dgx_ge_string(&strings, t.string), "Halt! Who goes there?"));

    /* who plays: two who carry, and a hero */
    dgx_ge_kinds_t kinds;
    CHECK(dgx_ge_kinds_init(&kinds, &pack, 16));
    const dgx_ge_anim_t *carrier = dgx_ge_kind(&kinds, TEST_BLOB_CARRIER), *hero_anim = dgx_ge_kind(&kinds, TEST_BLOB_HERO);
    dgx_ge_actor_t       guard, kid, hero;
    dgx_ge_actor_start(&guard, carrier, 0, 0, 0, 0);
    dgx_ge_actor_start(&kid, carrier, 0, 0, 0, 0);
    dgx_ge_actor_start(&hero, hero_anim, TEST_HERO_SCRIPT_RUN, 0, 0, 0);
    guard.pockets = dgx_ge_pockets_create(&kinds, 3, 0);
    kid.pockets   = dgx_ge_pockets_create(&kinds, 3, 0);
    dgx_ge_actor_t *const cast[3] = {&guard, &kid, &hero};

    /* lines, the important one marked; answers: no knife, only a friend */
    dgx_ge_talk_start(&t, &post, &(dgx_ge_cast_t){.roles = cast}, 7, 0, said, NULL);
    CHECK(t.state == DGX_GE_TALK_SAYING && t.role == TEST_POST_ROLE_GUARD && !t.important && t.style == TEST_STYLE_TALK);
    CHECK(!strcmp(dgx_ge_string(&strings, t.string), "Halt! Who goes there?"));
    dgx_ge_talk_event(&t, DGX_GE_UI_PREV, 10); /* means nothing on a line */
    CHECK(t.state == DGX_GE_TALK_SAYING && !t.important);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 20);
    CHECK(t.important && !strcmp(dgx_ge_string(&strings, t.string), "Read this."));
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 30);
    CHECK(t.state == DGX_GE_TALK_CHOOSING && t.role == TEST_POST_ROLE_KID && t.shown == 1 && !strcmp(answers(&t, &strings), "A friend."));

    /* with three knives: the gift is shown, nobody (more than 5) is not */
    const dgx_ge_pocket_t knife = {.kind = TEST_BLOB_KNIFE, .script = TEST_KNIFE_SCRIPT_HELD};
    CHECK(dgx_ge_pockets_put(kid.pockets, &knife, 3) == 3);
    dgx_ge_talk_start(&t, &post, &(dgx_ge_cast_t){.roles = cast}, 7, 0, said, NULL);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 10);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 20);
    CHECK(t.shown == 2 && !strcmp(answers(&t, &strings), "A friend.|Have a knife."));
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 30);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 30); /* no further */
    CHECK(t.pick == 1);
    said_events = 0;
    t.ahead_ms  = 15; /* the talk 15 ms ahead of the world (a pause): what it does to the world lands at the world's moment */
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 40); /* the gift: two knives over, the hero told hit, thanks in the language */
    CHECK(dgx_ge_pockets_count(kid.pockets, TEST_BLOB_KNIFE) == 1 && dgx_ge_pockets_count(guard.pockets, TEST_BLOB_KNIFE) == 2);
    CHECK(hero.script == TEST_HERO_SCRIPT_HURT && said_events > 0 && hero.entered_ms == 25);
    t.ahead_ms = 0;
    CHECK(t.state == DGX_GE_TALK_SAYING && !strcmp(dgx_ge_string(&strings, t.string), "Спасибо."));
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 50); /* back to the start */
    CHECK(t.state == DGX_GE_TALK_SAYING && !strcmp(dgx_ge_string(&strings, t.string), "Halt! Who goes there?"));
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 60);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 70);
    CHECK(t.pick == 0 && t.shown == 2);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 80); /* a friend: another style, the game says it */
    CHECK(t.state == DGX_GE_TALK_SAYING && t.role == DGX_GE_ROLE_GAME && t.style == TEST_STYLE_ALERT);
    CHECK(!strcmp(dgx_ge_string(&strings, t.string), "You may pass."));
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 90);
    CHECK(t.state == DGX_GE_TALK_OVER);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 100); /* over stays over */
    CHECK(t.state == DGX_GE_TALK_OVER);

    /* nobody plays the kid: answers by the expression alone (count is 0) */
    dgx_ge_actor_t *const alone[3] = {&guard, NULL, NULL};
    dgx_ge_talk_start(&t, &post, &(dgx_ge_cast_t){.roles = alone}, 0, 0, NULL, NULL);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 10);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 20);
    CHECK(t.shown == 1);

    /* a page of settings: the game's actor holds them, controls change them; an answer shown as they are */
    dgx_ge_dialog_t page;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_PAGE, &n);
    CHECK(blob && dgx_ge_dialog_open(&page, blob, n));
    dgx_ge_actor_t settings;
    dgx_ge_actor_start(&settings, dgx_ge_kind(&kinds, TEST_BLOB_SETTINGS), 0, 0, 0, 0);
    int image = -1;
    dgx_ge_talk_start(&t, &page, &(dgx_ge_cast_t){.game = &settings, .kinds = &kinds}, 0, 0, NULL, NULL);
    CHECK(t.state == DGX_GE_TALK_CHOOSING && t.role == DGX_GE_ROLE_GAME && t.shown == 4); /* Sound, Volume, Quiet, Back: not loud */
    CHECK(dgx_ge_talk_control(&t, 0, &image) && image == TEST_IMG_FOUR);                   /* sound on: ONE + 1 */
    CHECK(!dgx_ge_talk_control(&t, 3, &image));                                             /* Back: none */
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 10); /* the switch: off, the choice stays */
    CHECK(t.state == DGX_GE_TALK_CHOOSING && settings.params[TEST_SETTINGS_PARAM_SOUND] == 0 && t.changes == 1);
    CHECK(dgx_ge_talk_control(&t, 0, &image) && image == TEST_IMG_ONE);
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 20); /* the switch has no more: nothing, the choice stays where it is */
    CHECK(t.pick == 0 && t.changes == 1);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 30);
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 40); /* the slider: 3, and loud is shown now, before it: the slider stays chosen */
    CHECK(settings.params[TEST_SETTINGS_PARAM_VOLUME] == 3 && t.shown == 5 && t.pick == 2 && t.changes == 2);
    CHECK(dgx_ge_talk_control(&t, 2, &image) && image == TEST_IMG_EIGHT); /* ONE + min(3, 2) */
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 50); /* no further */
    CHECK(settings.params[TEST_SETTINGS_PARAM_VOLUME] == 3 && t.changes == 2);
    dgx_ge_talk_event(&t, DGX_GE_UI_LESS, 60);
    CHECK(settings.params[TEST_SETTINGS_PARAM_VOLUME] == 2 && t.shown == 4 && t.pick == 1);
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 70);
    dgx_ge_talk_event(&t, DGX_GE_UI_PREV, 80);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 90); /* loud: it leads on */
    CHECK(t.state == DGX_GE_TALK_SAYING && !strcmp(dgx_ge_string(&strings, t.string), "Loud."));
    /* a control with a label: changed, then on */
    dgx_ge_talk_start(&t, &page, &(dgx_ge_cast_t){.game = &settings, .kinds = &kinds}, 0, 0, NULL, NULL);
    CHECK(t.shown == 5);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 10);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 10);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 10);
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 20);
    CHECK(settings.params[TEST_SETTINGS_PARAM_SOUND] == 1 && t.state == DGX_GE_TALK_OVER);
    /* more and less move the choice where there is no control */
    dgx_ge_talk_start(&t, &page, &(dgx_ge_cast_t){.game = &settings, .kinds = &kinds}, 0, 0, NULL, NULL);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 10); /* Loud */
    dgx_ge_talk_event(&t, DGX_GE_UI_MORE, 20);
    CHECK(t.pick == 2 && t.changes == 0);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 30);
    dgx_ge_talk_event(&t, DGX_GE_UI_NEXT, 30); /* Back */
    dgx_ge_talk_event(&t, DGX_GE_UI_LESS, 40);
    CHECK(t.pick == 3);
    /* nobody plays the game: its conditions read 0, its controls change nothing; no kinds: no controls */
    dgx_ge_talk_start(&t, &page, NULL, 0, 0, NULL, NULL);
    CHECK(t.shown == 4 && !dgx_ge_talk_control(&t, 0, &image));
    dgx_ge_talk_event(&t, DGX_GE_UI_ACTIVATE, 10);
    CHECK(t.state == DGX_GE_TALK_CHOOSING && t.changes == 0);
    dgx_ge_talk_start(&t, &page, &(dgx_ge_cast_t){.game = &settings}, 0, 0, NULL, NULL);
    CHECK(!dgx_ge_talk_control(&t, 0, &image));

    /* lines of a number: the form the language gives it (three in Russian), the number as it was when said */
    const int forms[][2] = {{1, 0}, {2, 1}, {4, 1}, {5, 2}, {11, 2}, {12, 2}, {14, 2}, {21, 0}, {22, 1}, {25, 2}, {101, 0}, {111, 2}, {0, 2}};
    for (unsigned i = 0; i < sizeof forms / sizeof forms[0]; ++i) CHECK(dgx_ge_strings_form(&strings, forms[i][0], 3) == forms[i][1]);
    CHECK(dgx_ge_strings_form(&strings, 1, 1) == 0 && dgx_ge_strings_form(&(dgx_ge_strings_t){0}, 1, 2) == 0 && dgx_ge_strings_form(NULL, 7, 2) == 1);
    dgx_ge_dialog_t clock;
    blob = dgx_ge_pack_blob(&pack, TEST_BLOB_CLOCK, &n);
    CHECK(blob && dgx_ge_dialog_open(&clock, blob, n));
    settings.params[TEST_SETTINGS_PARAM_VOLUME] = 22;
    dgx_ge_talk_start(&t, &clock, &(dgx_ge_cast_t){.game = &settings}, 0, 0, NULL, NULL);
    settings.params[TEST_SETTINGS_PARAM_VOLUME] = 5; /* changed after it was said: the line keeps its number */
    char line[64];
    CHECK(t.state == DGX_GE_TALK_SAYING && t.forms == 3 && t.number == 22);
    CHECK(!strcmp(dgx_ge_strings_number(&strings, t.string, t.forms, t.number, line, sizeof line), "Осталось 22 минуты"));
    CHECK(!strcmp(dgx_ge_strings_number(&strings, t.string, t.forms, 21, line, sizeof line), "Осталась 21 минута"));
    CHECK(!strcmp(dgx_ge_strings_number(&strings, t.string, t.forms, -5, line, sizeof line), "Осталось -5 минут"));
    CHECK(!strcmp(dgx_ge_strings_number(&strings, t.string, t.forms, 5, line, 10), "Оста")); /* cut to its room, 9 bytes: between letters (2 bytes each) */
    dgx_ge_talk_play(&t, 0, true); /* seen whole: its second from now */
    dgx_ge_talk_play(&t, 1000, true);
    CHECK(t.state == DGX_GE_TALK_SAYING && t.forms == 1 && t.number == 5);
    CHECK(!strcmp(dgx_ge_strings_number(&strings, t.string, t.forms, t.number, line, sizeof line), "Volume 5."));
    /* a line of several numbers: each {name} in order, as they were when it was said */
    settings.params[TEST_SETTINGS_PARAM_SOUND] = 1;
    dgx_ge_talk_play(&t, 1000, true);
    dgx_ge_talk_play(&t, 2000, true);
    settings.params[TEST_SETTINGS_PARAM_SOUND] = 0;
    const int16_t said[] = {t.number, t.more[0]};
    CHECK(t.state == DGX_GE_TALK_SAYING && t.count == 2 && t.number == 1 && t.more[0] == 5);
    CHECK(!strcmp(dgx_ge_strings_numbers(&strings, t.string, t.forms, said, t.count, line, sizeof line), "Sound 1, volume 5."));

    /* not a dialog */
    CHECK(!dgx_ge_dialog_open(&other, data, 4) && !dgx_ge_dialog_open(&other, NULL, 100));
    dgx_ge_pockets_destroy(&guard.pockets);
    dgx_ge_pockets_destroy(&kid.pockets);
    dgx_ge_kinds_free(&kinds);
    free(data);
    CHECK_DONE();
}
