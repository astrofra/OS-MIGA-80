#include "compiler/value_ir/value_ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct value_lower_state {
    unsigned int entry_locals[MIGA80_MAX_BASIC_BLOCKS][MIGA80_MAX_LOCALS];
    unsigned int exit_locals[MIGA80_MAX_BASIC_BLOCKS][MIGA80_MAX_LOCALS];
    unsigned int loop_phis[MIGA80_MAX_BASIC_BLOCKS][MIGA80_MAX_LOCALS];
    unsigned int loop_preheader[MIGA80_MAX_BASIC_BLOCKS];
    unsigned int loop_latch[MIGA80_MAX_BASIC_BLOCKS];
    uint32_t loop_assigned_locals[MIGA80_MAX_BASIC_BLOCKS];
    unsigned char processed[MIGA80_MAX_BASIC_BLOCKS];
};

static int fail(struct miga80_diagnostic *diagnostic, unsigned int line,
                unsigned int column, const char *message)
{
    diagnostic->line = line;
    diagnostic->column = column;
    (void)snprintf(diagnostic->message, sizeof(diagnostic->message), "%s",
                   message);
    return 0;
}

static unsigned int add_value(struct miga80_value_function *function,
                              enum miga80_type type,
                              enum miga80_value_opcode opcode,
                              unsigned int left, unsigned int right,
                              uint32_t immediate,
                              unsigned int parameter_index,
                              unsigned int line, unsigned int column,
                              struct miga80_diagnostic *diagnostic)
{
    struct miga80_value_instruction *value;
    const unsigned int index = function->value_count;

    if (index == MIGA80_MAX_VALUE_INSTRUCTIONS) {
        (void)fail(diagnostic, line, column,
                   "value IR instruction limit exceeded");
        return MIGA80_INVALID_VALUE;
    }
    value = &function->values[index];
    (void)memset(value, 0, sizeof(*value));
    value->type = type;
    value->opcode = opcode;
    value->left = left;
    value->right = right;
    value->immediate = immediate;
    value->parameter_index = parameter_index;
    value->left_block = MIGA80_INVALID_BLOCK;
    value->right_block = MIGA80_INVALID_BLOCK;
    value->line = line;
    value->column = column;
    ++function->value_count;
    return index;
}

static int is_constant(const struct miga80_value_function *function,
                       unsigned int index, uint32_t *constant)
{
    if (index >= function->value_count ||
        function->values[index].opcode != MIGA80_VALUE_CONSTANT) {
        return 0;
    }
    if (constant != NULL) {
        *constant = function->values[index].immediate;
    }
    return 1;
}

static unsigned int make_constant(struct miga80_value_function *function,
                                  enum miga80_type type, uint32_t constant,
                                  unsigned int line, unsigned int column,
                                  struct miga80_diagnostic *diagnostic)
{
    if (miga80_type_is_integer(type)) {
        constant = miga80_normalize_integer(type, constant);
    }
    return add_value(function, type, MIGA80_VALUE_CONSTANT,
                     MIGA80_INVALID_VALUE, MIGA80_INVALID_VALUE, constant, 0U,
                     line, column, diagnostic);
}

static unsigned int make_neg(struct miga80_value_function *function,
                             unsigned int operand, unsigned int line,
                             unsigned int column,
                             struct miga80_diagnostic *diagnostic)
{
    uint32_t constant;
    const enum miga80_type type = function->values[operand].type;

    if (is_constant(function, operand, &constant)) {
        return make_constant(function, type, 0U - constant, line,
                             column, diagnostic);
    }
    if (function->values[operand].opcode == MIGA80_VALUE_NEG) {
        return function->values[operand].left;
    }
    return add_value(function, type, MIGA80_VALUE_NEG, operand,
                     MIGA80_INVALID_VALUE, 0U, 0U, line, column, diagnostic);
}

static unsigned int make_normalize_integer(
    struct miga80_value_function *function, unsigned int operand,
    enum miga80_type type, unsigned int line, unsigned int column,
    struct miga80_diagnostic *diagnostic)
{
    uint32_t constant;

    if (function->values[operand].type == type) {
        return operand;
    }
    if (is_constant(function, operand, &constant)) {
        return make_constant(function, type, constant, line, column,
                             diagnostic);
    }
    return add_value(function, type, MIGA80_VALUE_NORMALIZE_INTEGER, operand,
                     MIGA80_INVALID_VALUE, 0U, 0U, line, column, diagnostic);
}

static unsigned int make_numeric_conversion(
    struct miga80_value_function *function,
    enum miga80_value_opcode opcode, unsigned int operand,
    unsigned int line, unsigned int column,
    struct miga80_diagnostic *diagnostic)
{
    uint32_t constant;

    if (is_constant(function, operand, &constant)) {
        if (opcode == MIGA80_VALUE_FIX_FROM_I32) {
            uint32_t converted;

            if (!miga80_convert_i32_to_fix(constant, &converted)) {
                (void)fail(diagnostic, line, column,
                           "conversion to fix is out of range");
                return MIGA80_INVALID_VALUE;
            }
            return make_constant(function, MIGA80_TYPE_FIX, converted,
                                 line, column, diagnostic);
        }
        return make_constant(function, MIGA80_TYPE_I32,
                             miga80_convert_fix_to_i32(constant), line,
                             column, diagnostic);
    }
    return add_value(
        function,
        opcode == MIGA80_VALUE_FIX_FROM_I32 ? MIGA80_TYPE_FIX
                                            : MIGA80_TYPE_I32,
        opcode, operand, MIGA80_INVALID_VALUE, 0U, 0U, line, column,
        diagnostic);
}

