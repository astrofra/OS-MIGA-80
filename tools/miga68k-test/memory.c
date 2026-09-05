#include "memory.h"

#include <stdio.h>
#include <string.h>

#include "m68k.h"

#define MIGA68K_ADDRESS_SPACE_SIZE ((size_t)UINT32_C(0x01000000))

static uint8_t memory_bytes[MIGA68K_ADDRESS_SPACE_SIZE];
static int memory_faulted;
static char memory_fault_text[128];

static int range_within(uint32_t address, size_t width, uint32_t start,
                        uint32_t end)
{
    const uint64_t first = address;
    const uint64_t after = first + width;

    return first >= start && after <= end;
}

static int readable(uint32_t address, size_t width)
{
    return range_within(address, width, MIGA68K_VECTOR_START,
                        MIGA68K_VECTOR_END) ||
           range_within(address, width, MIGA68K_CODE_START,
                        MIGA68K_CODE_END) ||
           range_within(address, width, MIGA68K_DATA_START,
                        MIGA68K_DATA_END) ||
           range_within(address, width, MIGA68K_STACK_START,
                        MIGA68K_STACK_END);
}

static int writable(uint32_t address, size_t width)
{
    return range_within(address, width, MIGA68K_VECTOR_START,
                        MIGA68K_VECTOR_END) ||
           range_within(address, width, MIGA68K_DATA_START,
                        MIGA68K_DATA_END) ||
           range_within(address, width, MIGA68K_STACK_START,
                        MIGA68K_STACK_END);
}

static int aligned(uint32_t address, size_t width)
{
    return width == 1U || (address & 1U) == 0U;
}

static void record_fault(const char *operation, uint32_t address, size_t width)
{
    if (!memory_faulted) {
        (void)snprintf(memory_fault_text, sizeof(memory_fault_text),
                       "%s of %lu byte(s) at 0x%08x", operation,
                       (unsigned long)width, (unsigned int)address);
        memory_faulted = 1;
    }
    m68k_end_timeslice();
}

static uint32_t read_value(uint32_t address, size_t width,
                           const char *operation)
{
    uint32_t value = 0;
    size_t index;

    if (!aligned(address, width)) {
        record_fault("unaligned read", address, width);
        return 0;
    }
    if (!readable(address, width)) {
        record_fault(operation, address, width);
        return 0;
    }

    for (index = 0; index < width; ++index) {
        value = (value << 8) | memory_bytes[address + index];
    }
    return value;
}

static void write_value(uint32_t address, uint32_t value, size_t width,
                        const char *operation)
{
    size_t index;

    if (!aligned(address, width)) {
        record_fault("unaligned write", address, width);
        return;
    }
    if (!writable(address, width)) {
        record_fault(operation, address, width);
        return;
    }

    for (index = 0; index < width; ++index) {
        const unsigned int shift = (unsigned int)((width - index - 1U) * 8U);
        memory_bytes[address + index] = (uint8_t)(value >> shift);
    }
}

void miga68k_memory_reset(uint8_t poison)
{
    (void)memset(memory_bytes, poison, sizeof(memory_bytes));
    memory_faulted = 0;
    memory_fault_text[0] = '\0';
}

int miga68k_memory_load_code(uint32_t address, const uint8_t *data,
                             size_t size)
{
    if (data == NULL || !range_within(address, size, MIGA68K_CODE_START,
                                      MIGA68K_CODE_END)) {
        return 0;
    }
    (void)memcpy(&memory_bytes[address], data, size);
    return 1;
}

int miga68k_memory_host_write_u32(uint32_t address, uint32_t value)
{
    if (!aligned(address, 4U) || !writable(address, 4U)) {
        return 0;
    }
    memory_bytes[address] = (uint8_t)(value >> 24);
    memory_bytes[address + 1U] = (uint8_t)(value >> 16);
    memory_bytes[address + 2U] = (uint8_t)(value >> 8);
    memory_bytes[address + 3U] = (uint8_t)value;
    return 1;
}

int miga68k_memory_host_read_u32(uint32_t address, uint32_t *value)
{
    if (value == NULL || !aligned(address, 4U) ||
        !readable(address, 4U)) {
        return 0;
    }
    *value = ((uint32_t)memory_bytes[address] << 24) |
             ((uint32_t)memory_bytes[address + 1U] << 16) |
             ((uint32_t)memory_bytes[address + 2U] << 8) |
             (uint32_t)memory_bytes[address + 3U];
    return 1;
}

int miga68k_memory_host_write_u8(uint32_t address, uint8_t value)
{
    if (!writable(address, 1U)) {
        return 0;
    }
    memory_bytes[address] = value;
    return 1;
}

int miga68k_memory_host_fill(uint32_t address, size_t size, uint8_t value)
{
    if (!writable(address, size)) {
        return 0;
    }
    (void)memset(&memory_bytes[address], value, size);
    return 1;
}

uint32_t miga68k_memory_host_checksum(uint32_t address, size_t size)
{
    uint32_t hash = UINT32_C(2166136261);
    size_t index;

    if (!readable(address, size)) {
        return 0U;
    }
    for (index = 0U; index < size; ++index) {
        hash ^= memory_bytes[address + index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

int miga68k_memory_range_is(uint32_t address, size_t size, uint8_t value)
{
    size_t index;

    if (!readable(address, size)) {
        return 0;
    }
    for (index = 0; index < size; ++index) {
        if (memory_bytes[address + index] != value) {
            return 0;
        }
    }
    return 1;
}

int miga68k_memory_is_executable(uint32_t address)
{
    return range_within(address, 2U, MIGA68K_CODE_START, MIGA68K_CODE_END) &&
           aligned(address, 2U);
}

int miga68k_memory_has_fault(void)
{
    return memory_faulted;
}

const char *miga68k_memory_fault(void)
{
    return memory_fault_text;
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    return read_value(address, 1U, "invalid read");
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    return read_value(address, 2U, "invalid read");
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    return read_value(address, 4U, "invalid read");
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    write_value(address, value, 1U, "invalid write");
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    write_value(address, value, 2U, "invalid write");
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    write_value(address, value, 4U, "invalid write");
}

unsigned int m68k_read_disassembler_8(unsigned int address)
{
    return read_value(address, 1U, "invalid disassembler read");
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return read_value(address, 2U, "invalid disassembler read");
}

unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return read_value(address, 4U, "invalid disassembler read");
}
