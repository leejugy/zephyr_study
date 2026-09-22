#include "api_ymodem.h"

static uint16_t ymodem_crc16(uint16_t crc_in, uint8_t byte)
{
    uint32_t crc = crc_in;
    uint32_t in = byte | 0x100;

    do {
        crc <<= 1;
        in <<= 1;

        if (in & 0x100) {
            ++crc;
        }

        if (crc & 0x10000) {
            crc ^= 0x1021;
        }
    } while (!(in & 0x10000));

    return crc & 0xFFFFU;
}

static uint16_t ymodem_crc16_buf(const uint8_t *p_data, uint32_t size)
{
    uint32_t crc = 0;
    const uint8_t *data_end = p_data + size;

    while (p_data < data_end) {
        crc = ymodem_crc16(crc, *p_data++);
    }

    crc = ymodem_crc16(crc, 0);
    crc = ymodem_crc16(crc, 0);

    return crc & 0xffff;
}

static void ymodem_send_byte(ymodem_t *y, uint8_t c)
{
    uint8_t tx_buf[YMODEM_RECV_TX_BUF_LEN] = {
        0,
    };
    int tx_buf_size = 0;

    tx_buf[tx_buf_size++] = c;
    y->tx(y, tx_buf, tx_buf_size);
}

static void ymodem_recv_clear(ymodem_t *y)
{
    y->end_step = YMODEM_END_1;
    y->proc_step = YMODEM_PROC_FILE_INIT;
    y->parse_step = YMODEM_PARSER_HEADER;
    y->end = true;
    y->rx_size = 0;
}

static void ymodem_recv_cancel(ymodem_t *y)
{
    ymodem_send_byte(y, YMODEM_CAN);
    ymodem_send_byte(y, YMODEM_CAN);
    ymodem_recv_clear(y);
}

static void ymodem_recv_nack(ymodem_t *y)
{
    ymodem_send_byte(y, YMODEM_NACK);
    y->parse_step = YMODEM_PARSER_HEADER ;   
    y->rx_size = 0;
    y->nack_count++;
    y->seq_duplicate = false;
}

static int ymodem_recv_file_info(ymodem_t *y)
{
    int ret = 0;
    /* <filename>\0<file_size> <date> <authority> */
    y->rx_buf[strlen((char *)y->rx_buf)] = ' ';
    ret = sscanf((char *)y->rx_buf, "%s %d", 
                 y->file_name, 
                 &y->file_size);

    if (ret == 2) {
        return true;
    }

    return y->err = -EBADF;
}

static void ymodem_recv_process(ymodem_t *y)
{
    if (y->seq_duplicate) {
        return;
    }

    switch (y->proc_step) {
    case YMODEM_PROC_FILE_INIT:
        if (ymodem_recv_file_info(y) < 0) {
            ymodem_recv_cancel(y);
            break;
        }
        ymodem_send_byte(y, YMODEM_ACK);
        ymodem_send_byte(y, YMODEM_C);
        y->proc_step = YMODEM_PROC_FILE;
        break;

    case YMODEM_PROC_FILE:
        if (y->rx_file) {
            y->err = y->rx_file(y, y->rx_buf, y->rx_size);
            if (y->err < 0) {
                ymodem_recv_cancel(y);
                break;
            }
        }
        y->total_size += y->rx_size;
        ymodem_send_byte(y, YMODEM_ACK);
        break;

    case YMODEM_PROC_END:
        switch (y->end_step) {
        case YMODEM_END_1:
            y->end_step = YMODEM_END_2;
            ymodem_send_byte(y, YMODEM_NACK);
            break;

        case YMODEM_END_2:
            y->seq = UINT8_MAX;
            y->end_step = YMODEM_END_3;
            ymodem_send_byte(y, YMODEM_ACK);
            ymodem_send_byte(y, YMODEM_C);
            break;
        
        case YMODEM_END_3:
            ymodem_send_byte(y, YMODEM_ACK);
            ymodem_recv_clear(y);
            break;

        default:
            ymodem_recv_cancel(y);
            break;
        }
        break;
    
    default:
        ymodem_recv_cancel(y);
        break;
    }
}

static void ymodem_recv_header(ymodem_t *y, uint8_t c)
{
    if (c == YMODEM_SOH) {
        y->soh = true;
        y->parse_step = YMODEM_PARSER_SEQ;
    } else if (c == YMODEM_STX) {
        y->soh = false;
        y->parse_step = YMODEM_PARSER_SEQ;
    } else if (c == YMODEM_CAN || c == YMODEM_ESC) {
        y->err = -ECANCELED;
        ymodem_recv_cancel(y);
    } else if (c == YMODEM_EOT) {
        y->proc_step = YMODEM_PROC_END;
        ymodem_recv_process(y);
    }
}

