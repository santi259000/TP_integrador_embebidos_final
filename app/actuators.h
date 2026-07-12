#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <stdbool.h>
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
 * - LED verde en PA1.
 * - LED rojo en PA2.
 * - PWM para buzzer pasivo en PB0.
 */
void actuators_init(void);

/*
 * Función conservada de la Etapa 2.
 * Antes actualizaba el LED PWM y el buzzer según el ADC.
 * En la Variante 1, el ADC se usa principalmente como health check,
 * por lo que la lógica principal de actuadores pasa a depender
 * del estado normal/emergencia.
 */
void actuators_update_from_adc(uint16_t adc_value);

/*
 * Estado normal del sistema:
 * - LED verde encendido fijo.
 * - LED rojo apagado.
 * - Buzzer apagado.
 */
void actuators_set_normal(void);

/*
 * Estado de emergencia:
 * - LED verde apagado.
 * - LED rojo preparado para destellar.
 * - Buzzer encendido continuo.
 */
void actuators_set_emergency(void);

/*
 * Actualiza el destello del LED rojo durante emergencia.
 * Se debe llamar periódicamente desde una tarea.
 * La consigna pide destello de 4 Hz con 50 % de duty.
 */
void actuators_update_emergency_blink(void);

/*
 * Apaga el buzzer.
 * Sirve para salir del estado de emergencia o para estados seguros.
 */
void actuators_buzzer_off(void);

/*
 * Enciende el buzzer con tono continuo.
 * Se usa durante la emergencia.
 */
void actuators_buzzer_on(void);

/*
 * Función conservada del TP5 para compatibilidad con comandos.
 * Permite controlar el LED interno mediante comandos led=on/off/toggle.
 */
void actuators_apply_command(const actuator_command_t *command);

#endif