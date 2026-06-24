#include "button.h"

#include "FreeRTOS.h"
#include "task.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

/*
 * Botón de emergencia:
 * PB12 con pull-down externo.
 *
 * Sin presionar: PB12 = 0
 * Presionado:    PB12 = 1
 */
#define BUTTON_PORT        GPIOB
#define BUTTON_PIN         GPIO12
#define BUTTON_EXTI        EXTI12
#define BUTTON_DEBOUNCE_MS 50U

static volatile bool g_button_pressed = false;
static volatile bool g_button_event_pending = false;
static volatile bool g_button_event_state = false;
static volatile uint32_t g_button_event_count = 0U;
static volatile TickType_t g_button_last_tick = 0U;

void button_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_AFIO);

    /*
     * PB12 como entrada con pull-down.
     * Además del pull-down externo, se activa el pull-down interno.
     */
    gpio_set_mode(BUTTON_PORT,
                  GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN,
                  BUTTON_PIN);

    gpio_clear(BUTTON_PORT, BUTTON_PIN);

    /*
     * EXTI12 asociado a PB12.
     * Se usan ambos flancos para detectar presión y liberación.
     */
    exti_select_source(BUTTON_EXTI, BUTTON_PORT);
    exti_set_trigger(BUTTON_EXTI, EXTI_TRIGGER_BOTH);
    exti_reset_request(BUTTON_EXTI);
    exti_enable_request(BUTTON_EXTI);

    nvic_enable_irq(NVIC_EXTI15_10_IRQ);
}

bool button_get_event(bool *pressed)
{
    bool has_event = false;

    taskENTER_CRITICAL();

    if (g_button_event_pending) {
        g_button_event_pending = false;

        if (pressed != NULL) {
            *pressed = g_button_event_state;
        }

        has_event = true;
    }

    taskEXIT_CRITICAL();

    return has_event;
}

bool button_is_pressed(void)
{
    return g_button_pressed;
}

uint32_t button_get_event_count(void)
{
    return g_button_event_count;
}

/*
 * ISR para EXTI10 a EXTI15.
 * Como usamos PB12, entra por exti15_10_isr().
 */
void exti15_10_isr(void)
{
    if (exti_get_flag_status(BUTTON_EXTI)) {
        TickType_t now = xTaskGetTickCountFromISR();

        exti_reset_request(BUTTON_EXTI);

        /*
         * Antirrebote simple: ignora cambios demasiado cercanos.
         */
        if ((now - g_button_last_tick) >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
            g_button_last_tick = now;

            g_button_pressed = (gpio_get(BUTTON_PORT, BUTTON_PIN) != 0U);
            g_button_event_state = g_button_pressed;
            g_button_event_pending = true;
            g_button_event_count++;
        }
    }
}