#ifndef __UART_H__
#define __UART_H__

#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "api_config.h"
#include "main.h"

#ifndef CONFIG_UART_ASYNC_API
#warning "can't use uart dma api"
#endif

#ifndef CONFIG_UART_INTERRUPT_DRIVEN
#warning "can't use uart int api"
#endif


typedef enum {
    UART_MODE_DMA,
    UART_MODE_INTERRUPT,
} UART_MODE;

#define UART_RX_RING_BUF_SIZE 1024
#define UART_ISR_BUF 64

typedef struct {
    UART_MODE mode;
    const struct device *handle;
    k_timeout_t rx_timeout;
} uart_init_t;

typedef struct uart_t {
    struct ring_buf rx_ring;
    struct ring_buf tx_ring;
    struct k_sem tx_evt_sem;
    struct k_sem rx_evt_sem;
    struct k_mutex tx_mutex;
    struct k_mutex rx_mutex;
    const struct device *handle;
    uint8_t rx_ring_buf[UART_RX_RING_BUF_SIZE];
    uint8_t tx_ring_buf[UART_RX_RING_BUF_SIZE];
    int rx_err;
    int tx_err;
    k_timeout_t rx_timeout;
    bool init;
    int (*tx)(struct uart_t *uart, uint8_t *buf, size_t buf_size);
    int (*rx)(struct uart_t *uart, uint8_t *buf, size_t buf_size);
} uart_t;

int uart_init(uart_t *uart, uart_init_t *init);
int uart_send(uart_t *uart, uint8_t *buf, size_t buf_size);
int uart_receive(uart_t *uart, uint8_t *buf, size_t buf_size);

#endif