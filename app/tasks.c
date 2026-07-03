#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "button.h"

#include "app.h"
#include "actuators.h"
#include "sensors.h"
#include "tasks.h"
#include "../config/app_config.h"
#include "../drivers/uart_comm.h"
#include "../protocol/parser.h"
#include "../protocol/protocol.h"

#define ESTOP_RETRY_MS      500U
#define DAT_PERIOD_MS       500U
#define STS_PERIOD_MS       1000U
#define VARIANT_TASK_MS     10U

static QueueHandle_t g_uart_rx_isr_queue;
static QueueHandle_t g_parser_input_queue;
static QueueHandle_t g_uart_tx_queue;
static QueueHandle_t g_actuator_queue;

static volatile uint32_t g_parser_byte_count = 0U;
static volatile uint32_t g_parser_message_count = 0U;
static volatile uint32_t g_parser_error_count = 0U;

/*
 * Estado propio de la Variante 1.
 */
static volatile bool g_emergency_active = false;
static volatile bool g_estop_ack_received = false;
static TickType_t g_last_estop_tx_tick = 0U;

static void send_protocol_message(protocol_type_t type, const char *payload)
{
    protocol_message_t message;

    if (protocol_message_set(&message, type, payload)) {
        xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
    }
}

static void task_uart_rx(void *args)
{
    uint8_t byte = 0U;
    (void) args;

    while (1) {
        if (xQueueReceive(g_uart_rx_isr_queue, &byte, portMAX_DELAY) == pdPASS) {
            xQueueSend(g_parser_input_queue, &byte, portMAX_DELAY);
        }
    }
}

static void task_parser(void *args)
{
    uint8_t byte = 0U;
    parser_t parser;
    protocol_message_t message;
    parser_result_t result;
    (void) args;

    parser_init(&parser);

    while (1) {
        if (xQueueReceive(g_parser_input_queue, &byte, portMAX_DELAY) != pdPASS) {
            continue;
        }

        g_parser_byte_count++;

        result = parser_consume_byte(&parser, byte, &message);

        if (result == PARSER_RESULT_MESSAGE_READY) {
            protocol_message_t response;

            g_parser_message_count++;

            /*
             * Variante 1:
             * Si llega ACK:estop, se toma como confirmación de la parada
             * y se deja de reenviar EVT:e_stop.
             */
            if ((message.type == PROTOCOL_TYPE_ACK) &&
                (strcmp(message.payload, "estop") == 0)) {
                g_estop_ack_received = true;
                continue;
            }

            /*
             * Para otras tramas válidas se mantiene la lógica de Etapa 1:
             * responder ACK:ok.
             */
            if (app_process_message(&message, &response)) {
                xQueueSend(g_uart_tx_queue, &response, portMAX_DELAY);
            }

        } else if (result == PARSER_RESULT_ERROR) {
            g_parser_error_count++;

            /*
             * Si el parser detecta una trama inválida, responde ERR:bounds.
             */
            send_protocol_message(PROTOCOL_TYPE_ERR, "bounds");
        }
    }
}

/*
 * Tarea de la Variante 1:
 * - Lee eventos del botón.
 * - Cambia estado normal/emergencia.
 * - Reenvía EVT:e_stop cada 500 ms si no llegó ACK:estop.
 * - Actualiza el destello del LED rojo.
 */