static uint32_t fold_comparison(enum miga80_value_opcode opcode,
                                uint32_t left, uint32_t right)
{
    const uint32_t signed_left = left ^ UINT32_C(0x80000000);
    const uint32_t signed_right = right ^ UINT32_C(0x80000000);

    if (opcode == MIGA80_VALUE_EQ) {
        return left == right ? 1U : 0U;
    }
    if (opcode == MIGA80_VALUE_NE) {
        return left != right ? 1U : 0U;
    }
    if (opcode == MIGA80_VALUE_LT_I32) {
        return signed_left < signed_right ? 1U : 0U;
    }
    if (opcode == MIGA80_VALUE_LE_I32) {
        return signed_left <= signed_right ? 1U : 0U;
    }
    if (opcode == MIGA80_VALUE_GT_I32) {
        return signed_left > signed_right ? 1U : 0U;
    }
    if (opcode == MIGA80_VALUE_GE_I32) {
        return signed_left >= signed_right ? 1U : 0U;
    }
    if (opcode == MIGA80_VALUE_LT_U32) {
        return left < right ? 1U : 0U;
    }
    if (opcode == MIGA80_VALUE_LE_U32) {
        return left <= right ? 1U : 0U;
    }
    if (opcode == MIGA80_VALUE_GT_U32) {
        return left > right ? 1U : 0U;
    }
    return left >= right ? 1U : 0U;
}

static int comparison_opcode(enum miga80_value_opcode opcode)
{
    return (opcode >= MIGA80_VALUE_EQ &&
            opcode <= MIGA80_VALUE_GE_I32) ||
           (opcode >= MIGA80_VALUE_LT_U32 &&
            opcode <= MIGA80_VALUE_GE_U32);
}

static unsigned int make_binary(struct miga80_value_function *function,
                                enum miga80_value_opcode opcode,
                                unsigned int left, unsigned int right,
                                unsigned int line, unsigned int column,
                                struct miga80_diagnostic *diagnostic)
{
    uint32_t left_constant = 0U;
    uint32_t right_constant = 0U;
    const enum miga80_type operand_type = function->values[left].type;
    int left_is_constant = is_constant(function, left, &left_constant);
    int right_is_constant = is_constant(function, right, &right_constant);

    if ((opcode == MIGA80_VALUE_ADD || opcode == MIGA80_VALUE_MUL ||
         opcode == MIGA80_VALUE_MUL_FIX) &&
        left_is_constant && !right_is_constant) {
        const unsigned int temporary = left;

        left = right;
        right = temporary;
        right_constant = left_constant;
        left_is_constant = 0;
        right_is_constant = 1;
    }
    if ((opcode == MIGA80_VALUE_DIV_FIX || opcode == MIGA80_VALUE_DIV ||
         opcode == MIGA80_VALUE_DIV_U) &&
        right_is_constant &&
        right_constant == 0U) {
        (void)fail(diagnostic, line, column,
                   "division by zero in constant expression");
        return MIGA80_INVALID_VALUE;
    }
    if (left_is_constant && right_is_constant) {
        uint32_t folded;

        if (opcode == MIGA80_VALUE_ADD) {
            folded = left_constant + right_constant;
        } else if (opcode == MIGA80_VALUE_SUB) {
            folded = left_constant - right_constant;
        } else if (opcode == MIGA80_VALUE_MUL) {
            folded = left_constant * right_constant;
        } else if (opcode == MIGA80_VALUE_MUL_FIX) {
            folded = miga80_multiply_fix(left_constant, right_constant);
        } else if (opcode == MIGA80_VALUE_DIV_FIX) {
            if (!miga80_divide_fix(left_constant, right_constant, &folded)) {
                (void)fail(diagnostic, line, column,
                           "division by zero in constant expression");
                return MIGA80_INVALID_VALUE;
            }
        } else if (opcode == MIGA80_VALUE_DIV) {
            if (!miga80_divide_i32(left_constant, right_constant, &folded)) {
                (void)fail(diagnostic, line, column,
                           "division by zero in constant expression");
                return MIGA80_INVALID_VALUE;
            }
        } else if (opcode == MIGA80_VALUE_DIV_U) {
            if (right_constant == 0U) {
                (void)fail(diagnostic, line, column,
                           "division by zero in constant expression");
                return MIGA80_INVALID_VALUE;
            }
            folded = left_constant / right_constant;
        } else {
            folded = fold_comparison(opcode, left_constant, right_constant);
        }
        return make_constant(function,
                             comparison_opcode(opcode) ? MIGA80_TYPE_BOOL
                                                       : operand_type,
                             folded, line, column, diagnostic);
    }
    if (opcode == MIGA80_VALUE_ADD && right_is_constant &&
        right_constant == 0U) {
        return left;
    }
    if (opcode == MIGA80_VALUE_SUB) {
        if (right_is_constant && right_constant == 0U) {
            return left;
        }
        if (left == right) {
            return make_constant(function, operand_type, 0U, line, column,
                                 diagnostic);
        }
    }
    if ((opcode == MIGA80_VALUE_MUL || opcode == MIGA80_VALUE_MUL_FIX) &&
        right_is_constant) {
        if (right_constant == 0U) {
            return make_constant(function, operand_type, 0U, line, column,
                                 diagnostic);
        }
        if ((opcode == MIGA80_VALUE_MUL && right_constant == 1U) ||
            (opcode == MIGA80_VALUE_MUL_FIX &&
             right_constant == MIGA80_ABI_FIX_ONE)) {
            return left;
        }
    }
    if ((opcode == MIGA80_VALUE_DIV_FIX || opcode == MIGA80_VALUE_DIV ||
         opcode == MIGA80_VALUE_DIV_U) &&
        right_is_constant) {
        if ((opcode == MIGA80_VALUE_DIV_FIX &&
             right_constant == MIGA80_ABI_FIX_ONE) ||
            (opcode != MIGA80_VALUE_DIV_FIX && right_constant == 1U)) {
            return left;
        }
        if (opcode == MIGA80_VALUE_DIV_FIX &&
            right_constant == 0xffff0000U) {
            return make_neg(function, left, line, column, diagnostic);
        }
        if (opcode == MIGA80_VALUE_DIV &&
            right_constant == UINT32_MAX) {
            return make_neg(function, left, line, column, diagnostic);
        }
    }
    if (comparison_opcode(opcode) && left == right) {
        const uint32_t folded =
            opcode == MIGA80_VALUE_EQ || opcode == MIGA80_VALUE_LE_I32 ||
                    opcode == MIGA80_VALUE_GE_I32 ||
                    opcode == MIGA80_VALUE_LE_U32 ||
                    opcode == MIGA80_VALUE_GE_U32
                ? 1U
                : 0U;

        return make_constant(function, MIGA80_TYPE_BOOL, folded, line, column,
                             diagnostic);
    }
    return add_value(function,
                     comparison_opcode(opcode) ? MIGA80_TYPE_BOOL
                                               : operand_type,
                     opcode, left, right, 0U, 0U, line, column, diagnostic);
}

