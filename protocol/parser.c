#include <string.h>
#include <stdio.h>
#include "parser.h"
#include "protocol.h" // Necesitamos las funciones de validación de la Etapa 1

/*
 * Funciones auxiliares privadas del parser
 */

static int hex_char_to_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Manejo de Errores y Resincronización
static parser_result_t handle_parser_error(parser_t *parser, uint8_t byte)
{
    parser_reset(parser);

    // MAGIA DE RESINCRONIZACIÓN:
    // Si el byte que rompió el protocolo resultó ser una '@',
    // lo reutilizamos inmediatamente como el inicio de una trama nueva.
    if (byte == '@')
    {
        parser->state = PARSER_STATE_READ_LEN_HI;
    }

    return PARSER_RESULT_ERROR;
}

/*
 * Funciones públicas de la API
 */
// Validación de trama recibida

// Validación de trama recibida (Etapa 1)
static int protocol_validate(const char *frame, size_t frame_len)
{
    const char *last_colon = NULL;
    for (size_t i = 0; i < frame_len; i++)
    {
        if (frame[i] == ':')
            last_colon = &frame[i];
    }

    if (last_colon == NULL)
        return -1;
    if (last_colon + 3 != frame + frame_len)
        return -1;

    const char *cs_received_str = last_colon + 1;

    int hi = hex_char_to_nibble(cs_received_str[0]);
    int lo = hex_char_to_nibble(cs_received_str[1]);
    if (hi < 0 || lo < 0)
        return -1;

    uint8_t cs_received = (uint8_t)((hi << 4) | lo);

    size_t cs_input_len = (size_t)(last_colon - frame);
    // Usamos la función de checksum que declaramos en protocol.h
    uint8_t cs_calculated = protocol_compute_checksum(frame, cs_input_len);
    return (cs_calculated == cs_received) ? 0 : 1;
}

void parser_reset(parser_t *parser)
{
    if (parser == NULL)
        return;

    parser->state = PARSER_STATE_WAIT_START;
    parser->length_field[0] = '\0';
    parser->length_field[1] = '\0';
    parser->length_field[2] = '\0';
    parser->checksum_field[0] = '\0';
    parser->checksum_field[1] = '\0';
    parser->checksum_field[2] = '\0';
    parser->body[0] = '\0';
    parser->expected_body_length = 0U;
    parser->body_index = 0U;
}

void parser_init(parser_t *parser)
{
    parser_reset(parser);
}

// 3. El cerebro del Parser: La Máquina de Estados
parser_result_t parser_consume_byte(parser_t *parser, uint8_t byte, protocol_message_t *message)
{
    if (parser == NULL || message == NULL)
    {
        return PARSER_RESULT_ERROR;
    }

    // Regla global: ignorar '\r' en cualquier estado
    if (byte == '\r')
    {
        return PARSER_RESULT_IN_PROGRESS;
    }

    switch (parser->state)
    {

    case PARSER_STATE_WAIT_START:
        if (byte == '@')
        {
            parser->state = PARSER_STATE_READ_LEN_HI;
        }
        break;

    case PARSER_STATE_READ_LEN_HI:
        if (hex_char_to_nibble((char)byte) >= 0)
        {
            parser->length_field[0] = (char)byte;
            parser->state = PARSER_STATE_READ_LEN_LO;
        }
        else
        {
            return handle_parser_error(parser, byte);
        }
        break;

    case PARSER_STATE_READ_LEN_LO:
        if (hex_char_to_nibble((char)byte) >= 0)
        {
            parser->length_field[1] = (char)byte;
            parser->length_field[2] = '\0';

            // Decodificar la longitud esperada para saber cuándo parar de leer el body
            int hi = hex_char_to_nibble(parser->length_field[0]);
            int lo = hex_char_to_nibble(parser->length_field[1]);
            parser->expected_body_length = (uint8_t)((hi << 4) | lo);

            parser->state = PARSER_STATE_EXPECT_LEN_SEPARATOR;
        }
        else
        {
            return handle_parser_error(parser, byte);
        }
        break;

    case PARSER_STATE_EXPECT_LEN_SEPARATOR:
        if (byte == ':')
        {
            parser->body_index = 0U;
            parser->state = PARSER_STATE_READ_BODY;
        }
        else
        {
            return handle_parser_error(parser, byte);
        }
        break;

    case PARSER_STATE_READ_BODY:
        parser->body[parser->body_index++] = (char)byte;

        // Protección contra overflow de memoria (Si mienten con el LL)
        if (parser->body_index >= sizeof(parser->body))
        {
            return handle_parser_error(parser, byte);
        }

        // Si ya leímos todo lo que prometía el LL, pasamos al checksum
        if (parser->body_index == parser->expected_body_length)
        {
            parser->body[parser->body_index] = '\0'; // Cerramos el string
            parser->state = PARSER_STATE_EXPECT_CHECK_SEPARATOR;
        }
        break;

    case PARSER_STATE_EXPECT_CHECK_SEPARATOR:
        if (byte == ':')
        {
            parser->state = PARSER_STATE_READ_CHECK_HI;
        }
        else
        {
            return handle_parser_error(parser, byte);
        }
        break;

    case PARSER_STATE_READ_CHECK_HI:
        if (hex_char_to_nibble((char)byte) >= 0)
        {
            parser->checksum_field[0] = (char)byte;
            parser->state = PARSER_STATE_READ_CHECK_LO;
        }
        else
        {
            return handle_parser_error(parser, byte);
        }
        break;

    case PARSER_STATE_READ_CHECK_LO:
        if (hex_char_to_nibble((char)byte) >= 0)
        {
            parser->checksum_field[1] = (char)byte;
            parser->checksum_field[2] = '\0';
            parser->state = PARSER_STATE_EXPECT_END;
        }
        else
        {
            return handle_parser_error(parser, byte);
        }
        break;

    case PARSER_STATE_EXPECT_END:
        if (byte == '\n')
        {
            // 1. Reconstruir la trama temporal para pasarla a nuestra función de validación
            char frame_to_validate[PROTOCOL_MAX_FRAME_LENGTH + 1];
            snprintf(frame_to_validate, sizeof(frame_to_validate), "%s:%s:%s",
                     parser->length_field, parser->body, parser->checksum_field);

            // 2. Validar Checksum (Función de la Etapa 1)
            if (protocol_validate(frame_to_validate, strlen(frame_to_validate)) == 0)
            {

                // 3. Decodificar el tipo y el payload
                if (protocol_decode_body(parser->body, parser->expected_body_length, message))
                {
                    parser_reset(parser);
                    return PARSER_RESULT_MESSAGE_READY;
                }
            }
        }
        else
        {
            return handle_parser_error(parser, byte);
        }
    }

    return PARSER_RESULT_IN_PROGRESS;
}