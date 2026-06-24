#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../config/app_config.h"

#define PROTOCOL_START_CHAR      '@'
#define PROTOCOL_END_CHAR        '\n'
#define PROTOCOL_SEPARATOR_CHAR  ':'
#define PROTOCOL_TYPE_LENGTH     3U
#define PROTOCOL_MAX_BODY_SIZE   (PROTOCOL_TYPE_LENGTH + 1U + PROTOCOL_MAX_PAYLOAD_LENGTH)
#define PROTOCOL_MAX_FRAME_SIZE  PROTOCOL_MAX_FRAME_LENGTH

typedef enum {
    PROTOCOL_TYPE_CMD = 0,
    PROTOCOL_TYPE_DAT,
    PROTOCOL_TYPE_EVT,
    PROTOCOL_TYPE_STS,
    PROTOCOL_TYPE_ACK,
    PROTOCOL_TYPE_ERR,
    PROTOCOL_TYPE_INVALID
} protocol_type_t;

typedef struct {
    protocol_type_t type;
    char payload[PROTOCOL_MAX_PAYLOAD_LENGTH + 1U];
    uint8_t payload_length;
} protocol_message_t;

bool protocol_message_set(protocol_message_t *message, protocol_type_t type, const char *payload);
bool protocol_encode_frame(const protocol_message_t *message, char *frame, size_t frame_size, size_t *frame_length);
bool protocol_decode_body(const char *body, uint8_t body_length, protocol_message_t *message);
uint8_t protocol_compute_checksum(const char *data, size_t length);
const char *protocol_type_to_text(protocol_type_t type);
protocol_type_t protocol_type_from_text(const char *text);

#endif