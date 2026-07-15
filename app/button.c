#include "button.h"

#include "FreeRTOS.h"
#include "task.h"

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#define BUTTON_PORT        GPIOB
#define BUTTON_PIN         GPIO12
#define BUTTON_EXTI        EXTI12
#define BUTTON_DEBOUNCE_MS 50U

/*
 * Variables que toca la interrupción.
 * La ISR solo marca que hubo un cambio y guarda el estado leído.
 */
static volatile bool g_raw_event_pending = false;
static volatile bool g_raw_button_state = false;

/*
 * Variables procesadas desde la tarea.
 */
static bool g_button_pressed = false;
static bool g_last_reported_state = false;
static TickType_t g_last_event_tick = 0U;
static uint32_t g_button_event_count = 0U;

void button_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_AFIO);

    /*
     * PB12 como entrada con pull-down.
     * Botón suelto   -> PB12 = 0
     * Botón apretado -> PB12 = 1
     *
     * Aunque usamos pull-down interno, se mantiene también la resistencia
     * externa de 10k a GND como pide la consigna.
     */
    gpio_set_mode(BUTTON_PORT,
                  GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN,
                  BUTTON_PIN);

    gpio_clear(BUTTON_PORT, BUTTON_PIN);

    /*
     * Configuración de EXTI12 sobre PB12.
     * Se detectan ambos flancos:
     * - flanco de subida: botón presionado
     * - flanco de bajada: botón liberado
     */
    exti_select_source(BUTTON_EXTI, BUTTON_PORT);
    exti_set_trigger(BUTTON_EXTI, EXTI_TRIGGER_BOTH);
    exti_reset_request(BUTTON_EXTI);
    exti_enable_request(BUTTON_EXTI);

    nvic_enable_irq(NVIC_EXTI15_10_IRQ);

    g_raw_event_pending = false;
    g_raw_button_state = false;
    g_button_pressed = false;
    g_last_reported_state = false;
    g_last_event_tick = xTaskGetTickCount();
    g_button_event_count = 0U;
}

bool button_get_event(bool *pressed)
{
    bool raw_pending = false;
    bool raw_state = false;
    TickType_t now;

    /*
     * Copio y limpio el evento pendiente de forma protegida.
     */
    taskENTER_CRITICAL();

    if (g_raw_event_pending) {
        g_raw_event_pending = false;
        raw_pending = true;
        raw_state = g_raw_button_state;
    }

    taskEXIT_CRITICAL();

    if (!raw_pending) {
        return false;
    }

    now = xTaskGetTickCount();

    /*
     * Antirrebote por tiempo.
     * Si el cambio llegó demasiado cerca del anterior, se ignora.
     */
    if ((now - g_last_event_tick) < pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
        return false;
    }

    g_last_event_tick = now;

    /*
     * Si el estado no cambió respecto al último reportado,
     * no genero un nuevo evento.
     */
    if (raw_state == g_last_reported_state) {
        return false;
    }

    g_last_reported_state = raw_state;
    g_button_pressed = raw_state;
    g_button_event_count++;

    if (pressed != NULL) {
        *pressed = g_button_pressed;
    }

    return true;
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
 * ISR de EXTI15_10.
 * PB12 usa EXTI12, que comparte interrupción con EXTI10 a EXTI15.
 *
 * Importante:
 * La ISR no manda tramas, no usa colas y no hace debounce.
 * Solo guarda el estado del pin y deja una bandera pendiente.
 */
void exti15_10_isr(void)
{
    if (exti_get_flag_status(BUTTON_EXTI)) {
        exti_reset_request(BUTTON_EXTI);

        g_raw_button_state = (gpio_get(BUTTON_PORT, BUTTON_PIN) == 0U);         // Revisa si el botón no está presionado
        g_raw_event_pending = true;
    }
}