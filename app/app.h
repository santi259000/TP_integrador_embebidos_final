#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#include "protocol.h"

void app_init(void);

bool app_process_message(const protocol_message_t *input,
                         protocol_message_t *response);

bool app_build_status_message(protocol_message_t *message);

bool app_build_telemetry_message(protocol_message_t *message,
                                 uint32_t sequence);

#endif