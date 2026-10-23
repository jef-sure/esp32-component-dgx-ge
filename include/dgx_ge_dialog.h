#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dgx_ge_anim.h"

/*
 * Dialogs: who says what, the answers to choose from, what follows in the
 * world. A dialog is a script of data (tools/dgx_ge_dialog.py, the blob
 * "DGDL"): lines (say), choices (choose, an answer shown only where its
 * condition holds, of the params and pockets of the one choosing and the game's variables), events
 * for the ones taking part (tell), things from pockets into pockets (give),
 * labels and goto, end. It speaks; the matrices of those told decide.
 *
 * Roles: the script is written about roles; the game says, when a talk
 * begins, which actors play them. The role game is the game itself (it says
 * that something happened; a page of settings): what it shows and edits are
 * the game's variables (game_var.<name>, dgx_ge_game.h); an actor of it, if the
 * dialog names its kind (game <KIND>) and the game gives one, has params too.
 *
 * Controls: an answer may carry one, a kind (an animation of the pack) and
 * what it changes: a variable of the game (control SLIDER game_var.volume) or
 * a param of the one choosing (control SLIDER volume). Its first param is its value;
 * activate, more, less are told it as the events of the pack of those names
 * (an actor of it made for the moment: from script 0 with the value, told,
 * moved on to now, the value given back); its picture is that of script 0
 * with the value. A kind whose params 1, 2, 3 are its least value, its most
 * and its step (declared so, in that order, after the value; min and max are
 * words of expressions, so least, most, step) has a range: a
 * pointer may then put it at a place (dgx_ge_talk_control_range); without
 * them it is worked by its events alone. An answer without a label leads
 * nowhere: its control changed, the choice stays.
 *
 * Fields: an answer may carry a field of the game instead (the kind
 * DGX_GE_DIALOG_FIELD, its param the field's number: the packer's
 * <PREFIX>_FIELD_<name>), a text the game keeps (dgx_ge_field_t, given with
 * the cast). Activate on it begins editing: the talk stands on that answer
 * and takes no events of its own until the one showing it says the editing is
 * done (dgx_ge_talk_field_done: the text typed into the field, or as it was);
 * then on to its label, if it has one.
 *
 * Strings are numbers: the packer chose the language (msgid in the context of
 * the dialog's name, translated or as it is) and put its strings into the
 * blob "DGSS" (dgx_ge_strings_*).
 *
 * Pause: a dialog may stop the game while it is open (a page of settings): the
 * game does not play its world meanwhile (dgx_ge_talk_pauses tells it).
 *
 * Two clocks: every call that moves a talk on takes two moments. The talk's
 * own (now_ms, real time) goes on while the world stands: its lines with a
 * time, a choice with a time, letters, controls. The world's (world_ms) is
 * where the world is: what the talk does to it (tell, give, the conditions of
 * answers) lands there. A game that never stops its world gives the same
 * moment twice.
 *
 * A talk is where one is in a dialog: a few bytes. It is moved on by events
 * of the interface, which the game makes of its buttons (input is apart):
 * next, prev move the choice; activate goes on from a line, or takes the
 * answer chosen. And by time: a line with a time goes on by itself so long
 * after it is seen whole (dgx_ge_talk_play: the one showing it says when);
 * a choice with a time goes, unanswered, where its silence leads. Showing it
 * is a style's (the game's, the editor's).
 *
 * The blob, little-endian: "DGDL", version u16, roles u8, flags u8
 * (DGX_GE_DIALOG_PAUSE; 0 in packs before it: none), size of code
 * u16, start u16, the events activate, more, less of the pack (u8 each,
 * 0xFF none), 0xFF; the kinds of the roles (u16 each: blobs of the pack); the
 * names they are shown by (u16 each: strings, 0xFFFF none); their portraits
 * (u16 each: pictures of the pack, 0xFFFF none); the code:
 *
 *   SAY     role u8, flags u8 (1: important), string u16, ms u16 (0: it waits for a button)
 *   SAY_N   as SAY, then forms u8, param u8: a line of a number, the param of
 *           the one saying it (flags 2: a variable of the game, {game_var.<name>}); its forms are so many strings in a row, the
 *           language's rule chooses (dgx_ge_strings_form); {name} in it shows the number
 *   SAY_NS  as SAY, then forms u8, count u8 (up to DGX_GE_TALK_NUMBERS), params u8 each: a line of
 *           several numbers, its {names} in order, the form by the first; a translation that
 *           shows them in another order has {i} instead, the number of param i (flags 2: all
 *           of them variables of the game)
 *   CHOOSE  role u8, count u8, ms u16 (0: no limit), label u16 (where silence leads);
 *           each answer: string u16, label u16 (0xFFFF: it stays), control kind u16 (0xFFFF none),
 *           param u8, flags u8 (1: the param is a variable of the game), expr (its length 0: none)
 *   TELL    role u8, event u8
 *   GIVE    from u8, to u8, kind u16, count u8
 *   GOTO    label u16
 *   END
 *   STYLE   style u16
 */
