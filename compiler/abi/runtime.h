#ifndef MIGA80_COMPILER_ABI_RUNTIME_H
#define MIGA80_COMPILER_ABI_RUNTIME_H

/* Shared by C, the direct encoder, and the preprocessed 68020 trampoline. */
#define MIGA80_ABI_RUNTIME_FAULT_HANDLER_OFFSET 0
#define MIGA80_ABI_RUNTIME_CONTEXT_MIN_SIZE 4
#define MIGA80_ABI_RUNTIME_PSET_HANDLER_OFFSET 4
#define MIGA80_ABI_RUNTIME_PIXEL_BUFFER_OFFSET 8
#define MIGA80_ABI_RUNTIME_GRAPHICS_CONTEXT_SIZE 12
#define MIGA80_ABI_RUNTIME_BUDGET_OFFSET 12
#define MIGA80_ABI_RUNTIME_STACK_TOP_OFFSET 16
#define MIGA80_ABI_RUNTIME_HOST_STACK_OFFSET 20
#define MIGA80_ABI_RUNTIME_FAULT_CODE_OFFSET 24
#define MIGA80_ABI_RUNTIME_FAULT_LINE_OFFSET 28
#define MIGA80_ABI_RUNTIME_FAULT_COLUMN_OFFSET 32
#define MIGA80_ABI_RUNTIME_GUARDED_CONTEXT_SIZE 36
#define MIGA80_ABI_FAULT_DIVISION_BY_ZERO 1
#define MIGA80_ABI_FAULT_CONVERSION_OUT_OF_RANGE 2
#define MIGA80_ABI_FAULT_EXECUTION_BUDGET 3

#ifndef __ASSEMBLER__
#include <stdint.h>

/* Target addresses are fixed-width words even when inspected on the host. */
struct miga80_runtime_context {
    uint32_t fault_handler;
    uint32_t pset_handler;
    uint32_t pixel_buffer;
    uint32_t budget;
    uint32_t stack_top;
    uint32_t host_stack;
    uint32_t fault_code;
    uint32_t fault_line;
    uint32_t fault_column;
};
#endif
#endif
