#include "compiler/backend_m68k/backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/abi/abi.h"

#define MIGA80_DATA_REGISTER_COUNT 8U
#define MIGA80_SPILL_REGISTER_COUNT 7U
#define MIGA80_SPILL_SCRATCH_REGISTER 7
#define MIGA80_FIX_REGISTER_COUNT 6U
#define MIGA80_FIX_LOW_SCRATCH_REGISTER 6
#define MIGA80_FIX_DIV_REGISTER_COUNT 5U
#define MIGA80_FIX_DIV_LOW_SCRATCH_REGISTER 5
#define MIGA80_FIX_DIVISOR_SCRATCH_REGISTER 6
#define MIGA80_NO_REGISTER (-1)
#define MIGA80_NO_SPILL_SLOT UINT_MAX
#define MIGA80_LIVE_WORD_BITS 32U
#define MIGA80_LIVE_WORD_COUNT \
    ((MIGA80_MAX_VALUE_INSTRUCTIONS + MIGA80_LIVE_WORD_BITS - 1U) / \
     MIGA80_LIVE_WORD_BITS)
#define MIGA80_NO_DEFINITION_BLOCK 0xffU

struct allocation_plan {
    int registers[MIGA80_MAX_VALUE_INSTRUCTIONS];
    unsigned int spill_slots[MIGA80_MAX_VALUE_INSTRUCTIONS];
    int saved_registers[MIGA80_DATA_REGISTER_COUNT];
    unsigned int spill_slot_count;
    unsigned int phi_temporary_slot;
};

struct cfg_liveness {
    uint32_t block_use[MIGA80_MAX_BASIC_BLOCKS][MIGA80_LIVE_WORD_COUNT];
    uint32_t block_def[MIGA80_MAX_BASIC_BLOCKS][MIGA80_LIVE_WORD_COUNT];
    uint32_t live_in[MIGA80_MAX_BASIC_BLOCKS][MIGA80_LIVE_WORD_COUNT];
    uint32_t live_out[MIGA80_MAX_BASIC_BLOCKS][MIGA80_LIVE_WORD_COUNT];
    uint32_t phi_live_blocks[MIGA80_MAX_VALUE_INSTRUCTIONS];
    unsigned int last_use[MIGA80_MAX_VALUE_INSTRUCTIONS];
    unsigned char definition_block[MIGA80_MAX_VALUE_INSTRUCTIONS];
};

struct optimizer_workspace {
    struct allocation_plan plan;
    struct cfg_liveness liveness;
};

static int output_line(FILE *output, const char *format, ...)
{
    int written;
    va_list arguments;

    va_start(arguments, format);
    written = vfprintf(output, format, arguments);
    va_end(arguments);
    return written >= 0;
}

static int fail(struct miga80_diagnostic *diagnostic, unsigned int line,
                unsigned int column, const char *message)
{
    diagnostic->line = line;
    diagnostic->column = column;
    (void)snprintf(diagnostic->message, sizeof(diagnostic->message), "%s",
                   message);
    return 0;
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
           (opcode >= MIGA80_VALUE_EQ && opcode <= MIGA80_VALUE_GE_I32) ||
           (opcode >= MIGA80_VALUE_LT_U32 &&
            opcode <= MIGA80_VALUE_GE_U32) ||
           opcode == MIGA80_VALUE_PHI;
}

static int opcode_has_right(enum miga80_value_opcode opcode)
{
    return opcode == MIGA80_VALUE_ADD || opcode == MIGA80_VALUE_SUB ||
           opcode == MIGA80_VALUE_MUL ||
           opcode == MIGA80_VALUE_MUL_FIX || opcode == MIGA80_VALUE_DIV ||
           opcode == MIGA80_VALUE_DIV_FIX ||
           opcode == MIGA80_VALUE_DIV_U ||
           (opcode >= MIGA80_VALUE_EQ && opcode <= MIGA80_VALUE_GE_I32) ||
           (opcode >= MIGA80_VALUE_LT_U32 &&
            opcode <= MIGA80_VALUE_GE_U32) ||
           opcode == MIGA80_VALUE_PHI;
}

static int opcode_is_commutative(enum miga80_value_opcode opcode)
{
    return opcode == MIGA80_VALUE_ADD || opcode == MIGA80_VALUE_MUL ||
           opcode == MIGA80_VALUE_MUL_FIX ||
           opcode == MIGA80_VALUE_EQ || opcode == MIGA80_VALUE_NE;
}

static int opcode_is_comparison(enum miga80_value_opcode opcode)
{
    return (opcode >= MIGA80_VALUE_EQ &&
            opcode <= MIGA80_VALUE_GE_I32) ||
           (opcode >= MIGA80_VALUE_LT_U32 &&
            opcode <= MIGA80_VALUE_GE_U32);
}

static unsigned int parameter_class_index(
    const enum miga80_type *types, unsigned int parameter_index,
    int address_class)
{
    unsigned int class_index = 0U;
    unsigned int index;

    for (index = 0U; index < parameter_index; ++index) {
        if (miga80_type_is_address(types[index]) == address_class) {
            ++class_index;
        }
    }
    return class_index;
}

static int validate_value_function(
    const struct miga80_value_function *function,
    struct miga80_diagnostic *diagnostic)
{
    unsigned char seen_blocks[MIGA80_MAX_BASIC_BLOCKS];
    unsigned int scalar_parameters = 0U;
    unsigned int address_parameters = 0U;
    unsigned int index;

    if (function->parameter_count > MIGA80_MAX_PARAMETERS ||
        !miga80_type_is_value(function->result_type) ||
        function->value_count == 0U ||
        function->value_count > MIGA80_MAX_VALUE_INSTRUCTIONS ||
        function->result >= function->value_count ||
        !function->values[function->result].live ||
        function->block_count == 0U ||
        function->block_count > MIGA80_MAX_BASIC_BLOCKS ||
        function->block_order_count != function->block_count ||
        function->entry_block >= function->block_count) {
        return fail(diagnostic, 0U, 0U, "invalid O1 value function");
    }
    for (index = 0U; index < function->parameter_count; ++index) {
        if (!miga80_type_is_value(function->parameter_types[index])) {
            return fail(diagnostic, 0U, 0U,
                        "invalid O1 parameter type");
        }
        if (miga80_type_is_address(function->parameter_types[index])) {
            ++address_parameters;
        } else {
            ++scalar_parameters;
        }
    }
    if (scalar_parameters > MIGA80_ABI_MAX_SCALAR_ARGUMENTS ||
        address_parameters > MIGA80_ABI_MAX_ADDRESS_ARGUMENTS ||
        !miga80_validate_constant_pool(&function->pool)) {
        return fail(diagnostic, 0U, 0U,
                    "invalid O1 register classes or immutable pool");
    }
    (void)memset(seen_blocks, 0, sizeof(seen_blocks));
    for (index = 0U; index < function->block_order_count; ++index) {
        const unsigned int block_index = function->block_order[index];
        const struct miga80_value_basic_block *block;
        unsigned int edge;

        if (block_index >= function->block_count || seen_blocks[block_index]) {
            return fail(diagnostic, 0U, 0U,
                        "invalid O1 basic-block order");
        }
        seen_blocks[block_index] = 1U;
        block = &function->blocks[block_index];
        if (block->first_value > function->value_count ||
            block->value_count > function->value_count - block->first_value ||
            block->successor_count > MIGA80_MAX_BLOCK_SUCCESSORS ||
            block->terminator > MIGA80_VALUE_BRANCH ||
            (block->terminator == MIGA80_VALUE_RETURN &&
             block->successor_count != 0U) ||
            (block->terminator == MIGA80_VALUE_JUMP &&
             block->successor_count != 1U) ||
            (block->terminator == MIGA80_VALUE_BRANCH &&
             (block->successor_count != 2U ||
              block->condition >= function->value_count ||
              function->values[block->condition].type != MIGA80_TYPE_BOOL))) {
            return fail(diagnostic, 0U, 0U, "invalid O1 basic block");
        }
        for (edge = 0U; edge < block->successor_count; ++edge) {
            if (block->successors[edge] >= function->block_count) {
                return fail(diagnostic, 0U, 0U,
                            "invalid O1 basic-block successor");
            }
        }
    }
    for (index = 0U; index < function->value_count; ++index) {
        const struct miga80_value_instruction *value =
            &function->values[index];

        if (!value->live) {
            continue;
        }
        if (!miga80_type_is_value(value->type) ||
            value->opcode < MIGA80_VALUE_CONSTANT ||
            value->opcode > MIGA80_VALUE_PHI) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 value instruction");
        }
        if (value->opcode == MIGA80_VALUE_PHI &&
            (value->left >= function->value_count ||
             value->right >= function->value_count ||
             value->left == index || value->right == index)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 phi operand");
        }
        if (value->opcode != MIGA80_VALUE_PHI &&
            ((opcode_has_left(value->opcode) && value->left >= index) ||
             (opcode_has_right(value->opcode) && value->right >= index))) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 value operand");
        }
        if (value->opcode == MIGA80_VALUE_NEG &&
            (!miga80_type_is_signed_numeric(value->type) ||
             function->values[value->left].type != value->type)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 integer negation");
        }
        if (value->opcode == MIGA80_VALUE_NORMALIZE_INTEGER &&
            (!miga80_type_is_integer(value->type) ||
             function->values[value->left].type != MIGA80_TYPE_I32)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 constant conversion");
        }
        if (value->opcode == MIGA80_VALUE_FIX_FROM_I32 &&
            (value->type != MIGA80_TYPE_FIX ||
             function->values[value->left].type != MIGA80_TYPE_I32)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 i32-to-fix conversion");
        }
        if (value->opcode == MIGA80_VALUE_I32_FROM_FIX &&
            (value->type != MIGA80_TYPE_I32 ||
             function->values[value->left].type != MIGA80_TYPE_FIX)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 fix-to-i32 conversion");
        }
        if ((value->opcode == MIGA80_VALUE_ADD ||
             value->opcode == MIGA80_VALUE_SUB ||
             value->opcode == MIGA80_VALUE_MUL ||
             value->opcode == MIGA80_VALUE_MUL_FIX ||
             value->opcode == MIGA80_VALUE_DIV_FIX ||
             value->opcode == MIGA80_VALUE_DIV ||
             value->opcode == MIGA80_VALUE_DIV_U ||
             opcode_is_comparison(value->opcode)) &&
            (function->values[value->left].type !=
                 function->values[value->right].type ||
             (opcode_is_comparison(value->opcode)
                  ? value->type != MIGA80_TYPE_BOOL
                  : value->type != function->values[value->left].type))) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 binary value type");
        }
        if ((value->opcode == MIGA80_VALUE_ADD ||
             value->opcode == MIGA80_VALUE_SUB) &&
            !miga80_type_is_numeric(value->type)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 numeric arithmetic");
        }
        if (value->opcode == MIGA80_VALUE_MUL &&
            !miga80_type_is_integer(value->type)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 integer multiplication");
        }
        if (value->opcode == MIGA80_VALUE_MUL_FIX &&
            value->type != MIGA80_TYPE_FIX) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 fix multiplication");
        }
        if (value->opcode == MIGA80_VALUE_DIV_FIX &&
            value->type != MIGA80_TYPE_FIX) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 fix division");
        }
        if (value->opcode == MIGA80_VALUE_DIV &&
            !miga80_type_is_signed_integer(value->type)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 signed division");
        }
        if (value->opcode == MIGA80_VALUE_DIV_U &&
            (!miga80_type_is_integer(value->type) ||
             miga80_type_is_signed_integer(value->type))) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 unsigned division");
        }
        if (value->opcode >= MIGA80_VALUE_LT_I32 &&
            value->opcode <= MIGA80_VALUE_GE_I32 &&
            !miga80_type_is_signed_numeric(
                function->values[value->left].type)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 signed comparison");
        }
        if (value->opcode >= MIGA80_VALUE_LT_U32 &&
            value->opcode <= MIGA80_VALUE_GE_U32 &&
            (!miga80_type_is_integer(function->values[value->left].type) ||
             miga80_type_is_signed_integer(
                 function->values[value->left].type))) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 unsigned comparison");
        }
        if (value->opcode == MIGA80_VALUE_PARAMETER &&
            (value->parameter_index >= function->parameter_count ||
             value->type !=
                 function->parameter_types[value->parameter_index])) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 parameter index");
        }
        if (value->opcode == MIGA80_VALUE_PHI &&
            (value->left_block >= function->block_count ||
             value->right_block >= function->block_count ||
             function->values[value->left].type != value->type ||
             function->values[value->right].type != value->type)) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 phi predecessor");
        }
        if (value->opcode == MIGA80_VALUE_CONSTANT &&
            ((value->type == MIGA80_TYPE_BOOL && value->immediate > 1U) ||
             (miga80_type_is_integer(value->type) &&
              !miga80_integer_value_is_canonical(value->type,
                                                  value->immediate)) ||
             (value->type == MIGA80_TYPE_STRING &&
              (value->immediate >= function->pool.entry_count ||
               function->pool.entries[value->immediate].type !=
                   MIGA80_TYPE_STRING)))) {
            return fail(diagnostic, value->line, value->column,
                        "invalid O1 constant");
        }
        if (value->opcode == MIGA80_VALUE_CONSTANT &&
            value->type == MIGA80_TYPE_SYMBOL) {
            unsigned int entry;
            int found = 0;

            for (entry = 0U; entry < function->pool.entry_count; ++entry) {
                if (function->pool.entries[entry].type ==
                        MIGA80_TYPE_SYMBOL &&
                    value->immediate != 0U &&
                    miga80_pool_symbol_id(&function->pool, entry) ==
                        value->immediate) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                return fail(diagnostic, value->line, value->column,
                            "invalid O1 symbol constant");
            }
        }
    }
    if (function->values[function->result].type != function->result_type) {
        return fail(diagnostic, 0U, 0U, "invalid O1 result type");
    }
    return 1;
}

