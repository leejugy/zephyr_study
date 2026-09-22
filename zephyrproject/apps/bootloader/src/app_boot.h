#ifndef __BOOT_H__
#define __BOOT_H__

#include "main.h"
#include <zephyr/drivers/flash.h>

#define BOOT_FLASH_DEV DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
#define BOOT_PART_NODE DT_NODELABEL(boot_partition)
#define BOOT_ADDRESS DT_REG_ADDR(BOOT_PART_NODE)
#define BOOT_LOADER_SIZE DT_REG_SIZE(BOOT_PART_NODE)
#define APP_ADDRESS BOOT_ADDRESS + BOOT_LOADER_SIZE
#define BOOT_STM32_RESET
#define BOOT_AUTO_TIMEOUT 1000

#ifdef BOOT_STM32_RESET
#include <stm32h743xx.h>
#endif

void boot_init();
void boot_start_thread();

#endif