#ifndef MIGA80_COMPILER_BACKEND_M68K_OPTIMIZED_INTERNAL_H
#define MIGA80_COMPILER_BACKEND_M68K_OPTIMIZED_INTERNAL_H

#include <limits.h>

#include "compiler/value_ir/value_ir.h"

#define MIGA80_DATA_REGISTER_COUNT 8U
#define MIGA80_NO_REGISTER (-1)
#define MIGA80_NO_SPILL_SLOT UINT_MAX

struct allocation_plan {
    int registers[MIGA80_MAX_VALUE_INSTRUCTIONS];
    unsigned int spill_slots[MIGA80_MAX_VALUE_INSTRUCTIONS];
    int saved_registers[MIGA80_DATA_REGISTER_COUNT];
    unsigned int spill_slot_count;
    unsigned int phi_temporary_slot;
    int guarded;
};

int miga80_prepare_m68k_o1_plan(
    const struct miga80_value_function *function,
    struct allocation_plan **result,
    struct miga80_diagnostic *diagnostic);
void miga80_release_m68k_o1_plan(struct allocation_plan *plan);
int miga80_m68k_o1_backward_edge(const struct miga80_value_function *function,
                                unsigned int predecessor,
                                unsigned int successor);

#endif
