#ifndef MIGA80_COMPILER_VALUE_IR_H
#define MIGA80_COMPILER_VALUE_IR_H

#include <limits.h>
#include <stdint.h>

#include "compiler/ir/ir.h"

#define MIGA80_MAX_CFG_JOINS ((MIGA80_MAX_BASIC_BLOCKS - 1U) / 3U)
#define MIGA80_MAX_VALUE_INSTRUCTIONS \
    (MIGA80_MAX_PARAMETERS + MIGA80_MAX_AST_NODES + \
     2U * MIGA80_MAX_STATEMENTS + MIGA80_MAX_CFG_JOINS * MIGA80_MAX_LOCALS)
#define MIGA80_INVALID_VALUE UINT_MAX

enum miga80_value_opcode {
    MIGA80_VALUE_CONSTANT,
    MIGA80_VALUE_PARAMETER,
    MIGA80_VALUE_NEG,
    MIGA80_VALUE_ADD,
    MIGA80_VALUE_SUB,
    MIGA80_VALUE_MUL,
    MIGA80_VALUE_MUL_FIX,
    MIGA80_VALUE_DIV_FIX,
    MIGA80_VALUE_DIV,
    MIGA80_VALUE_DIV_U,
    MIGA80_VALUE_FIX_FROM_I32,
    MIGA80_VALUE_I32_FROM_FIX,
    MIGA80_VALUE_EQ,
    MIGA80_VALUE_NE,
    MIGA80_VALUE_LT_I32,
    MIGA80_VALUE_LE_I32,
    MIGA80_VALUE_GT_I32,
    MIGA80_VALUE_GE_I32,
    MIGA80_VALUE_LT_U32,
    MIGA80_VALUE_LE_U32,
    MIGA80_VALUE_GT_U32,
    MIGA80_VALUE_GE_U32,
    MIGA80_VALUE_NORMALIZE_INTEGER,
    MIGA80_VALUE_CALL_PSET,
    MIGA80_VALUE_CALL_LAYER,
    MIGA80_VALUE_CALL_LINE_START,
    MIGA80_VALUE_CALL_LINE_END,
    MIGA80_VALUE_PHI
};

static inline unsigned int miga80_value_call_arguments(
    enum miga80_value_opcode opcode)
{
    switch (opcode) {
    case MIGA80_VALUE_CALL_LAYER: return 1U;
    case MIGA80_VALUE_CALL_LINE_END: return 2U;
    case MIGA80_VALUE_CALL_LINE_START:
    case MIGA80_VALUE_CALL_PSET: return 3U;
    default: return 0U;
    }
}

static inline unsigned int miga80_value_call_offset(
    enum miga80_value_opcode opcode)
{
    switch (opcode) {
    case MIGA80_VALUE_CALL_LAYER: return MIGA80_ABI_RUNTIME_LAYER_HANDLER_OFFSET;
    case MIGA80_VALUE_CALL_LINE_START:
        return MIGA80_ABI_RUNTIME_LINE_START_HANDLER_OFFSET;
    case MIGA80_VALUE_CALL_LINE_END:
        return MIGA80_ABI_RUNTIME_LINE_END_HANDLER_OFFSET;
    default: return MIGA80_ABI_RUNTIME_PSET_HANDLER_OFFSET;
    }
}

struct miga80_value_instruction {
    enum miga80_type type;
    enum miga80_value_opcode opcode;
    unsigned int left;
    unsigned int right;
    unsigned int third;
    uint32_t immediate;
    unsigned int parameter_index;
    unsigned int left_block;
    unsigned int right_block;
    unsigned int line;
    unsigned int column;
    unsigned int use_count;
    int live;
};

enum miga80_value_terminator {
    MIGA80_VALUE_RETURN,
    MIGA80_VALUE_JUMP,
    MIGA80_VALUE_BRANCH
};

struct miga80_value_basic_block {
    unsigned int first_value;
    unsigned int value_count;
    unsigned int predecessors[MIGA80_MAX_BLOCK_SUCCESSORS];
    unsigned int predecessor_count;
    unsigned int successors[MIGA80_MAX_BLOCK_SUCCESSORS];
    unsigned int successor_count;
    unsigned int condition;
    enum miga80_value_terminator terminator;
    unsigned int line;
    unsigned int column;
};

struct miga80_value_function {
    char name[MIGA80_MAX_NAME + 1U];
    unsigned int parameter_count;
    enum miga80_type parameter_types[MIGA80_MAX_PARAMETERS];
    enum miga80_type result_type;
    struct miga80_value_instruction values[MIGA80_MAX_VALUE_INSTRUCTIONS];
    unsigned int value_count;
    struct miga80_value_basic_block blocks[MIGA80_MAX_BASIC_BLOCKS];
    unsigned int block_count;
    unsigned int entry_block;
    unsigned int block_order[MIGA80_MAX_BASIC_BLOCKS];
    unsigned int block_order_count;
    unsigned int result;
    struct miga80_constant_pool pool;
};

int miga80_build_value_ir(const struct miga80_ir_function *source,
                          struct miga80_value_function *result,
                          struct miga80_diagnostic *diagnostic);

#endif
