#include "api_uart.h"

static void uart_int_tx_ready(const struct device *handle, uart_t *uart)
{
    int ret = 0;
    uint8_t buf[UART_ISR_BUF] = {
        0,
    };

    ret = uart_irq_tx_ready(handle);
    if (ret <= 0) {
        uart->tx_err = ret;
        return;
    }

    ret = ring_buf_get(&uart->tx_ring, buf, ret);
    if (ret == 0) {
        uart_irq_tx_disable(handle);
        k_sem_give(&uart->tx_evt_sem);
        return;
    }

    ret = uart_fifo_fill(handle, buf, ret);
    if (ret <= 0) {
        uart->tx_err = ret;
    }
}

static void uart_int_rx_ready(const struct device *handle, uart_t *uart)
{
    int ret = 0;
    uint8_t buf[UART_ISR_BUF] = {
        0,
    };

    ret = uart_irq_rx_ready(handle);
    if (ret <= 0) {
        uart->rx_err = ret;
        return;
    }

    ret = uart_fifo_read(handle, buf, sizeof(buf));
    if (ret <= 0) {
        uart->rx_err = ret;
        return;
    }

    ret = ring_buf_put(&uart->rx_ring, buf, ret);
    if (ret > 0) {
        k_sem_give(&uart->rx_evt_sem);
    }
}

static void uart_int_cb(const struct device *handle, 
                 void *user_data)
{
    uart_t *uart = user_data;

    while (1) {
        uart_irq_update(handle);

        if (uart_irq_is_pending(handle) <= 0) {
			break;
		}

        uart_int_tx_ready(handle, uart);
        uart_int_rx_ready(handle, uart);
    }
    
}

