#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdio.h>
#include <errno.h>
#include <zephyr/kernel.h>

#define CONFIG_YMODEM_RX_TIMEOUT 1000 * 10
#define CONFIG_YMODEM_NACK_MAX_COUNT 100

extern const struct device *const console_dev;

#endif