static int live_set_contains(const uint32_t *set, unsigned int value)
{
    return (set[value / MIGA80_LIVE_WORD_BITS] &
            (UINT32_C(1) << (value % MIGA80_LIVE_WORD_BITS))) != 0U;
}

static void live_set_insert(uint32_t *set, unsigned int value)
{
    set[value / MIGA80_LIVE_WORD_BITS] |=
        UINT32_C(1) << (value % MIGA80_LIVE_WORD_BITS);
}

static void add_block_use(struct cfg_liveness *liveness,
                          unsigned int block_index, unsigned int value)
{
    if (!live_set_contains(liveness->block_def[block_index], value)) {
        live_set_insert(liveness->block_use[block_index], value);
    }
}

static int add_phi_edge_uses(
    const struct miga80_value_function *function, unsigned int predecessor,
    unsigned int successor, uint32_t *edge_live,
    struct miga80_diagnostic *diagnostic)
{
    const struct miga80_value_basic_block *block =
        &function->blocks[successor];
    unsigned int offset;

    for (offset = 0U; offset < block->value_count; ++offset) {
        const struct miga80_value_instruction *value =
            &function->values[block->first_value + offset];
        unsigned int source;

        if (!value->live || value->opcode != MIGA80_VALUE_PHI) {
            continue;
        }
        if (value->left_block == predecessor) {
            source = value->left;
        } else if (value->right_block == predecessor) {
            source = value->right;
        } else {
            return fail(diagnostic, value->line, value->column,
                        "O1 phi does not match CFG predecessor");
        }
        live_set_insert(edge_live, source);
    }
    return 1;
}

static int build_cfg_liveness(
    const struct miga80_value_function *function,
    struct cfg_liveness *liveness,
    struct miga80_diagnostic *diagnostic)
{
    unsigned int block_index;
    unsigned int index;
    unsigned int pass_count = 0U;
    int changed;

    (void)memset(liveness, 0, sizeof(*liveness));
    (void)memset(liveness->definition_block, MIGA80_NO_DEFINITION_BLOCK,
                 sizeof(liveness->definition_block));
    for (block_index = 0U; block_index < function->block_count;
         ++block_index) {
        const struct miga80_value_basic_block *block =
            &function->blocks[block_index];
        unsigned int offset;

        for (offset = 0U; offset < block->value_count; ++offset) {
            const unsigned int value_index = block->first_value + offset;
            const struct miga80_value_instruction *value =
                &function->values[value_index];

            if (!value->live) {
                continue;
            }
            if (liveness->definition_block[value_index] !=
                MIGA80_NO_DEFINITION_BLOCK) {
                return fail(diagnostic, value->line, value->column,
                            "O1 value belongs to multiple basic blocks");
            }
            liveness->definition_block[value_index] =
                (unsigned char)block_index;
            if (value->opcode != MIGA80_VALUE_PHI) {
                if (opcode_has_left(value->opcode)) {
                    add_block_use(liveness, block_index, value->left);
                }
                if (opcode_has_right(value->opcode)) {
                    add_block_use(liveness, block_index, value->right);
                }
            }
            live_set_insert(liveness->block_def[block_index], value_index);
        }
        if (block->terminator == MIGA80_VALUE_BRANCH) {
            add_block_use(liveness, block_index, block->condition);
        } else if (block->terminator == MIGA80_VALUE_RETURN) {
            add_block_use(liveness, block_index, function->result);
        }
    }

    do {
        changed = 0;
        for (index = function->block_order_count; index > 0U; --index) {
            const unsigned int current = function->block_order[index - 1U];
            const struct miga80_value_basic_block *block =
                &function->blocks[current];
            uint32_t new_in[MIGA80_LIVE_WORD_COUNT];
            uint32_t new_out[MIGA80_LIVE_WORD_COUNT];
            unsigned int edge;
            unsigned int word;

            (void)memset(new_out, 0, sizeof(new_out));
            for (edge = 0U; edge < block->successor_count; ++edge) {
                const unsigned int successor = block->successors[edge];
                uint32_t edge_live[MIGA80_LIVE_WORD_COUNT];

                (void)memcpy(edge_live, liveness->live_in[successor],
                             sizeof(edge_live));
                if (!add_phi_edge_uses(function, current, successor,
                                       edge_live, diagnostic)) {
                    return 0;
                }
                for (word = 0U; word < MIGA80_LIVE_WORD_COUNT; ++word) {
                    new_out[word] |= edge_live[word];
                }
            }
            for (word = 0U; word < MIGA80_LIVE_WORD_COUNT; ++word) {
                new_in[word] =
                    liveness->block_use[current][word] |
                    (new_out[word] & ~liveness->block_def[current][word]);
            }
            if (memcmp(new_out, liveness->live_out[current],
                       sizeof(new_out)) != 0 ||
                memcmp(new_in, liveness->live_in[current],
                       sizeof(new_in)) != 0) {
                (void)memcpy(liveness->live_out[current], new_out,
                             sizeof(new_out));
                (void)memcpy(liveness->live_in[current], new_in,
                             sizeof(new_in));
                changed = 1;
            }
        }
        ++pass_count;
        if (pass_count >
            function->block_count * function->value_count + 1U) {
            return fail(diagnostic, 0U, 0U,
                        "O1 CFG liveness did not converge");
        }
    } while (changed);

