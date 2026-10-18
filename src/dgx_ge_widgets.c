#include <string.h>

#include "dgx_ge_widgets.h"

#define KNOB 3 /* points: the knob of a slider, the width of a switch's mark */

void dgx_ge_widgets_init(dgx_ge_widgets_t *t, dgx_ge_widget_t *widgets, int capacity, int unit)
{
    memset(t, 0, sizeof(*t));
    t->widgets  = widgets;
    t->capacity = (uint8_t)(capacity > 127 ? 127 : capacity);
    t->unit     = (uint8_t)(unit < 1 ? 1 : unit);
    t->focus = t->pressed = -1;
}

int dgx_ge_widgets_add(dgx_ge_widgets_t *t, int parent, const dgx_ge_widget_t *w)
{
    if (t->count >= t->capacity || (parent >= 0 && parent >= t->count)) return -1;
    t->widgets[t->count] = *w;
    t->widgets[t->count].parent = (int8_t)(t->count == 0 ? -1 : parent);
    t->widgets[t->count].flags &= (uint8_t)~DGX_GE_WIDGET_FOCUSED;
    return t->count++;
}

static bool hidden(const dgx_ge_widgets_t *t, int i)
{
    for (; i >= 0; i = t->widgets[i].parent)
        if (t->widgets[i].flags & DGX_GE_WIDGET_HIDDEN) return true;
    return false;
}

static bool interactive(const dgx_ge_widget_t *w)
{
    return w->kind == DGX_GE_WIDGET_LIST || w->kind == DGX_GE_WIDGET_SWITCH || w->kind == DGX_GE_WIDGET_SLIDER || w->kind == DGX_GE_WIDGET_BUTTON;
}

/* the size a widget asks for, in pixels: its own, else its content's */
static int want_w(const dgx_ge_widgets_t *t, const dgx_ge_widget_t *w)
{
    if (w->want_w) return w->want_w * t->unit;
    if (w->text) return w->text->width;
    if (w->image) return w->image->width;
    return 0;
}

static int want_h(const dgx_ge_widgets_t *t, const dgx_ge_widget_t *w)
{
    if (w->want_h) return w->want_h * t->unit;
    if (w->text) return w->text->height;
    if (w->image) return w->image->height;
    return 0;
}

static void lay(dgx_ge_widgets_t *t, int i)
{
    dgx_ge_widget_t *p = &t->widgets[i];
    const int        u = t->unit, pad = p->pad * u, gap = p->gap * u;
    const int        ix = p->x + pad, iy = p->y + pad, iw = p->w - 2 * pad, ih = p->h - 2 * pad;
    const bool       column = p->flags & DGX_GE_WIDGET_COLUMN, row = p->flags & DGX_GE_WIDGET_ROW;
    int              along = 0;
    for (int c = 0; c < t->count; ++c) {
        dgx_ge_widget_t *w = &t->widgets[c];
        if (w->parent != i || (w->flags & DGX_GE_WIDGET_HIDDEN)) continue;
        int ww = want_w(t, w), wh = want_h(t, w);
        if (column) {
            if (w->flags & DGX_GE_WIDGET_STRETCH || !ww) ww = iw;
            w->x = ix + (w->at_x >= 0 ? w->at_x * u : iw - ww + w->at_x * u), w->y = iy + along, w->w = ww, w->h = wh;
            along += wh + gap;
        } else if (row) {
            if (w->flags & DGX_GE_WIDGET_STRETCH || !wh) wh = ih;
            w->x = ix + along, w->y = iy + (w->at_y >= 0 ? w->at_y * u : ih - wh + w->at_y * u), w->w = ww, w->h = wh;
            along += ww + gap;
        } else {
            if (!ww) ww = iw - (w->at_x >= 0 ? w->at_x * u : -w->at_x * u);
            if (!wh) wh = ih - (w->at_y >= 0 ? w->at_y * u : -w->at_y * u);
            w->x = w->at_x >= 0 ? ix + w->at_x * u : ix + iw + w->at_x * u - ww;
            w->y = w->at_y >= 0 ? iy + w->at_y * u : iy + ih + w->at_y * u - wh;
            w->w = ww, w->h = wh;
        }
        lay(t, c);
    }
}

void dgx_ge_widgets_layout(dgx_ge_widgets_t *t, dgx_ge_rect_t root)
{
    if (!t->count) return;
    dgx_ge_widget_t *r = &t->widgets[0];
    r->x = (int16_t)root.x, r->y = (int16_t)root.y, r->w = (int16_t)root.w, r->h = (int16_t)root.h;
    lay(t, 0);
}

/* the value a control shows: its actor's param follows */
static void follow(const dgx_ge_widgets_t *t, dgx_ge_widget_t *w)
{
    if (w->actor && t->value_param < DGX_GE_ANIM_PARAMS) w->actor->params[t->value_param] = w->value;
}

static int rows_of(const dgx_ge_widget_t *w)
{
    return w->text && w->text->shown ? w->text->shown : w->max - w->min + 1;
}

