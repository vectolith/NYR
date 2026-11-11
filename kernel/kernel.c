#include <stdbool.h>
#include <stdint.h>
#include "include/limine.h"
#include "include/memory.h"

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

LIMINE_REQUEST
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = 0
};

LIMINE_REQUEST
static volatile struct limine_memmap_request memory_map_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
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

static void serial_write_u64(uint64_t value) {
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

static void serial_write_hex(uint64_t value) {
    static const char hex[] = "0123456789abcdef";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(hex[(value >> shift) & 0xfu]);
    }
}

static void serial_line(const char *label, uint64_t value) {
    serial_write(label);
    serial_write_u64(value);
    serial_write("\r\n");
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

static __attribute__((noreturn)) void halt_forever(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

__attribute__((noreturn))
void nyr_kernel_main(void) {
    serial_init();
    serial_write("NYR: kernel entered\r\n");

    if (!LIMINE_BASE_REVISION_SUPPORTED(limine_base_revision)) {
        serial_write("NYR: unsupported Limine base revision\r\n");
        halt_forever();
    }

    struct limine_framebuffer_response *fb_response = framebuffer_request.response;
    if (fb_response != 0 && fb_response->framebuffer_count > 0 &&
        fb_response->framebuffers != 0 && fb_response->framebuffers[0] != 0) {
        struct limine_framebuffer *fb = fb_response->framebuffers[0];

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

    struct limine_hhdm_response *hhdm = hhdm_request.response;
    struct limine_memmap_response *memory_map = memory_map_request.response;
    if (hhdm == 0 || memory_map == 0) {
        serial_write("NYR: memory map or HHDM response unavailable\r\n");
        halt_forever();
    }

    serial_write("NYR: HHDM offset ");
    serial_write_hex(hhdm->offset);
    serial_write("\r\n");
    serial_line("NYR: Limine memory-map entry count ", memory_map->entry_count);
    serial_write("NYR: starting physical memory manager\r\n");

    if (!nyr_pmm_init(memory_map, hhdm->offset)) {
        serial_write("NYR: physical memory manager initialization failed\r\n");
        halt_forever();
    }

    static struct nyr_pmm_stats stats;
    if (!nyr_pmm_get_stats(&stats)) {
        serial_write("NYR: physical memory manager statistics unavailable\r\n");
        halt_forever();
    }

    serial_line("NYR: PMM managed frames ", stats.managed_frames);
    serial_line("NYR: PMM free frames ", stats.free_frames);
    serial_line("NYR: PMM allocated frames ", stats.allocated_frames);

    if (!nyr_pmm_self_test()) {
        serial_write("NYR: PMM self-test failed\r\n");
        halt_forever();
    }
    serial_write("NYR: PMM self-test passed\r\n");
    serial_write("NYR: boot milestone reached; idle halt loop\r\n");
    halt_forever();
}