static void task_variant1(void *args)
{
    bool pressed = false;
    TickType_t now;
    (void) args;

    actuators_set_normal();

    while (1) {
        /*
         * Actualiza el parpadeo del LED rojo si la emergencia está activa.
         */
        actuators_update_emergency_blink();

        /*
         * Procesa eventos del botón.
         */
        if (button_get_event(&pressed)) {
            if (pressed) {
                /*
                 * Botón presionado: entra en emergencia.
                 */
                if (!g_emergency_active) {
                    g_emergency_active = true;
                    g_estop_ack_received = false;
                    g_last_estop_tx_tick = xTaskGetTickCount();

                    actuators_set_emergency();
                    send_protocol_message(PROTOCOL_TYPE_EVT, "e_stop");
                }
            } else {
                /*
                 * Botón liberado/rearme: sale de emergencia.
                 */
                if (g_emergency_active) {
                    g_emergency_active = false;
                    g_estop_ack_received = false;

                    actuators_set_normal();
                    send_protocol_message(PROTOCOL_TYPE_EVT, "e_stop_rel");
                }
            }
        }

        /*
         * Si está en emergencia y todavía no llegó ACK:estop,
         * reenvía EVT:e_stop cada 500 ms.
         */
        if (g_emergency_active && !g_estop_ack_received) {
            now = xTaskGetTickCount();

            if ((now - g_last_estop_tx_tick) >= pdMS_TO_TICKS(ESTOP_RETRY_MS)) {
                g_last_estop_tx_tick = now;
                send_protocol_message(PROTOCOL_TYPE_EVT, "e_stop");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(VARIANT_TASK_MS));
    }
}

uint32_t tasks_get_parser_byte_count(void)
{
    return g_parser_byte_count;
}

uint32_t tasks_get_parser_message_count(void)
{
    return g_parser_message_count;
}

uint32_t tasks_get_parser_error_count(void)
{
    return g_parser_error_count;
}

/*
 * Telemetría de Variante 1:
 * - DAT:adc=NNNN cada 500 ms.
 * - STS:OK cada 1 s.
 */
static void task_telemetry(void *args)
{
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_dat_tick = xTaskGetTickCount();
    TickType_t last_sts_tick = xTaskGetTickCount();
    TickType_t now;
    protocol_message_t message;
    uint32_t counter = 0U;
    (void) args;

    while (1) {
        now = xTaskGetTickCount();

        if ((now - last_dat_tick) >= pdMS_TO_TICKS(DAT_PERIOD_MS)) {
            last_dat_tick = now;

            if (app_build_telemetry_message(&message, counter)) {
                /*
                 * En la Variante 1 el ADC se usa como health check.
                 * No modifica directamente la emergencia.
                 */
                xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
            }

            counter++;
        }

        if ((now - last_sts_tick) >= pdMS_TO_TICKS(STS_PERIOD_MS)) {
            last_sts_tick = now;

            /*
             * Heartbeat periódico pedido por la Variante 1.
             */
            if (protocol_message_set(&message, PROTOCOL_TYPE_STS, "OK")) {
                xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
            }
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50U));
    }
}

static void task_actuators(void *args)
{
    actuator_command_t command;
    (void) args;

    while (1) {
        if (xQueueReceive(g_actuator_queue, &command, portMAX_DELAY) == pdPASS) {
            actuators_apply_command(&command);
        }
    }
}

static void task_uart_tx(void *args)
{
    protocol_message_t message;
    char frame[PROTOCOL_MAX_FRAME_SIZE];
    size_t frame_length = 0U;
    (void) args;

    while (1) {
        if (xQueueReceive(g_uart_tx_queue, &message, portMAX_DELAY) != pdPASS) {
            continue;
        }

        if (protocol_encode_frame(&message, frame, sizeof(frame), &frame_length)) {
            uart_comm_send_bytes((const uint8_t *) frame, frame_length);
        }
    }
}

void tasks_start(void)
{
    g_uart_rx_isr_queue = xQueueCreate(UART_RX_ISR_QUEUE_LENGTH, sizeof(uint8_t));
    g_parser_input_queue = xQueueCreate(PARSER_INPUT_QUEUE_LENGTH, sizeof(uint8_t));
    g_uart_tx_queue = xQueueCreate(UART_TX_QUEUE_LENGTH, sizeof(protocol_message_t));
    g_actuator_queue = xQueueCreate(ACTUATOR_QUEUE_LENGTH, sizeof(actuator_command_t));

    configASSERT(g_uart_rx_isr_queue != NULL);
    configASSERT(g_parser_input_queue != NULL);
    configASSERT(g_uart_tx_queue != NULL);
    configASSERT(g_actuator_queue != NULL);

    uart_comm_set_rx_queue(g_uart_rx_isr_queue);
    button_init();

    xTaskCreate(task_uart_rx, "uart_rx", 160, NULL, 3, NULL);
    xTaskCreate(task_parser, "parser", 192, NULL, 3, NULL);

    /*
     * Tarea específica de la Variante 1.
     */
    xTaskCreate(task_variant1, "variant1", 192, NULL, 2, NULL);

    /*
     * DAT:adc=NNNN cada 500 ms y STS:OK cada 1 s.
     */
    xTaskCreate(task_telemetry, "telemetry", 192, NULL, 2, NULL);

    xTaskCreate(task_actuators, "actuators", 160, NULL, 2, NULL);
    xTaskCreate(task_uart_tx, "uart_tx", 192, NULL, 2, NULL);

    vTaskStartScheduler();
    configASSERT(false);
}