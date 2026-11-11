#include <stdint.h>
#include "include/serial.h"

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void nyr_serial_init(void) {
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x80);
    outb(0x3f8, 0x01);
    outb(0x3f9, 0x00);
    outb(0x3fb, 0x03);
    outb(0x3fa, 0xc7);
    outb(0x3fc, 0x0b);
}

static void serial_putc(char c) {
    unsigned int timeout = 1000000;
    while (!(inb(0x3fd) & 0x20) && --timeout) {
    }
    if (timeout != 0) {
        outb(0x3f8, (uint8_t)c);
    }
}

void nyr_serial_write(const char *text) {
    while (*text != '\0') {
        serial_putc(*text++);
    }
}

void nyr_serial_write_u64(uint64_t value) {
    char digits[20];
    unsigned int count = 0;
    if (value == 0) {
        serial_putc('0');
        return;
    }
    while (value != 0 && count < sizeof(digits)) {
        digits[count++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (count != 0) {
        serial_putc(digits[--count]);
    }
}

void nyr_serial_write_hex(uint64_t value) {
    static const char hex[] = "0123456789abcdef";
    nyr_serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(hex[(value >> shift) & 0xfu]);
    }
}