#define DGX_GE_DIALOG_MAGIC   0x4C444744u /* "DGDL" */
#define DGX_GE_DIALOG_VERSION 7u
#define DGX_GE_DIALOG_ROLES   8
#define DGX_GE_ROLE_GAME      0xFF
#define DGX_GE_DIALOG_PAUSE   1u /* flags: the game stands while it is open */
#define DGX_GE_DIALOG_FIELD   0xFFFEu /* the kind of a control that is a field of the game (its param: the field's number) */

typedef struct {
    const uint8_t *code;
    uint16_t       size, start;
    uint8_t        roles;
    uint8_t        flags; /* DGX_GE_DIALOG_* */
    const uint8_t *kinds; /* u16 a role */
    const uint8_t *names; /* u16 a role: the string it is shown by, 0xFFFF none */
    const uint8_t *faces; /* u16 a role: its portrait, a picture of the pack, 0xFFFF none */
    const uint8_t *ui;    /* the events of the pack a control is told: activate, more, less; 0xFF none */
} dgx_ge_dialog_t;

/* The string a role is shown by (its name), -1 none (the game, or a role without one). */
int dgx_ge_dialog_role_name(const dgx_ge_dialog_t *dialog, int role);

/* The portrait of a role (a picture of the pack), -1 none (the game, or a role without one). */
int dgx_ge_dialog_role_face(const dgx_ge_dialog_t *dialog, int role);

/* Take the blob of a dialog; false when it is not one. */
bool dgx_ge_dialog_open(dgx_ge_dialog_t *dialog, const void *data, size_t size);

/*
 * The strings of a pack in its language: the blob "DGSS", little-endian:
 * "DGSS", version u16, count u16, the offset of the rule of forms u32 (0
 * none: n != 1), offsets u32 of each string, the strings (UTF-8, each ending
 * in 0), the rule (an expression, n its param 0: which form a number takes).
 */
#define DGX_GE_STRINGS_MAGIC 0x53534744u /* "DGSS" */
typedef struct {
    const uint8_t *base;
    uint16_t       count;
    size_t         size;
    const uint8_t *rule; /* NULL: n != 1 */
} dgx_ge_strings_t;

bool dgx_ge_strings_open(dgx_ge_strings_t *strings, const void *data, size_t size);

/* A string by its number, UTF-8 ending in 0; "" when there is none. */
const char *dgx_ge_string(const dgx_ge_strings_t *strings, int id);

/* Which of so many forms of a string of a number the language gives n (0 the first). */
int dgx_ge_strings_form(const dgx_ge_strings_t *strings, int n, int forms);

/*
 * A line of a number made: the form of n of the string at first ({name} in
 * it shows n) into out, cut to its size between letters; out itself.
 */
const char *dgx_ge_strings_number(const dgx_ge_strings_t *strings, int first, int forms, int n, char *out, size_t size);

/*
 * The same with several numbers: each {name} shows the next of them (the last again past their count), {i} (digits:
 * the packer's, of a translation that puts them in another order) the one of that place; the form of the first.
 */
const char *dgx_ge_strings_numbers(const dgx_ge_strings_t *strings, int first, int forms, const int32_t *n, int count, char *out, size_t size);

/* So many numbers a line may show at most. */
#define DGX_GE_TALK_NUMBERS 4

/* Events of the interface: what the game makes of its buttons. */
typedef enum {
    DGX_GE_UI_NEXT,
    DGX_GE_UI_PREV,
    DGX_GE_UI_ACTIVATE,
    DGX_GE_UI_BACK,
    DGX_GE_UI_MORE, /* the control of the answer chosen: more; none, the choice moves on (as next) */
    DGX_GE_UI_LESS, /* less; none, back (as prev) */
} dgx_ge_ui_t;

struct dgx_ge_kinds;

/* A text field of the game: its text (UTF-8, ending in 0) and the bytes it holds, the 0 too. */
typedef struct {
    char   *text;
    uint8_t size;
} dgx_ge_field_t;

/*
 * Who plays a talk: the actors of its roles (one a role, NULL nobody), the game's actor (a dialog that names its kind:
 * game <KIND>), the kinds of controls, the game's text fields (by the packer's numbers, <PREFIX>_FIELD_<name>). The
 * game's variables are dgx_ge_game_vars, given once.
 */
typedef struct {
    dgx_ge_actor_t *const *roles;  /* NULL: nobody */
    dgx_ge_actor_t        *game;   /* NULL: none (its params in conditions read 0, controls of them change nothing) */
    struct dgx_ge_kinds   *kinds;  /* the animations of controls; NULL: no controls */
    const dgx_ge_field_t  *fields; /* NULL: none (an answer with a field is shown, never edited) */
    uint8_t                field_count;
} dgx_ge_cast_t;

