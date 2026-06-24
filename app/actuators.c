#include <stdint.h>
#include <string.h>

#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "actuators.h"

/*
 * LED interno de la Blue Pill.
 * Se conserva como indicador auxiliar del sistema.
 */
#define STATUS_LED_PORT GPIOC
#define STATUS_LED_PIN  GPIO13

#define SYS_FREQ_HZ        72000000U

/*
 * LED externo con PWM:
 * PA1 corresponde al canal TIM2_CH2.
 * Se utiliza para variar el brillo del LED según el valor leído por ADC.
 */
#define LED_PWM_PORT       GPIOA
#define LED_PWM_PIN        GPIO1
#define LED_PWM_TIMER      TIM2
#define LED_PWM_OC         TIM_OC2
#define LED_PWM_PERIOD     999U
#define LED_PWM_FREQ_HZ    1000U
#define LED_PWM_PRESCALER  ((SYS_FREQ_HZ / (LED_PWM_FREQ_HZ * (LED_PWM_PERIOD + 1U))) - 1U)

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

#define ADC_MAX_VALUE      4095U

/*
 * Umbral a partir del cual se activa el buzzer.
 * Se eligió 3000 como valor de prueba para que el buzzer suene
 * cuando el potenciómetro se encuentre en una zona alta.
 */
#define BUZZER_THRESHOLD   3000U

static void led_pwm_setup(void)
{
    /*
     * Se habilitan los clocks del puerto GPIOA y del timer TIM2.
     */
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_TIM2);

    /*
     * PA1 se configura como salida alternativa push-pull.
     * Esta configuración permite que el pin sea manejado por el periférico TIM2
     * en lugar de funcionar como una salida GPIO común.
     */
    gpio_set_mode(LED_PWM_PORT,
                  GPIO_MODE_OUTPUT_2_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
                  LED_PWM_PIN);

    timer_disable_counter(LED_PWM_TIMER);

    /*
     * Configuración de frecuencia PWM para el LED:
     *
     * f_PWM = SYS_FREQ / ((PSC + 1) * (ARR + 1))
     *
     * Con SYS_FREQ = 72 MHz, ARR = 999 y f_PWM = 1 kHz:
     * PSC = 71.
     */
    timer_set_prescaler(LED_PWM_TIMER, LED_PWM_PRESCALER);
    timer_set_period(LED_PWM_TIMER, LED_PWM_PERIOD);

    /*
     * Se configura el canal TIM2_CH2 en modo PWM1.
     * El valor de comparación define el duty cycle.
     */
    timer_set_oc_mode(LED_PWM_TIMER, LED_PWM_OC, TIM_OCM_PWM1);
    timer_enable_oc_output(LED_PWM_TIMER, LED_PWM_OC);

    /*
     * Duty inicial en 0: LED apagado al iniciar.
     */
    timer_set_oc_value(LED_PWM_TIMER, LED_PWM_OC, 0);

    timer_enable_counter(LED_PWM_TIMER);
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
     *
     * 2 kHz se encuentra dentro de un rango audible adecuado
     * para probar un buzzer pasivo.
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

    gpio_set(STATUS_LED_PORT, STATUS_LED_PIN);

    /*
     * Inicialización de las salidas PWM usadas en la Etapa 2.
     */
    led_pwm_setup();
    buzzer_pwm_setup();
}

void actuators_update_from_adc(uint16_t adc_value)
{
    uint32_t led_duty;

    /*
     * Protección por si el valor recibido supera el rango esperado del ADC.
     */
    if (adc_value > ADC_MAX_VALUE) {
        adc_value = ADC_MAX_VALUE;
    }

    /*
     * Mapeo del valor ADC al duty cycle del PWM del LED:
     *
     * ADC = 0    -> duty = 0
     * ADC = 2048 -> duty aproximado 50 %
     * ADC = 4095 -> duty máximo
     */
    led_duty = ((uint32_t) adc_value * LED_PWM_PERIOD) / ADC_MAX_VALUE;
    timer_set_oc_value(LED_PWM_TIMER, LED_PWM_OC, led_duty);

    /*
     * Control del buzzer pasivo.
     * Si el ADC supera el umbral, se aplica un duty de 50 %
     * para generar una señal cuadrada audible.
     * Si no supera el umbral, el duty se pone en 0 y el buzzer se apaga.
     */
    if (adc_value >= BUZZER_THRESHOLD) {
        timer_set_oc_value(BUZZER_TIMER, BUZZER_OC, BUZZER_PERIOD / 2U);
    } else {
        timer_set_oc_value(BUZZER_TIMER, BUZZER_OC, 0);
    }
}

void actuators_apply_command(const actuator_command_t *command)
{
    /*
     * Esta función se conserva para compatibilidad con la lógica previa del TP5.
     * En la Etapa 2 el control principal de actuadores se realiza desde
     * actuators_update_from_adc().
     */
    if (command == NULL) {
        return;
    }

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