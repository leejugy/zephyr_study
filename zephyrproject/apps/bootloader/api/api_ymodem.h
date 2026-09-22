#ifndef __YMODEM_H__
#define __YMODEM_H__

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include "main.h"

#define YMODEM_FILE_LEN 256
#define YMODEM_BUF_LEN 1024

#define YMODEM_MAX_LEN 1029
#define YMODEM_SOH_LEN 128
#define YMODEM_STX_LEN 1024
#define YMODEM_RECV_TX_BUF_LEN 2

#define YMODEM_SOH 0x01
#define YMODEM_STX 0x02
#define YMODEM_EOT 0x04
#define YMODEM_ACK 0x06
#define YMODEM_NACK 0x15
#define YMODEM_CAN 0x18
#define YMODEM_ESC 0x1b
#define YMODEM_C 'C'

typedef enum {
    YMODEM_PROC_FILE_INIT,
    YMODEM_PROC_FILE,
    YMODEM_PROC_END,
} YMODEM_PROC_STEP;

typedef enum {
    YMODEM_PARSER_HEADER,
    YMODEM_PARSER_SEQ,
    YMODEM_PARSER_NSEQ,
    YMODEM_PARSER_DATA,
    YMODEM_PARSER_CRC16_HIGH,
    YMODEM_PARSER_CRC16_LOW,
} YMODEM_PARSER_STEP;

typedef enum {
    YMODEM_END_1,
    YMODEM_END_2,
    YMODEM_END_3,
} YMODEM_END_STEP;

typedef struct ymodem_t {
    char file_name[YMODEM_FILE_LEN];
    void *arg;
    uint32_t file_size;
    uint8_t rx_buf[YMODEM_BUF_LEN];
    uint32_t rx_size;
    uint32_t total_size;
    uint8_t parse_step;
    uint8_t proc_step;
    uint8_t end_step;
    uint16_t nack_count;
    uint16_t recv_crc;
    bool soh;
    bool end;
    bool seq_duplicate;
    uint8_t seq;
    uint8_t recv_seq;
    int err;
    int (*rx)(struct ymodem_t *y, uint8_t *buf, uint32_t buf_size);
    int (*tx)(struct ymodem_t *y, uint8_t *buf, uint32_t buf_size);
    int (*rx_file)(struct ymodem_t *y, uint8_t *buf, uint32_t buf_size);
    int (*tx_file)(struct ymodem_t *y, uint8_t *buf, uint32_t buf_size);
} ymodem_t;

typedef struct {
    int (*rx)(struct ymodem_t *y, uint8_t *buf, uint32_t buf_size);
    int (*tx)(struct ymodem_t *y, uint8_t *buf, uint32_t buf_size);
    int (*rx_file)(struct ymodem_t *y, uint8_t *buf, uint32_t buf_size);
    void *arg;
} ymodem_rx_t;

int ymodem_recv_start(ymodem_t *y, ymodem_rx_t *init);
char *ymodem_get_file_name(ymodem_t *y);
size_t ymodem_get_file_size(ymodem_t *y);
#endif