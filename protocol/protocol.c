#include <string.h>

#include "protocol.h"
#include <stdio.h>
/*
 * En este archivo se lograron los siguientes objetivos:

 * - Construir tramas del tipo @LL:TTT:PAYLOAD:CC\n
 * - Calcular checksum XOR de LL:TTT:PAYLOAD
 * - Decodificar el body TTT:PAYLOAD
 *
 * Documentación útil:
 * - docs/PROTOCOL.md
 * - docs/FRAME_GENERATION_AND_PARSING.md

 Las funciones que se crearon y modificaron fueron:

- protocol_compute_checksum
- protocol_encode_frame
- protocol_decode_body




 */

const char *protocol_type_to_text(protocol_type_t type)
{
    switch (type)
    {
    case PROTOCOL_TYPE_CMD:
        return "CMD";
    case PROTOCOL_TYPE_DAT:
        return "DAT";
    case PROTOCOL_TYPE_EVT:
        return "EVT";
    case PROTOCOL_TYPE_STS:
        return "STS";
    case PROTOCOL_TYPE_ACK:
        return "ACK";
    case PROTOCOL_TYPE_ERR:
        return "ERR";
    default:
        return "INV";
    }
}

protocol_type_t protocol_type_from_text(const char *text)
{
    if (text == NULL)
    {
        return PROTOCOL_TYPE_INVALID;
    }

    if (strncmp(text, "CMD", PROTOCOL_TYPE_LENGTH) == 0)
    {
        return PROTOCOL_TYPE_CMD;
    }
    if (strncmp(text, "DAT", PROTOCOL_TYPE_LENGTH) == 0)
    {
        return PROTOCOL_TYPE_DAT;
    }
    if (strncmp(text, "EVT", PROTOCOL_TYPE_LENGTH) == 0)
    {
        return PROTOCOL_TYPE_EVT;
    }
    if (strncmp(text, "STS", PROTOCOL_TYPE_LENGTH) == 0)
    {
        return PROTOCOL_TYPE_STS;
    }
    if (strncmp(text, "ACK", PROTOCOL_TYPE_LENGTH) == 0)
    {
        return PROTOCOL_TYPE_ACK;
    }
    if (strncmp(text, "ERR", PROTOCOL_TYPE_LENGTH) == 0)
    {
        return PROTOCOL_TYPE_ERR;
    }

    return PROTOCOL_TYPE_INVALID;
}

bool protocol_message_set(protocol_message_t *message, protocol_type_t type, const char *payload)
{
    size_t payload_length;

    if ((message == NULL) || (payload == NULL))
    {
        return false;
    }

    payload_length = strlen(payload);
    if (payload_length > PROTOCOL_MAX_PAYLOAD_LENGTH)
    {
        return false;
    }

    message->type = type;
    message->payload_length = (uint8_t)payload_length;
    memcpy(message->payload, payload, payload_length + 1U);
    return true;
}

uint8_t protocol_compute_checksum(const char *data, size_t length)
{
    uint8_t cs = 0U;
    for (size_t i = 0U; i < length; i++)
    {
        cs ^= (uint8_t)data[i];
    }
    return cs;
}

bool protocol_encode_frame(const protocol_message_t *message, char *frame, size_t frame_size, size_t *frame_length)
{
    if (message == NULL || frame == NULL || frame_length == NULL)
    {
        return false;
    }

    // Convertir el enum (ej. PROTOCOL_TYPE_CMD) a texto ("CMD")
    const char *type_str = protocol_type_to_text(message->type);
    if (strncmp(type_str, "INV", 3) == 0)
    {
        return false; // Tipo inválido
    }

    // Crear el body = "TTT:PAYLOAD"
    char body[PROTOCOL_MAX_BODY_SIZE + 1];
    int body_len = snprintf(body, sizeof(body), "%s:%s", type_str, message->payload);
    if (body_len < 0 || (size_t)body_len > PROTOCOL_MAX_BODY_SIZE)
    {
        return false;
    }

    // Calcular LL en hexadecimal y armar "LL:TTT:PAYLOAD"
    char cs_input[PROTOCOL_MAX_FRAME_LENGTH + 1];
    int cs_bytes = snprintf(cs_input, sizeof(cs_input), "%02X:%s", (unsigned int)body_len, body);
    if (cs_bytes < 0 || (size_t)cs_bytes >= sizeof(cs_input))
    {
        return false;
    }

    // Calcular checksum sobre "LL:TTT:PAYLOAD"
    uint8_t cs = protocol_compute_checksum(cs_input, (size_t)cs_bytes);

    // Armar la trama final @LL:TTT:PAYLOAD:CC\n
    int written = snprintf(frame, frame_size, "@%s:%02X\n", cs_input, cs);
    if (written < 0 || (size_t)written >= frame_size)
    {
        return false;
    }

    // Guardar la longitud final para que la UART sepa cuántos bytes transmitir
    *frame_length = (size_t)written;

    return true;
}

bool protocol_decode_body(const char *body, uint8_t body_length, protocol_message_t *message)
{
    if (body == NULL || message == NULL || body_length < 4)
    {
        return false;
    }

    // Validar formato TTT:PAYLOAD (tiene que tener los dos puntos en la posición 3)
    if (body[3] != ':')
    {
        return false;
    }

    // Extraer los primeros 3 caracteres (TTT)
    char type_str[4];
    strncpy(type_str, body, 3);
    type_str[3] = '\0'; // Asegurar que sea un string válido en C

    // Convertir TTT a protocol_type_t usando la función que ya viene en el archivo
    protocol_type_t type = protocol_type_from_text(type_str);
    if (type == PROTOCOL_TYPE_INVALID)
    {
        return false;
    }

    // Copiar payload al struct message (lo que está después de los ':')
    const char *payload_str = &body[4];

    // Usamos la función del proyecto para poblar la estructura de forma segura
    return protocol_message_set(message, type, payload_str);
}