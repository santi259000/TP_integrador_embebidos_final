#include <libopencm3/stm32/rcc.h>

#include "app/actuators.h"
#include "app/app.h"
#include "app/sensors.h"
#include "app/tasks.h"
#include "drivers/uart_comm.h"
#include "platform/system_init.h"

int main(void)
{
    rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

    system_init_board();
    actuators_init();
    sensors_init();
    uart_comm_init();
    
    app_init();

    tasks_start();

    for (;;) {
    }
}

//Hola Santi
