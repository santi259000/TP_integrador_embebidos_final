#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "actuators.h"

/*
 * LED interno de la Blue Pill.
 * Se conserva como un indicador auxiliar del sistema.
 */
#define STATUS_LED_PORT GPIOC
#define STATUS_LED_PIN  GPIO13

#define SYS_FREQ_HZ        72000000U

/*
 * Variante 1 (la que elegimos):
 * LED verde en PA1 -> estado normal.
 * LED rojo en PA2  -> emergencia.
 */

 //Configuración de los pines y puertos para cada led 

#define GREEN_LED_PORT     GPIOA
#define GREEN_LED_PIN      GPIO1

#define RED_LED_PORT       GPIOA
#define RED_LED_PIN        GPIO2

/*
 * Buzzer pasivo con PWM:
 * PB0 corresponde al canal TIM3_CH3.
 * Al ser un buzzer pasivo, necesita una señal periódica para sonar.
 */
#define BUZZER_PORT        GPIOB
#define BUZZER_PIN         GPIO0
#define BUZZER_TIMER       TIM3
#define BUZZER_OC          TIM_OC3
#define BUZZER_PERIOD      999U
#define BUZZER_FREQ_HZ     2000U
#define BUZZER_PRESCALER   ((SYS_FREQ_HZ / (BUZZER_FREQ_HZ * (BUZZER_PERIOD + 1U))) - 1U)

/*
 * Destello LED rojo:
 * La consigna pide 4 Hz con 50 % de duty.
 *
 * f = 4 Hz -> T = 250 ms
 * 50 % duty -> 125 ms encendido y 125 ms apagado.
 */
#define RED_BLINK_HALF_PERIOD_MS 125U

static bool g_emergency_active = false;
static bool g_red_led_state = false;
static TickType_t g_last_blink_tick = 0U;

static void leds_setup(void)
{
    /*
     * Se habilita GPIOA para los LED externos.
     */
    rcc_periph_clock_enable(RCC_GPIOA);

    /*
     * PA1 y PA2 como salidas digitales push-pull.
     * PA1: LED verde.
     * PA2: LED rojo.
     */
    gpio_set_mode(GREEN_LED_PORT,
                  GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL,
                  GREEN_LED_PIN);

    gpio_set_mode(RED_LED_PORT,
                  GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL,
                  RED_LED_PIN);

    /*
     * Estado inicial seguro:
     * verde encendido, rojo apagado.
     */
    gpio_set(GREEN_LED_PORT, GREEN_LED_PIN);
    gpio_clear(RED_LED_PORT, RED_LED_PIN);
}

static void buzzer_pwm_setup(void)
{
    /*
     * Se habilitan los clocks del puerto GPIOB y del timer TIM3.
     */
    rcc_periph_clock_enable(RCC_GPIOB);
    rcc_periph_clock_enable(RCC_TIM3);

    /*
     * PB0 se configura como salida alternativa para TIM3_CH3.
     * El buzzer pasivo se excita con una señal PWM.
     */
    gpio_set_mode(BUZZER_PORT,
                  GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
                  BUZZER_PIN);

    timer_disable_counter(BUZZER_TIMER);

    /*
     * Configuración de frecuencia para el buzzer:
     *
     * f_PWM = SYS_FREQ / ((PSC + 1) * (ARR + 1))
     *
     * Con SYS_FREQ = 72 MHz, ARR = 999 y f_PWM = 2 kHz:
     * PSC = 35.
     */
    timer_set_prescaler(BUZZER_TIMER, BUZZER_PRESCALER);
    timer_set_period(BUZZER_TIMER, BUZZER_PERIOD);

    /*
     * Canal TIM3_CH3 en modo PWM1.
     */
    timer_set_oc_mode(BUZZER_TIMER, BUZZER_OC, TIM_OCM_PWM1);
    timer_enable_oc_output(BUZZER_TIMER, BUZZER_OC);

    /*
     * Duty inicial en 0: buzzer apagado al iniciar.
     */
    timer_set_oc_value(BUZZER_TIMER, BUZZER_OC, 0);

    timer_enable_counter(BUZZER_TIMER);
}