static unsigned int make_phi(struct miga80_value_function *function,
                             enum miga80_type type, unsigned int left,
                             unsigned int left_block, unsigned int right,
                             unsigned int right_block,
                             struct miga80_diagnostic *diagnostic)
{
    unsigned int result;

    if (left == right) {
        return left;
    }
    result = add_value(function, type, MIGA80_VALUE_PHI, left, right, 0U, 0U,
                       0U, 0U, diagnostic);
    if (result != MIGA80_INVALID_VALUE) {
        function->values[result].left_block = left_block;
        function->values[result].right_block = right_block;
    }
    return result;
}

static unsigned int make_loop_phi(
    struct miga80_value_function *function, enum miga80_type type,
    unsigned int initial, unsigned int preheader, unsigned int latch,
    struct miga80_diagnostic *diagnostic)
{
    const unsigned int result =
        add_value(function, type, MIGA80_VALUE_PHI, initial,
                  MIGA80_INVALID_VALUE, 0U, 0U, 0U, 0U, diagnostic);

    if (result != MIGA80_INVALID_VALUE) {
        function->values[result].left_block = preheader;
        function->values[result].right_block = latch;
    }
    return result;
}

static int opcode_has_left(enum miga80_value_opcode opcode)
{
    return opcode == MIGA80_VALUE_NEG || opcode == MIGA80_VALUE_ADD ||
           opcode == MIGA80_VALUE_SUB || opcode == MIGA80_VALUE_MUL ||
           opcode == MIGA80_VALUE_MUL_FIX ||
           opcode == MIGA80_VALUE_DIV_FIX ||
           opcode == MIGA80_VALUE_DIV || opcode == MIGA80_VALUE_DIV_U ||
           opcode == MIGA80_VALUE_FIX_FROM_I32 ||
           opcode == MIGA80_VALUE_I32_FROM_FIX ||
           opcode == MIGA80_VALUE_NORMALIZE_INTEGER ||
           comparison_opcode(opcode) || opcode == MIGA80_VALUE_PHI;
}

static int opcode_has_right(enum miga80_value_opcode opcode)
{
    return opcode == MIGA80_VALUE_ADD || opcode == MIGA80_VALUE_SUB ||
           opcode == MIGA80_VALUE_MUL ||
           opcode == MIGA80_VALUE_MUL_FIX || opcode == MIGA80_VALUE_DIV ||
           opcode == MIGA80_VALUE_DIV_FIX ||
           opcode == MIGA80_VALUE_DIV_U ||
           comparison_opcode(opcode) || opcode == MIGA80_VALUE_PHI;
}

static int mark_root(struct miga80_value_function *function,
                     unsigned int value, unsigned int *worklist,
                     unsigned int *worklist_size,
                     struct miga80_diagnostic *diagnostic)
{
    if (value >= function->value_count) {
        return fail(diagnostic, 0U, 0U, "value IR root is invalid");
    }
    ++function->values[value].use_count;
    if (!function->values[value].live) {
        function->values[value].live = 1;
        worklist[(*worklist_size)++] = value;
    }
    return 1;
}

static int mark_live_values(struct miga80_value_function *function,
                            struct miga80_diagnostic *diagnostic)
{
    unsigned int worklist[MIGA80_MAX_VALUE_INSTRUCTIONS];
    unsigned int worklist_size = 0U;
    unsigned int block_index;
    unsigned int value_index;

