#ifndef NYR_SERIAL_H
#define NYR_SERIAL_H
#include <stdint.h>
void nyr_serial_init(void);
void nyr_serial_write(const char *text);
void nyr_serial_write_u64(uint64_t value);
void nyr_serial_write_hex(uint64_t value);
#endif
