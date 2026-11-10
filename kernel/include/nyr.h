#ifndef NYR_H
#define NYR_H

#include <stdint.h>

#define NYR_BOOT_MAGIC 0x4E595230u

typedef struct nyr_boot_info {
    uint32_t magic;
    uint32_t version;
    uint64_t framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} nyr_boot_info;

#endif
