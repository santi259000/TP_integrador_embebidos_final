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

static QueueHandle_t g_uart_rx_isr_queue;
static QueueHandle_t g_parser_input_queue;
static QueueHandle_t g_uart_tx_queue;
static QueueHandle_t g_actuator_queue;

static volatile uint32_t g_parser_byte_count = 0U;
static volatile uint32_t g_parser_message_count = 0U;
static volatile uint32_t g_parser_error_count = 0U;

/* Banderas globales para la Variante 1 */
volatile bool g_ack_estop_received = false;
volatile bool g_emergency_active = false;

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
             * Etapa 3: Variante 1 - Interceptar el ACK del Bridge
             */
            if (message.type == PROTOCOL_TYPE_ACK && strncmp(message.payload, "estop", 5) == 0) {
                g_ack_estop_received = true;
            }

            /*
             * Etapa 1:
             * Si el parser entregó una trama válida, la aplicación responde ACK:ok.
             */
            if (app_process_message(&message, &response)) {
                xQueueSend(g_uart_tx_queue, &response, portMAX_DELAY);
            }

        } else if (result == PARSER_RESULT_ERROR) {
            protocol_message_t error_message;

            g_parser_error_count++;

            /*
             * Etapa 1:
             * Si el parser detecta una trama inválida, responde ERR:bounds.
             */
            if (protocol_message_set(&error_message, PROTOCOL_TYPE_ERR, "bounds")) {
                xQueueSend(g_uart_tx_queue, &error_message, portMAX_DELAY);
            }
        }
    }
}

static void task_button(void *args)
{
    bool pressed = false;
    TickType_t last_send_time = 0;
    protocol_message_t message;
    actuator_command_t act_cmd;
    
    (void) args;

    while (1) {
        if (button_get_event(&pressed)) {
            if (pressed) {
                g_emergency_active = true;
                g_ack_estop_received = false; 
                
                /* 1. Activar Periféricos (LED Rojo parpadeando, Buzzer encendido) */
                strncpy(act_cmd.target, "sys", sizeof(act_cmd.target));
                strncpy(act_cmd.action, "em_on", sizeof(act_cmd.action));
                xQueueSend(g_actuator_queue, &act_cmd, portMAX_DELAY);

                /* 2. Enviar el primer EVT:e_stop */
                if (protocol_message_set(&message, PROTOCOL_TYPE_EVT, "e_stop")) {
                    xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
                    last_send_time = xTaskGetTickCount();
                }
            } else {
                g_emergency_active = false;
                
                /* 1. Desactivar Periféricos (Volver a LED Verde fijo) */
                strncpy(act_cmd.target, "sys", sizeof(act_cmd.target));
                strncpy(act_cmd.action, "em_off", sizeof(act_cmd.action));
                xQueueSend(g_actuator_queue, &act_cmd, portMAX_DELAY);

                /* 2. Enviar liberación */
                if (protocol_message_set(&message, PROTOCOL_TYPE_EVT, "e_stop_rel")) {
                    xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
                }
            }
        }

        /* Lógica de reenvío por Timeout (500 ms) - Requisito del TP */
        if (g_emergency_active && !g_ack_estop_received) {
            TickType_t current_time = xTaskGetTickCount();
            if ((current_time - last_send_time) > pdMS_TO_TICKS(500)) {
                /* Pasaron 500ms y no llegó el ACK, reenviar: */
                if (protocol_message_set(&message, PROTOCOL_TYPE_EVT, "e_stop")) {
                    xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
                    last_send_time = current_time; 
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
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

static void task_telemetry(void *args)
{
    TickType_t last_wake = xTaskGetTickCount();
    protocol_message_t message;
    uint32_t counter = 0U;
    (void) args;

    while (1) {
        /*
         * Tarea periódica de telemetría:
         */
        if (app_build_telemetry_message(&message, counter)) {
            actuators_update_from_adc(sensors_get_last_adc());
            xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
        }

        /*
         * Mensaje de estado del sistema.
         */
        if ((counter % (STATUS_PERIOD_MS / TELEMETRY_PERIOD_MS)) == 0U) {
            app_build_status_message(&message);
            xQueueSend(g_uart_tx_queue, &message, portMAX_DELAY);
        }

        counter++;

        /*
         * Espera periódica sin bloquear el resto del sistema.
         */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(TELEMETRY_PERIOD_MS));
    }
}

static void task_actuators(void *args)
{
    actuator_command_t command;
    (void) args;

    while (1) {
        /* * Usamos un timeout de 50ms en lugar de portMAX_DELAY.
         * Esto permite que la tarea se despierte periódicamente 
         * para hacer parpadear el LED rojo si la emergencia está activa.
         */
        if (xQueueReceive(g_actuator_queue, &command, pdMS_TO_TICKS(50)) == pdPASS) {
            actuators_apply_command(&command);
        }
        
        actuators_update_emergency_blink();
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
    xTaskCreate(task_button, "button", 160, NULL, 2, NULL);
    xTaskCreate(task_telemetry, "telemetry", 192, NULL, 2, NULL);
    xTaskCreate(task_actuators, "actuators", 160, NULL, 2, NULL);
    xTaskCreate(task_uart_tx, "uart_tx", 192, NULL, 2, NULL);

    vTaskStartScheduler();
    configASSERT(false);
}