    for (index = 0U; index < function->value_count; ++index) {
        if (function->values[index].live &&
            function->values[index].opcode == MIGA80_VALUE_PHI) {
            const unsigned int definition =
                liveness->definition_block[index];
            uint32_t live_blocks;

            if (definition == MIGA80_NO_DEFINITION_BLOCK) {
                return fail(diagnostic, function->values[index].line,
                            function->values[index].column,
                            "O1 phi has no defining basic block");
            }
            live_blocks = UINT32_C(1) << definition;
            for (block_index = 0U; block_index < function->block_count;
                 ++block_index) {
                if (live_set_contains(liveness->live_in[block_index], index) ||
                    live_set_contains(liveness->live_out[block_index],
                                      index)) {
                    live_blocks |= UINT32_C(1) << block_index;
                }
            }
            liveness->phi_live_blocks[index] = live_blocks;
        }
    }
    return 1;
}

static int phi_is_source_at_definition(
    const struct miga80_value_function *function,
    const struct cfg_liveness *liveness, unsigned int source,
    unsigned int destination)
{
    const unsigned int block_index =
        liveness->definition_block[destination];
    const struct miga80_value_basic_block *block =
        &function->blocks[block_index];
    unsigned int offset;

    for (offset = 0U; offset < block->value_count; ++offset) {
        const struct miga80_value_instruction *value =
            &function->values[block->first_value + offset];

        if (value->live && value->opcode == MIGA80_VALUE_PHI &&
            (value->left == source || value->right == source)) {
            return 1;
        }
    }
    return 0;
}

static int allocate_phi_slots(
    const struct miga80_value_function *function,
    const struct cfg_liveness *liveness, struct allocation_plan *plan,
    struct miga80_diagnostic *diagnostic)
{
    unsigned int index;

    for (index = 0U; index < function->value_count; ++index) {
        const struct miga80_value_instruction *value =
            &function->values[index];
        unsigned int slot;

        if (!value->live || value->opcode != MIGA80_VALUE_PHI) {
            continue;
        }
        for (slot = 0U; slot < plan->spill_slot_count; ++slot) {
            unsigned int prior;
            int conflict = 0;

            for (prior = 0U; prior < index; ++prior) {
                if (function->values[prior].live &&
                    function->values[prior].opcode == MIGA80_VALUE_PHI &&
                    plan->spill_slots[prior] == slot &&
                    ((liveness->phi_live_blocks[prior] &
                      liveness->phi_live_blocks[index]) != 0U ||
                     phi_is_source_at_definition(function, liveness, prior,
                                                 index))) {
                    conflict = 1;
                    break;
                }
            }
            if (!conflict) {
                break;
            }
        }
        if (slot == plan->spill_slot_count) {
            const unsigned int frame_size = (slot + 1U) * 4U;

            if (slot == MIGA80_MAX_VALUE_INSTRUCTIONS ||
                !miga80_abi_frame_size_is_valid(frame_size)) {
                return fail(diagnostic, value->line, value->column,
                            "O1 phi frame exceeds ABI limit");
            }
            ++plan->spill_slot_count;
        }
        plan->spill_slots[index] = slot;
        plan->saved_registers[MIGA80_SPILL_SCRATCH_REGISTER] = 1;
    }
    return 1;
}

static int phi_source_for_edge(
    const struct miga80_value_instruction *phi, unsigned int predecessor,
    unsigned int *source)
{
    if (phi->left_block == predecessor) {
        *source = phi->left;
        return 1;
    }
    if (phi->right_block == predecessor) {
        *source = phi->right;
        return 1;
    }
    return 0;
}

static int reserve_phi_temporary_if_needed(
    const struct miga80_value_function *function,
    struct allocation_plan *plan, unsigned int phi_slot_count,
    struct miga80_diagnostic *diagnostic)
{
    unsigned int predecessor;

    plan->phi_temporary_slot = MIGA80_NO_SPILL_SLOT;
    for (predecessor = 0U; predecessor < function->block_count;
         ++predecessor) {
        const struct miga80_value_basic_block *predecessor_block =
            &function->blocks[predecessor];
        unsigned int edge;

        for (edge = 0U; edge < predecessor_block->successor_count; ++edge) {
            const unsigned int successor = predecessor_block->successors[edge];
            const struct miga80_value_basic_block *successor_block =
                &function->blocks[successor];
            unsigned int mapping[MIGA80_MAX_VALUE_INSTRUCTIONS];
            unsigned int offset;
            unsigned int slot;

            for (slot = 0U; slot < phi_slot_count; ++slot) {
                mapping[slot] = MIGA80_NO_SPILL_SLOT;
            }
            for (offset = 0U; offset < successor_block->value_count;
                 ++offset) {
                const unsigned int phi_index =
                    successor_block->first_value + offset;
                const struct miga80_value_instruction *phi =
                    &function->values[phi_index];
                unsigned int source;
                unsigned int destination_slot;
                unsigned int source_slot;

                if (!phi->live || phi->opcode != MIGA80_VALUE_PHI) {
                    continue;
                }
                if (!phi_source_for_edge(phi, predecessor, &source)) {
                    return fail(diagnostic, phi->line, phi->column,
                                "O1 phi does not match CFG predecessor");
                }
                destination_slot = plan->spill_slots[phi_index];
                source_slot = plan->spill_slots[source];
                if (destination_slot >= phi_slot_count ||
                    source_slot == MIGA80_NO_SPILL_SLOT ||
                    source_slot >= phi_slot_count ||
                    source_slot == destination_slot) {
                    continue;
                }
                if (mapping[destination_slot] != MIGA80_NO_SPILL_SLOT) {
                    return fail(diagnostic, phi->line, phi->column,
                                "O1 phi destinations share an edge slot");
                }
                mapping[destination_slot] = source_slot;
            }
            for (slot = 0U; slot < phi_slot_count; ++slot) {
                unsigned int current = slot;
                unsigned int step;

                for (step = 0U; step < phi_slot_count; ++step) {
                    if (mapping[current] == MIGA80_NO_SPILL_SLOT) {
                        break;
                    }
                    current = mapping[current];
                }
                if (step == phi_slot_count &&
                    mapping[current] != MIGA80_NO_SPILL_SLOT) {
                    const unsigned int frame_size =
                        (plan->spill_slot_count + 1U) * 4U;

                    if (plan->spill_slot_count ==
                            MIGA80_MAX_VALUE_INSTRUCTIONS ||
                        !miga80_abi_frame_size_is_valid(frame_size)) {
                        return fail(diagnostic, 0U, 0U,
                                    "O1 phi temporary exceeds ABI limit");
                    }
                    plan->phi_temporary_slot = plan->spill_slot_count++;
                    return 1;
                }
            }
        }
    }
    return 1;
}

static int find_free_register(const unsigned int *owners,
                              unsigned int register_count,
                              unsigned int preferred)
{
    unsigned int reg;

    if (preferred < register_count &&
        owners[preferred] == MIGA80_INVALID_VALUE) {
        return (int)preferred;
    }
    for (reg = 0U; reg < register_count; ++reg) {
        if (owners[reg] == MIGA80_INVALID_VALUE) {
            return (int)reg;
        }
    }
    return MIGA80_NO_REGISTER;
}

static int value_dies_at(const struct cfg_liveness *liveness,
                         unsigned int block_index, unsigned int value,
                         unsigned int position)
{
    return !live_set_contains(liveness->live_out[block_index], value) &&
           liveness->last_use[value] == position;
}

static int build_allocation_plan(
    const struct miga80_value_function *function,
    struct cfg_liveness *liveness, unsigned int register_count,
    struct allocation_plan *plan,
    struct miga80_diagnostic *diagnostic)
{
    unsigned int parameter_owners[MIGA80_DATA_REGISTER_COUNT];
    unsigned int phi_slot_count;
    unsigned int address_pass;
    unsigned int index;
    unsigned int order_index;

    plan->spill_slot_count = 0U;
    for (index = 0U; index < MIGA80_MAX_VALUE_INSTRUCTIONS; ++index) {
        plan->registers[index] = MIGA80_NO_REGISTER;
        plan->spill_slots[index] = MIGA80_NO_SPILL_SLOT;
    }
    for (index = 0U; index < MIGA80_DATA_REGISTER_COUNT; ++index) {
        plan->saved_registers[index] = 0;
        parameter_owners[index] = MIGA80_INVALID_VALUE;
    }
    if (!allocate_phi_slots(function, liveness, plan, diagnostic)) {
        return 0;
    }
    phi_slot_count = plan->spill_slot_count;
    if (!reserve_phi_temporary_if_needed(function, plan, phi_slot_count,
                                         diagnostic)) {
        return 0;
    }
    phi_slot_count = plan->spill_slot_count;

    for (address_pass = 0U; address_pass < 2U; ++address_pass) {
        for (index = 0U; index < function->value_count; ++index) {
            const struct miga80_value_instruction *value =
                &function->values[index];

            if (value->live &&
                value->opcode == MIGA80_VALUE_PARAMETER &&
                (unsigned int)miga80_type_is_address(value->type) ==
                    address_pass) {
                unsigned int reg;

                if (address_pass == 0U) {
                    enum miga80_abi_register abi_register;
                    const unsigned int class_index = parameter_class_index(
                        function->parameter_types, value->parameter_index,
                        0);

                    if (!miga80_abi_scalar_argument_register(
                            class_index, &abi_register) ||
                        abi_register < MIGA80_ABI_D0 ||
                        abi_register > MIGA80_ABI_D7) {
                        return fail(diagnostic, value->line, value->column,
                                    "O1 scalar parameter register is invalid");
                    }
                    reg = (unsigned int)(abi_register - MIGA80_ABI_D0);
                } else {
                    for (reg = 0U; reg < register_count; ++reg) {
                        if (parameter_owners[reg] == MIGA80_INVALID_VALUE) {
                            break;
                        }
                    }
                    if (reg == register_count) {
                        return fail(diagnostic, value->line, value->column,
                                    "O1 address parameter has no data register");
                    }
                }
                if (reg >= register_count ||
                    parameter_owners[reg] != MIGA80_INVALID_VALUE) {
                    return fail(diagnostic, value->line, value->column,
                                "O1 parameter register is assigned twice");
                }
                parameter_owners[reg] = index;
                plan->registers[index] = (int)reg;
                if (reg >= (unsigned int)(MIGA80_ABI_D3 - MIGA80_ABI_D0)) {
                    plan->saved_registers[reg] = 1;
                }
            }
        }
    }