void dgx_ge_widgets_show(const dgx_ge_widgets_t *t, dgx_ge_scene_t *scene, uint8_t layer)
{
    const int u = t->unit;
    for (int i = 0; i < t->count; ++i) {
        const dgx_ge_widget_t *w = &t->widgets[i];
        if (hidden(t, i)) continue;
        const dgx_ge_rect_t box = dgx_ge_rect(w->x, w->y, w->w, w->h);
        const bool          focused = w->flags & DGX_GE_WIDGET_FOCUSED;
        switch (w->kind) {
        case DGX_GE_WIDGET_FRAME:
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = box, .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = w->color2});
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(w->x + u, w->y + u, w->w - 2 * u, w->h - 2 * u), .layer = layer,
                                                       .flags = DGX_GE_ITEM_FILL, .color = w->color});
            break;
        case DGX_GE_WIDGET_TEXT:
        case DGX_GE_WIDGET_BUTTON:
            if (w->kind == DGX_GE_WIDGET_BUTTON || focused)
                dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = box, .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = focused ? w->color2 : w->color});
            if (w->text)
                dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &w->text->image, .map = w->map, .x = (int16_t)w->x, .y = (int16_t)w->y, .clip = box,
                                                           .layer = layer, .color = w->kind == DGX_GE_WIDGET_BUTTON ? w->color2 : w->color});
            break;
        case DGX_GE_WIDGET_PICTURE:
            if (w->image)
                dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = w->image, .map = w->map, .x = (int16_t)(w->x + w->image->origin_x),
                                                           .y = (int16_t)(w->y + w->image->origin_y), .clip = box, .layer = layer, .color = w->color});
            break;
        case DGX_GE_WIDGET_LIST: {
            const int rows = rows_of(w), rh = rows > 0 ? w->h / rows : w->h, pick = w->value - w->min;
            if (pick >= 0 && pick < rows)
                dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(w->x, w->y + pick * rh, w->w, rh), .layer = layer, .flags = DGX_GE_ITEM_FILL,
                                                           .color = w->color2});
            if (w->text)
                dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.image = &w->text->image, .map = w->map, .x = (int16_t)w->x, .y = (int16_t)w->y, .clip = box,
                                                           .layer = layer, .color = w->color});
            break;
        }
        case DGX_GE_WIDGET_SWITCH:
        case DGX_GE_WIDGET_SLIDER: {
            if (w->actor && w->actor->image >= 0) {
                /* its own look: the game draws the actor where the widget is (dgx_ge_actor_x/y set by the game); here only the box when focused */
                if (focused) dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = box, .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = w->color2});
                break;
            }
            /* a plain look: a track with a knob at the value (a switch: at the left or the right) */
            const int span = w->max > w->min ? w->max - w->min : 1, knob = KNOB * u, at = (w->w - knob) * (w->value - w->min) / span;
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(w->x, w->y + w->h / 2 - u, w->w, 2 * u), .layer = layer, .flags = DGX_GE_ITEM_FILL,
                                                       .color = focused ? w->color2 : w->color});
            dgx_ge_scene_actor(scene, &(dgx_ge_item_t){.clip = dgx_ge_rect(w->x + at, w->y, knob, w->h), .layer = layer, .flags = DGX_GE_ITEM_FILL, .color = w->color2});
            break;
        }
        default:
            break;
        }
    }
}

int dgx_ge_widgets_hit(const dgx_ge_widgets_t *t, int x, int y)
{
    /* the deepest one under the point; of two as deep (a control over a row of a list), the smaller */
    int best = -1, depth = -1, area = 0;
    for (int i = 0; i < t->count; ++i) {
        const dgx_ge_widget_t *w = &t->widgets[i];
        if (!interactive(w) || hidden(t, i) || x < w->x || y < w->y || x >= w->x + w->w || y >= w->y + w->h) continue;
        int d = 0;
        for (int p = w->parent; p >= 0; p = t->widgets[p].parent) ++d;
        if (d > depth || (d == depth && w->w * w->h < area)) best = i, depth = d, area = w->w * w->h;
    }
    return best;
}

static void tell(dgx_ge_widget_t *w, int what)
{
    if (w->on) w->on(w, what, w->ctx);
}

void dgx_ge_widgets_focus(dgx_ge_widgets_t *t, int i)
{
    if (t->focus == i) return;
    if (t->focus >= 0) t->widgets[t->focus].flags &= (uint8_t)~DGX_GE_WIDGET_FOCUSED;
    t->focus = (int8_t)(i >= 0 && i < t->count ? i : -1);
    if (t->focus >= 0) {
        t->widgets[t->focus].flags |= DGX_GE_WIDGET_FOCUSED;
        tell(&t->widgets[t->focus], DGX_GE_WIDGET_FOCUS);
    }
}