void actuators_init(void)
{
    /*
     * Inicialización del LED interno PC13.
     * Se mantiene como indicador auxiliar del firmware.
     */
    rcc_periph_clock_enable(RCC_GPIOC);

    gpio_set_mode(STATUS_LED_PORT,
                  GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_PUSHPULL,
                  STATUS_LED_PIN);

    /*
     * En la Blue Pill, PC13 suele ser activo en bajo.
     * Se deja apagado inicialmente.
     */
    gpio_set(STATUS_LED_PORT, STATUS_LED_PIN);

    /*
     * Inicialización de salidas de Variante 1:
     * - LED verde PA1.
     * - LED rojo PA2.
     * - Buzzer pasivo PB0 con PWM.
     */
    leds_setup();
    buzzer_pwm_setup();

    actuators_set_normal();
}

/*
 * Función conservada de la Etapa 2.
 * En la Variante 1, el ADC se usa como health check (analiza el estado si sigue funcionando todo como corresponde) y se reporta por UART.
 * Por eso esta función ya no modifica directamente LED/buzzer.
 */
void actuators_update_from_adc(uint16_t adc_value)
{
    (void) adc_value;
}

void actuators_set_normal(void)
{
    g_emergency_active = false;
    g_red_led_state = false;

    /*
     * Estado normal:
     * - LED verde encendido.
     * - LED rojo apagado.
     * - Buzzer apagado.
     */
    gpio_set(GREEN_LED_PORT, GREEN_LED_PIN);
    gpio_clear(RED_LED_PORT, RED_LED_PIN);
    actuators_buzzer_off();
}

void actuators_set_emergency(void)
{
    g_emergency_active = true;
    g_red_led_state = true;
    g_last_blink_tick = xTaskGetTickCount();

    /*
     * Estado emergencia:
     * - LED verde apagado.
     * - LED rojo comienza encendido y luego destella.
     * - Buzzer encendido.
     */
    gpio_clear(GREEN_LED_PORT, GREEN_LED_PIN);
    gpio_set(RED_LED_PORT, RED_LED_PIN);
    actuators_buzzer_on();
}

void actuators_update_emergency_blink(void)
{
    TickType_t now;

    if (!g_emergency_active) {
        return;
    }

    now = xTaskGetTickCount();

    /*
     * Destello de 4 Hz con 50 % de duty:
     * cambia de estado cada 125 ms.
     */
    if ((now - g_last_blink_tick) >= pdMS_TO_TICKS(RED_BLINK_HALF_PERIOD_MS)) {
        g_last_blink_tick = now;

        g_red_led_state = !g_red_led_state;

        if (g_red_led_state) {
            gpio_set(RED_LED_PORT, RED_LED_PIN);
        } else {
            gpio_clear(RED_LED_PORT, RED_LED_PIN);
        }
    }
}

void actuators_buzzer_on(void)
{
    /*
     * Duty 50 % para generar una especie de tono continuo en el buzzer pasivo.
     */
    timer_set_oc_value(BUZZER_TIMER, BUZZER_OC, BUZZER_PERIOD / 2U);
}

void actuators_buzzer_off(void)
{
    /*
     * Duty 0: buzzer apagado.
     */
    timer_set_oc_value(BUZZER_TIMER, BUZZER_OC, 0);
}

void actuators_apply_command(const actuator_command_t *command)
{
    if (command == NULL) {
        return;
    }

    /* --- Comandos de Emergencia de la Variante 1 --- */
    if (strcmp(command->target, "sys") == 0) {
        if (strcmp(command->action, "em_on") == 0) {
            actuators_set_emergency();
        } else if (strcmp(command->action, "em_off") == 0) {
            actuators_set_normal();
        }
        return; 
    }

    /*
     * Esta función se conserva para compatibilidad con la lógica previa del TP5.
     * Permite controlar el LED interno PC13 con comandos led=on/off/toggle.
     */
    if (strcmp(command->target, "led") != 0) {
        return;
    }

    if (strcmp(command->action, "on") == 0) {
        gpio_clear(STATUS_LED_PORT, STATUS_LED_PIN);
        return;
    }

    if (strcmp(command->action, "off") == 0) {
        gpio_set(STATUS_LED_PORT, STATUS_LED_PIN);
        return;
    }

    if (strcmp(command->action, "toggle") == 0) {
        gpio_toggle(STATUS_LED_PORT, STATUS_LED_PIN);
    }
}