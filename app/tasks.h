#ifndef TASKS_H
#define TASKS_H

#include <stdint.h>

void tasks_start(void);
uint32_t tasks_get_parser_byte_count(void);
uint32_t tasks_get_parser_message_count(void);
uint32_t tasks_get_parser_error_count(void);

#endif