/* a value set, within its bounds, the look following, the callback told when it changed */
static void set_value(const dgx_ge_widgets_t *t, dgx_ge_widget_t *w, int value)
{
    if (value < w->min) value = w->min;
    if (value > w->max) value = w->max;
    if (value == w->value) return;
    w->value = (int16_t)value;
    follow(t, w);
    tell(w, DGX_GE_WIDGET_CHANGED);
}

/* the point of a slider or a list in its own coordinates to a value */
static void point_to_value(const dgx_ge_widgets_t *t, dgx_ge_widget_t *w, int lx, int ly)
{
    if (w->kind == DGX_GE_WIDGET_SLIDER) {
        /* the knob's middle at the point: its travel is the width less the knob; a slider too narrow for a knob: the width itself */
        const int knob = KNOB * t->unit;
        int       span = w->w - knob, at = lx - knob / 2;
        if (span <= 0) span = w->w - 1, at = lx;
        at = at < 0 ? 0 : at > span ? span : at;
        const int step = w->step > 0 ? w->step : 1;
        int       v = span > 0 ? w->min + ((w->max - w->min) * at + span / 2) / span : w->min;
        v = w->min + (v - w->min + step / 2) / step * step;
        set_value(t, w, v);
    } else if (w->kind == DGX_GE_WIDGET_LIST) {
        const int rows = rows_of(w), rh = rows > 0 ? w->h / rows : w->h;
        if (rh > 0) set_value(t, w, w->min + ly / rh);
    }
}

bool dgx_ge_widgets_pointer(dgx_ge_widgets_t *t, const dgx_ge_pointer_t *p)
{
    if (p->pressed) {
        const int i = dgx_ge_widgets_hit(t, p->x, p->y);
        t->pressed = (int8_t)i;
        if (i < 0) return false;
        dgx_ge_widgets_focus(t, i);
        point_to_value(t, &t->widgets[i], p->x - t->widgets[i].x, p->y - t->widgets[i].y);
        return true;
    }
    if (t->pressed < 0) return false;
    dgx_ge_widget_t *w = &t->widgets[t->pressed];
    if (p->down) {
        if (w->kind == DGX_GE_WIDGET_SLIDER) point_to_value(t, w, p->x - w->x, p->y - w->y); /* the pointer dragged: the knob follows */
        return true;
    }
    if (p->released) {
        const bool inside = p->x >= w->x && p->y >= w->y && p->x < w->x + w->w && p->y < w->y + w->h;
        t->pressed = -1;
        if (!inside) return true;
        if (w->kind == DGX_GE_WIDGET_SWITCH) set_value(t, w, w->value ? 0 : 1);
        if (w->kind == DGX_GE_WIDGET_BUTTON || w->kind == DGX_GE_WIDGET_LIST) tell(w, DGX_GE_WIDGET_ACTIVATED);
        return true;
    }
    return false;
}

/* the next widget that may be focused after i, round the tree (step 1 or -1); -1 none */
static int next_focus(const dgx_ge_widgets_t *t, int i, int step)
{
    for (int k = 0; k < t->count; ++k) {
        i = (i + step + t->count) % t->count;
        if (interactive(&t->widgets[i]) && !hidden(t, i)) return i;
    }
    return -1;
}

bool dgx_ge_widgets_key(dgx_ge_widgets_t *t, dgx_ge_ui_t event)
{
    dgx_ge_widget_t *w = t->focus >= 0 ? &t->widgets[t->focus] : NULL;
    switch (event) {
    case DGX_GE_UI_NEXT:
    case DGX_GE_UI_PREV: {
        const int step = event == DGX_GE_UI_NEXT ? 1 : -1;
        /* in a list the rows first, then the focus goes on */
        if (w && w->kind == DGX_GE_WIDGET_LIST && w->value + step >= w->min && w->value + step <= w->max) {
            set_value(t, w, w->value + step);
            return true;
        }
        const int i = next_focus(t, t->focus < 0 ? (step > 0 ? -1 : 0) : t->focus, step);
        if (i < 0) return false;
        dgx_ge_widgets_focus(t, i);
        return true;
    }
    case DGX_GE_UI_MORE:
    case DGX_GE_UI_LESS:
        if (!w) return false;
        if (w->kind == DGX_GE_WIDGET_SLIDER) {
            set_value(t, w, w->value + (event == DGX_GE_UI_MORE ? 1 : -1) * (w->step > 0 ? w->step : 1));
            return true;
        }
        if (w->kind == DGX_GE_WIDGET_SWITCH) {
            set_value(t, w, event == DGX_GE_UI_MORE ? 1 : 0);
            return true;
        }
        return false;
    case DGX_GE_UI_ACTIVATE:
        if (!w) return false;
        if (w->kind == DGX_GE_WIDGET_SWITCH) set_value(t, w, w->value ? 0 : 1);
        else if (w->kind == DGX_GE_WIDGET_BUTTON || w->kind == DGX_GE_WIDGET_LIST) tell(w, DGX_GE_WIDGET_ACTIVATED);
        else return false;
        return true;
    default:
        return false;
    }
}
