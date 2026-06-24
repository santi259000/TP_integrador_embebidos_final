#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <stdint.h>

/*
 * Tipo de comando para controlar actuadores.
 * Se conserva del TP5 para compatibilidad con comandos como:
 * led=on, led=off y led=toggle.
 *
 * El archivo actuators.c usa los campos:
 * - target: indica qué actuador se quiere controlar, por ejemplo "led".
 * - action: indica la acción a realizar, por ejemplo "on", "off" o "toggle".
 */
typedef struct {
    char target[8];
    char action[16];
} actuator_command_t;

/*
 * Inicializa las salidas del sistema:
 * - LED interno PC13.
 * - PWM para LED externo.
 * - PWM para buzzer pasivo.
 */
void actuators_init(void);

/*
 * Actualiza los actuadores según el valor leído por ADC:
 * - Cambia el duty cycle del LED PWM.
 * - Activa o apaga el buzzer según el umbral definido.
 */
void actuators_update_from_adc(uint16_t adc_value);

/*
 * Función conservada del TP5 para compatibilidad con comandos.
 * Permite controlar el LED interno mediante comandos led=on/off/toggle.
 */
void actuators_apply_command(const actuator_command_t *command);

#endif