    if (!mark_root(function, function->result, worklist, &worklist_size,
                   diagnostic)) {
        return 0;
    }
    for (block_index = 0U; block_index < function->block_count;
         ++block_index) {
        if (function->blocks[block_index].terminator == MIGA80_VALUE_BRANCH &&
            !mark_root(function, function->blocks[block_index].condition,
                       worklist, &worklist_size, diagnostic)) {
            return 0;
        }
    }
    for (value_index = 0U; value_index < function->value_count;
         ++value_index) {
        const struct miga80_value_instruction *value =
            &function->values[value_index];

        if ((value->opcode == MIGA80_VALUE_DIV_FIX ||
             value->opcode == MIGA80_VALUE_DIV ||
             value->opcode == MIGA80_VALUE_DIV_U) &&
            !is_constant(function, value->right, NULL) &&
            !mark_root(function, value_index, worklist, &worklist_size,
                       diagnostic)) {
            return 0;
        }
        if (value->opcode == MIGA80_VALUE_FIX_FROM_I32 &&
            function->values[value->left].opcode !=
                MIGA80_VALUE_I32_FROM_FIX &&
            !is_constant(function, value->left, NULL) &&
            !mark_root(function, value_index, worklist, &worklist_size,
                       diagnostic)) {
            return 0;
        }
    }
    while (worklist_size != 0U) {
        const unsigned int value_index = worklist[--worklist_size];
        struct miga80_value_instruction *value =
            &function->values[value_index];

        if (opcode_has_left(value->opcode)) {
            if (value->left >= function->value_count ||
                value->left == value_index ||
                !mark_root(function, value->left, worklist,
                           &worklist_size, diagnostic)) {
                return fail(diagnostic, value->line, value->column,
                            "value IR left operand is invalid");
            }
        }
        if (opcode_has_right(value->opcode)) {
            if (value->right >= function->value_count ||
                value->right == value_index ||
                !mark_root(function, value->right, worklist,
                           &worklist_size, diagnostic)) {
                return fail(diagnostic, value->line, value->column,
                            "value IR right operand is invalid");
            }
        }
    }
    return 1;
}

static enum miga80_value_opcode value_opcode(enum miga80_ir_opcode opcode)
{
    switch (opcode) {
    case MIGA80_IR_ADD_I32:
        return MIGA80_VALUE_ADD;
    case MIGA80_IR_SUB_I32:
        return MIGA80_VALUE_SUB;
    case MIGA80_IR_MUL_I32:
        return MIGA80_VALUE_MUL;
    case MIGA80_IR_MUL_FIX:
        return MIGA80_VALUE_MUL_FIX;
    case MIGA80_IR_DIV_FIX:
        return MIGA80_VALUE_DIV_FIX;
    case MIGA80_IR_DIV_I32:
        return MIGA80_VALUE_DIV;
    case MIGA80_IR_DIV_U32:
        return MIGA80_VALUE_DIV_U;
    case MIGA80_IR_EQ_I32:
    case MIGA80_IR_EQ_BOOL:
        return MIGA80_VALUE_EQ;
    case MIGA80_IR_NE_I32:
    case MIGA80_IR_NE_BOOL:
        return MIGA80_VALUE_NE;
    case MIGA80_IR_LT_I32:
        return MIGA80_VALUE_LT_I32;
    case MIGA80_IR_LE_I32:
        return MIGA80_VALUE_LE_I32;
    case MIGA80_IR_GT_I32:
        return MIGA80_VALUE_GT_I32;
    case MIGA80_IR_GE_I32:
        return MIGA80_VALUE_GE_I32;
    case MIGA80_IR_LT_U32:
        return MIGA80_VALUE_LT_U32;
    case MIGA80_IR_LE_U32:
        return MIGA80_VALUE_LE_U32;
    case MIGA80_IR_GT_U32:
        return MIGA80_VALUE_GT_U32;
    default:
        return MIGA80_VALUE_GE_U32;
    }
}

static int build_predecessors(const struct miga80_ir_function *source,
                              struct miga80_value_function *result,
                              struct miga80_diagnostic *diagnostic)
{
    unsigned int block_index;

    for (block_index = 0U; block_index < source->block_count; ++block_index) {
        const struct miga80_ir_basic_block *source_block =
            &source->blocks[block_index];
        struct miga80_value_basic_block *block = &result->blocks[block_index];

        block->condition = MIGA80_INVALID_VALUE;
        block->predecessors[0] = MIGA80_INVALID_BLOCK;
        block->predecessors[1] = MIGA80_INVALID_BLOCK;
        block->successors[0] = source_block->successors[0];
        block->successors[1] = source_block->successors[1];
        block->successor_count = source_block->successor_count;
    }
    for (block_index = 0U; block_index < source->block_count; ++block_index) {
        const struct miga80_ir_basic_block *source_block =
            &source->blocks[block_index];
        unsigned int successor_index;

        for (successor_index = 0U;
             successor_index < source_block->successor_count;
             ++successor_index) {
            struct miga80_value_basic_block *successor =
                &result->blocks[source_block->successors[successor_index]];

            if (successor->predecessor_count ==
                MIGA80_MAX_BLOCK_SUCCESSORS) {
                return fail(diagnostic, 0U, 0U,
                            "value IR join has too many predecessors");
            }
            successor->predecessors[successor->predecessor_count++] =
                block_index;
        }
    }
    return 1;
}

static uint32_t block_bit(unsigned int block_index)
{
    return UINT32_C(1) << block_index;
}

static int analyze_loops(const struct miga80_ir_function *source,
                         const struct miga80_value_function *result,
                         struct value_lower_state *state,
                         struct miga80_diagnostic *diagnostic)
{
    uint32_t dominators[MIGA80_MAX_BASIC_BLOCKS];
    const uint32_t all_blocks =
        source->block_count == MIGA80_MAX_BASIC_BLOCKS
            ? UINT32_MAX
            : block_bit(source->block_count) - UINT32_C(1);
    unsigned int block_index;
    unsigned int pass_count = 0U;
    int changed;

    for (block_index = 0U; block_index < source->block_count;
         ++block_index) {
        dominators[block_index] =
            block_index == source->entry_block ? block_bit(block_index)
                                               : all_blocks;
    }
    do {
        changed = 0;
        for (block_index = 0U; block_index < source->block_count;
             ++block_index) {
            const struct miga80_value_basic_block *block =
                &result->blocks[block_index];
            uint32_t merged = all_blocks;
            uint32_t updated;
            unsigned int predecessor_index;

            if (block_index == source->entry_block) {
                continue;
            }
            if (block->predecessor_count == 0U) {
                return fail(diagnostic, 0U, 0U,
                            "value IR contains an unreachable block");
            }
            for (predecessor_index = 0U;
                 predecessor_index < block->predecessor_count;
                 ++predecessor_index) {
                merged &= dominators[block->predecessors[predecessor_index]];
            }
            updated = merged | block_bit(block_index);
            if (updated != dominators[block_index]) {
                dominators[block_index] = updated;
                changed = 1;
            }
        }
        if (++pass_count > source->block_count * source->block_count + 1U) {
            return fail(diagnostic, 0U, 0U,
                        "value IR dominators did not converge");
        }
    } while (changed);

