#ifndef NYR_CONSOLE_H
#define NYR_CONSOLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define NYR_COLOR_BG_DARK_NAVY  0x00101824u
#define NYR_COLOR_FG_WHITE      0x00E0E6EDu
#define NYR_COLOR_FG_CYAN       0x004DD0E1u
#define NYR_COLOR_FG_GREEN      0x0081C784u
#define NYR_COLOR_FG_GOLD       0x00FFD54Fu
#define NYR_COLOR_FG_RED        0x00E57373u

void nyr_console_init(void *framebuffer_addr, uint64_t width, uint64_t height, uint64_t pitch);
bool nyr_console_is_ready(void);
void nyr_console_set_colors(uint32_t fg, uint32_t bg);
void nyr_console_clear(void);
void nyr_console_putc(char c);
void nyr_console_write(const char *text);

/* Unified kernel logging: outputs to both serial COM1 and the graphical console */
void nyr_kprint(const char *text);
void nyr_kprint_u64(uint64_t val);
void nyr_kprint_hex(uint64_t val);
void nyr_kprintf(const char *fmt, ...);

#endif /* NYR_CONSOLE_H */
