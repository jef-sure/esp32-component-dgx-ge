#include "dgx_ge_input.h"

void dgx_ge_input_update(dgx_ge_input_t *input, uint32_t held)
{
    input->spent &= held;
    held &= ~input->spent;
    input->pressed  = held & ~input->held;
    input->released = input->held & ~held;
    input->held     = held;
}

void dgx_ge_input_spend(dgx_ge_input_t *input, uint32_t keys)
{
    keys &= input->held | input->pressed;
    input->spent |= keys;
    input->held &= ~keys, input->pressed &= ~keys, input->released &= ~keys;
}

uint32_t dgx_ge_input_scripted(const dgx_ge_input_step_t *steps, int count, uint32_t since_ms)
{
    uint32_t held = 0;
    for (int i = 0; i < count && steps[i].at_ms <= since_ms; ++i) held = steps[i].held;
    return held;
}

void dgx_ge_input_pointer(dgx_ge_input_t *input, int x, int y, bool down)
{
    dgx_ge_pointer_t *p = &input->pointer;
    p->pressed  = down && !p->down;
    p->released = !down && p->down;
    p->down     = down;
    p->x = (int16_t)x, p->y = (int16_t)y;
}