    for (block_index = 0U; block_index < source->block_count;
         ++block_index) {
        const struct miga80_value_basic_block *block =
            &result->blocks[block_index];
        unsigned int edge;

        for (edge = 0U; edge < block->successor_count; ++edge) {
            const unsigned int successor = block->successors[edge];

            if ((dominators[block_index] & block_bit(successor)) == 0U) {
                continue;
            }
            if (state->loop_latch[successor] != MIGA80_INVALID_BLOCK) {
                return fail(diagnostic, 0U, 0U,
                            "loop header has multiple latches");
            }
            state->loop_latch[successor] = block_index;
        }
    }

    for (block_index = 0U; block_index < source->block_count;
         ++block_index) {
        const struct miga80_value_basic_block *header =
            &result->blocks[block_index];
        const unsigned int latch = state->loop_latch[block_index];
        uint32_t loop_blocks;
        uint32_t declared_loop_blocks = 0U;
        uint32_t pending;
        unsigned int predecessor_index;
        unsigned int local_mask = 0U;
        unsigned int exit_block = MIGA80_INVALID_BLOCK;

        if (latch == MIGA80_INVALID_BLOCK) {
            continue;
        }
        if (header->predecessor_count != 2U) {
            return fail(diagnostic, 0U, 0U,
                        "loop header must have two predecessors");
        }
        for (predecessor_index = 0U;
             predecessor_index < header->predecessor_count;
             ++predecessor_index) {
            const unsigned int predecessor =
                header->predecessors[predecessor_index];

            if (predecessor != latch) {
                if (state->loop_preheader[block_index] !=
                    MIGA80_INVALID_BLOCK) {
                    return fail(diagnostic, 0U, 0U,
                                "loop header has multiple preheaders");
                }
                state->loop_preheader[block_index] = predecessor;
            }
        }
        if (state->loop_preheader[block_index] == MIGA80_INVALID_BLOCK) {
            return fail(diagnostic, 0U, 0U,
                        "loop header has no preheader");
        }

        if (result->blocks[latch].successor_count != 1U ||
            result->blocks[latch].successors[0] != block_index ||
            source->blocks[latch].instruction_count != 1U ||
            source->instructions[source->blocks[latch].first_instruction]
                    .opcode != MIGA80_IR_JUMP) {
            return fail(diagnostic, 0U, 0U,
                        "loop does not have a dedicated latch");
        }

        loop_blocks = block_bit(block_index) | block_bit(latch);
        pending = block_bit(latch);
        while (pending != 0U) {
            unsigned int current = 0U;
            const struct miga80_value_basic_block *block;

            while ((pending & block_bit(current)) == 0U) {
                ++current;
            }
            pending &= ~block_bit(current);
            block = &result->blocks[current];
            for (predecessor_index = 0U;
                 predecessor_index < block->predecessor_count;
                 ++predecessor_index) {
                const unsigned int predecessor =
                    block->predecessors[predecessor_index];
                uint32_t predecessor_bit;

                if (predecessor >= source->block_count) {
                    return fail(diagnostic, 0U, 0U,
                                "loop predecessor is outside the CFG");
                }
                predecessor_bit = block_bit(predecessor);

                if (predecessor != block_index &&
                    (loop_blocks & predecessor_bit) == 0U) {
                    loop_blocks |= predecessor_bit;
                    pending |= predecessor_bit;
                }
            }
        }
        for (predecessor_index = 0U;
             predecessor_index < source->block_count;
             ++predecessor_index) {
            if ((source->block_loop_membership[predecessor_index] &
                 block_bit(block_index)) != 0U) {
                declared_loop_blocks |= block_bit(predecessor_index);
            }
        }
        if ((declared_loop_blocks & block_bit(block_index)) == 0U ||
            (declared_loop_blocks & block_bit(latch)) == 0U ||
            (declared_loop_blocks &
             block_bit(state->loop_preheader[block_index])) != 0U ||
            (loop_blocks & ~declared_loop_blocks) != 0U) {
            return fail(diagnostic, 0U, 0U,
                        "loop membership is not normalized");
        }
        loop_blocks = declared_loop_blocks;
        for (predecessor_index = 0U;
             predecessor_index < source->block_count;
             ++predecessor_index) {
            const struct miga80_value_basic_block *block;
            unsigned int edge;

            if ((loop_blocks & block_bit(predecessor_index)) == 0U) {
                continue;
            }
            block = &result->blocks[predecessor_index];
            for (edge = 0U; edge < block->successor_count; ++edge) {
                const unsigned int successor = block->successors[edge];

                if ((loop_blocks & block_bit(successor)) != 0U) {
                    continue;
                }
                if (exit_block == MIGA80_INVALID_BLOCK) {
                    exit_block = successor;
                } else if (exit_block != successor) {
                    return fail(diagnostic, 0U, 0U,
                                "loop has multiple exit blocks");
                }
            }
        }
        if (exit_block == MIGA80_INVALID_BLOCK) {
            return fail(diagnostic, 0U, 0U,
                        "loop has no dedicated exit block");
        }
        for (predecessor_index = 0U;
             predecessor_index < source->block_count;
             ++predecessor_index) {
            const struct miga80_ir_basic_block *block;
            unsigned int offset;

            if ((loop_blocks & block_bit(predecessor_index)) == 0U) {
                continue;
            }
            block = &source->blocks[predecessor_index];
            for (offset = 0U; offset < block->instruction_count; ++offset) {
                const struct miga80_ir_instruction *instruction =
                    &source->instructions[block->first_instruction + offset];

                if (instruction->opcode == MIGA80_IR_STORE_LOCAL_I32 ||
                    instruction->opcode == MIGA80_IR_STORE_LOCAL_BOOL) {
                    local_mask |= UINT32_C(1) << instruction->operand;
                }
            }
        }
        state->loop_assigned_locals[block_index] = local_mask;
    }
    return 1;
}

