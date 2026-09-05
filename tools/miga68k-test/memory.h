#ifndef MIGA80_MIGA68K_MEMORY_H
#define MIGA80_MIGA68K_MEMORY_H

#include <stddef.h>
#include <stdint.h>

#define MIGA68K_VECTOR_START UINT32_C(0x000000)
#define MIGA68K_VECTOR_END UINT32_C(0x000400)
#define MIGA68K_CODE_START UINT32_C(0x001000)
#define MIGA68K_CODE_END UINT32_C(0x010000)
#define MIGA68K_DATA_START UINT32_C(0x010000)
#define MIGA68K_DATA_END UINT32_C(0x080000)
#define MIGA68K_STACK_START UINT32_C(0x080000)
#define MIGA68K_STACK_END UINT32_C(0x090000)

void miga68k_memory_reset(uint8_t poison);
int miga68k_memory_load_code(uint32_t address, const uint8_t *data,
                             size_t size);
int miga68k_memory_host_write_u32(uint32_t address, uint32_t value);
int miga68k_memory_host_read_u32(uint32_t address, uint32_t *value);
int miga68k_memory_host_write_u8(uint32_t address, uint8_t value);
int miga68k_memory_host_fill(uint32_t address, size_t size, uint8_t value);
uint32_t miga68k_memory_host_checksum(uint32_t address, size_t size);
int miga68k_memory_range_is(uint32_t address, size_t size, uint8_t value);
int miga68k_memory_is_executable(uint32_t address);
int miga68k_memory_has_fault(void);
const char *miga68k_memory_fault(void);

#endif