    for (order_index = 0U; order_index < function->block_order_count;
         ++order_index) {
        const unsigned int block_index = function->block_order[order_index];
        const struct miga80_value_basic_block *block =
            &function->blocks[block_index];
        unsigned int owners[MIGA80_DATA_REGISTER_COUNT];
        unsigned int spill_owners[MIGA80_MAX_VALUE_INSTRUCTIONS];
        unsigned int offset;

        for (index = 0U; index < MIGA80_DATA_REGISTER_COUNT; ++index) {
            owners[index] = MIGA80_INVALID_VALUE;
        }
        for (index = 0U; index < MIGA80_MAX_VALUE_INSTRUCTIONS; ++index) {
            spill_owners[index] = MIGA80_INVALID_VALUE;
            liveness->last_use[index] = MIGA80_INVALID_VALUE;
        }
        for (index = 0U; index < function->value_count; ++index) {
            const struct miga80_value_instruction *value =
                &function->values[index];

            if (!value->live ||
                !live_set_contains(liveness->live_in[block_index], index) ||
                value->opcode == MIGA80_VALUE_CONSTANT) {
                continue;
            }
            if (plan->registers[index] != MIGA80_NO_REGISTER) {
                const unsigned int reg =
                    (unsigned int)plan->registers[index];

                if (reg >= register_count ||
                    owners[reg] != MIGA80_INVALID_VALUE) {
                    return fail(diagnostic, value->line, value->column,
                                "O1 live-in register conflict");
                }
                owners[reg] = index;
            } else if (plan->spill_slots[index] != MIGA80_NO_SPILL_SLOT) {
                const unsigned int slot = plan->spill_slots[index];

                if (spill_owners[slot] != MIGA80_INVALID_VALUE) {
                    return fail(diagnostic, value->line, value->column,
                                "O1 live-in spill conflict");
                }
                spill_owners[slot] = index;
            } else {
                return fail(diagnostic, value->line, value->column,
                            "O1 live-in value has no location");
            }
        }

        for (offset = 0U; offset < block->value_count; ++offset) {
            const unsigned int value_index = block->first_value + offset;
            const struct miga80_value_instruction *value =
                &function->values[value_index];

            if (!value->live || value->opcode == MIGA80_VALUE_PHI) {
                continue;
            }
            if (opcode_has_left(value->opcode)) {
                liveness->last_use[value->left] = value_index;
            }
            if (opcode_has_right(value->opcode)) {
                liveness->last_use[value->right] = value_index;
            }
        }
        if (block->terminator == MIGA80_VALUE_BRANCH) {
            liveness->last_use[block->condition] =
                block->first_value + block->value_count;
        } else if (block->terminator == MIGA80_VALUE_RETURN) {
            liveness->last_use[function->result] =
                block->first_value + block->value_count;
        }

        for (offset = 0U; offset < block->value_count; ++offset) {
            const unsigned int value_index = block->first_value + offset;
            const struct miga80_value_instruction *value =
                &function->values[value_index];
            int left_register = MIGA80_NO_REGISTER;
            int right_register = MIGA80_NO_REGISTER;
            int destination = MIGA80_NO_REGISTER;
            unsigned int destination_spill = MIGA80_NO_SPILL_SLOT;
            unsigned int preferred = MIGA80_DATA_REGISTER_COUNT;
            unsigned int owner_index;

            for (owner_index = 0U; owner_index < register_count;
                 ++owner_index) {
                const unsigned int owner = owners[owner_index];

                if (owner != MIGA80_INVALID_VALUE &&
                    !live_set_contains(liveness->live_out[block_index],
                                       owner) &&
                    (liveness->last_use[owner] == MIGA80_INVALID_VALUE ||
                     liveness->last_use[owner] < value_index)) {
                    owners[owner_index] = MIGA80_INVALID_VALUE;
                }
            }
            for (owner_index = phi_slot_count;
                 owner_index < plan->spill_slot_count; ++owner_index) {
                const unsigned int owner = spill_owners[owner_index];

                if (owner != MIGA80_INVALID_VALUE &&
                    !live_set_contains(liveness->live_out[block_index],
                                       owner) &&
                    (liveness->last_use[owner] == MIGA80_INVALID_VALUE ||
                     liveness->last_use[owner] < value_index)) {
                    spill_owners[owner_index] = MIGA80_INVALID_VALUE;
                }
            }

            if (value->live && value->opcode == MIGA80_VALUE_PARAMETER) {
                const unsigned int parameter_register =
                    (unsigned int)plan->registers[value_index];

                if (parameter_register >= register_count ||
                    owners[parameter_register] != MIGA80_INVALID_VALUE) {
                    return fail(diagnostic, value->line, value->column,
                                "O1 parameter definition conflicts");
                }
                owners[parameter_register] = value_index;
                continue;
            }
            if (!value->live || value->opcode == MIGA80_VALUE_CONSTANT ||
                value->opcode == MIGA80_VALUE_PHI) {
                continue;
            }
            if (opcode_has_left(value->opcode)) {
                left_register = plan->registers[value->left];
            }
            if (opcode_has_right(value->opcode)) {
                right_register = plan->registers[value->right];
            }

            if (function->result == value_index) {
                preferred = 0U;
            }
            if (value->opcode == MIGA80_VALUE_MUL &&
                function->values[value->right].opcode ==
                    MIGA80_VALUE_CONSTANT &&
                function->values[value->right].immediate == 3U) {
                destination =
                    find_free_register(owners, register_count, preferred);
            }
            if (destination == MIGA80_NO_REGISTER &&
                left_register != MIGA80_NO_REGISTER &&
                value_dies_at(liveness, block_index, value->left,
                              value_index)) {
                destination = left_register;
            }
            if (destination == MIGA80_NO_REGISTER &&
                opcode_is_commutative(value->opcode) &&
                right_register != MIGA80_NO_REGISTER &&
                value_dies_at(liveness, block_index, value->right,
                              value_index)) {
                destination = right_register;
            }
            if (destination == MIGA80_NO_REGISTER) {
                destination =
                    find_free_register(owners, register_count, preferred);
            }
            if (destination == MIGA80_NO_REGISTER) {
                unsigned int slot;

                if (opcode_has_left(value->opcode) &&
                    plan->spill_slots[value->left] != MIGA80_NO_SPILL_SLOT &&
                    plan->spill_slots[value->left] >= phi_slot_count &&
                    value_dies_at(liveness, block_index, value->left,
                                  value_index)) {
                    destination_spill = plan->spill_slots[value->left];
                } else if (opcode_has_right(value->opcode) &&
                           plan->spill_slots[value->right] !=
                               MIGA80_NO_SPILL_SLOT &&
                           plan->spill_slots[value->right] >=
                               phi_slot_count &&
                           value_dies_at(liveness, block_index,
                                         value->right, value_index)) {
                    destination_spill = plan->spill_slots[value->right];
                } else {
                    for (slot = phi_slot_count;
                         slot < plan->spill_slot_count; ++slot) {
                        if (spill_owners[slot] == MIGA80_INVALID_VALUE) {
                            destination_spill = slot;
                            break;
                        }
                    }
                    if (destination_spill == MIGA80_NO_SPILL_SLOT) {
                        const unsigned int frame_size =
                            (plan->spill_slot_count + 1U) * 4U;

                        if (plan->spill_slot_count ==
                                MIGA80_MAX_VALUE_INSTRUCTIONS ||
                            !miga80_abi_frame_size_is_valid(frame_size)) {
                            return fail(diagnostic, value->line,
                                        value->column,
                                        "O1 spill frame exceeds ABI limit");
                        }
                        destination_spill = plan->spill_slot_count++;
                    }
                }
            }

            if (left_register != MIGA80_NO_REGISTER &&
                value_dies_at(liveness, block_index, value->left,
                              value_index) &&
                left_register != destination) {
                owners[left_register] = MIGA80_INVALID_VALUE;
            }
            if (right_register != MIGA80_NO_REGISTER &&
                value_dies_at(liveness, block_index, value->right,
                              value_index) &&
                right_register != destination) {
                owners[right_register] = MIGA80_INVALID_VALUE;
            }
            if (opcode_has_left(value->opcode) &&
                plan->spill_slots[value->left] != MIGA80_NO_SPILL_SLOT &&
                plan->spill_slots[value->left] >= phi_slot_count &&
                value_dies_at(liveness, block_index, value->left,
                              value_index) &&
                plan->spill_slots[value->left] != destination_spill) {
                spill_owners[plan->spill_slots[value->left]] =
                    MIGA80_INVALID_VALUE;
            }
            if (opcode_has_right(value->opcode) &&
                plan->spill_slots[value->right] != MIGA80_NO_SPILL_SLOT &&
                plan->spill_slots[value->right] >= phi_slot_count &&
                value_dies_at(liveness, block_index, value->right,
                              value_index) &&
                plan->spill_slots[value->right] != destination_spill) {
                spill_owners[plan->spill_slots[value->right]] =
                    MIGA80_INVALID_VALUE;
            }
            if (destination != MIGA80_NO_REGISTER) {
                owners[destination] = value_index;
                plan->registers[value_index] = destination;
                if (destination >=
                    (int)(MIGA80_ABI_D3 - MIGA80_ABI_D0)) {
                    plan->saved_registers[destination] = 1;
                }
            } else {
                plan->spill_slots[value_index] = destination_spill;
                spill_owners[destination_spill] = value_index;
                plan->saved_registers[MIGA80_SPILL_SCRATCH_REGISTER] = 1;
            }
        }
    }
    return 1;
}