static int predecessors_processed(
    const struct miga80_value_function *function,
    const struct value_lower_state *state, unsigned int block_index)
{
    const struct miga80_value_basic_block *block =
        &function->blocks[block_index];
    unsigned int index;

    for (index = 0U; index < block->predecessor_count; ++index) {
        if (block->predecessors[index] != state->loop_latch[block_index] &&
            !state->processed[block->predecessors[index]]) {
            return 0;
        }
    }
    return 1;
}

static int merge_local_values(const struct miga80_ir_function *source,
                              struct miga80_value_function *result,
                              struct value_lower_state *state,
                              unsigned int block_index,
                              struct miga80_diagnostic *diagnostic)
{
    const struct miga80_value_basic_block *block =
        &result->blocks[block_index];
    unsigned int local;

    if (state->loop_latch[block_index] != MIGA80_INVALID_BLOCK) {
        const unsigned int preheader = state->loop_preheader[block_index];

        if (!state->processed[preheader]) {
            return fail(diagnostic, 0U, 0U,
                        "loop preheader is not available");
        }
        for (local = 0U; local < source->local_count; ++local) {
            const unsigned int initial =
                state->exit_locals[preheader][local];

            if ((state->loop_assigned_locals[block_index] &
                 (UINT32_C(1) << local)) == 0U) {
                state->entry_locals[block_index][local] = initial;
                continue;
            }
            if (initial == MIGA80_INVALID_VALUE) {
                return fail(diagnostic, 0U, 0U,
                            "loop local is not initialized before entry");
            }
            state->loop_phis[block_index][local] =
                make_loop_phi(result, source->local_types[local], initial,
                              preheader, state->loop_latch[block_index],
                              diagnostic);
            if (state->loop_phis[block_index][local] ==
                MIGA80_INVALID_VALUE) {
                return 0;
            }
            state->entry_locals[block_index][local] =
                state->loop_phis[block_index][local];
        }
        return 1;
    }
    if (block->predecessor_count == 0U) {
        return block_index == result->entry_block;
    }
    if (block->predecessor_count == 1U) {
        (void)memcpy(state->entry_locals[block_index],
                     state->exit_locals[block->predecessors[0]],
                     sizeof(state->entry_locals[block_index]));
        return 1;
    }
    for (local = 0U; local < source->local_count; ++local) {
        const unsigned int left =
            state->exit_locals[block->predecessors[0]][local];
        const unsigned int right =
            state->exit_locals[block->predecessors[1]][local];

        if (left == MIGA80_INVALID_VALUE && right == MIGA80_INVALID_VALUE) {
            state->entry_locals[block_index][local] = MIGA80_INVALID_VALUE;
            continue;
        }
        if (left == MIGA80_INVALID_VALUE || right == MIGA80_INVALID_VALUE) {
            return fail(diagnostic, 0U, 0U,
                        "local is not initialized on every incoming edge");
        }
        state->entry_locals[block_index][local] =
            make_phi(result, source->local_types[local], left,
                     block->predecessors[0], right, block->predecessors[1],
                     diagnostic);
        if (state->entry_locals[block_index][local] == MIGA80_INVALID_VALUE) {
            return 0;
        }
    }
    return 1;
}

static int lower_block_values(const struct miga80_ir_function *source,
                              struct miga80_value_function *result,
                              struct value_lower_state *state,
                              unsigned int block_index,
                              struct miga80_diagnostic *diagnostic)
{
    const struct miga80_ir_basic_block *source_block =
        &source->blocks[block_index];
    struct miga80_value_basic_block *block = &result->blocks[block_index];
    unsigned int stack[MIGA80_MAX_IR_STACK];
    unsigned int stack_size = 0U;
    unsigned int offset;

