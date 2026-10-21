/* Input: buttons as they were, the axes of a pad with the dead zone off, the bridges between axes and buttons, the pointer by itself and from dgx-di. */
#include <stdlib.h>

#include "check.h"
#include "dgx_ge_input.h"
#if __has_include("dgx_di.h")
#include "dgx_ge_input_di.h"
#endif

static int16_t pad[DGX_GE_AXES];

static void axes(dgx_ge_input_t *in, int lx, int ly, int rx, int ry, int lt, int rt)
{
    pad[DGX_GE_AXIS_LX] = (int16_t)lx, pad[DGX_GE_AXIS_LY] = (int16_t)ly, pad[DGX_GE_AXIS_RX] = (int16_t)rx;
    pad[DGX_GE_AXIS_RY] = (int16_t)ry, pad[DGX_GE_AXIS_LT] = (int16_t)lt, pad[DGX_GE_AXIS_RT] = (int16_t)rt;
    dgx_ge_input_axes(in, pad);
}

int main(void)
{
    /* one player's input is no bigger than said (with the pointer's swipe) */
    CHECK(sizeof(dgx_ge_input_t) == 44);
    /* the seven buttons stay where they were; A and BACK are their other names; the new ones go on, none twice */
    CHECK(DGX_GE_KEY_ACTION == 1u << 4 && DGX_GE_KEY_SELECT == 1u << 6 && DGX_GE_KEY_A == DGX_GE_KEY_ACTION && DGX_GE_KEY_BACK == DGX_GE_KEY_SELECT);
    CHECK(DGX_GE_KEY_B == 1u << 7 && DGX_GE_KEY_R3 == 1u << 16);

    /* no bridge: buttons as before — pressed, released, a spent one unseen until let go */
    dgx_ge_input_t in = {0};
    dgx_ge_input_update(&in, DGX_GE_KEY_LEFT | DGX_GE_KEY_B);
    CHECK(in.held == (DGX_GE_KEY_LEFT | DGX_GE_KEY_B) && in.pressed == in.held && !in.released);
    dgx_ge_input_spend(&in, DGX_GE_KEY_B);
    dgx_ge_input_update(&in, DGX_GE_KEY_B);
    CHECK(!in.held && in.released == DGX_GE_KEY_LEFT && !in.pressed);
    dgx_ge_input_update(&in, 0);
    dgx_ge_input_update(&in, DGX_GE_KEY_B);
    CHECK(in.pressed == DGX_GE_KEY_B);
    /* the axes go nowhere without a bridge */
    axes(&in, 32767, 0, 0, 0, 32767, 0);
    dgx_ge_input_update(&in, 0);
    CHECK(!in.held && in.axes[DGX_GE_AXIS_LX] == 32767 && in.axes[DGX_GE_AXIS_LT] == 32767);

    /* the dead zone, the default one (2621): inside nothing, the whole way the whole range */
    in = (dgx_ge_input_t){0};
    axes(&in, 1000, -2000, 2000, 1000, 2000, 32767);
    CHECK(!in.axes[DGX_GE_AXIS_LX] && !in.axes[DGX_GE_AXIS_LY] && !in.axes[DGX_GE_AXIS_RX] && !in.axes[DGX_GE_AXIS_RY]);
    CHECK(!in.axes[DGX_GE_AXIS_LT] && in.axes[DGX_GE_AXIS_RT] == 32767);
    /* round, not by axis: 2000 and 2000 are 2828 long, past it; each alone would be inside */
    axes(&in, 2000, 2000, 32767, 0, -500, 0);
    CHECK(in.axes[DGX_GE_AXIS_LX] > 0 && in.axes[DGX_GE_AXIS_LX] == in.axes[DGX_GE_AXIS_LY] && in.axes[DGX_GE_AXIS_LX] < 500);
    CHECK(in.axes[DGX_GE_AXIS_RX] == 32767 && !in.axes[DGX_GE_AXIS_RY] && !in.axes[DGX_GE_AXIS_LT]);
    /* past it the rest is stretched: just past the edge is near 0, halfway between edge and end is half */
    axes(&in, 2621 + (32767 - 2621) / 2, 0, 2700, 0, 0, 0);
    CHECK(abs(in.axes[DGX_GE_AXIS_LX] - 16384) <= 1 && in.axes[DGX_GE_AXIS_RX] > 0 && in.axes[DGX_GE_AXIS_RX] < 100);
    /* a square stick in its corner: no farther than the circle, along the diagonal 23170 each */
    axes(&in, 32767, 32767, -32767, 32767, 0, 0);
    CHECK(abs(in.axes[DGX_GE_AXIS_LX] - 23170) <= 1 && abs(in.axes[DGX_GE_AXIS_LY] - 23170) <= 1 && abs(in.axes[DGX_GE_AXIS_RX] + 23170) <= 1);
    /* a game's own dead zone: 1 is none to speak of */
    in.dead = 1;
    axes(&in, 100, 0, 0, 0, 100, 0);
    CHECK(abs(in.axes[DGX_GE_AXIS_LX] - 100) <= 1 && abs(in.axes[DGX_GE_AXIS_LT] - 100) <= 1);

    /* the stick into the arrows: from half the travel on, kept down to three eighths, then let go */
    in = (dgx_ge_input_t){.dead = 1, .bridge = DGX_GE_BRIDGE_STICK_KEYS};
    axes(&in, 15000, 0, 0, 0, 0, 0);
    dgx_ge_input_update(&in, 0);
    CHECK(!in.held);
    axes(&in, 20000, -20000, 0, 0, 0, 0);
    dgx_ge_input_update(&in, DGX_GE_KEY_A);
    CHECK(in.held == (DGX_GE_KEY_RIGHT | DGX_GE_KEY_UP | DGX_GE_KEY_A) && in.pressed == in.held);
    axes(&in, 14000, 0, 0, 0, 0, 0);
    dgx_ge_input_update(&in, DGX_GE_KEY_A);
    CHECK(in.held == (DGX_GE_KEY_RIGHT | DGX_GE_KEY_A) && in.released == DGX_GE_KEY_UP && !in.pressed);
    axes(&in, 12000, 0, 0, 0, 0, 0);
    dgx_ge_input_update(&in, 0);
    CHECK(!in.held && in.released == (DGX_GE_KEY_RIGHT | DGX_GE_KEY_A));
    axes(&in, 14000, 0, 0, 0, 0, 0);
    dgx_ge_input_update(&in, 0);
    CHECK(!in.held);
    /* left and down by the same */
    axes(&in, -20000, 20000, 0, 0, 0, 0);
    dgx_ge_input_update(&in, 0);
    CHECK(in.held == (DGX_GE_KEY_LEFT | DGX_GE_KEY_DOWN));
    /* a menu takes the arrow the stick holds: spent until the stick lets it go */
    dgx_ge_input_spend(&in, DGX_GE_KEY_LEFT);
    axes(&in, -20000, 20000, 0, 0, 0, 0);
    dgx_ge_input_update(&in, 0);
    CHECK(in.held == DGX_GE_KEY_DOWN && !in.pressed);
    axes(&in, 0, 0, 0, 0, 0, 0);
    dgx_ge_input_update(&in, 0);
    axes(&in, -20000, 0, 0, 0, 0, 0);
    dgx_ge_input_update(&in, 0);
    CHECK(in.pressed == DGX_GE_KEY_LEFT);

    /* the triggers into LT, RT */
    in = (dgx_ge_input_t){.dead = 1, .bridge = DGX_GE_BRIDGE_TRIGGER_KEYS};
    axes(&in, 32767, 0, 0, 0, 10000, 20000);
    dgx_ge_input_update(&in, 0);
    CHECK(in.held == DGX_GE_KEY_RT);

    /* the arrows into the stick: all the way, the diagonal on the circle; let go, it rests though no axes come */
    in = (dgx_ge_input_t){.bridge = DGX_GE_BRIDGE_KEYS_STICK};
    dgx_ge_input_update(&in, DGX_GE_KEY_RIGHT);
    CHECK(in.axes[DGX_GE_AXIS_LX] == 32767 && !in.axes[DGX_GE_AXIS_LY] && in.held == DGX_GE_KEY_RIGHT);
    dgx_ge_input_update(&in, DGX_GE_KEY_LEFT | DGX_GE_KEY_DOWN);
    CHECK(in.axes[DGX_GE_AXIS_LX] == -23170 && in.axes[DGX_GE_AXIS_LY] == 23170);
    dgx_ge_input_update(&in, 0);
    CHECK(!in.axes[DGX_GE_AXIS_LX] && !in.axes[DGX_GE_AXIS_LY]);
    /* the stick itself moved: the arrows do not take it over */
    axes(&in, 0, -20000, 0, 0, 0, 0);
    dgx_ge_input_update(&in, DGX_GE_KEY_RIGHT);
    CHECK(!in.axes[DGX_GE_AXIS_LX] && in.axes[DGX_GE_AXIS_LY] < -15000);
    /* the platform's axes stay as it said last until it says again: the stick still there, then let go */
    dgx_ge_input_update(&in, 0);
    CHECK(in.axes[DGX_GE_AXIS_LY] < -15000);
    axes(&in, 0, 0, 0, 0, 0, 0);
    dgx_ge_input_update(&in, DGX_GE_KEY_UP);
    /* the platform's axes after the arrows': the platform's, not reset */
    CHECK(in.axes[DGX_GE_AXIS_LY] == -32767);
    axes(&in, 0, 20000, 0, 0, 0, 0);
    dgx_ge_input_update(&in, 0);
    CHECK(in.axes[DGX_GE_AXIS_LY] > 15000);

    /* both ways at once: an arrow moves the stick, the stick holds the arrow — the same button, once */
    in = (dgx_ge_input_t){.bridge = DGX_GE_BRIDGE_KEYS_STICK | DGX_GE_BRIDGE_STICK_KEYS};
    dgx_ge_input_update(&in, DGX_GE_KEY_UP);
    CHECK(in.held == DGX_GE_KEY_UP && in.axes[DGX_GE_AXIS_LY] == -32767);
    dgx_ge_input_update(&in, 0);
    CHECK(!in.held && in.released == DGX_GE_KEY_UP && !in.axes[DGX_GE_AXIS_LY]);
    /* the pointer by itself: where, pressed and released; no swipes */
    {
        dgx_ge_input_t e = {0};
        dgx_ge_input_pointer(&e, 100, 5, true);
        CHECK(e.pointer.pressed && e.pointer.down && e.pointer.x == 100 && e.pointer.y == 5);
        dgx_ge_input_pointer(&e, 100, 100, true);
        CHECK(!e.pointer.pressed && !e.pointer.edge && !e.pointer.swipe);
        dgx_ge_input_pointer(&e, 100, 100, false);
        CHECK(e.pointer.released && !e.pointer.down);
    }
#if __has_include("dgx_di.h")
    /* the pointer from dgx-di's touch: all it says, a swipe from an edge and a plain one too */
    {
        /* a display of 320x160 pixels over 320x160 mm: a swipe from an edge down within 9 pixels of it, away 15 */
        dgx_ge_input_t e      = {0};
        dgx_di_touch_t t      = {0};
        dgx_screen_t   screen = {.width = 320, .height = 160};
        dgx_di_touch_display(&t, &screen, 320, 160);
        dgx_di_touch_take(&t, 100, 5, true, 0), dgx_ge_input_take_di(&e, &t);
        CHECK(e.pointer.pressed && e.pointer.down && e.pointer.x == 100 && e.pointer.y == 5 && !e.pointer.edge);
        dgx_di_touch_take(&t, 102, 26, true, 20), dgx_ge_input_take_di(&e, &t);
        CHECK(e.pointer.edge == DGX_GE_EDGE_TOP && !e.pointer.pressed && e.pointer.x == 102 && e.pointer.y == 26);
        dgx_di_touch_take(&t, 102, 26, false, 40), dgx_ge_input_take_di(&e, &t);
        CHECK(e.pointer.released && !e.pointer.down && !e.pointer.edge);
        dgx_di_touch_take(&t, 100, 80, true, 60), dgx_di_touch_take(&t, 50, 80, true, 80), dgx_ge_input_take_di(&e, &t);
        CHECK(e.pointer.swipe == DGX_GE_SWIPE_LEFT && !e.pointer.edge);
    }
#else
    fprintf(stderr, "test_input: no DGX-DI beside the engine, its touch as the pointer not tested\n");
#endif
    CHECK_DONE();
}
