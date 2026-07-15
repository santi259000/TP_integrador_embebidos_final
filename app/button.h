#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Se inicializa el pulsador de emergencia conectado al PB12.
 * Se configura como entrada digital con EXTI y antirrebote por tiempo.
 */
void button_init(void);

/*
 * Devuelve true si se detectó un cambio válido en el estado del botón.
 * La función limpia el evento después de leerlo.
 */
bool button_get_event(bool *pressed);

/*
 * Devuelve el estado actual del botón:
 * true  = presionado
 * false = liberado
 */
bool button_is_pressed(void);

/*
 * Devuelve la cantidad de eventos válidos detectados.
 * Sirve para depuración o para mostrar evidencia en el informe.
 */
uint32_t button_get_event_count(void);

#endif