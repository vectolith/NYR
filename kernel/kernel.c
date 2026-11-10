#include "include/nyr.h"

static volatile unsigned char *fb;
static uint32_t pitch;

static void fill(uint32_t rgb, uint32_t w, uint32_t h) {
    for (uint32_t y=0; y<h; ++y)
        for (uint32_t x=0; x<w; ++x) {
            volatile uint32_t *p=(volatile uint32_t *)(fb + y*pitch + x*4);
            *p=rgb;
        }
}

__attribute__((noreturn)) void nyr_kernel_main(nyr_boot_info *boot) {
    if (boot && boot->magic == 0x4E595230 && boot->framebuffer && boot->bpp == 32) {
        fb=(volatile unsigned char*)boot->framebuffer;
        pitch=boot->pitch;
        fill(0x00101824, boot->width, boot->height);
    }
    for (;;) __asm__ volatile ("cli; hlt");
}
