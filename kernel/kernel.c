#include <stdbool.h>
#include <stdint.h>
#include "include/limine.h"

#define LIMINE_REQUEST __attribute__((used, section(".limine_requests")))
#define LIMINE_MARKER __attribute__((used))

LIMINE_MARKER __attribute__((section(".limine_requests_start")))
static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;

LIMINE_REQUEST
static volatile uint64_t limine_base_revision[] = LIMINE_BASE_REVISION(6);

LIMINE_REQUEST
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0,
    .response = 0
};

LIMINE_MARKER __attribute__((section(".limine_requests_end")))
static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static void serial_init(void) {
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

static void serial_write(const char *text) {
    while (*text != '\0') {
        serial_putc(*text++);
    }
}

static void framebuffer_fill(struct limine_framebuffer *fb, uint32_t pixel) {
    volatile uint32_t *pixels = (volatile uint32_t *)fb->address;
    const uint64_t stride = fb->pitch / sizeof(uint32_t);

    for (uint64_t y = 0; y < fb->height; ++y) {
        for (uint64_t x = 0; x < fb->width; ++x) {
            pixels[y * stride + x] = pixel;
        }
    }
}

__attribute__((noreturn))
void nyr_kernel_main(void) {
    serial_init();
    serial_write("NYR: kernel entered\r\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        serial_write("NYR: unsupported Limine base revision\r\n");
        for (;;) {
            __asm__ volatile ("cli; hlt");
        }
    }

    struct limine_framebuffer_response *response = framebuffer_request.response;
    if (response != 0 && response->framebuffer_count > 0 &&
        response->framebuffers != 0 && response->framebuffers[0] != 0) {
        struct limine_framebuffer *fb = response->framebuffers[0];

        if (fb->address != 0 && fb->bpp == 32 &&
            fb->memory_model == LIMINE_FRAMEBUFFER_RGB &&
            fb->pitch >= fb->width * sizeof(uint32_t)) {
            framebuffer_fill(fb, 0x00101824);
            serial_write("NYR: framebuffer initialized\r\n");
        } else {
            serial_write("NYR: framebuffer format unsupported\r\n");
        }
    } else {
        serial_write("NYR: framebuffer unavailable\r\n");
    }

    serial_write("NYR: boot milestone reached; idle halt loop\r\n");
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}