static const char *data_register_name(int reg)
{
    return miga80_abi_gnu_register_name(
        (enum miga80_abi_register)(MIGA80_ABI_D0 + reg));
}

static unsigned int spill_offset(const struct allocation_plan *plan,
                                 unsigned int source)
{
    return (plan->spill_slots[source] + 1U) * 4U;
}

static int emit_move(FILE *output,
                     const struct miga80_value_function *function,
                     const struct allocation_plan *plan, unsigned int source,
                     int destination)
{
    const struct miga80_value_instruction *value = &function->values[source];

    if (value->opcode == MIGA80_VALUE_CONSTANT) {
        const uint32_t constant = value->immediate;

        if (value->type == MIGA80_TYPE_STRING) {
            return miga80_emit_gnu_m68k_string_address(
                       output, function->name, (unsigned int)constant,
                       "%a0") &&
                   output_line(output, "        move.l  %%a0,%s\n",
                               data_register_name(destination));
        }
        if (constant <= UINT32_C(127)) {
            return output_line(output, "        moveq   #%u,%s\n",
                               (unsigned int)constant,
                               data_register_name(destination));
        }
        if (constant >= UINT32_C(0xffffff80)) {
            return output_line(output, "        moveq   #-%u,%s\n",
                               (unsigned int)(0U - constant),
                               data_register_name(destination));
        }
        return output_line(output, "        move.l  #0x%08x,%s\n",
                           (unsigned int)constant,
                           data_register_name(destination));
    }
    if (plan->registers[source] == destination) {
        return 1;
    }
    if (plan->registers[source] != MIGA80_NO_REGISTER) {
        return output_line(output, "        move.l  %s,%s\n",
                           data_register_name(plan->registers[source]),
                           data_register_name(destination));
    }
    if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        return output_line(output, "        move.l  -%u(%%a6),%s\n",
                           spill_offset(plan, source),
                           data_register_name(destination));
    }
    return 0;
}

static int emit_register_source(FILE *output, const char *operation,
                                const struct allocation_plan *plan,
                                unsigned int source, int destination)
{
    if (plan->registers[source] != MIGA80_NO_REGISTER) {
        return output_line(output, "        %-7s %s,%s\n", operation,
                           data_register_name(plan->registers[source]),
                           data_register_name(destination));
    }
    if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        return output_line(output, "        %-7s -%u(%%a6),%s\n", operation,
                           spill_offset(plan, source),
                           data_register_name(destination));
    }
    return 0;
}

static int emit_add_immediate(FILE *output, uint32_t constant,
                              int destination)
{
    if (constant >= 1U && constant <= 8U) {
        return output_line(output, "        addq.l  #%u,%s\n",
                           (unsigned int)constant,
                           data_register_name(destination));
    }
    if (constant >= UINT32_C(0xfffffff8)) {
        return output_line(output, "        subq.l  #%u,%s\n",
                           (unsigned int)(0U - constant),
                           data_register_name(destination));
    }
    return output_line(output, "        add.l   #0x%08x,%s\n",
                       (unsigned int)constant,
                       data_register_name(destination));
}

static int emit_sub_immediate(FILE *output, uint32_t constant,
                              int destination)
{
    if (constant >= 1U && constant <= 8U) {
        return output_line(output, "        subq.l  #%u,%s\n",
                           (unsigned int)constant,
                           data_register_name(destination));
    }
    if (constant >= UINT32_C(0xfffffff8)) {
        return output_line(output, "        addq.l  #%u,%s\n",
                           (unsigned int)(0U - constant),
                           data_register_name(destination));
    }
    return output_line(output, "        sub.l   #0x%08x,%s\n",
                       (unsigned int)constant,
                       data_register_name(destination));
}

static unsigned int power_of_two_shift(uint32_t constant)
{
    unsigned int shift = 0U;

    if (constant == 0U || (constant & (constant - 1U)) != 0U) {
        return 0U;
    }
    while (constant > 1U) {
        constant >>= 1;
        ++shift;
    }
    return shift;
}

static int emit_multiply(FILE *output,
                         const struct miga80_value_function *function,
                         const struct allocation_plan *plan,
                         const struct miga80_value_instruction *value,
                         int destination, unsigned int source)
{
    const struct miga80_value_instruction *operand = &function->values[source];

    if (operand->opcode == MIGA80_VALUE_CONSTANT) {
        const uint32_t constant = operand->immediate;
        const unsigned int shift = power_of_two_shift(constant);

        if (constant == 2U) {
            return output_line(output, "        add.l   %s,%s\n",
                               data_register_name(destination),
                               data_register_name(destination));
        }
        if (constant == 3U &&
            destination != plan->registers[value->left]) {
            return output_line(output, "        add.l   %s,%s\n",
                               data_register_name(destination),
                               data_register_name(destination)) &&
                   emit_register_source(output, "add.l", plan, value->left,
                                        destination);
        }
        if (shift >= 1U && shift <= 8U) {
            return output_line(output, "        lsl.l   #%u,%s\n", shift,
                               data_register_name(destination));
        }
        return output_line(output, "        muls.l  #0x%08x,%s\n",
                           (unsigned int)constant,
                           data_register_name(destination));
    }
    return emit_register_source(output, "muls.l", plan, source,
                                destination);
}

static int emit_fix_multiply(
    FILE *output, const struct miga80_value_function *function,
    const struct allocation_plan *plan, unsigned int source,
    int destination)
{
    const struct miga80_value_instruction *operand =
        &function->values[source];
    int multiplied;

    if (operand->opcode == MIGA80_VALUE_CONSTANT) {
        multiplied = output_line(
            output, "        muls.l  #0x%08x,%%d7:%s\n",
            (unsigned int)operand->immediate,
            data_register_name(destination));
    } else if (plan->registers[source] != MIGA80_NO_REGISTER) {
        multiplied = output_line(
            output, "        muls.l  %s,%%d7:%s\n",
            data_register_name(plan->registers[source]),
            data_register_name(destination));
    } else if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        multiplied = output_line(
            output, "        muls.l  -%u(%%a6),%%d7:%s\n",
            spill_offset(plan, source), data_register_name(destination));
    } else {
        return 0;
    }
    return multiplied &&
           output_line(output,
                       "        move.w  %%d7,%s\n"
                       "        swap    %s\n",
                       data_register_name(destination),
                       data_register_name(destination));
}

static int emit_fix_division(
    FILE *output, const struct miga80_value_function *function,
    unsigned int value_index, int dynamic_divisor, int destination)
{
    if (dynamic_divisor &&
        (!output_line(output, "        tst.l   %%d6\n") ||
         !output_line(output, "        beq     .L_%s_divzero_%u\n",
                      function->name, value_index))) {
        return 0;
    }
    return output_line(
        output,
        "        move.l  %s,%%d7\n"
        "        eor.l   %%d6,%%d7\n"
        "        move.l  %%d7,-(%%a7)\n"
        "        tst.l   %s\n"
        "        bpl     .L_%s_fixdiv_left_%u\n"
        "        neg.l   %s\n"
        ".L_%s_fixdiv_left_%u:\n"
        "        tst.l   %%d6\n"
        "        bpl     .L_%s_fixdiv_right_%u\n"
        "        neg.l   %%d6\n"
        ".L_%s_fixdiv_right_%u:\n"
        "        move.l  %s,%%d7\n"
        "        lsr.l   #8,%%d7\n"
        "        lsr.l   #8,%%d7\n"
        "        swap    %s\n"
        "        clr.w   %s\n"
        "        move.l  %s,-(%%a7)\n"
        "        move.l  %%d7,%s\n"
        "        moveq   #0,%%d7\n"
        "        divul.l %%d6,%%d7:%s\n"
        "        move.l  (%%a7)+,%s\n"
        "        divu.l  %%d6,%%d7:%s\n"
        "        move.l  (%%a7)+,%%d7\n"
        "        tst.l   %%d7\n"
        "        bpl     .L_%s_fixdiv_done_%u\n"
        "        neg.l   %s\n"
        ".L_%s_fixdiv_done_%u:\n",
        data_register_name(destination), data_register_name(destination),
        function->name, value_index, data_register_name(destination),
        function->name, value_index, function->name, value_index,
        function->name, value_index, data_register_name(destination),
        data_register_name(destination), data_register_name(destination),
        data_register_name(destination), data_register_name(destination),
        data_register_name(destination), data_register_name(destination),
        data_register_name(destination), function->name, value_index,
        data_register_name(destination), function->name, value_index);
}

static int emit_i32_to_fix(FILE *output,
                           const struct miga80_value_function *function,
                           unsigned int value_index, int destination,
                           int range_guard)
{
    if (range_guard &&
        !output_line(output,
                     "        add.l   #0x00008000,%s\n"
                     "        cmp.l   #0x00010000,%s\n"
                     "        bcc     .L_%s_conversion_%u\n"
                     "        sub.l   #0x00008000,%s\n",
                     data_register_name(destination),
                     data_register_name(destination), function->name,
                     value_index, data_register_name(destination))) {
        return 0;
    }
    return output_line(output,
                       "        swap    %s\n"
                       "        clr.w   %s\n",
                       data_register_name(destination),
                       data_register_name(destination));
}

static int emit_fix_to_i32(FILE *output,
                           const struct miga80_value_function *function,
                           unsigned int value_index, int destination)
{
    return output_line(
        output,
        "        tst.l   %s\n"
        "        bpl     .L_%s_fix_to_i32_positive_%u\n"
        "        add.l   #0x0000ffff,%s\n"
        ".L_%s_fix_to_i32_positive_%u:\n"
        "        swap    %s\n"
        "        ext.l   %s\n",
        data_register_name(destination), function->name, value_index,
        data_register_name(destination), function->name, value_index,
        data_register_name(destination), data_register_name(destination));
}