    block->first_value = result->value_count;
    if (!merge_local_values(source, result, state, block_index, diagnostic)) {
        return 0;
    }
    (void)memcpy(state->exit_locals[block_index],
                 state->entry_locals[block_index],
                 sizeof(state->exit_locals[block_index]));
    for (offset = 0U; offset < source_block->instruction_count; ++offset) {
        const struct miga80_ir_instruction *instruction =
            &source->instructions[source_block->first_instruction + offset];
        unsigned int value = MIGA80_INVALID_VALUE;

        switch (instruction->opcode) {
        case MIGA80_IR_PUSH_I32:
        case MIGA80_IR_PUSH_FIX:
            value = make_constant(result, instruction->type,
                                  instruction->operand, instruction->line,
                                  instruction->column, diagnostic);
            break;
        case MIGA80_IR_PUSH_BOOL:
            value = make_constant(result, MIGA80_TYPE_BOOL,
                                  instruction->operand, instruction->line,
                                  instruction->column, diagnostic);
            break;
        case MIGA80_IR_PUSH_STRING:
            value = make_constant(result, MIGA80_TYPE_STRING,
                                  instruction->operand, instruction->line,
                                  instruction->column, diagnostic);
            break;
        case MIGA80_IR_PUSH_SYMBOL:
            value = make_constant(
                result, MIGA80_TYPE_SYMBOL,
                miga80_pool_symbol_id(&source->pool, instruction->operand),
                instruction->line, instruction->column, diagnostic);
            break;
        case MIGA80_IR_PUSH_PARAMETER_I32:
        case MIGA80_IR_PUSH_PARAMETER_BOOL:
            value = instruction->operand;
            break;
        case MIGA80_IR_PUSH_LOCAL_I32:
        case MIGA80_IR_PUSH_LOCAL_BOOL:
            value = state->exit_locals[block_index][instruction->operand];
            if (value == MIGA80_INVALID_VALUE) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "local is uninitialized during value lowering");
            }
            break;
        case MIGA80_IR_STORE_LOCAL_I32:
        case MIGA80_IR_STORE_LOCAL_BOOL:
            state->exit_locals[block_index][instruction->operand] =
                stack[--stack_size];
            continue;
        case MIGA80_IR_NEG_I32:
            value = make_neg(result, stack[--stack_size], instruction->line,
                             instruction->column, diagnostic);
            break;
        case MIGA80_IR_NORMALIZE_INTEGER:
            value = make_normalize_integer(
                result, stack[--stack_size], instruction->type,
                instruction->line, instruction->column, diagnostic);
            break;
        case MIGA80_IR_FIX_FROM_I32:
            value = make_numeric_conversion(
                result, MIGA80_VALUE_FIX_FROM_I32, stack[--stack_size],
                instruction->line, instruction->column, diagnostic);
            break;
        case MIGA80_IR_I32_FROM_FIX:
            value = make_numeric_conversion(
                result, MIGA80_VALUE_I32_FROM_FIX, stack[--stack_size],
                instruction->line, instruction->column, diagnostic);
            break;
        case MIGA80_IR_ADD_I32:
        case MIGA80_IR_SUB_I32:
        case MIGA80_IR_MUL_I32:
        case MIGA80_IR_MUL_FIX:
        case MIGA80_IR_DIV_FIX:
        case MIGA80_IR_DIV_I32:
        case MIGA80_IR_DIV_U32:
        case MIGA80_IR_EQ_I32:
        case MIGA80_IR_NE_I32:
        case MIGA80_IR_EQ_BOOL:
        case MIGA80_IR_NE_BOOL:
        case MIGA80_IR_LT_I32:
        case MIGA80_IR_LE_I32:
        case MIGA80_IR_GT_I32:
        case MIGA80_IR_GE_I32:
        case MIGA80_IR_LT_U32:
        case MIGA80_IR_LE_U32:
        case MIGA80_IR_GT_U32:
        case MIGA80_IR_GE_U32: {
            const unsigned int right = stack[--stack_size];
            const unsigned int left = stack[--stack_size];

            value = make_binary(result, value_opcode(instruction->opcode),
                                left, right, instruction->line,
                                instruction->column, diagnostic);
            break;
        }
        case MIGA80_IR_BRANCH_FALSE:
            block->terminator = MIGA80_VALUE_BRANCH;
            block->condition = stack[--stack_size];
            continue;
        case MIGA80_IR_JUMP:
            block->terminator = MIGA80_VALUE_JUMP;
            continue;
        case MIGA80_IR_RETURN:
            block->terminator = MIGA80_VALUE_RETURN;
            result->result = stack[--stack_size];
            continue;
        default:
            return fail(diagnostic, instruction->line, instruction->column,
                        "unknown typed IR instruction during value lowering");
        }
        if (value == MIGA80_INVALID_VALUE) {
            return 0;
        }
        stack[stack_size++] = value;
    }
    if (stack_size != 0U) {
        return fail(diagnostic, 0U, 0U,
                    "value lowering leaves a non-empty block stack");
    }
    block->value_count = result->value_count - block->first_value;
    state->processed[block_index] = 1U;
    return 1;
}

static int finalize_loop_phis(
    const struct miga80_ir_function *source,
    struct miga80_value_function *result,
    const struct value_lower_state *state, unsigned int latch,
    struct miga80_diagnostic *diagnostic)
{
    unsigned int header;

    for (header = 0U; header < source->block_count; ++header) {
        unsigned int local;

        if (state->loop_latch[header] != latch) {
            continue;
        }
        for (local = 0U; local < source->local_count; ++local) {
            const unsigned int phi = state->loop_phis[header][local];
            const unsigned int value = state->exit_locals[latch][local];

            if (phi == MIGA80_INVALID_VALUE) {
                continue;
            }
            if (value == MIGA80_INVALID_VALUE) {
                return fail(diagnostic, 0U, 0U,
                            "loop local is not initialized at the latch");
            }
            result->values[phi].right = value;
        }
    }
    return 1;
}

static void replace_value(struct miga80_value_function *function,
                          unsigned int replaced,
                          unsigned int replacement)
{
    unsigned int index;

    for (index = 0U; index < function->value_count; ++index) {
        struct miga80_value_instruction *value = &function->values[index];

        if (opcode_has_left(value->opcode) && value->left == replaced) {
            value->left = replacement;
        }
        if (opcode_has_right(value->opcode) && value->right == replaced) {
            value->right = replacement;
        }
    }
    for (index = 0U; index < function->block_count; ++index) {
        if (function->blocks[index].condition == replaced) {
            function->blocks[index].condition = replacement;
        }
    }
    if (function->result == replaced) {
        function->result = replacement;
    }
}

static int remove_trivial_loop_phis(
    struct miga80_value_function *function,
    struct miga80_diagnostic *diagnostic)
{
    int changed;

