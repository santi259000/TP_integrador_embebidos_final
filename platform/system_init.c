#include <libopencm3/stm32/rcc.h>

#include "system_init.h"

void system_init_board(void)
{
    rcc_periph_clock_enable(RCC_AFIO);
}
