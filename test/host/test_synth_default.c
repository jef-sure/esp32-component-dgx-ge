/* With no synthesizer said (no dgx_ge_synths), the build carries FM alone: the synthesizer of a song that names none. */
#include <stdio.h>

#include "dgx_ge_synth.h"

_Static_assert(DGX_GE_SYNTH_FM == 1 && DGX_GE_SYNTH_CHIP == 0 && DGX_GE_SYNTH_NES == 0, "none said: FM alone");

int main(void)
{
    printf("test_synth_default.c: ok\n");
    return 0;
}