    do {
        unsigned int index;

        changed = 0;
        for (index = 0U; index < function->value_count; ++index) {
            struct miga80_value_instruction *value =
                &function->values[index];
            unsigned int replacement = MIGA80_INVALID_VALUE;

            if (value->opcode != MIGA80_VALUE_PHI) {
                continue;
            }
            if (value->left == MIGA80_INVALID_VALUE ||
                value->right == MIGA80_INVALID_VALUE) {
                return fail(diagnostic, value->line, value->column,
                            "loop phi is incomplete");
            }
            if (value->left == value->right || value->right == index) {
                replacement = value->left;
            } else if (value->left == index) {
                replacement = value->right;
            }
            if (replacement == MIGA80_INVALID_VALUE ||
                replacement == index) {
                continue;
            }
            replace_value(function, index, replacement);
            value->opcode = MIGA80_VALUE_CONSTANT;
            value->left = MIGA80_INVALID_VALUE;
            value->right = MIGA80_INVALID_VALUE;
            value->immediate = 0U;
            changed = 1;
        }
    } while (changed);
    return 1;
}

static int build_block_layout_order(
    const struct miga80_ir_function *source,
    struct miga80_value_function *result,
    struct miga80_diagnostic *diagnostic)
{
    unsigned char selected[MIGA80_MAX_BASIC_BLOCKS];
    unsigned int order_index;

    (void)memset(selected, 0, sizeof(selected));
    for (order_index = 0U; order_index < source->block_count; ++order_index) {
        unsigned int best = MIGA80_INVALID_BLOCK;
        unsigned int block_index;

        for (block_index = 0U; block_index < source->block_count;
             ++block_index) {
            if (!selected[block_index] &&
                (best == MIGA80_INVALID_BLOCK ||
                 source->blocks[block_index].first_instruction <
                     source->blocks[best].first_instruction)) {
                best = block_index;
            }
        }
        if (best == MIGA80_INVALID_BLOCK) {
            return fail(diagnostic, 0U, 0U,
                        "unable to order value IR basic blocks");
        }
        selected[best] = 1U;
        result->block_order[result->block_order_count++] = best;
    }
    return 1;
}

int miga80_build_value_ir(const struct miga80_ir_function *source,
                          struct miga80_value_function *result,
                          struct miga80_diagnostic *diagnostic)
{
    struct value_lower_state *state;
    unsigned int block_index;
    unsigned int local_index;
    unsigned int processed_count = 0U;
    unsigned int index;
    int success = 0;

    if (source == NULL || result == NULL || diagnostic == NULL) {
        return 0;
    }
    (void)memset(result, 0, sizeof(*result));
    (void)memset(diagnostic, 0, sizeof(*diagnostic));
    if (!miga80_validate_ir(source, diagnostic)) {
        return 0;
    }
    (void)memcpy(result->name, source->name, sizeof(result->name));
    (void)memcpy(result->parameter_types, source->parameter_types,
                 sizeof(result->parameter_types));
    result->parameter_count = source->parameter_count;
    result->result_type = source->result_type;
    result->block_count = source->block_count;
    result->entry_block = source->entry_block;
    result->result = MIGA80_INVALID_VALUE;
    (void)memcpy(&result->pool, &source->pool, sizeof(result->pool));
    if (!build_predecessors(source, result, diagnostic)) {
        return 0;
    }
    for (index = 0U; index < source->parameter_count; ++index) {
        if (add_value(result, source->parameter_types[index],
                      MIGA80_VALUE_PARAMETER, MIGA80_INVALID_VALUE,
                      MIGA80_INVALID_VALUE, 0U, index, 0U, 0U, diagnostic) ==
            MIGA80_INVALID_VALUE) {
            return 0;
        }
    }
    state = (struct value_lower_state *)malloc(sizeof(*state));
    if (state == NULL) {
        return fail(diagnostic, 0U, 0U,
                    "unable to allocate value CFG state");
    }
    (void)memset(state, 0, sizeof(*state));
    for (block_index = 0U; block_index < MIGA80_MAX_BASIC_BLOCKS;
         ++block_index) {
        state->loop_preheader[block_index] = MIGA80_INVALID_BLOCK;
        state->loop_latch[block_index] = MIGA80_INVALID_BLOCK;
        for (local_index = 0U; local_index < MIGA80_MAX_LOCALS;
             ++local_index) {
            state->entry_locals[block_index][local_index] =
                MIGA80_INVALID_VALUE;
            state->exit_locals[block_index][local_index] =
                MIGA80_INVALID_VALUE;
            state->loop_phis[block_index][local_index] =
                MIGA80_INVALID_VALUE;
        }
    }
    if (!analyze_loops(source, result, state, diagnostic)) {
        goto done;
    }
    while (processed_count < source->block_count) {
        int progressed = 0;

        for (index = 0U; index < source->block_count; ++index) {
            if (!state->processed[index] &&
                predecessors_processed(result, state, index)) {
                if (!lower_block_values(source, result, state, index,
                                        diagnostic) ||
                    !finalize_loop_phis(source, result, state, index,
                                        diagnostic)) {
                    goto done;
                }
                ++processed_count;
                progressed = 1;
            }
        }
        if (!progressed) {
            (void)fail(diagnostic, 0U, 0U,
                       "value IR loop lowering is not implemented");
            goto done;
        }
    }
    if (result->result == MIGA80_INVALID_VALUE) {
        (void)fail(diagnostic, 0U, 0U, "typed IR has no value return");
        goto done;
    }
    success = remove_trivial_loop_phis(result, diagnostic) &&
              build_block_layout_order(source, result, diagnostic) &&
              mark_live_values(result, diagnostic);

done:
    free(state);
    return success;
}
