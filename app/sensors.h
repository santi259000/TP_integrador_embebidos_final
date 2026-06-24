#ifndef SENSORS_H
#define SENSORS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Inicializa el módulo de sensores.
 * En esta etapa configura el ADC1 para leer el potenciómetro conectado en PA0.
 */
void sensors_init(void);

/*
 * Realiza una conversión ADC en PA0 y devuelve el valor leído.
 * El valor esperado está entre 0 y 4095, porque el ADC es de 12 bits.
 */
uint16_t sensors_read_adc(void);

/*
 * Devuelve la última lectura ADC guardada.
 * Se usa para actualizar los actuadores con el mismo valor que se envía por UART.
 */
uint16_t sensors_get_last_adc(void);

/*
 * Construye el payload de telemetría con el formato adc=NNNN.
 * Luego ese payload será encapsulado en una trama DAT por el protocolo.
 */
bool sensors_build_telemetry_payload(char *buffer, size_t buffer_size, uint32_t sequence);

#endif