typedef enum {
    DGX_GE_TALK_SAYING,   /* a line shown: activate goes on */
    DGX_GE_TALK_CHOOSING, /* answers shown: next, prev choose, activate takes one */
    DGX_GE_TALK_OVER,
} dgx_ge_talk_state_t;

typedef struct {
    const dgx_ge_dialog_t *dialog;
    dgx_ge_actor_t        *actors[DGX_GE_DIALOG_ROLES]; /* who plays each role; NULL nobody */
    dgx_ge_actor_t        *game;                        /* the game's actor, NULL none */
    struct dgx_ge_kinds   *kinds;                       /* of the controls */
    const dgx_ge_field_t  *fields;                      /* the game's text fields, NULL none */
    uint8_t                field_count;
    bool                   editing; /* the field of the answer chosen is edited: the talk takes no events (dgx_ge_talk_field_done) */
    uint16_t               pc;    /* the step after the one shown */
    uint16_t               at;    /* the step shown: its say or choose */
    uint8_t                state; /* dgx_ge_talk_state_t */
    uint8_t                role;  /* who says it, who chooses */
    bool                   important;
    uint16_t               string; /* the line said (a line of a number: its first form) */
    int32_t                number; /* its number, as it was when it was said */
    int32_t                more[DGX_GE_TALK_NUMBERS - 1]; /* a line of several numbers: the others */
    uint8_t                count;  /* how many numbers the line shows */
    uint8_t                forms;  /* a line of a number: so many forms, 0 none */
    uint16_t               style;  /* the style of the pack its lines are shown in now */
    uint8_t                pick;   /* the answer chosen, of those shown */
    uint8_t                shown;  /* how many answers are shown */
    bool                   seen;   /* the line is seen whole: its time runs from seen_ms */
    uint16_t               wait_ms; /* the time of the line or the choice shown, 0 none */
    uint32_t               since_ms, seen_ms;
    uint32_t               now_ms;   /* the talk's own time at the last call */
    uint32_t               world_ms; /* the world's at the last call: tell, give and conditions land at it */
    uint8_t                changes; /* counted up whenever a control or a field changed a value: for the one showing it, the game */
    uint8_t                choices; /* counted up whenever a choice is shown, again too (its answers as the conditions are then): for the one showing it */
    dgx_ge_anim_say_fn     say;    /* what the actors told say, as the game hears them; NULL nothing */
    void                  *ctx;
} dgx_ge_talk_t;

/*
 * A talk begins at the start of a dialog: who plays it (NULL: nobody), the
 * style its lines are shown in until it says another. It goes on to the
 * first line or choice: what it does on the way lands at world_ms.
 */
void dgx_ge_talk_start(dgx_ge_talk_t *talk, const dgx_ge_dialog_t *dialog, const dgx_ge_cast_t *cast, uint16_t style, uint32_t now_ms,
                       uint32_t world_ms, dgx_ge_anim_say_fn say, void *ctx);

/* An event of the interface at a moment (the talk's, the world's): the talk goes on as it says. */
void dgx_ge_talk_event(dgx_ge_talk_t *talk, dgx_ge_ui_t event, uint32_t now_ms, uint32_t world_ms);

/*
 * Time goes on to now: a line with a time goes on so long after it is seen
 * whole (seen: as the one showing it says now: an important text scrolled to
 * its end; an unimportant one at once); a choice with a time, unanswered, goes
 * where its silence leads. Both by the talk's own time; the world at world_ms.
 */
void dgx_ge_talk_play(dgx_ge_talk_t *talk, uint32_t now_ms, uint32_t world_ms, bool seen);

/* A talk going on whose dialog stops the game while it is open (DGX_GE_DIALOG_PAUSE): the game plays no world meanwhile. */
bool dgx_ge_talk_pauses(const dgx_ge_talk_t *talk);

/* An answer shown, by its place among those shown: its string; false past the last. */
bool dgx_ge_talk_answer(const dgx_ge_talk_t *talk, int shown, uint16_t *string);

/* The control of an answer shown: its picture now (an image of the pack, by its value); false when it has none (or no kind of it). */
bool dgx_ge_talk_control(const dgx_ge_talk_t *talk, int shown, int *image);

/* The control of an answer shown: its value now and its range (the kind's params 1..3: least, most, step); false when it has none, or no range. */
bool dgx_ge_talk_control_range(const dgx_ge_talk_t *talk, int shown, int *value, int *least, int *most, int *step);

/* The field of an answer shown (the game's); NULL when it has none, or the game gave no such field. */
const dgx_ge_field_t *dgx_ge_talk_field(const dgx_ge_talk_t *talk, int shown);

/*
 * The editing of the field of the answer chosen is over (the one showing it typed into the field, or left it as it
 * was), at a moment (the talk's, the world's): changed, the talk's changes counted up; then on to the answer's label,
 * if it has one, else the choice stays.
 */
void dgx_ge_talk_field_done(dgx_ge_talk_t *talk, bool changed, uint32_t now_ms, uint32_t world_ms);

#ifdef __cplusplus
}
#endif