static int uart_send_it(struct uart_t *uart, uint8_t *buf, size_t buf_size)
{
    int ret = 0;
    int len = 0;

    ret = ring_buf_put(&uart->tx_ring, buf, buf_size);
    if (ret == 0) {
        return ret;
    }

    len = ret;
    uart_irq_tx_enable(uart->handle);

    ret = k_sem_take(&uart->tx_evt_sem, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    return len;
}

static int uart_receive_it(struct uart_t *uart, uint8_t *buf, size_t buf_size)
{
    int ret = 0;

    ret = ring_buf_get(&uart->rx_ring, buf, buf_size);
    if (ret > 0) {
        return ret;
    }

    ret = k_sem_take(&uart->rx_evt_sem, uart->rx_timeout);
    if (ret < 0) {
        if (ret == -EBUSY || ret == -EAGAIN) {
            return 0;
        }
        return ret;
    }

    return ring_buf_get(&uart->rx_ring, buf, buf_size);
}

static int uart_send_dma(struct uart_t *uart, uint8_t *buf, size_t buf_size)
{
    int ret = 0;

    ret = uart_tx(uart->handle, buf, buf_size, SYS_FOREVER_US);
    if (ret < 0) {
        return ret;
    }

    ret = k_sem_take(&uart->tx_evt_sem, K_FOREVER);
    if (ret < 0) {
        return ret;
    }

    return buf_size;
}

static int uart_receive_dma(struct uart_t *uart, uint8_t *buf, size_t buf_size)
{
    int ret = 0;

    ret = ring_buf_get(&uart->rx_ring, buf, buf_size);
    if (ret > 0) {
        return ret;
    }

    ret = k_sem_take(&uart->rx_evt_sem, uart->rx_timeout);
    if (ret < 0) {
        if (ret == -EBUSY || ret == -EAGAIN) {
            return 0;
        }
        return ret;
    }

    return ring_buf_get(&uart->rx_ring, buf, buf_size);
}

static void uart_dma_cb(const struct device *dev,
			            struct uart_event *evt, 
                        void *user_data)
{
    uart_t *uart = user_data;
    int ret = 0;

    switch(evt->type) {
    case UART_TX_DONE:
    case UART_TX_ABORTED:
        k_sem_give(&uart->tx_evt_sem);
        break;

    case UART_RX_RDY:
        ret = ring_buf_put(&uart->rx_ring, 
                           evt->data.rx.buf + evt->data.rx.offset,
                           evt->data.rx.len);
        if (ret > 0) {
            k_sem_give(&uart->rx_evt_sem);
        }
        break;

    case UART_RX_DISABLED:
    case UART_RX_BUF_RELEASED:
    case UART_RX_BUF_REQUEST:
        break;

    case UART_RX_STOPPED:
        ret = uart_rx_enable(uart->handle, 
                         uart->tx_ring_buf, 
                         sizeof(uart->tx_ring_buf),
                         0);
        if (ret < 0) {
            return;
        }
        break;
    }
}

static int uart_dma_init(uart_t *uart, uart_init_t *init)
{
    int ret = 0;

    ret = uart_callback_set(uart->handle, uart_dma_cb, uart);
    if (ret < 0) {
        return ret;
    }
    
    ret = uart_rx_enable(uart->handle, 
                         uart->tx_ring_buf, 
                         sizeof(uart->tx_ring_buf),
                         0);
    if (ret < 0) {
        return ret;
    }
    
    uart->tx = uart_send_dma;
    uart->rx = uart_receive_dma;
    return 0;
}

static int uart_interrupt_init(uart_t *uart, uart_init_t *init)
{
    int ret = 0;

    ret = uart_irq_callback_user_data_set(uart->handle, uart_int_cb, uart);
    if (ret < 0) {
        return ret;
    }

    uart_irq_rx_enable(uart->handle);
    uart->tx = uart_send_it;
    uart->rx = uart_receive_it;
    return ret;
}

int uart_init(uart_t *uart, uart_init_t *init)
{
    int ret = 0;

    if (uart == NULL || init == NULL) {
        return -EINVAL;
    }

    if (uart->init == true) {
        return -EALREADY;
    }

    ring_buf_init(&uart->rx_ring, sizeof(uart->rx_ring_buf), uart->rx_ring_buf);
    ring_buf_init(&uart->tx_ring, sizeof(uart->tx_ring_buf), uart->tx_ring_buf);
    k_sem_init(&uart->tx_evt_sem, 0, 1);
    k_sem_init(&uart->rx_evt_sem, 0, 1);
    k_mutex_init(&uart->tx_mutex);
    k_mutex_init(&uart->rx_mutex);
    
    uart->handle = init->handle;
    uart->rx_timeout = init->rx_timeout;

    switch(init->mode) {
    case UART_MODE_DMA:
        ret = uart_dma_init(uart, init);
        if (ret < 0) { 
            return ret;
        }
        break;

    case UART_MODE_INTERRUPT:
        ret = uart_interrupt_init(uart, init);
        if (ret < 0) { 
            return ret;
        }
        break;
    
    default:
        return -EINVAL;
    }

    uart->init = true;
    return 0;
}

int uart_send(uart_t *uart, uint8_t *buf, size_t buf_size)
{
    if (uart == NULL || buf == NULL) {
        return -EINVAL;
    }

    if (uart->init == false) {
        return -ENOTSUP;
    }

    if (buf_size == 0) {
        return 0;
    }

    int ret = 0;

    k_mutex_lock(&uart->tx_mutex, K_FOREVER);
    ret = uart->tx(uart, buf, buf_size);
    k_mutex_unlock(&uart->tx_mutex);

    return ret;
}

int uart_receive(uart_t *uart, uint8_t *buf, size_t buf_size)
{
    if (uart == NULL || buf == NULL) {
        return -EINVAL;
    }

    if (uart->init == false) {
        return -ENOTSUP;
    }

    if (buf_size == 0) {
        return 0;
    }

    int ret = 0;

    k_mutex_lock(&uart->rx_mutex, K_FOREVER);
    ret = uart->rx(uart, buf, buf_size);
    k_mutex_unlock(&uart->rx_mutex);

    return ret;
}