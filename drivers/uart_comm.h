#ifndef UART_COMM_H
#define UART_COMM_H

#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "queue.h"

void uart_comm_init(void);
void uart_comm_set_rx_queue(QueueHandle_t queue);
void uart_comm_send_bytes(const uint8_t *data, size_t length);
uint32_t uart_comm_get_rx_irq_count(void);
uint32_t uart_comm_get_rx_drop_count(void);

#endif