static int emit_division(FILE *output,
                         const struct miga80_value_function *function,
                         const struct allocation_plan *plan,
                         unsigned int value_index, unsigned int source,
                         int destination,
                         enum miga80_value_opcode opcode)
{
    const struct miga80_value_instruction *operand =
        &function->values[source];
    const char *operation =
        opcode == MIGA80_VALUE_DIV ? "divs.l" : "divu.l";

    if (operand->opcode == MIGA80_VALUE_CONSTANT) {
        return output_line(output, "        %-7s #0x%08x,%s\n", operation,
                           (unsigned int)operand->immediate,
                           data_register_name(destination));
    }
    if (plan->registers[source] != MIGA80_NO_REGISTER) {
        if (!output_line(output, "        tst.l   %s\n",
                         data_register_name(plan->registers[source]))) {
            return 0;
        }
    } else if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        if (!output_line(output, "        tst.l   -%u(%%a6)\n",
                         spill_offset(plan, source))) {
            return 0;
        }
    } else {
        return 0;
    }
    return output_line(output, "        beq     .L_%s_divzero_%u\n",
                       function->name, value_index) &&
           emit_register_source(output, operation, plan, source,
                                destination);
}

static int store_spilled_result(FILE *output,
                                const struct allocation_plan *plan,
                                unsigned int index, int source)
{
    if (plan->spill_slots[index] == MIGA80_NO_SPILL_SLOT) {
        return 1;
    }
    return output_line(output, "        move.l  %s,-%u(%%a6)\n",
                       data_register_name(source), spill_offset(plan, index));
}

static int emit_comparison(FILE *output,
                           const struct miga80_value_function *function,
                           const struct allocation_plan *plan,
                           enum miga80_value_opcode opcode,
                           unsigned int source, int destination)
{
    const struct miga80_value_instruction *operand = &function->values[source];
    const char *condition = "seq";
    int compared;

    if (opcode == MIGA80_VALUE_NE) {
        condition = "sne";
    } else if (opcode == MIGA80_VALUE_LT_I32) {
        condition = "slt";
    } else if (opcode == MIGA80_VALUE_LE_I32) {
        condition = "sle";
    } else if (opcode == MIGA80_VALUE_GT_I32) {
        condition = "sgt";
    } else if (opcode == MIGA80_VALUE_GE_I32) {
        condition = "sge";
    } else if (opcode == MIGA80_VALUE_LT_U32) {
        condition = "scs";
    } else if (opcode == MIGA80_VALUE_LE_U32) {
        condition = "sls";
    } else if (opcode == MIGA80_VALUE_GT_U32) {
        condition = "shi";
    } else if (opcode == MIGA80_VALUE_GE_U32) {
        condition = "scc";
    }
    if (operand->opcode == MIGA80_VALUE_CONSTANT) {
        if (operand->type == MIGA80_TYPE_STRING) {
            compared = miga80_emit_gnu_m68k_string_address(
                           output, function->name,
                           (unsigned int)operand->immediate, "%a0") &&
                       output_line(output, "        cmp.l   %%a0,%s\n",
                                   data_register_name(destination));
        } else {
            compared = output_line(output, "        cmp.l   #0x%08x,%s\n",
                                   (unsigned int)operand->immediate,
                                   data_register_name(destination));
        }
    } else {
        compared = emit_register_source(output, "cmp.l", plan, source,
                                        destination);
    }
    return compared &&
           output_line(output,
                       "        %-7s %s\n"
                       "        and.l   #1,%s\n",
                       condition, data_register_name(destination),
                       data_register_name(destination));
}

static int emit_value(FILE *output,
                      const struct miga80_value_function *function,
                      const struct allocation_plan *plan, unsigned int index)
{
    const struct miga80_value_instruction *value = &function->values[index];
    const int destination = plan->registers[index] != MIGA80_NO_REGISTER
                                ? plan->registers[index]
                                : MIGA80_SPILL_SCRATCH_REGISTER;
    unsigned int source = value->right;
    int emitted;

    if (value->opcode == MIGA80_VALUE_NEG) {
        emitted =
            emit_move(output, function, plan, value->left, destination) &&
            output_line(output, "        neg.l   %s\n",
                        data_register_name(destination)) &&
            miga80_emit_gnu_m68k_normalize_integer(
                output, value->type, data_register_name(destination));
        return emitted &&
               store_spilled_result(output, plan, index, destination);
    }

    if (value->opcode == MIGA80_VALUE_NORMALIZE_INTEGER) {
        emitted = emit_move(output, function, plan, value->left,
                            destination) &&
                  miga80_emit_gnu_m68k_normalize_integer(
                      output, value->type, data_register_name(destination));
        return emitted &&
               store_spilled_result(output, plan, index, destination);
    }

    if (value->opcode == MIGA80_VALUE_FIX_FROM_I32) {
        emitted = emit_move(output, function, plan, value->left,
                            destination) &&
                  emit_i32_to_fix(
                      output, function, index, destination,
                      function->values[value->left].opcode !=
                          MIGA80_VALUE_I32_FROM_FIX);
        return emitted &&
               store_spilled_result(output, plan, index, destination);
    }

    if (value->opcode == MIGA80_VALUE_I32_FROM_FIX) {
        emitted = emit_move(output, function, plan, value->left,
                            destination) &&
                  emit_fix_to_i32(output, function, index, destination);
        return emitted &&
               store_spilled_result(output, plan, index, destination);
    }

    if (value->opcode == MIGA80_VALUE_MUL_FIX) {
        const int fix_destination =
            plan->registers[index] != MIGA80_NO_REGISTER
                ? plan->registers[index]
                : MIGA80_FIX_LOW_SCRATCH_REGISTER;

        if (plan->registers[value->right] == fix_destination &&
            function->values[value->right].opcode !=
                MIGA80_VALUE_CONSTANT) {
            source = value->left;
        } else if (!emit_move(output, function, plan, value->left,
                              fix_destination)) {
            return 0;
        }
        emitted = emit_fix_multiply(output, function, plan, source,
                                    fix_destination);
        return emitted &&
               store_spilled_result(output, plan, index, fix_destination);
    }

    if (value->opcode == MIGA80_VALUE_DIV_FIX) {
        const int fix_destination =
            plan->registers[index] != MIGA80_NO_REGISTER
                ? plan->registers[index]
                : MIGA80_FIX_DIV_LOW_SCRATCH_REGISTER;

        if (!emit_move(output, function, plan, value->right,
                       MIGA80_FIX_DIVISOR_SCRATCH_REGISTER) ||
            !emit_move(output, function, plan, value->left,
                       fix_destination)) {
            return 0;
        }
        emitted = emit_fix_division(
            output, function, index,
            function->values[value->right].opcode != MIGA80_VALUE_CONSTANT,
            fix_destination);
        return emitted &&
               store_spilled_result(output, plan, index, fix_destination);
    }

    if (opcode_is_commutative(value->opcode) &&
        plan->registers[value->right] == destination &&
        function->values[value->right].opcode != MIGA80_VALUE_CONSTANT) {
        source = value->left;
    } else if (!emit_move(output, function, plan, value->left, destination)) {
        return 0;
    }

    if ((value->opcode >= MIGA80_VALUE_EQ &&
         value->opcode <= MIGA80_VALUE_GE_I32) ||
        (value->opcode >= MIGA80_VALUE_LT_U32 &&
         value->opcode <= MIGA80_VALUE_GE_U32)) {
        emitted = emit_comparison(output, function, plan, value->opcode,
                                  source, destination);
        return emitted &&
               store_spilled_result(output, plan, index, destination);
    }
    if (function->values[source].opcode == MIGA80_VALUE_CONSTANT) {
        if (value->opcode == MIGA80_VALUE_ADD) {
            emitted = emit_add_immediate(
                          output, function->values[source].immediate,
                          destination) &&
                      miga80_emit_gnu_m68k_normalize_integer(
                          output, value->type,
                          data_register_name(destination));
            return emitted &&
                   store_spilled_result(output, plan, index, destination);
        }
        if (value->opcode == MIGA80_VALUE_SUB) {
            emitted = emit_sub_immediate(
                          output, function->values[source].immediate,
                          destination) &&
                      miga80_emit_gnu_m68k_normalize_integer(
                          output, value->type,
                          data_register_name(destination));
            return emitted &&
                   store_spilled_result(output, plan, index, destination);
        }
    }
    if (value->opcode == MIGA80_VALUE_ADD) {
        emitted = emit_register_source(output, "add.l", plan, source,
                                       destination);
    } else if (value->opcode == MIGA80_VALUE_SUB) {
        emitted = emit_register_source(output, "sub.l", plan, source,
                                       destination);
    } else if (value->opcode == MIGA80_VALUE_DIV ||
               value->opcode == MIGA80_VALUE_DIV_U) {
        emitted = emit_division(output, function, plan, index, source,
                                destination, value->opcode);
    } else {
        emitted =
            emit_multiply(output, function, plan, value, destination, source);
    }
    if (emitted && (value->opcode == MIGA80_VALUE_ADD ||
                    value->opcode == MIGA80_VALUE_SUB ||
                    value->opcode == MIGA80_VALUE_MUL ||
                    value->opcode == MIGA80_VALUE_MUL_FIX ||
                    value->opcode == MIGA80_VALUE_DIV_FIX ||
                    value->opcode == MIGA80_VALUE_DIV ||
                    value->opcode == MIGA80_VALUE_DIV_U)) {
        emitted = miga80_emit_gnu_m68k_normalize_integer(
            output, value->type, data_register_name(destination));
    }
    return emitted && store_spilled_result(output, plan, index, destination);
}

