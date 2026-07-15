#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "sensors.h"

/*
 * Variable global interna para guardar la última lectura del ADC (potenciómetro).
 * Esto permite que otros módulos consulten el último valor medido
 * sin tener que iniciar una nueva conversión.
 */
static uint16_t g_last_adc_value = 0U;

void sensors_init(void)
{
    /*
     * Se habilitan los clocks necesarios:
     * - GPIOA: porque PA0 se usa como entrada analógica.
     * - ADC1: porque se utiliza el conversor analógico-digital 1.
     */
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_ADC1);

    /*
     * PA0 se configura como entrada analógica.
     * En este pin se conecta el punto medio del potenciómetro.
     * El ADC leerá una tensión entre 0 V y 3.3 V.
     */
    gpio_set_mode(GPIOA,
                  GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_ANALOG,
                  GPIO0);

    /*
     * Se configura el clock del ADC.
     * Con el sistema a 72 MHz, se divide PCLK2 por 6:
     * ADCCLK = 72 MHz / 6 = 12 MHz.
     */
    rcc_set_adcpre(RCC_CFGR_ADCPRE_PCLK2_DIV6);

    /*
     * Configuración básica del ADC:
     * - Se apaga antes de configurar.
     * - No se usa modo scan, porque se lee un solo canal.
     * - Se usa conversión simple.
     * - El resultado se alinea a la derecha.
     * - Se define un tiempo de muestreo alto para una lectura estable.
     */
    adc_power_off(ADC1);
    adc_disable_scan_mode(ADC1);
    adc_set_single_conversion_mode(ADC1);
    adc_set_right_aligned(ADC1);
    adc_set_sample_time(ADC1, ADC_CHANNEL0, ADC_SMPR_SMP_239DOT5CYC);

    /*
     * Se enciende el ADC y se calibra.
     * La calibración ayuda a mejorar la precisión de la lectura.
     */
    adc_power_on(ADC1);

    adc_reset_calibration(ADC1);
    adc_calibrate(ADC1);
}

uint16_t sensors_read_adc(void)
{
    uint8_t channel_array[1] = {ADC_CHANNEL0};

    /*
     * Se selecciona el canal ADC1_IN0, correspondiente al pin PA0.
     * La secuencia tiene un solo canal porque solo se lee el potenciómetro.
     */
    adc_set_regular_sequence(ADC1, 1, channel_array);

    /*
     * Se inicia la conversión y se espera hasta que el ADC indique fin de conversión.
     * El resultado esperado está entre 0 y 4095, ya que el ADC es de 12 bits.
     */
    adc_start_conversion_direct(ADC1);

    while (!adc_eoc(ADC1)) {
    }

    /*
     * Se guarda y devuelve la última lectura.
     */
    g_last_adc_value = adc_read_regular(ADC1);
    return g_last_adc_value;
}

uint16_t sensors_get_last_adc(void)
{
    /*
     * Devuelve la última lectura tomada por sensors_read_adc().
     * Se usa para actualizar actuadores con el mismo valor que se envía por UART.
     */
    return g_last_adc_value;
}

bool sensors_build_telemetry_payload(char *buffer, size_t buffer_size, uint32_t sequence)
{
    int written;
    uint16_t adc_value;
    (void) sequence;

    if ((buffer == NULL) || (buffer_size == 0U)) {
        return false;
    }

    /*
     * Se lee el potenciómetro mediante ADC.
     */
    adc_value = sensors_read_adc();

    /*
     * Se arma el payload de telemetría.
     * Se usa ancho fijo de 4 dígitos para cumplir con el formato adc=NNNN.
     */
    written = snprintf(buffer, buffer_size, "adc=%04u", (unsigned int) adc_value);

    return (written > 0) && ((size_t) written < buffer_size);
}