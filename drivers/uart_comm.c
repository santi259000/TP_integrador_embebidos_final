#include <stddef.h>
#include <stdint.h>

#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/usart.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "../config/app_config.h"
#include "uart_comm.h"

static QueueHandle_t g_uart_rx_queue = NULL;
static volatile uint32_t g_uart_rx_irq_count = 0U;
static volatile uint32_t g_uart_rx_drop_count = 0U;

void uart_comm_set_rx_queue(QueueHandle_t queue)
{
    g_uart_rx_queue = queue;
}

void uart_comm_init(void)
{
    rcc_periph_clock_enable(RCC_GPIOA);
    rcc_periph_clock_enable(RCC_USART1);

    gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
                  GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO9);
    gpio_set_mode(GPIOA, GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_PULL_UPDOWN, GPIO10);
    gpio_set(GPIOA, GPIO10);

    usart_set_baudrate(USART1, UART_BAUDRATE);
    usart_set_databits(USART1, 8);
    usart_set_stopbits(USART1, USART_STOPBITS_1);
    usart_set_mode(USART1, USART_MODE_TX_RX);
    usart_set_parity(USART1, USART_PARITY_NONE);
    usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

    nvic_set_priority(NVIC_USART1_IRQ, 0x50);
    nvic_enable_irq(NVIC_USART1_IRQ);

    usart_enable_rx_interrupt(USART1);
    usart_enable(USART1);
}

void uart_comm_send_bytes(const uint8_t *data, size_t length)
{
    size_t i;

    if (data == NULL) {
        return;
    }

    for (i = 0U; i < length; i++) {
        usart_send_blocking(USART1, data[i]);
    }
}

uint32_t uart_comm_get_rx_irq_count(void)
{
    return g_uart_rx_irq_count;
}

uint32_t uart_comm_get_rx_drop_count(void)
{
    return g_uart_rx_drop_count;
}

void usart1_isr(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    uint8_t byte;

    if (usart_get_flag(USART1, USART_SR_RXNE) == 0) {
        return;
    }

    byte = (uint8_t) usart_recv(USART1);
    g_uart_rx_irq_count++;
    if (g_uart_rx_queue != NULL) {
        if (xQueueSendFromISR(g_uart_rx_queue, &byte, &higher_priority_task_woken) != pdPASS) {
            g_uart_rx_drop_count++;
        }
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}
