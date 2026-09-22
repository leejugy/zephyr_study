#include "main.h"
#include "api_ymodem.h"
#include "api_uart.h"
#include "app_boot.h"

//todo ymodem flash write 구현
uart_t console_uart = {
    0,
};

ymodem_t console_ymodem = {
    0,
};

const struct device *flash = BOOT_FLASH_DEV;
int64_t boot_time = 0;
size_t sector_size = 0;
size_t write_size = 0;

static void boot_flash_info_get(void)
{
    struct flash_pages_info info = {
        0,
    };
    int ret = 0;

    ret = device_is_ready(flash);
    if (ret == 0) {
        printk("flash device not ready\r\n");
        return;
    }

    ret = flash_get_page_info_by_idx(flash, 0, &info);
    if (ret < 0) {
        printk("fail to get flash page info - %s\r\n", strerror(-ret));
        return;
    }

    write_size = flash_get_write_block_size(flash);
    sector_size = info.size;
}

static int ymodem_uart_rx(struct ymodem_t *y,
                                 uint8_t *buf,
                                 uint32_t buf_size)
{
    if (y == NULL || buf == NULL || buf_size == 0) {
        return -EINVAL;
    }

    return uart_receive(y->arg, buf, buf_size);
}

static int ymodem_uart_tx(struct ymodem_t *y,
                                 uint8_t *buf,
                                 uint32_t buf_size)
{
    if (y == NULL || buf == NULL || buf_size == 0) {
        return -EINVAL; 
    }

    return uart_send(y->arg, buf, buf_size);
}

static int ymodem_receive_file(struct ymodem_t *y, uint8_t *buf, uint32_t buf_size)
{
    size_t offset = y->total_size + BOOT_LOADER_SIZE;
    size_t ymodem_padding_size = 0;
    size_t ymodem_padding_offset = 0;

    int ret = 0;

    if (offset % sector_size == 0) {
        ret = flash_erase(flash, offset, sector_size);
        if (ret < 0) {
            return ret;
        }
    }

    if (y->total_size + buf_size >= y->file_size) {
        if (y->soh) {
            ymodem_padding_offset = y->file_size - y->total_size;
            ymodem_padding_size = YMODEM_SOH_LEN - ymodem_padding_offset;
            memset(&buf[ymodem_padding_offset], 0xff, ymodem_padding_size);
        } else {
            ymodem_padding_offset = y->file_size - y->total_size;
            ymodem_padding_size = YMODEM_STX_LEN - ymodem_padding_offset;
            memset(&buf[ymodem_padding_offset], 0xff, ymodem_padding_size);
        }
    }

    ret = flash_write(flash, offset, buf, buf_size);
    if (ret < 0) {
        return ret;
    }

    return buf_size;
}

static void boot_ymodem_uart_update(uart_t *uart)
{
    ymodem_rx_t init = {
        0,
    };

    int ret = 0;

    init.rx = ymodem_uart_rx;
    init.tx = ymodem_uart_tx;
    init.rx_file = ymodem_receive_file;
    init.arg = uart;

    ret = ymodem_recv_start(&console_ymodem, &init);

    if (ret < 0) {
        printk("ymodem fail - %s\r\n", strerror(-ret));
    } else if (ret > 0) {
        printk("\r\nymodem recv - %s, file size - %d\r\n",
               ymodem_get_file_name(&console_ymodem),
               ymodem_get_file_size(&console_ymodem));
    }
}

#ifdef BOOT_STM32_RESET
static void boot_jump_app(void)
{
    uint32_t loop = 0;
    uint32_t app_reset_addr = *(__IO uint32_t*)(APP_ADDRESS + 4);
    void (*reset)() = (void (*)())app_reset_addr;

    if ((app_reset_addr & 0xff000000) != 0x08000000) {
        printk("there is no application at 0x%08x\r\n", APP_ADDRESS);
        return;
    }

    __disable_irq();
    for (; loop < (sizeof(NVIC->ICER)/sizeof(NVIC->ICER[0])); loop++) {
        NVIC->ICER[loop] = 0xFFFFFFFFU;
    }

    for (loop = 0; loop < (sizeof(NVIC->ICPR)/sizeof(NVIC->ICPR[0])); loop++) {
        NVIC->ICPR[loop] = 0xFFFFFFFFU;
    }

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    HAL_RCC_DeInit();
    HAL_DeInit();

    __set_MSP(*(__IO uint32_t*)APP_ADDRESS);
    __enable_irq();
    __DSB();
    __ISB();
    reset();
}
#endif

static void boot_print_info(void)
{
    uint32_t app_reset_addr = *(__IO uint32_t*)(APP_ADDRESS + 4);

    printk("\r========================================================\r\n");
    printk("*  Zephyr Boot Loader\r\n");
    printk("*  Board : %s\r\n", CONFIG_BOARD_TARGET);
    printk("*  Boot address : 0x%08x\r\n", BOOT_ADDRESS);
    printk("*  Jump address : 0x%08x\r\n", APP_ADDRESS);
    printk("*  App detect   : %s\r\n", 
           ((app_reset_addr & 0xff000000) == 0x08000000) ? 
            "\x1b[32;1mV\x1b[0m" : "\x1b[31;1mX\x1b[0m");
    printk("========================================================\r\n");
}

static void boot_print_menu(void)
{
    printk("\r=============================================\r\n");
    printk("*  Download application ------------ 1\r\n");
    printk("*  start application --------------- 2\r\n");
    printk("=============================================\r\n");
}

static void boot_selection(uint8_t c, bool *auto_boot)
{
    switch (c) {
    case '\n':
    case '\r':
        boot_print_menu();
        *auto_boot = false;
        break;

    case '1':
        printk("Press Esc to cancel\r\n");
        boot_ymodem_uart_update(&console_uart);
        boot_print_menu();
        break;

    case '2':
        boot_jump_app();
        break;
    
    default:
        break;
    }
}

static void boot_auto(void)
{
    if (k_uptime_get() - boot_time < BOOT_AUTO_TIMEOUT) {
        printk("\rAuto boot will be start at ...%4lld ms", 
               BOOT_AUTO_TIMEOUT - (k_uptime_get() - boot_time));
    } else {
        printk("\x1b[2K\r");
        k_msleep(100);
        boot_jump_app();
    }
}

static void boot_menu(void)
{
    uint8_t c = 0;
    int ret = 0;
    static bool auto_boot = true;

    if (auto_boot) {
        boot_auto();
    }

    ret = uart_receive(&console_uart, &c, sizeof(c));
    if (ret < 0) {
        printk("uart receive fail boot - %s\r\n", strerror(-ret));
        return;
    } else if (ret > 0) {
        boot_selection(c, &auto_boot);
    }
}

static void *boot_thread(void *p1, void *p2, void *p3)
{
    boot_time = k_uptime_get();

    boot_print_info();
    boot_flash_info_get();

    while (1) {
        boot_menu();
        k_msleep(10);
    }

    return NULL;
}

K_THREAD_DEFINE(boot,
                16384,
                boot_thread,
                NULL,
                NULL,
                NULL,
                10,
                0,
                SYS_FOREVER_MS);

void boot_init()
{
    uart_init_t init = {
        0,
    };

    int ret = 0;

    init.handle = console_dev;
    init.mode = UART_MODE_INTERRUPT;
    init.rx_timeout = K_NO_WAIT;

    ret = uart_init(&console_uart, &init);
    if (ret < 0) {
        printk("invalid console uart - %s\r\n", strerror(-ret));
        return;
    }
}

void boot_start_thread()
{
    k_thread_start(boot);
}
