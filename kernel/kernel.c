#include "include/limine.h"
#define NYR_SECTION __attribute__((used, section(".limine_reqs")))
NYR_SECTION static volatile uint64_t limine_requests_start[] = LIMINE_REQUESTS_START_MARKER;
NYR_SECTION static volatile uint64_t limine_base_revision[3] = LIMINE_BASE_REVISION(3);
NYR_SECTION static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0, .response = 0
};
NYR_SECTION static volatile uint64_t limine_requests_end[] = LIMINE_REQUESTS_END_MARKER;
static inline void outb(uint16_t port, uint8_t value) { __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port)); }
static inline uint8_t inb(uint16_t port) { uint8_t value; __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port)); return value; }
static void serial_init(void) {
    outb(0x3f9, 0); outb(0x3fb, 0x80); outb(0x3f8, 1);
    outb(0x3f9, 0); outb(0x3fb, 3); outb(0x3fa, 0xc7); outb(0x3fc, 0x0b);
}
static void serial_putc(char c) { unsigned int n = 1000000; while (!(inb(0x3fd) & 0x20) && --n) {} if (n) outb(0x3f8, (uint8_t)c); }
static void serial_write(const char *s) { while (*s) serial_putc(*s++); }
static void fill(uint32_t rgb, struct limine_framebuffer *fb) {
    for (uint64_t y = 0; y < fb->height; ++y)
        for (uint64_t x = 0; x < fb->width; ++x)
            *(volatile uint32_t *)((volatile uint8_t *)fb->address + y * fb->pitch + x * 4) = rgb;
}
__attribute__((noreturn)) void nyr_kernel_main(void) {
    serial_init();
    serial_write("NYR: kernel entered\r\n");
    if (limine_base_revision[2] != 0) {
        serial_write("NYR: Limine base revision unsupported\r\n");
        for (;;) __asm__ volatile ("cli; hlt");
    }
    struct limine_framebuffer_response *r = framebuffer_request.response;
    if (r && r->framebuffer_count && r->framebuffers && r->framebuffers[0] &&
        r->framebuffers[0]->address && r->framebuffers[0]->bpp == 32 &&
        r->framebuffers[0]->memory_model == LIMINE_FRAMEBUFFER_RGB) {
        fill(0x00101824, r->framebuffers[0]);
        serial_write("NYR: framebuffer initialized\r\n");
    } else serial_write("NYR: framebuffer unavailable\r\n");
    serial_write("NYR: boot milestone reached; idle halt loop\r\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
