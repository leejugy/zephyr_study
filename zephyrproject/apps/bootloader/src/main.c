#include "main.h"
#include "api_ymodem.h"
#include "api_uart.h"
#include "app_boot.h"

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_console)
const struct device *const console_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static void main_init()
{
    boot_init();
}

static void main_thread()
{
    boot_start_thread();
}

int main(void)
{
    main_init();
    main_thread();
}