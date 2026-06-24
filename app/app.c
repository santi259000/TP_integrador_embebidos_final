#include "app.h"

#include <stdio.h>

#include "sensors.h"
#include "tasks.h"

void app_init(void)
{
}

/*
 * Etapa 1 - Eco modificado:
 * Si el parser entregó un mensaje válido, la aplicación responde ACK:ok.
 * No importa si el payload es ping, led=on, status?, etc.
 */
bool app_process_message(const protocol_message_t *input,
                         protocol_message_t *response)
{
    if ((input == NULL) || (response == NULL)) {
        return false;
    }

    return protocol_message_set(response, PROTOCOL_TYPE_ACK, "ok");
}

/*
 * Mensaje de estado del sistema.
 * Se puede usar en etapas posteriores o para depuración.
 */
bool app_build_status_message(protocol_message_t *message)
{
    char payload[PROTOCOL_MAX_PAYLOAD_LENGTH + 1U];

    if (message == NULL) {
        return false;
    }

    snprintf(payload,
             sizeof(payload),
             "pb=%lu,pm=%lu,pe=%lu",
             (unsigned long)tasks_get_parser_byte_count(),
             (unsigned long)tasks_get_parser_message_count(),
             (unsigned long)tasks_get_parser_error_count());

    return protocol_message_set(message, PROTOCOL_TYPE_STS, payload);
}

/*
 * Mensaje de telemetría para Etapa 2.
 * Arma un DAT con el valor del ADC.
 */
bool app_build_telemetry_message(protocol_message_t *message,
                                 uint32_t sequence)
{
    char payload[PROTOCOL_MAX_PAYLOAD_LENGTH + 1U];

    if (message == NULL) {
        return false;
    }

    if (!sensors_build_telemetry_payload(payload, sizeof(payload), sequence)) {
        return false;
    }

    return protocol_message_set(message, PROTOCOL_TYPE_DAT, payload);
}