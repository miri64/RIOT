#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>

typedef struct {
    volatile union {
        uint8_t rdwr;
        uint32_t reg;
    } EP1;
    volatile union {
        struct {
            uint32_t WR_DONE:1;
            uint32_t IN_EP_DATA_FREE:1;
            uint32_t OUT_EP_DATA_AVAIL:1;
        } bit;
        uint32_t reg;
    } EP1_CONF;
} usb_serial_jtag;

/* TODO: move to ESP32C3 */
#define USB_JTAG_SERIAL ((usb_serial_jtag *)0x60043000)

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
    (void)buffer;
    (void)count;

    /* TODO */

    return -ENOTSUP;
}

void stdio_init(void)
{
    /* TODO */
}