static void ymodem_recv_data(ymodem_t *y, uint8_t c)
{
    y->rx_buf[y->rx_size++] = c;
    if (y->soh) {
        if (y->rx_size == YMODEM_SOH_LEN) {
            y->parse_step = YMODEM_PARSER_CRC16_HIGH;
        }
    } else {
        if (y->rx_size == YMODEM_STX_LEN) {
            y->parse_step = YMODEM_PARSER_CRC16_HIGH;
        }
    }
}

static void ymodem_recv_seq(ymodem_t *y, uint8_t c)
{
    if ((uint8_t)(y->seq) == c) {
        y->seq_duplicate = true;
    } else if ((uint8_t)(y->seq + 1) != c) {
        ymodem_recv_nack(y);
        return;
    }
    y->recv_seq = c;
    y->parse_step = YMODEM_PARSER_NSEQ;
}

static void ymodem_recv_nseq(ymodem_t *y, uint8_t c)
{
    if (y->recv_seq != (uint8_t)~c) {
        ymodem_recv_nack(y);
        return;
    }
    y->parse_step = YMODEM_PARSER_DATA;
}

static void ymodem_recv_crc_high(ymodem_t *y, uint8_t c)
{
    y->recv_crc = (c << 8) & 0xff00;
    y->parse_step = YMODEM_PARSER_CRC16_LOW;
}

static void ymodem_recv_crc_low(ymodem_t *y, uint8_t c)
{
    uint16_t crc = 0;

    y->recv_crc += c;
    y->parse_step = YMODEM_PARSER_HEADER;

    crc = ymodem_crc16_buf(y->rx_buf, y->rx_size);
    if (crc == y->recv_crc) {
        ymodem_recv_process(y);
        y->seq = y->recv_seq;
        y->seq_duplicate = false;
        y->rx_size = 0;
    } else {
        ymodem_recv_nack(y);
    }
}

static void ymodem_recv_parser(ymodem_t *y, uint8_t *buf, int buf_size)
{
    int loop = 0;

    for (; loop < buf_size; loop++) {
        switch (y->parse_step) {
        case YMODEM_PARSER_HEADER:
            ymodem_recv_header(y, buf[loop]);
            break;

        case YMODEM_PARSER_SEQ:
            ymodem_recv_seq(y, buf[loop]);
            break;

        case YMODEM_PARSER_NSEQ:
            ymodem_recv_nseq(y, buf[loop]);
            break;
        
        case YMODEM_PARSER_DATA:
            ymodem_recv_data(y, buf[loop]);
            break;
            
        case YMODEM_PARSER_CRC16_HIGH:
            ymodem_recv_crc_high(y, buf[loop]);
            break;

        case YMODEM_PARSER_CRC16_LOW:
            ymodem_recv_crc_low(y, buf[loop]);
            break;

        default:
            break;
        }
    }
}

int ymodem_recv_start(ymodem_t *y, ymodem_rx_t *init)
{
    int ret = 0;

    memset(y->file_name, 0, sizeof(y->file_name));
    y->rx = init->rx;
    y->tx = init->tx;
    y->end = false;
    y->arg = init->arg;
    y->rx_file = init->rx_file;
    y->file_size = 0;
    y->total_size = 0;
    y->nack_count = 0;
    y->err = 0;
    y->seq = UINT8_MAX;
    y->recv_seq = 0;
    y->seq_duplicate = 0;
    y->rx_size = 0;
    y->end_step = YMODEM_END_1;
    y->proc_step = YMODEM_PROC_FILE_INIT;
    y->parse_step = YMODEM_PARSER_HEADER;

    if (y->rx == NULL || y->tx == NULL) {
        return -EINVAL;
    }

    int64_t  timeout = k_uptime_get();
    uint8_t rx_buf[YMODEM_MAX_LEN] = {
        0,
    };

    while (!y->end) {
        if (k_uptime_get() - timeout > CONFIG_YMODEM_RX_TIMEOUT) {
            ymodem_recv_cancel(y);
            return -ETIMEDOUT;
        }

        if (y->nack_count > CONFIG_YMODEM_NACK_MAX_COUNT) {
            ymodem_recv_cancel(y);
            return -EOVERFLOW;
        }

        ret = y->rx(y, rx_buf, sizeof(rx_buf));
        if (ret < 0) {
            ymodem_recv_cancel(y);
            return ret;
        }

        if (ret > 0) {
            timeout = k_uptime_get();
            ymodem_recv_parser(y, rx_buf, ret);
        }

        if (y->proc_step == YMODEM_PROC_FILE_INIT) {
            ymodem_send_byte(y, YMODEM_C);
            k_msleep(100);
        }
    }

    if (y->err < 0) {
        return y->err;
    }

    if (y->file_size <= y->total_size) {
        return y->file_size;
    }

    return -EBADMSG;
}

char *ymodem_get_file_name(ymodem_t *y)
{
    return y->file_name;
}

size_t ymodem_get_file_size(ymodem_t *y)
{
    return y->file_size;
}