static int build_saved_register_list(const struct allocation_plan *plan,
                                     char *text, size_t text_size)
{
    size_t used = 0U;
    unsigned int reg;

    text[0] = '\0';
    for (reg = 3U; reg < MIGA80_DATA_REGISTER_COUNT; ++reg) {
        int written;

        if (!plan->saved_registers[reg]) {
            continue;
        }
        written = snprintf(text + used, text_size - used, "%s%s",
                           used == 0U ? "" : "/", data_register_name((int)reg));
        if (written < 0 || (size_t)written >= text_size - used) {
            return 0;
        }
        used += (size_t)written;
    }
    return 1;
}

struct phi_edge_copy {
    unsigned int phi_index;
    unsigned int source;
    unsigned int source_slot;
    int pending;
};

static int emit_phi_copy(FILE *output,
                         const struct miga80_value_function *function,
                         const struct allocation_plan *plan,
                         const struct phi_edge_copy *copy)
{
    if (copy->source == MIGA80_INVALID_VALUE) {
        if (plan->phi_temporary_slot == MIGA80_NO_SPILL_SLOT) {
            return 0;
        }
        return output_line(output,
                           "        move.l  -%u(%%a6),%%d7\n"
                           "        move.l  %%d7,-%u(%%a6)\n",
                           (plan->phi_temporary_slot + 1U) * 4U,
                           spill_offset(plan, copy->phi_index));
    }
    if (function->values[copy->source].opcode == MIGA80_VALUE_CONSTANT) {
        if (function->values[copy->source].type == MIGA80_TYPE_STRING) {
            return miga80_emit_gnu_m68k_string_address(
                       output, function->name,
                       (unsigned int)function->values[copy->source].immediate,
                       "%a0") &&
                   output_line(output,
                               "        move.l  %%a0,-%u(%%a6)\n",
                               spill_offset(plan, copy->phi_index));
        }
        return output_line(
            output, "        move.l  #0x%08x,-%u(%%a6)\n",
            (unsigned int)function->values[copy->source].immediate,
            spill_offset(plan, copy->phi_index));
    }
    if (plan->registers[copy->source] != MIGA80_NO_REGISTER) {
        return output_line(output, "        move.l  %s,-%u(%%a6)\n",
                           data_register_name(plan->registers[copy->source]),
                           spill_offset(plan, copy->phi_index));
    }
    if (plan->spill_slots[copy->source] != MIGA80_NO_SPILL_SLOT) {
        return output_line(output,
                           "        move.l  -%u(%%a6),%%d7\n"
                           "        move.l  %%d7,-%u(%%a6)\n",
                           spill_offset(plan, copy->source),
                           spill_offset(plan, copy->phi_index));
    }
    return 0;
}

static int emit_phi_edge(FILE *output,
                         const struct miga80_value_function *function,
                         const struct allocation_plan *plan,
                         unsigned int predecessor,
                         unsigned int successor)
{
    const struct miga80_value_basic_block *block =
        &function->blocks[successor];
    struct phi_edge_copy copies[MIGA80_MAX_LOCALS];
    unsigned int copy_count = 0U;
    unsigned int remaining;
    unsigned int offset;

    for (offset = 0U; offset < block->value_count; ++offset) {
        const unsigned int phi_index = block->first_value + offset;
        const struct miga80_value_instruction *phi =
            &function->values[phi_index];
        unsigned int source;
        unsigned int source_slot;

        if (!phi->live || phi->opcode != MIGA80_VALUE_PHI) {
            continue;
        }
        if (!phi_source_for_edge(phi, predecessor, &source) ||
            plan->spill_slots[phi_index] == MIGA80_NO_SPILL_SLOT) {
            return 0;
        }
        source_slot = plan->spill_slots[source];
        if (source_slot == plan->spill_slots[phi_index]) {
            continue;
        }
        if (copy_count == MIGA80_MAX_LOCALS) {
            return 0;
        }
        copies[copy_count].phi_index = phi_index;
        copies[copy_count].source = source;
        copies[copy_count].source_slot = source_slot;
        copies[copy_count].pending = 1;
        ++copy_count;
    }

    remaining = copy_count;
    while (remaining != 0U) {
        unsigned int safe = MIGA80_INVALID_VALUE;
        unsigned int copy_index;

        for (copy_index = 0U; copy_index < copy_count; ++copy_index) {
            unsigned int other;
            int destination_is_source = 0;

            if (!copies[copy_index].pending) {
                continue;
            }
            for (other = 0U; other < copy_count; ++other) {
                if (other != copy_index && copies[other].pending &&
                    copies[other].source_slot ==
                        plan->spill_slots[copies[copy_index].phi_index]) {
                    destination_is_source = 1;
                    break;
                }
            }
            if (!destination_is_source) {
                safe = copy_index;
                break;
            }
        }
        if (safe != MIGA80_INVALID_VALUE) {
            if (!emit_phi_copy(output, function, plan, &copies[safe])) {
                return 0;
            }
            copies[safe].pending = 0;
            --remaining;
            continue;
        }
        if (plan->phi_temporary_slot == MIGA80_NO_SPILL_SLOT) {
            return 0;
        }
        for (copy_index = 0U; copy_index < copy_count; ++copy_index) {
            if (copies[copy_index].pending) {
                unsigned int other;
                const unsigned int destination_slot =
                    plan->spill_slots[copies[copy_index].phi_index];
                int replaced = 0;

                if (!output_line(output,
                                 "        move.l  -%u(%%a6),%%d7\n"
                                 "        move.l  %%d7,-%u(%%a6)\n",
                                 (destination_slot + 1U) * 4U,
                                 (plan->phi_temporary_slot + 1U) * 4U)) {
                    return 0;
                }
                for (other = 0U; other < copy_count; ++other) {
                    if (copies[other].pending &&
                        copies[other].source_slot == destination_slot) {
                        copies[other].source = MIGA80_INVALID_VALUE;
                        copies[other].source_slot =
                            plan->phi_temporary_slot;
                        replaced = 1;
                    }
                }
                if (!replaced) {
                    return 0;
                }
                break;
            }
        }
    }
    return 1;
}

static int emit_epilogue(FILE *output, const char *saved_registers,
                         unsigned int frame_size)
{
    if (saved_registers[0] != '\0' &&
        !output_line(output, "        movem.l (%%a7)+,%s\n",
                     saved_registers)) {
        return 0;
    }
    if (frame_size != 0U && !output_line(output, "        unlk    %%a6\n")) {
        return 0;
    }
    return output_line(output, "        rts\n");
}

static int emit_address_parameter_copies(
    FILE *output, const struct miga80_value_function *function,
    const struct allocation_plan *plan)
{
    unsigned int index;

    for (index = 0U; index < function->value_count; ++index) {
        const struct miga80_value_instruction *value =
            &function->values[index];

        if (value->live && value->opcode == MIGA80_VALUE_PARAMETER &&
            miga80_type_is_address(value->type)) {
            enum miga80_abi_register abi_register;
            const char *source_name;
            const unsigned int class_index = parameter_class_index(
                function->parameter_types, value->parameter_index, 1);

            if (!miga80_abi_address_argument_register(class_index,
                                                       &abi_register) ||
                (source_name = miga80_abi_gnu_register_name(abi_register)) ==
                    NULL ||
                plan->registers[index] == MIGA80_NO_REGISTER ||
                !output_line(output, "        move.l  %s,%s\n", source_name,
                             data_register_name(plan->registers[index]))) {
                return 0;
            }
        }
    }
    return 1;
}

static int emit_address_return(
    FILE *output, const struct miga80_value_function *function,
    const struct allocation_plan *plan)
{
    const unsigned int source = function->result;
    const struct miga80_value_instruction *value =
        &function->values[source];

    if (value->opcode == MIGA80_VALUE_CONSTANT) {
        return miga80_emit_gnu_m68k_string_address(
            output, function->name, (unsigned int)value->immediate, "%a0");
    }
    if (plan->registers[source] != MIGA80_NO_REGISTER) {
        return output_line(output, "        movea.l %s,%%a0\n",
                           data_register_name(plan->registers[source]));
    }
    if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        return output_line(output, "        movea.l -%u(%%a6),%%a0\n",
                           spill_offset(plan, source));
    }
    return 0;
}

static int emit_jump_edge(FILE *output,
                          const struct miga80_value_function *function,
                          const struct allocation_plan *plan,
                          unsigned int predecessor,
                          unsigned int successor, int branch_required)
{
    return emit_phi_edge(output, function, plan, predecessor, successor) &&
           (!branch_required ||
            output_line(output, "        bra     .L_%s_b%u\n", function->name,
                        successor));
}

static int emit_branch(FILE *output,
                       const struct miga80_value_function *function,
                       const struct allocation_plan *plan,
                       unsigned int block_index)
{
    const struct miga80_value_basic_block *block =
        &function->blocks[block_index];
    const struct miga80_value_instruction *condition =
        &function->values[block->condition];

    if (condition->opcode == MIGA80_VALUE_CONSTANT) {
        const unsigned int successor =
            condition->immediate != 0U ? block->successors[0]
                                       : block->successors[1];

        return emit_jump_edge(output, function, plan, block_index, successor,
                              1);
    }
    if (plan->registers[block->condition] != MIGA80_NO_REGISTER) {
        if (!output_line(output, "        tst.l   %s\n",
                         data_register_name(
                             plan->registers[block->condition]))) {
            return 0;
        }
    } else if (plan->spill_slots[block->condition] != MIGA80_NO_SPILL_SLOT) {
        if (!output_line(output,
                         "        move.l  -%u(%%a6),%%d7\n"
                         "        tst.l   %%d7\n",
                         spill_offset(plan, block->condition))) {
            return 0;
        }
    } else {
        return 0;
    }
    return output_line(output, "        beq     .L_%s_b%u_false\n",
                       function->name, block_index) &&
           emit_jump_edge(output, function, plan, block_index,
                          block->successors[0], 1) &&
           output_line(output, ".L_%s_b%u_false:\n", function->name,
                       block_index) &&
           emit_jump_edge(output, function, plan, block_index,
                          block->successors[1], 1);
}

