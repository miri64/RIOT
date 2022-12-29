#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>

#include "isrpipe.h"
#include "stdio_uart.h"

#include "irq_arch.h"
#include "hal/interrupt_controller_types.h"
#include "hal/interrupt_controller_ll.h"
#include "vendor/esp/common_macros.h"
#include "rom/ets_sys.h"

typedef struct {
    volatile union {
        uint8_t rdwr;
        uint32_t reg;
    } EP1; /* 0x0 */
    volatile union {
        struct {
            uint32_t WR_DONE:1;
            uint32_t IN_EP_DATA_FREE:1;
            uint32_t OUT_EP_DATA_AVAIL:1;
        } bit;
        uint32_t reg;
    } EP1_CONF; /* 0x4 */
    volatile union {
        struct {
            uint16_t IN_FLUSH_INT_RAW:1;
            uint16_t SOF_INT_RAW:1;
            uint16_t SERIAL_OUT_RECV_PKT_INT_ST:1;
        } bit;
        uint32_t reg;
    } INT_RAW;  /* 0x8 */
    volatile union {
        uint32_t reg;
    } INT;  /* 0xc */
    volatile union {
        uint32_t reg;
    } INT_ENA; /* 0x10 */
    volatile union {
        uint32_t reg;
    } INT_CLR;  /* 0x14 */
} usb_serial_jtag;

#define USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT_CLR (1 << 2)

/* TODO: move to ESP32C3 */
#define USB_JTAG_SERIAL ((usb_serial_jtag *)0x60043000)

static uint8_t _rx_buf_mem[STDIO_UART_RX_BUFSIZE];
static isrpipe_t stdio_serial_isrpipe = ISRPIPE_INIT(_rx_buf_mem);

static void write_byte(uint8_t c)
{
    while (!USB_JTAG_SERIAL->EP1_CONF.bit.IN_EP_DATA_FREE) {}
    USB_JTAG_SERIAL->EP1.reg = c;
}


ssize_t stdio_write(const void *buffer, size_t len)
{
    const uint8_t *c = buffer;
    const uint8_t *end = c + len;

    while (c != end) {
        write_byte(*c++);
    }

    USB_JTAG_SERIAL->EP1_CONF.bit.WR_DONE = 1;
    return len;
}

ssize_t stdio_read(void* buffer, size_t count)
{
    if (1) { // IS_USED(MODULE_STDIO_UART_RX)) {
        return (ssize_t)isrpipe_read(&stdio_serial_isrpipe, buffer, count);
    }

    /* TODO */

    return -ENOTSUP;
}

static void IRAM _serial_intr_handler(void *arg)
{
    (void)arg;

    irq_isr_enter();

    while (USB_JTAG_SERIAL->EP1_CONF.bit.OUT_EP_DATA_AVAIL) {
        isrpipe_write_one(&stdio_serial_isrpipe, USB_JTAG_SERIAL->EP1.reg);
    }

    USB_JTAG_SERIAL->INT_CLR.reg = USB_SERIAL_JTAG_SERIAL_OUT_RECV_PKT_INT_CLR;

    irq_isr_exit();
}

void stdio_init(void)
{
    /* route all UART interrupt sources to same the CPU interrupt */
    intr_matrix_set(PRO_CPU_NUM, ETS_USB_SERIAL_JTAG_INTR_SOURCE, CPU_INUM_SERIAL_JTAG);
    /* we have to enable therefore the CPU interrupt here */
    intr_cntrl_ll_set_int_handler(CPU_INUM_SERIAL_JTAG, _serial_intr_handler, NULL);
    intr_cntrl_ll_enable_interrupts(BIT(CPU_INUM_SERIAL_JTAG));
#ifdef SOC_CPU_HAS_FLEXIBLE_INTC
    /* set interrupt level */
    intr_cntrl_ll_set_int_level(CPU_INUM_SERIAL_JTAG, 1);
#endif
}
