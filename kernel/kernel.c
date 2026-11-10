#include "include/nyr.h"

static volatile unsigned char *fb;
static uint32_t pitch;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
    const uint16_t com1 = 0x3F8;
    outb(com1 + 1, 0x00);
    outb(com1 + 3, 0x80);
    outb(com1 + 0, 0x01); /* 115200 baud */
    outb(com1 + 1, 0x00);
    outb(com1 + 3, 0x03); /* 8 data bits, no parity, one stop bit */
    outb(com1 + 2, 0xC7);
    outb(com1 + 4, 0x0B);
}

static void serial_putc(char c) {
    unsigned int spins = 1000000;
    while (!(inb(0x3FD) & 0x20) && --spins) { }
    if (spins) outb(0x3F8, (uint8_t)c);
}

static void serial_write(const char *s) {
    while (*s) serial_putc(*s++);
}

static void fill(uint32_t rgb, uint32_t w, uint32_t h) {
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            volatile uint32_t *p = (volatile uint32_t *)(fb + y * pitch + x * 4);
            *p = rgb;
        }
}

__attribute__((noreturn)) void nyr_kernel_main(nyr_boot_info *boot) {
    serial_init();
    serial_write("NYR: kernel entered\r\n");

    if (boot && boot->magic == NYR_BOOT_MAGIC && boot->framebuffer && boot->bpp == 32) {
        fb = (volatile unsigned char *)boot->framebuffer;
        pitch = boot->pitch;
        fill(0x00101824, boot->width, boot->height);
        serial_write("NYR: framebuffer initialized\r\n");
    } else {
        serial_write("NYR: framebuffer unavailable or boot info invalid\r\n");
    }

    for (;;) __asm__ volatile ("cli; hlt");
}
