#include "include/console.h"
#include "include/serial.h"
#include "include/lock.h"

#include <stdarg.h>

#define FONT_WIDTH  8u
#define FONT_HEIGHT 16u

/*
 * Compact 8x16 bitmap font covering ASCII 32..126.
 * Each character is 16 bytes (one byte per row).
 */
static const uint8_t FONT_8X16[95][16] = {
    /* 32 ' ' */ {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 33 '!' */ {0,0,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0x18,0x18,0,0,0,0},
    /* 34 '"' */ {0,0x66,0x66,0x66,0x24,0,0,0,0,0,0,0,0,0,0,0},
    /* 35 '#' */ {0,0,0x6c,0x6c,0xfe,0x6c,0x6c,0x6c,0xfe,0x6c,0x6c,0,0,0,0,0},
    /* 36 '$' */ {0,0x18,0x7e,0xc0,0xc0,0x7c,0x06,0x06,0x7e,0x18,0,0,0,0,0,0},
    /* 37 '%' */ {0,0,0xc6,0xcc,0x18,0x30,0x60,0xc6,0xcc,0,0,0,0,0,0,0},
    /* 38 '&' */ {0,0,0x38,0x6c,0x38,0x76,0xdc,0xcc,0x76,0,0,0,0,0,0,0},
    /* 39 '\''*/ {0,0x18,0x18,0x30,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 40 '(' */ {0,0,0x0c,0x18,0x30,0x30,0x30,0x30,0x18,0x0c,0,0,0,0,0,0},
    /* 41 ')' */ {0,0,0x30,0x18,0x0c,0x0c,0x0c,0x0c,0x18,0x30,0,0,0,0,0,0},
    /* 42 '*' */ {0,0,0,0x66,0x3c,0xff,0x3c,0x66,0,0,0,0,0,0,0,0},
    /* 43 '+' */ {0,0,0,0x18,0x18,0x7e,0x18,0x18,0,0,0,0,0,0,0,0},
    /* 44 ',' */ {0,0,0,0,0,0,0,0,0x18,0x18,0x30,0,0,0,0,0},
    /* 45 '-' */ {0,0,0,0,0,0x7e,0,0,0,0,0,0,0,0,0,0},
    /* 46 '.' */ {0,0,0,0,0,0,0,0,0x18,0x18,0,0,0,0,0,0},
    /* 47 '/' */ {0,0,0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0,0,0,0,0,0,0},
    /* 48 '0' */ {0,0,0x3c,0x66,0xc3,0xc3,0xc3,0x66,0x3c,0,0,0,0,0,0,0},
    /* 49 '1' */ {0,0,0x18,0x38,0x18,0x18,0x18,0x18,0x7e,0,0,0,0,0,0,0},
    /* 50 '2' */ {0,0,0x7c,0xc6,0x06,0x1c,0x30,0x60,0xfe,0,0,0,0,0,0,0},
    /* 51 '3' */ {0,0,0x7e,0x06,0x06,0x3c,0x06,0x06,0x7e,0,0,0,0,0,0,0},
    /* 52 '4' */ {0,0,0x0c,0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0,0,0,0,0,0,0},
    /* 53 '5' */ {0,0,0xfe,0xc0,0xc0,0xfc,0x06,0x06,0xfc,0,0,0,0,0,0,0},
    /* 54 '6' */ {0,0,0x38,0x60,0xc0,0xfc,0xc6,0xc6,0x7c,0,0,0,0,0,0,0},
    /* 55 '7' */ {0,0,0xfe,0x06,0x0c,0x18,0x30,0x60,0x60,0,0,0,0,0,0,0},
    /* 56 '8' */ {0,0,0x7c,0xc6,0xc6,0x7c,0xc6,0xc6,0x7c,0,0,0,0,0,0,0},
    /* 57 '9' */ {0,0,0x7c,0xc6,0xc6,0x7e,0x06,0x0c,0x78,0,0,0,0,0,0,0},
    /* 58 ':' */ {0,0,0,0x18,0x18,0,0,0x18,0x18,0,0,0,0,0,0,0},
    /* 59 ';' */ {0,0,0,0x18,0x18,0,0,0x18,0x18,0x30,0,0,0,0,0,0},
    /* 60 '<' */ {0,0,0x0e,0x1c,0x38,0x70,0x38,0x1c,0x0e,0,0,0,0,0,0,0},
    /* 61 '=' */ {0,0,0,0x7e,0,0x7e,0,0,0,0,0,0,0,0,0,0},
    /* 62 '>' */ {0,0,0x70,0x38,0x1c,0x0e,0x1c,0x38,0x70,0,0,0,0,0,0,0},
    /* 63 '?' */ {0,0,0x7c,0xc6,0x0c,0x18,0x18,0,0x18,0,0,0,0,0,0,0},
    /* 64 '@' */ {0,0,0x7c,0xc6,0xde,0xde,0xdc,0xc0,0x7c,0,0,0,0,0,0,0},
    /* 65 'A' */ {0,0,0x38,0x6c,0xc6,0xfe,0xc6,0xc6,0xc6,0,0,0,0,0,0,0},
    /* 66 'B' */ {0,0,0xfc,0x66,0x66,0x7c,0x66,0x66,0xfc,0,0,0,0,0,0,0},
    /* 67 'C' */ {0,0,0x3c,0x66,0xc0,0xc0,0xc0,0x66,0x3c,0,0,0,0,0,0,0},
    /* 68 'D' */ {0,0,0xf8,0x6c,0x66,0x66,0x66,0x6c,0xf8,0,0,0,0,0,0,0},
    /* 69 'E' */ {0,0,0xfe,0x62,0x68,0x78,0x68,0x62,0xfe,0,0,0,0,0,0,0},
    /* 70 'F' */ {0,0,0xfe,0x62,0x68,0x78,0x68,0x60,0xf0,0,0,0,0,0,0,0},
    /* 71 'G' */ {0,0,0x3c,0x66,0xc0,0xc0,0xce,0x66,0x3a,0,0,0,0,0,0,0},
    /* 72 'H' */ {0,0,0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0,0,0,0,0,0,0},
    /* 73 'I' */ {0,0,0x7e,0x18,0x18,0x18,0x18,0x18,0x7e,0,0,0,0,0,0,0},
    /* 74 'J' */ {0,0,0x1e,0x06,0x06,0x06,0xc6,0xc6,0x7c,0,0,0,0,0,0,0},
    /* 75 'K' */ {0,0,0xc6,0xcc,0xd8,0xf0,0xd8,0xcc,0xc6,0,0,0,0,0,0,0},
    /* 76 'L' */ {0,0,0xf0,0x60,0x60,0x60,0x62,0x66,0xfe,0,0,0,0,0,0,0},
    /* 77 'M' */ {0,0,0xc6,0xee,0xfe,0xd6,0xc6,0xc6,0xc6,0,0,0,0,0,0,0},
    /* 78 'N' */ {0,0,0xc6,0xe6,0xf6,0xde,0xce,0xc6,0xc6,0,0,0,0,0,0,0},
    /* 79 'O' */ {0,0,0x38,0x6c,0xc6,0xc6,0xc6,0x6c,0x38,0,0,0,0,0,0,0},
    /* 80 'P' */ {0,0,0xfc,0x66,0x66,0x7c,0x60,0x60,0xf0,0,0,0,0,0,0,0},
    /* 81 'Q' */ {0,0,0x7c,0xc6,0xc6,0xc6,0xc6,0xce,0x7c,0x06,0,0,0,0,0,0},
    /* 82 'R' */ {0,0,0xfc,0x66,0x66,0x7c,0x6c,0x66,0xc6,0,0,0,0,0,0,0},
    /* 83 'S' */ {0,0,0x7c,0xc6,0x60,0x38,0x0c,0xc6,0x7c,0,0,0,0,0,0,0},
    /* 84 'T' */ {0,0,0x7e,0x5a,0x18,0x18,0x18,0x18,0x3c,0,0,0,0,0,0,0},
    /* 85 'U' */ {0,0,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0,0,0,0,0,0,0},
    /* 86 'V' */ {0,0,0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x10,0,0,0,0,0,0,0},
    /* 87 'W' */ {0,0,0xc6,0xc6,0xc6,0xd6,0xfe,0xee,0xc6,0,0,0,0,0,0,0},
    /* 88 'X' */ {0,0,0xc6,0xee,0x7c,0x38,0x7c,0xee,0xc6,0,0,0,0,0,0,0},
    /* 89 'Y' */ {0,0,0x66,0x66,0x66,0x3c,0x18,0x18,0x3c,0,0,0,0,0,0,0},
    /* 90 'Z' */ {0,0,0xfe,0x06,0x0c,0x18,0x30,0x60,0xfe,0,0,0,0,0,0,0},
    /* 91 '[' */ {0,0,0x3c,0x30,0x30,0x30,0x30,0x30,0x3c,0,0,0,0,0,0,0},
    /* 92 '\'*/ {0,0,0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0,0,0,0,0,0,0},
    /* 93 ']' */ {0,0,0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0,0,0,0,0,0,0},
    /* 94 '^' */ {0,0x38,0x6c,0xc6,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 95 '_' */ {0,0,0,0,0,0,0,0,0,0,0,0xff,0,0,0,0},
    /* 96 '`' */ {0,0x30,0x18,0x0c,0,0,0,0,0,0,0,0,0,0,0,0},
    /* 97 'a' */ {0,0,0,0x78,0x0c,0x7c,0xcc,0xcc,0x76,0,0,0,0,0,0,0},
    /* 98 'b' */ {0,0,0xe0,0x60,0x7c,0x66,0x66,0x66,0xdc,0,0,0,0,0,0,0},
    /* 99 'c' */ {0,0,0,0x78,0xcc,0xc0,0xc0,0xcc,0x78,0,0,0,0,0,0,0},
    /* 100 'd'*/ {0,0,0x1c,0x0c,0x7c,0xcc,0xcc,0xcc,0x76,0,0,0,0,0,0,0},
    /* 101 'e'*/ {0,0,0,0x78,0xcc,0xfc,0xc0,0xcc,0x78,0,0,0,0,0,0,0},
    /* 102 'f'*/ {0,0,0x38,0x6c,0x60,0xf0,0x60,0x60,0xf0,0,0,0,0,0,0,0},
    /* 103 'g'*/ {0,0,0,0x76,0xcc,0xcc,0x7c,0x0c,0x78,0,0,0,0,0,0,0},
    /* 104 'h'*/ {0,0,0xe0,0x60,0x6c,0x76,0x66,0x66,0xe6,0,0,0,0,0,0,0},
    /* 105 'i'*/ {0,0,0x18,0,0x38,0x18,0x18,0x18,0x3c,0,0,0,0,0,0,0},
    /* 106 'j'*/ {0,0,0x0c,0,0x1c,0x0c,0x0c,0x0c,0xcc,0x78,0,0,0,0,0,0},
    /* 107 'k'*/ {0,0,0xe0,0x60,0x66,0x6c,0x78,0x6c,0xe6,0,0,0,0,0,0,0},
    /* 108 'l'*/ {0,0,0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0,0,0,0,0,0,0},
    /* 109 'm'*/ {0,0,0,0xea,0xfe,0xd6,0xc6,0xc6,0xc6,0,0,0,0,0,0,0},
    /* 110 'n'*/ {0,0,0,0xd8,0xec,0xc6,0xc6,0xc6,0xc6,0,0,0,0,0,0,0},
    /* 111 'o'*/ {0,0,0,0x7c,0xc6,0xc6,0xc6,0xc6,0x7c,0,0,0,0,0,0,0},
    /* 112 'p'*/ {0,0,0,0xdc,0x66,0x66,0x7c,0x60,0xf0,0,0,0,0,0,0,0},
    /* 113 'q'*/ {0,0,0,0x76,0xcc,0xcc,0x7c,0x0c,0x1e,0,0,0,0,0,0,0},
    /* 114 'r'*/ {0,0,0,0xdc,0x76,0x66,0x60,0x60,0xf0,0,0,0,0,0,0,0},
    /* 115 's'*/ {0,0,0,0x7c,0xc0,0x78,0x0c,0xc6,0x7c,0,0,0,0,0,0,0},
    /* 116 't'*/ {0,0,0x30,0x30,0xfc,0x30,0x30,0x34,0x18,0,0,0,0,0,0,0},
    /* 117 'u'*/ {0,0,0,0xcc,0xcc,0xcc,0xcc,0xcc,0x76,0,0,0,0,0,0,0},
    /* 118 'v'*/ {0,0,0,0xc6,0xc6,0xc6,0x6c,0x38,0x10,0,0,0,0,0,0,0},
    /* 119 'w'*/ {0,0,0,0xc6,0xc6,0xd6,0xfe,0xfe,0x6c,0,0,0,0,0,0,0},
    /* 120 'x'*/ {0,0,0,0xc6,0x6c,0x38,0x38,0x6c,0xc6,0,0,0,0,0,0,0},
    /* 121 'y'*/ {0,0,0,0xc6,0xc6,0x66,0x3c,0x18,0x30,0x60,0,0,0,0,0,0},
    /* 122 'z'*/ {0,0,0,0xfe,0x0c,0x18,0x30,0x60,0xfe,0,0,0,0,0,0,0},
    /* 123 '{'*/ {0,0,0x0e,0x18,0x18,0x70,0x18,0x18,0x0e,0,0,0,0,0,0,0},
    /* 124 '|'*/ {0,0,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0,0,0,0,0,0,0},
    /* 125 '}'*/ {0,0,0x70,0x18,0x18,0x0e,0x18,0x18,0x70,0,0,0,0,0,0,0},
    /* 126 '~'*/ {0,0x76,0xdc,0,0,0,0,0,0,0,0,0,0,0,0,0}
};

static volatile uint32_t *fb_pixels = 0;
static uint64_t fb_width = 0;
static uint64_t fb_height = 0;
static uint64_t fb_stride = 0;
static uint64_t cursor_col = 0;
static uint64_t cursor_row = 0;
static uint64_t max_cols = 0;
static uint64_t max_rows = 0;
static uint32_t current_fg = NYR_COLOR_FG_WHITE;
static uint32_t current_bg = NYR_COLOR_BG_DARK_NAVY;
static struct nyr_spinlock console_lock = NYR_SPINLOCK_INITIALIZER;
static bool console_ready = false;

void nyr_console_init(void *framebuffer_addr, uint64_t width, uint64_t height, uint64_t pitch) {
    if (framebuffer_addr == 0 || width < FONT_WIDTH || height < FONT_HEIGHT) {
        return;
    }
    fb_pixels = (volatile uint32_t *)framebuffer_addr;
    fb_width = width;
    fb_height = height;
    fb_stride = pitch / sizeof(uint32_t);
    max_cols = width / FONT_WIDTH;
    max_rows = height / FONT_HEIGHT;
    cursor_col = 0;
    cursor_row = 0;
    current_fg = NYR_COLOR_FG_WHITE;
    current_bg = NYR_COLOR_BG_DARK_NAVY;
    console_ready = true;
    nyr_console_clear();
}

bool nyr_console_is_ready(void) {
    return console_ready;
}

void nyr_console_set_colors(uint32_t fg, uint32_t bg) {
    current_fg = fg;
    current_bg = bg;
}

void nyr_console_clear(void) {
    if (!console_ready) return;
    for (uint64_t y = 0; y < fb_height; ++y) {
        for (uint64_t x = 0; x < fb_width; ++x) {
            fb_pixels[y * fb_stride + x] = current_bg;
        }
    }
    cursor_col = 0;
    cursor_row = 0;
}

static void console_scroll_up(void) {
    const uint64_t scroll_pixels = FONT_HEIGHT;
    const uint64_t copy_height = fb_height - scroll_pixels;

    for (uint64_t y = 0; y < copy_height; ++y) {
        for (uint64_t x = 0; x < fb_width; ++x) {
            fb_pixels[y * fb_stride + x] = fb_pixels[(y + scroll_pixels) * fb_stride + x];
        }
    }
    for (uint64_t y = copy_height; y < fb_height; ++y) {
        for (uint64_t x = 0; x < fb_width; ++x) {
            fb_pixels[y * fb_stride + x] = current_bg;
        }
    }
    if (cursor_row > 0) {
        cursor_row--;
    }
}

static void render_glyph(char c, uint64_t col, uint64_t row, uint32_t fg, uint32_t bg) {
    if (c < 32 || c > 126) {
        c = '?';
    }
    const uint8_t *glyph = FONT_8X16[(unsigned char)c - 32];
    uint64_t px = col * FONT_WIDTH;
    uint64_t py = row * FONT_HEIGHT;

    for (uint32_t dy = 0; dy < FONT_HEIGHT; ++dy) {
        uint8_t row_bits = glyph[dy];
        for (uint32_t dx = 0; dx < FONT_WIDTH; ++dx) {
            uint32_t color = (row_bits & (0x80u >> dx)) ? fg : bg;
            fb_pixels[(py + dy) * fb_stride + (px + dx)] = color;
        }
    }
}

void nyr_console_putc(char c) {
    if (!console_ready) return;

    if (c == '\r') {
        cursor_col = 0;
        return;
    }
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= max_rows) {
            console_scroll_up();
        }
        return;
    }
    if (c == '\t') {
        cursor_col = (cursor_col + 8u) & ~7u;
        if (cursor_col >= max_cols) {
            cursor_col = 0;
            cursor_row++;
            if (cursor_row >= max_rows) {
                console_scroll_up();
            }
        }
        return;
    }

    render_glyph(c, cursor_col, cursor_row, current_fg, current_bg);
    cursor_col++;
    if (cursor_col >= max_cols) {
        cursor_col = 0;
        cursor_row++;
        if (cursor_row >= max_rows) {
            console_scroll_up();
        }
    }
}

void nyr_console_write(const char *text) {
    if (!console_ready || text == 0) return;
    nyr_spinlock_acquire(&console_lock);
    while (*text != '\0') {
        nyr_console_putc(*text++);
    }
    nyr_spinlock_release(&console_lock);
}

void nyr_kprint(const char *text) {
    nyr_serial_write(text);
    if (console_ready) {
        nyr_console_write(text);
    }
}

void nyr_kprint_u64(uint64_t val) {
    nyr_serial_write_u64(val);
    if (console_ready) {
        char buf[24];
        unsigned int i = 0;
        if (val == 0) {
            nyr_console_write("0");
            return;
        }
        while (val > 0 && i < sizeof(buf) - 1) {
            buf[i++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
        for (unsigned int j = 0; j < i / 2; ++j) {
            char t = buf[j];
            buf[j] = buf[i - 1 - j];
            buf[i - 1 - j] = t;
        }
        buf[i] = '\0';
        nyr_console_write(buf);
    }
}

void nyr_kprint_hex(uint64_t val) {
    nyr_serial_write_hex(val);
    if (console_ready) {
        char buf[19];
        buf[0] = '0';
        buf[1] = 'x';
        for (int i = 15; i >= 0; --i) {
            uint8_t nibble = (uint8_t)((val >> (i * 4)) & 0xFu);
            buf[2 + (15 - i)] = (char)(nibble < 10 ? '0' + nibble : 'a' + (nibble - 10));
        }
        buf[18] = '\0';
        nyr_console_write(buf);
    }
}

void nyr_kprintf(const char *fmt, ...) {
    if (fmt == 0) return;
    va_list args;
    va_start(args, fmt);

    while (*fmt != '\0') {
        if (*fmt != '%') {
            char c = *fmt++;
            char s[2] = { c, '\0' };
            nyr_kprint(s);
            continue;
        }
        fmt++; /* skip '%' */
        switch (*fmt) {
            case 's': {
                const char *str = va_arg(args, const char *);
                nyr_kprint(str ? str : "(null)");
                break;
            }
            case 'd':
            case 'u': {
                uint64_t val = (uint64_t)va_arg(args, unsigned int);
                nyr_kprint_u64(val);
                break;
            }
            case 'l': {
                fmt++;
                if (*fmt == 'u' || *fmt == 'd') {
                    uint64_t val = va_arg(args, uint64_t);
                    nyr_kprint_u64(val);
                } else if (*fmt == 'x') {
                    uint64_t val = va_arg(args, uint64_t);
                    nyr_kprint_hex(val);
                }
                break;
            }
            case 'x':
            case 'p': {
                uint64_t val = va_arg(args, uint64_t);
                nyr_kprint_hex(val);
                break;
            }
            case 'c': {
                char ch = (char)va_arg(args, int);
                char s[2] = { ch, '\0' };
                nyr_kprint(s);
                break;
            }
            case '%': {
                nyr_kprint("%");
                break;
            }
            default:
                break;
        }
        fmt++;
    }

    va_end(args);
}
