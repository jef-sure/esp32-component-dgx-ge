#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dgx_ge_dialog.h"
#include "dgx_ge_expr.h"
#include "dgx_ge_pockets.h"

enum { OP_SAY = 1, OP_CHOOSE, OP_TELL, OP_GIVE, OP_GOTO, OP_END, OP_STYLE, OP_SAY_N, OP_SAY_NS };
#define HEADER_BYTES 16u
#define STAYS        0xFFFFu /* an answer that leads nowhere */
#define NO_KIND      0xFFFFu
#define SAY_IMPORTANT 1u /* the flags of a line: shown whole */
#define SAY_VARS      2u /* its numbers are variables of the game, not params of the one saying it */
#define ANSWER_VAR    1u /* the flags of an answer: its control edits a variable of the game, not a param of the one choosing */
/* steps made without a line or a choice before a dialog is taken for one that goes round for ever */
#define MAX_STEPS 1024

static inline uint16_t u16_at(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

static inline uint32_t u32_at(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool dgx_ge_dialog_open(dgx_ge_dialog_t *d, const void *data, size_t size)
{
    memset(d, 0, sizeof(*d));
    const uint8_t *p = data;
    if (!p || size < HEADER_BYTES || u32_at(p) != DGX_GE_DIALOG_MAGIC || u16_at(p + 4) != DGX_GE_DIALOG_VERSION) return false;
    const unsigned roles = p[6], code = u16_at(p + 8), start = u16_at(p + 10);
    if (roles > DGX_GE_DIALOG_ROLES || HEADER_BYTES + 6u * roles + code > size || start >= code) return false;
    d->roles = (uint8_t)roles;
    d->flags = p[7];
    d->ui    = p + 12;
    d->kinds = p + HEADER_BYTES;
    d->names = d->kinds + 2 * roles;
    d->faces = d->names + 2 * roles;
    d->code  = d->faces + 2 * roles;
    d->size  = (uint16_t)code;
    d->start = (uint16_t)start;
    return true;
}

int dgx_ge_dialog_role_name(const dgx_ge_dialog_t *d, int role)
{
    if (role < 0 || role >= d->roles) return -1;
    const uint16_t n = u16_at(d->names + 2 * role);
    return n == 0xFFFF ? -1 : n;
}

int dgx_ge_dialog_role_face(const dgx_ge_dialog_t *d, int role)
{
    if (role < 0 || role >= d->roles) return -1;
    const uint16_t f = u16_at(d->faces + 2 * role);
    return f == 0xFFFF ? -1 : f;
}

bool dgx_ge_strings_open(dgx_ge_strings_t *s, const void *data, size_t size)
{
    memset(s, 0, sizeof(*s));
    const uint8_t *p = data;
    if (!p || size < 12 || u32_at(p) != DGX_GE_STRINGS_MAGIC || u16_at(p + 4) != 2 || 12u + 4u * u16_at(p + 6) > size) return false;
    const uint32_t rule = u32_at(p + 8);
    if (rule && (rule >= size || rule + 1u + p[rule] > size)) return false;
    s->base  = p;
    s->count = u16_at(p + 6);
    s->size  = size;
    s->rule  = rule ? p + rule : NULL;
    return true;
}

const char *dgx_ge_string(const dgx_ge_strings_t *s, int id)
{
    if (!s || id < 0 || id >= s->count) return "";
    const uint32_t at = u32_at(s->base + 12 + 4 * id);
    return at < s->size ? (const char *)s->base + at : "";
}

int dgx_ge_strings_form(const dgx_ge_strings_t *s, int n, int forms)
{
    const int32_t           param = n;
    const dgx_ge_expr_ctx_t c     = {.params = &param, .param_count = 1};
    const int               form  = s && s->rule ? (int)dgx_ge_expr_eval(s->rule, &c) : n != 1;
    return form < 0 ? 0 : form >= forms ? forms - 1 : form;
}

const char *dgx_ge_strings_number(const dgx_ge_strings_t *s, int first, int forms, int n, char *out, size_t size)
{
    const int32_t one = n;
    return dgx_ge_strings_numbers(s, first, forms, &one, 1, out, size);
}

const char *dgx_ge_strings_numbers(const dgx_ge_strings_t *s, int first, int forms, const int32_t *n, int count, char *out, size_t size)
{
    if (!size) return out;
    const char *text = dgx_ge_string(s, first + (forms > 1 && count > 0 ? dgx_ge_strings_form(s, n[0], forms) : 0));
    size_t      o    = 0;
    int         k    = 0;
    for (const char *p = text; *p && o + 1 < size;) {
        const char *close = *p == '{' ? strchr(p, '}') : NULL;
        if (close) {
            /* {name}: the next number; {i} (the packer's, a translation in another order): the number of that place */
            const int at = p[1] >= '0' && p[1] <= '9' ? atoi(p + 1) : k++;
            char      digits[12]; /* -2147483648 */
            const int nd = count > 0 ? snprintf(digits, sizeof digits, "%" PRId32, n[at < count ? at : count - 1]) : 0;
            for (int i = 0; i < nd && o + 1 < size; ++i) out[o++] = digits[i];
            p = close + 1;
        } else {
            out[o++] = *p++;
        }
    }
    /* cut between letters, never inside one (UTF-8: a letter of several bytes whole or not at all) */
    size_t lead = o;
    while (lead > 0 && ((uint8_t)out[lead - 1] & 0xC0) == 0x80) --lead;
    if (lead > 0) {
        const uint8_t b    = (uint8_t)out[lead - 1];
        const size_t  need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
        if (o - (lead - 1) < need) o = lead - 1;
    }
    out[o] = 0;
    return out;
}

/* The bytes of an answer of a choose at p: string u16, label u16, control u16, param u8, flags u8, its expression (length first). */
static unsigned answer_bytes(const uint8_t *p)
{
    return 8 + 1 + p[8];
}

/* The one choosing: the actor of the role, or the game's. */
static dgx_ge_actor_t *chooser(const dgx_ge_talk_t *t)
{
    return t->role == DGX_GE_ROLE_GAME ? t->game : t->role < DGX_GE_DIALOG_ROLES ? t->actors[t->role] : NULL;
}

/* Does the condition of an answer hold for the one choosing? None: it does. */
static bool holds(const dgx_ge_talk_t *t, const uint8_t *expr)
{
    if (!expr[0]) return true;
    const dgx_ge_actor_t *a = chooser(t);
    if (a) return dgx_ge_actor_eval(a, expr) != 0;
    const dgx_ge_expr_ctx_t c = {.now_ms = t->world_ms}; /* the world's moment */
    return dgx_ge_expr_eval(expr, &c) != 0;
}

/* The answers of the choose shown now, in order: the n-th of those whose condition holds; NULL past them. */
static const uint8_t *answer_at(const dgx_ge_talk_t *t, int n)
{
    const uint8_t *p = t->dialog->code + t->at;
    const uint8_t *a = p + 7;
    for (unsigned i = 0; i < p[2]; ++i, a += answer_bytes(a))
        if (holds(t, a + 8) && n-- == 0) return a;
    return NULL;
}

static dgx_ge_actor_t *actor_of(const dgx_ge_talk_t *t, int role)
{
    return role < DGX_GE_DIALOG_ROLES && role < t->dialog->roles ? t->actors[role] : NULL;
}

/* On to the next line or choice, or the end. */
static void run(dgx_ge_talk_t *t)
{
    const dgx_ge_dialog_t *d = t->dialog;
    for (int n = 0; n < MAX_STEPS; ++n) {
        if (t->pc >= d->size) break;
        const uint8_t *p    = d->code + t->pc;
        const unsigned left = d->size - t->pc;
        switch (p[0]) {
        case OP_SAY:
        case OP_SAY_N:
        case OP_SAY_NS: {
            const unsigned size = p[0] == OP_SAY_N ? 9 : p[0] == OP_SAY_NS ? (left >= 9 ? 9u + p[8] : 9u) : 7;
            if (left < size || (p[0] == OP_SAY_NS && (p[8] < 1 || p[8] > DGX_GE_TALK_NUMBERS))) goto over;
            t->at = t->pc, t->pc += (uint16_t)size;
            t->state = DGX_GE_TALK_SAYING, t->role = p[1], t->important = (p[2] & SAY_IMPORTANT) != 0, t->string = u16_at(p + 3);
            t->wait_ms = u16_at(p + 5), t->seen = false, t->since_ms = t->now_ms;
            t->forms = 0, t->number = 0, t->count = 0;
            if (p[0] != OP_SAY) {
                /* the numbers as they are now: the line shows what was said */
                const dgx_ge_actor_t *a     = t->role == DGX_GE_ROLE_GAME ? t->game : actor_of(t, t->role);
                const uint8_t        *param = p[0] == OP_SAY_N ? p + 8 : p + 9;
                t->forms                    = p[7] ? p[7] : 1;
                t->count                    = p[0] == OP_SAY_N ? 1 : p[8];
                for (int i = 0; i < t->count; ++i) {
                    const int32_t v = p[2] & SAY_VARS ? dgx_ge_var(dgx_ge_game_vars, param[i]) : a && a->anim && param[i] < a->anim->params ? a->params[param[i]] : 0;
                    if (i) t->more[i - 1] = v;
                    else t->number = v;
                }
            }
            return;
        }
        case OP_CHOOSE: {
            if (left < 7) goto over;
            unsigned size = 7;
            for (unsigned i = 0; i < p[2]; ++i) {
                if (size + 9 > left || size + answer_bytes(p + size) > left) goto over;
                size += answer_bytes(p + size);
            }
            t->at = t->pc, t->pc += (uint16_t)size, t->role = p[1];
            t->shown = 0;
            while (answer_at(t, t->shown)) ++t->shown;
            if (!t->shown) break; /* nothing to say to it: on past it */
            t->state = DGX_GE_TALK_CHOOSING, t->pick = 0, ++t->choices;
            t->wait_ms = u16_at(p + 3), t->since_ms = t->now_ms;
            return;
        }
        case OP_TELL: {
            if (left < 3) goto over;
            dgx_ge_actor_t *a = actor_of(t, p[1]);
            t->pc += 3;
            if (a) dgx_ge_actor_event(a, p[2], t->world_ms, t->say, t->ctx); /* at the world's moment */
            break;
        }
        case OP_GIVE: {
            if (left < 6) goto over;
            dgx_ge_actor_t *from = actor_of(t, p[1]), *to = actor_of(t, p[2]);
            const int       kind = u16_at(p + 3);
            int             count = p[5];
            t->pc += 6;
            /* as many as there are and go in, place after place */
            while (count > 0 && from && to && from->pockets && to->pockets) {
                const int place = dgx_ge_pockets_find(from->pockets, kind);
                const int moved = place >= 0 ? dgx_ge_pockets_move(from->pockets, place, count, to->pockets, -1, t->world_ms) : 0;
                if (moved <= 0) break;
                count -= moved;
            }
            break;
        }
        case OP_GOTO:
            if (left < 3 || u16_at(p + 1) >= d->size) goto over;
            t->pc = u16_at(p + 1);
            break;
        case OP_STYLE:
            if (left < 3) goto over;
            t->style = u16_at(p + 1), t->pc += 3;
            break;
        case OP_END:
        default: goto over;
        }
    }
over:
    t->state = DGX_GE_TALK_OVER;
}

void dgx_ge_talk_start(dgx_ge_talk_t *t, const dgx_ge_dialog_t *dialog, const dgx_ge_cast_t *cast, uint16_t style, uint32_t now_ms,
                       uint32_t world_ms, dgx_ge_anim_say_fn say, void *ctx)
{
    memset(t, 0, sizeof(*t));
    t->dialog = dialog;
    for (int i = 0; i < dialog->roles && i < DGX_GE_DIALOG_ROLES; ++i) t->actors[i] = cast && cast->roles ? cast->roles[i] : NULL;
    if (cast) t->game = cast->game, t->kinds = cast->kinds, t->fields = cast->fields, t->field_count = cast->fields ? cast->field_count : 0;
    t->pc       = dialog->start;
    t->style    = style;
    t->now_ms   = now_ms;
    t->world_ms = world_ms; /* before the first steps: a tell before the first line lands in the world too */
    t->say      = say;
    t->ctx      = ctx;
    run(t);
}

/* Where the value a control of an answer edits lives: a variable of the game, or a param of the one choosing; NULL none. */
static int32_t *control_value(const dgx_ge_talk_t *t, const uint8_t *a)
{
    if (a[7] & ANSWER_VAR) return dgx_ge_game_vars && a[6] < dgx_ge_game_vars->count ? &dgx_ge_game_vars->values[a[6]] : NULL;
    dgx_ge_actor_t *owner = chooser(t);
    return owner && a[6] < DGX_GE_ANIM_PARAMS ? &owner->params[a[6]] : NULL;
}

/* The kind of the control of an answer; NULL none (or a field). */
static const dgx_ge_anim_t *control_kind(const dgx_ge_talk_t *t, const uint8_t *a)
{
    const uint16_t kind = u16_at(a + 4);
    return kind != NO_KIND && kind != DGX_GE_DIALOG_FIELD && t->kinds ? dgx_ge_kind(t->kinds, kind) : NULL;
}

/* The actor of a control for the moment: script 0, the value it shows, now; false when it has none. */
static bool control_actor(const dgx_ge_talk_t *t, const uint8_t *a, dgx_ge_actor_t *c)
{
    const dgx_ge_anim_t *anim  = control_kind(t, a);
    const int32_t       *value = control_value(t, a);
    if (!anim || !value) return false;
    dgx_ge_actor_start(c, anim, 0, 0, 0, t->now_ms);
    c->params[0] = *value;
    dgx_ge_actor_rehearse(c, t->now_ms);
    return true;
}

/* An event of the interface told the control of an answer (the event of the pack: activate, more, less); its value given back. */
static void tell_control(dgx_ge_talk_t *t, const uint8_t *a, int which)
{
    dgx_ge_actor_t c;
    const uint8_t  event = t->dialog->ui[which];
    if (event == 0xFF || !control_actor(t, a, &c)) return;
    dgx_ge_actor_rehearse_event(&c, event, t->now_ms);
    dgx_ge_actor_rehearse(&c, t->now_ms);
    int32_t *value = control_value(t, a);
    if (*value == c.params[0]) return;
    *value = c.params[0], ++t->changes;
    /* the answers shown as the conditions say now; the one chosen stays chosen while it is shown */
    t->shown = 0;
    for (const uint8_t *b; (b = answer_at(t, t->shown)); ++t->shown)
        if (b == a) t->pick = t->shown;
    if (t->pick >= t->shown) t->pick = t->shown ? (uint8_t)(t->shown - 1) : 0;
}

/* An answer taken: on to its label (none, or past the end: the talk is over). */
static void take(dgx_ge_talk_t *t, const uint8_t *a)
{
    if (!a || u16_at(a + 2) >= t->dialog->size) {
        t->state = DGX_GE_TALK_OVER;
        return;
    }
    t->pc = u16_at(a + 2);
    run(t);
}

void dgx_ge_talk_event(dgx_ge_talk_t *t, dgx_ge_ui_t event, uint32_t now_ms, uint32_t world_ms)
{
    t->now_ms = now_ms, t->world_ms = world_ms;
    if (t->editing) return; /* a field edited: the one showing it says when it is done */
    if (t->state == DGX_GE_TALK_SAYING) {
        if (event == DGX_GE_UI_ACTIVATE || event == DGX_GE_UI_NEXT) run(t);
    } else if (t->state == DGX_GE_TALK_CHOOSING) {
        const uint8_t *a       = answer_at(t, t->pick);
        const bool     field   = a && u16_at(a + 4) == DGX_GE_DIALOG_FIELD;
        const bool     control = a && u16_at(a + 4) != NO_KIND && !field;
        /* a field: activate edits it (a field the game did not give stays as it is) */
        if (field) {
            if (event == DGX_GE_UI_ACTIVATE && dgx_ge_talk_field(t, t->pick)) {
                t->editing = true;
                return;
            }
            if (event == DGX_GE_UI_ACTIVATE) return;
        }
        /* more, less: the control's; without one they move the choice */
        if (control && (event == DGX_GE_UI_MORE || event == DGX_GE_UI_LESS)) {
            tell_control(t, a, event == DGX_GE_UI_MORE ? 1 : 2);
            return;
        }
        if ((event == DGX_GE_UI_NEXT || event == DGX_GE_UI_MORE) && t->pick + 1 < t->shown) ++t->pick;
        if ((event == DGX_GE_UI_PREV || event == DGX_GE_UI_LESS) && t->pick > 0) --t->pick;
        if (event == DGX_GE_UI_ACTIVATE) {
            if (control) tell_control(t, a, 0);
            if (control && u16_at(a + 2) == STAYS) return;
            take(t, a);
        }
    }
}

const dgx_ge_field_t *dgx_ge_talk_field(const dgx_ge_talk_t *t, int shown)
{
    const uint8_t *a = t->state == DGX_GE_TALK_CHOOSING ? answer_at(t, shown) : NULL;
    if (!a || u16_at(a + 4) != DGX_GE_DIALOG_FIELD || !t->fields || a[6] >= t->field_count) return NULL;
    const dgx_ge_field_t *f = &t->fields[a[6]];
    return f->text && f->size ? f : NULL;
}

void dgx_ge_talk_field_done(dgx_ge_talk_t *t, bool changed, uint32_t now_ms, uint32_t world_ms)
{
    if (!t->editing) return;
    t->now_ms = now_ms, t->world_ms = world_ms;
    t->editing = false, t->since_ms = t->now_ms; /* a choice with a time: its time from now, not from before the editing */
    if (changed) ++t->changes;
    const uint8_t *a = answer_at(t, t->pick);
    if (a && u16_at(a + 2) != STAYS) take(t, a);
}

bool dgx_ge_talk_pauses(const dgx_ge_talk_t *t)
{
    return t->state != DGX_GE_TALK_OVER && t->dialog && (t->dialog->flags & DGX_GE_DIALOG_PAUSE);
}

void dgx_ge_talk_play(dgx_ge_talk_t *t, uint32_t now_ms, uint32_t world_ms, bool seen)
{
    t->now_ms = now_ms, t->world_ms = world_ms;
    if (!t->wait_ms || t->editing) return; /* a field edited: a choice with a time waits for it */
    if (t->state == DGX_GE_TALK_SAYING) {
        if (seen && !t->seen) t->seen = true, t->seen_ms = now_ms;
        if (t->seen && (int32_t)(now_ms - t->seen_ms) >= t->wait_ms) run(t);
    } else if (t->state == DGX_GE_TALK_CHOOSING && (int32_t)(now_ms - t->since_ms) >= t->wait_ms) {
        /* unanswered: where silence leads */
        const uint16_t to = u16_at(t->dialog->code + t->at + 5);
        if (to >= t->dialog->size) {
            t->state = DGX_GE_TALK_OVER;
            return;
        }
        t->pc = to;
        run(t);
    }
}

bool dgx_ge_talk_answer(const dgx_ge_talk_t *t, int shown, uint16_t *string)
{
    if (t->state != DGX_GE_TALK_CHOOSING) return false;
    const uint8_t *a = answer_at(t, shown);
    if (!a) return false;
    *string = u16_at(a);
    return true;
}

bool dgx_ge_talk_control(const dgx_ge_talk_t *t, int shown, int *image)
{
    dgx_ge_actor_t c;
    const uint8_t *a = t->state == DGX_GE_TALK_CHOOSING ? answer_at(t, shown) : NULL;
    if (!a || !control_actor(t, a, &c)) return false;
    *image = c.image;
    return true;
}

bool dgx_ge_talk_control_range(const dgx_ge_talk_t *t, int shown, int *value, int *least, int *most, int *step)
{
    const uint8_t       *a    = t->state == DGX_GE_TALK_CHOOSING ? answer_at(t, shown) : NULL;
    const dgx_ge_anim_t *anim = a ? control_kind(t, a) : NULL;
    const int32_t       *now  = anim ? control_value(t, a) : NULL;
    if (!now || anim->params < 4) return false;
    *value = *now;
    *least = (int16_t)u16_at(anim->initial + 2), *most = (int16_t)u16_at(anim->initial + 4), *step = (int16_t)u16_at(anim->initial + 6);
    return *most > *least && *step > 0;
}
