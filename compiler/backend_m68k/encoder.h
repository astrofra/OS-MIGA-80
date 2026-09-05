#ifndef MIGA80_COMPILER_BACKEND_M68K_ENCODER_H
#define MIGA80_COMPILER_BACKEND_M68K_ENCODER_H

#include <stddef.h>
#include <stdint.h>

#include "compiler/ir/ir.h"

/*
 * Bounded direct encoder used by the first on-target vertical slice.
 * It emits a position-independent 68020 function directly into caller-owned
 * memory and deliberately supports only the instruction subset it can prove.
 */
int miga80_encode_m68k_o0(uint8_t *bytes, size_t capacity,
                          const struct miga80_ir_function *function,
                          size_t *encoded_size,
                          struct miga80_diagnostic *diagnostic);

#endif