static int emit_allocated_function(
    FILE *output, const struct miga80_value_function *function,
    const struct allocation_plan *plan,
    struct miga80_diagnostic *diagnostic)
{
    char saved_registers[64];
    unsigned int frame_size;
    unsigned int order_index;

    frame_size = plan->spill_slot_count * 4U;
    if (!miga80_abi_frame_size_is_valid(frame_size) ||
        !build_saved_register_list(plan, saved_registers,
                                   sizeof(saved_registers))) {
        if (diagnostic->message[0] == '\0') {
            (void)fail(diagnostic, 0U, 0U,
                       "unable to build O1 saved-register set");
        }
        return 0;
    }

    if (!output_line(output,
                     "/* Generated by miga80c -O1 for native ABI %u.%u; "
                     "development oracle only. */\n"
                     "        .text\n"
                     "        .even\n"
                     "        .globl  %s\n"
                     "%s:\n",
                     MIGA80_ABI_VERSION_MAJOR, MIGA80_ABI_VERSION_MINOR,
                     function->name, function->name)) {
        return fail(diagnostic, 0U, 0U, "unable to write O1 assembly");
    }
    if (frame_size != 0U &&
        !output_line(output, "        link.w  %%a6,#-%u\n", frame_size)) {
        return fail(diagnostic, 0U, 0U, "unable to write O1 spill frame");
    }
    if (saved_registers[0] != '\0' &&
        !output_line(output, "        movem.l %s,-(%%a7)\n",
                     saved_registers)) {
        return fail(diagnostic, 0U, 0U, "unable to write O1 prologue");
    }
    if (!emit_address_parameter_copies(output, function, plan)) {
        return fail(diagnostic, 0U, 0U,
                    "unable to copy O1 address parameters");
    }

    for (order_index = 0U; order_index < function->block_order_count;
         ++order_index) {
        const unsigned int block_index = function->block_order[order_index];
        const struct miga80_value_basic_block *block =
            &function->blocks[block_index];
        unsigned int offset;

        if (!output_line(output, ".L_%s_b%u:\n", function->name,
                         block_index)) {
            return fail(diagnostic, 0U, 0U, "unable to write O1 block label");
        }
        for (offset = 0U; offset < block->value_count; ++offset) {
            const unsigned int index = block->first_value + offset;
            const struct miga80_value_instruction *value =
                &function->values[index];

            if (value->live && value->opcode != MIGA80_VALUE_CONSTANT &&
                value->opcode != MIGA80_VALUE_PARAMETER &&
                value->opcode != MIGA80_VALUE_PHI &&
                !emit_value(output, function, plan, index)) {
                return fail(diagnostic, value->line, value->column,
                            "unable to write O1 value instruction");
            }
        }
        if (block->terminator == MIGA80_VALUE_BRANCH) {
            if (!emit_branch(output, function, plan, block_index)) {
                return fail(diagnostic, 0U, 0U,
                            "unable to write O1 conditional branch");
            }
        } else if (block->terminator == MIGA80_VALUE_JUMP) {
            const int branch_required =
                order_index + 1U == function->block_order_count ||
                function->block_order[order_index + 1U] !=
                    block->successors[0];

            if (!emit_jump_edge(output, function, plan, block_index,
                                block->successors[0], branch_required)) {
                return fail(diagnostic, 0U, 0U,
                            "unable to write O1 jump edge");
            }
        } else {
            const int returned =
                function->result_type == MIGA80_TYPE_STRING
                    ? emit_address_return(output, function, plan)
                    : emit_move(output, function, plan, function->result, 0);

            if (!returned ||
                !emit_epilogue(output, saved_registers, frame_size)) {
                return fail(diagnostic, 0U, 0U,
                            "unable to write O1 return");
            }
        }
    }
    {
        int has_division_fault = 0;
        unsigned int index;

        for (index = 0U; index < function->value_count; ++index) {
            const struct miga80_value_instruction *value =
                &function->values[index];

            if (!value->live ||
                (value->opcode != MIGA80_VALUE_DIV_FIX &&
                 value->opcode != MIGA80_VALUE_DIV &&
                 value->opcode != MIGA80_VALUE_DIV_U) ||
                function->values[value->right].opcode ==
                    MIGA80_VALUE_CONSTANT) {
                continue;
            }
            if (!miga80_emit_gnu_m68k_fault_site(
                    output, function->name, index, value->line,
                    value->column)) {
                return fail(diagnostic, value->line, value->column,
                            "unable to write O1 division fault site");
            }
            has_division_fault = 1;
        }
        if (has_division_fault &&
            !miga80_emit_gnu_m68k_fault_tail(output, function->name)) {
            return fail(diagnostic, 0U, 0U,
                        "unable to write O1 division fault tail");
        }
    }
    {
        int has_conversion_fault = 0;
        unsigned int index;

        for (index = 0U; index < function->value_count; ++index) {
            const struct miga80_value_instruction *value =
                &function->values[index];

            if (!value->live ||
                value->opcode != MIGA80_VALUE_FIX_FROM_I32 ||
                function->values[value->left].opcode ==
                    MIGA80_VALUE_I32_FROM_FIX) {
                continue;
            }
            if (!miga80_emit_gnu_m68k_conversion_fault_site(
                    output, function->name, index, value->line,
                    value->column)) {
                return fail(diagnostic, value->line, value->column,
                            "unable to write O1 conversion fault site");
            }
            has_conversion_fault = 1;
        }
        if (has_conversion_fault &&
            !miga80_emit_gnu_m68k_conversion_fault_tail(
                output, function->name)) {
            return fail(diagnostic, 0U, 0U,
                        "unable to write O1 conversion fault tail");
        }
    }
    if (!miga80_emit_gnu_m68k_constant_pool(output, function->name,
                                             &function->pool)) {
        return fail(diagnostic, 0U, 0U,
                    "unable to write O1 immutable pool");
    }
    if (fflush(output) != 0 || ferror(output)) {
        return fail(diagnostic, 0U, 0U, "unable to finish O1 assembly");
    }
    return 1;
}

int miga80_emit_gnu_m68k_o1(FILE *output,
                            const struct miga80_value_function *function,
                            struct miga80_diagnostic *diagnostic)
{
    struct optimizer_workspace *workspace;
    struct allocation_plan *plan;
    struct cfg_liveness *liveness;
    unsigned int register_count = MIGA80_DATA_REGISTER_COUNT;
    unsigned int index;
    int emitted;

    if (output == NULL || function == NULL || diagnostic == NULL) {
        return 0;
    }
    (void)memset(diagnostic, 0, sizeof(*diagnostic));
    if (!validate_value_function(function, diagnostic)) {
        return 0;
    }
    for (index = 0U; index < function->value_count; ++index) {
        if (function->values[index].live &&
            function->values[index].opcode == MIGA80_VALUE_DIV_FIX) {
            register_count = MIGA80_FIX_DIV_REGISTER_COUNT;
            break;
        }
        if (function->values[index].live &&
            function->values[index].opcode == MIGA80_VALUE_MUL_FIX) {
            register_count = MIGA80_FIX_REGISTER_COUNT;
        }
    }
    workspace = (struct optimizer_workspace *)malloc(sizeof(*workspace));
    if (workspace == NULL) {
        return fail(diagnostic, 0U, 0U,
                    "unable to allocate O1 optimizer workspace");
    }
    plan = &workspace->plan;
    liveness = &workspace->liveness;
    if (!build_cfg_liveness(function, liveness, diagnostic) ||
        !build_allocation_plan(function, liveness, register_count, plan,
                               diagnostic)) {
        free(workspace);
        return 0;
    }
    if (plan->spill_slot_count != 0U &&
        !build_allocation_plan(function, liveness,
                               register_count < MIGA80_SPILL_REGISTER_COUNT
                                   ? register_count
                                   : MIGA80_SPILL_REGISTER_COUNT,
                               plan, diagnostic)) {
        free(workspace);
        return 0;
    }
    if (register_count == MIGA80_FIX_DIV_REGISTER_COUNT) {
        plan->saved_registers[MIGA80_FIX_DIVISOR_SCRATCH_REGISTER] = 1;
        plan->saved_registers[MIGA80_SPILL_SCRATCH_REGISTER] = 1;
        for (index = 0U; index < function->value_count; ++index) {
            if (function->values[index].live &&
                function->values[index].opcode == MIGA80_VALUE_DIV_FIX &&
                plan->registers[index] == MIGA80_NO_REGISTER) {
                plan->saved_registers[MIGA80_FIX_DIV_LOW_SCRATCH_REGISTER] =
                    1;
                break;
            }
        }
    } else if (register_count == MIGA80_FIX_REGISTER_COUNT) {
        plan->saved_registers[MIGA80_SPILL_SCRATCH_REGISTER] = 1;
        for (index = 0U; index < function->value_count; ++index) {
            if (function->values[index].live &&
                function->values[index].opcode == MIGA80_VALUE_MUL_FIX &&
                plan->registers[index] == MIGA80_NO_REGISTER) {
                plan->saved_registers[MIGA80_FIX_LOW_SCRATCH_REGISTER] = 1;
                break;
            }
        }
    }
    emitted = emit_allocated_function(output, function, plan, diagnostic);
    free(workspace);
    return emitted;
}
