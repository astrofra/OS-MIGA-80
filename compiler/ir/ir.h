#ifndef MIGA80_COMPILER_IR_H
#define MIGA80_COMPILER_IR_H

#include <limits.h>
#include <stdint.h>

#include "compiler/frontend/frontend.h"

#define MIGA80_MAX_IR_INSTRUCTIONS \
    (MIGA80_MAX_AST_NODES + 3U * MIGA80_MAX_STATEMENTS)
#define MIGA80_MAX_IR_STACK MIGA80_MAX_AST_NODES
#define MIGA80_MAX_BASIC_BLOCKS 32U
#define MIGA80_MAX_BLOCK_SUCCESSORS 2U
#define MIGA80_INVALID_BLOCK UINT_MAX

enum miga80_ir_opcode {
    MIGA80_IR_PUSH_I32,
    MIGA80_IR_PUSH_FIX,
    MIGA80_IR_PUSH_BOOL,
    MIGA80_IR_PUSH_STRING,
    MIGA80_IR_PUSH_SYMBOL,
    MIGA80_IR_PUSH_PARAMETER_I32,
    MIGA80_IR_PUSH_PARAMETER_BOOL,
    MIGA80_IR_PUSH_LOCAL_I32,
    MIGA80_IR_PUSH_LOCAL_BOOL,
    MIGA80_IR_STORE_LOCAL_I32,
    MIGA80_IR_STORE_LOCAL_BOOL,
    MIGA80_IR_NEG_I32,
    MIGA80_IR_ADD_I32,
    MIGA80_IR_SUB_I32,
    MIGA80_IR_MUL_I32,
    MIGA80_IR_MUL_FIX,
    MIGA80_IR_DIV_FIX,
    MIGA80_IR_DIV_I32,
    MIGA80_IR_DIV_U32,
    MIGA80_IR_FIX_FROM_I32,
    MIGA80_IR_I32_FROM_FIX,
    MIGA80_IR_EQ_I32,
    MIGA80_IR_NE_I32,
    MIGA80_IR_EQ_BOOL,
    MIGA80_IR_NE_BOOL,
    MIGA80_IR_LT_I32,
    MIGA80_IR_LE_I32,
    MIGA80_IR_GT_I32,
    MIGA80_IR_GE_I32,
    MIGA80_IR_LT_U32,
    MIGA80_IR_LE_U32,
    MIGA80_IR_GT_U32,
    MIGA80_IR_GE_U32,
    MIGA80_IR_NORMALIZE_INTEGER,
    MIGA80_IR_BRANCH_FALSE,
    MIGA80_IR_JUMP,
    MIGA80_IR_RETURN
};

struct miga80_ir_instruction {
    enum miga80_ir_opcode opcode;
    enum miga80_type type;
    uint32_t operand;
    unsigned int line;
    unsigned int column;
};

struct miga80_ir_basic_block {
    unsigned int first_instruction;
    unsigned int instruction_count;
    unsigned int successors[MIGA80_MAX_BLOCK_SUCCESSORS];
    unsigned int successor_count;
};

struct miga80_ir_function {
    char name[MIGA80_MAX_NAME + 1U];
    unsigned int parameter_count;
    enum miga80_type parameter_types[MIGA80_MAX_PARAMETERS];
    unsigned int local_count;
    enum miga80_type local_types[MIGA80_MAX_LOCALS];
    enum miga80_type result_type;
    struct miga80_ir_instruction instructions[MIGA80_MAX_IR_INSTRUCTIONS];
    unsigned int instruction_count;
    struct miga80_ir_basic_block blocks[MIGA80_MAX_BASIC_BLOCKS];
    uint32_t block_loop_membership[MIGA80_MAX_BASIC_BLOCKS];
    unsigned int block_count;
    unsigned int entry_block;
    struct miga80_constant_pool pool;
};

int miga80_lower_function(const struct miga80_ast_function *ast,
                          struct miga80_ir_function *ir,
                          struct miga80_diagnostic *diagnostic);
int miga80_validate_ir(const struct miga80_ir_function *ir,
                       struct miga80_diagnostic *diagnostic);
int miga80_evaluate_ir(const struct miga80_ir_function *ir,
                       const uint32_t *arguments, unsigned int argument_count,
                       uint32_t *result,
                       struct miga80_diagnostic *diagnostic);

#endif
