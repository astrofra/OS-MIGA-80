#include "compiler/backend_m68k/encoder.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/abi/abi.h"
#include "compiler/backend_m68k/optimized_internal.h"

struct branch_fixup {
    size_t displacement_offset;
    unsigned int target_block;
};

struct encoder {
    uint8_t *bytes;
    size_t capacity;
    size_t size;
    size_t block_offsets[MIGA80_MAX_BASIC_BLOCKS];
    struct branch_fixup fixups[MIGA80_MAX_IR_INSTRUCTIONS];
    unsigned int fixup_count;
    struct miga80_diagnostic *diagnostic;
};

static int fail(struct encoder *encoder,
                const struct miga80_ir_instruction *instruction,
                const char *message)
{
    encoder->diagnostic->line = instruction == NULL ? 0U : instruction->line;
    encoder->diagnostic->column =
        instruction == NULL ? 0U : instruction->column;
    (void)snprintf(encoder->diagnostic->message,
                   sizeof(encoder->diagnostic->message), "%s", message);
    return 0;
}

static int emit_u16(struct encoder *encoder, uint16_t value)
{
    if (encoder->size > encoder->capacity ||
        encoder->capacity - encoder->size < 2U) {
        return fail(encoder, NULL, "direct encoder output buffer is full");
    }
    encoder->bytes[encoder->size++] = (uint8_t)(value >> 8);
    encoder->bytes[encoder->size++] = (uint8_t)value;
    return 1;
}

static int emit_u32(struct encoder *encoder, uint32_t value)
{
    return emit_u16(encoder, (uint16_t)(value >> 16)) &&
           emit_u16(encoder, (uint16_t)value);
}

static int emit_word_long(struct encoder *encoder, uint16_t opcode,
                          uint32_t value)
{
    return emit_u16(encoder, opcode) && emit_u32(encoder, value);
}

static int emit_branch(struct encoder *encoder, uint16_t opcode,
                       unsigned int target_block)
{
    if (encoder->fixup_count == MIGA80_MAX_IR_INSTRUCTIONS ||
        target_block >= MIGA80_MAX_BASIC_BLOCKS ||
        !emit_u16(encoder, opcode)) {
        return fail(encoder, NULL, "direct encoder branch limit exceeded");
    }
    encoder->fixups[encoder->fixup_count].displacement_offset = encoder->size;
    encoder->fixups[encoder->fixup_count].target_block = target_block;
    ++encoder->fixup_count;
    return emit_u16(encoder, 0U);
}

static int emit_normalize(struct encoder *encoder, enum miga80_type type)
{
    if (type == MIGA80_TYPE_I8) {
        return emit_u16(encoder, UINT16_C(0x49c0)); /* extb.l d0 */
    }
    if (type == MIGA80_TYPE_U8) {
        return emit_word_long(encoder, UINT16_C(0x0280), UINT32_C(0xff));
    }
    if (type == MIGA80_TYPE_I16) {
        return emit_u16(encoder, UINT16_C(0x48c0)); /* ext.l d0 */
    }
    if (type == MIGA80_TYPE_U16) {
        return emit_word_long(encoder, UINT16_C(0x0280),
                              UINT32_C(0xffff));
    }
    return type == MIGA80_TYPE_I32 || type == MIGA80_TYPE_FIX;
}

static uint16_t comparison_opcode(enum miga80_ir_opcode opcode)
{
    switch (opcode) {
    case MIGA80_IR_EQ_I32:
    case MIGA80_IR_EQ_BOOL:
        return UINT16_C(0x57c0); /* seq d0 */
    case MIGA80_IR_NE_I32:
    case MIGA80_IR_NE_BOOL:
        return UINT16_C(0x56c0); /* sne d0 */
    case MIGA80_IR_LT_I32:
        return UINT16_C(0x5dc0); /* slt d0 */
    case MIGA80_IR_LE_I32:
        return UINT16_C(0x5fc0); /* sle d0 */
    case MIGA80_IR_GT_I32:
        return UINT16_C(0x5ec0); /* sgt d0 */
    case MIGA80_IR_GE_I32:
        return UINT16_C(0x5cc0); /* sge d0 */
    case MIGA80_IR_LT_U32:
        return UINT16_C(0x55c0); /* scs d0 */
    case MIGA80_IR_LE_U32:
        return UINT16_C(0x53c0); /* sls d0 */
    case MIGA80_IR_GT_U32:
        return UINT16_C(0x52c0); /* shi d0 */
    case MIGA80_IR_GE_U32:
        return UINT16_C(0x54c0); /* scc d0 */
    default:
        return 0U;
    }
}

static int emit_binary(struct encoder *encoder,
                       const struct miga80_ir_instruction *instruction)
{
    const uint16_t set_condition = comparison_opcode(instruction->opcode);

    if (!emit_u16(encoder, UINT16_C(0x221f)) || /* move.l (sp)+,d1 */
        !emit_u16(encoder, UINT16_C(0x201f))) {  /* move.l (sp)+,d0 */
        return 0;
    }
    if (instruction->opcode == MIGA80_IR_ADD_I32) {
        if (!emit_u16(encoder, UINT16_C(0xd081))) {
            return 0;
        }
    } else if (instruction->opcode == MIGA80_IR_SUB_I32) {
        if (!emit_u16(encoder, UINT16_C(0x9081))) {
            return 0;
        }
    } else if (instruction->opcode == MIGA80_IR_MUL_I32) {
        if (!emit_u16(encoder, UINT16_C(0x4c01)) ||
            !emit_u16(encoder, UINT16_C(0x0800))) {
            return 0;
        }
    } else if (instruction->opcode == MIGA80_IR_MUL_FIX) {
        if (!emit_u16(encoder, UINT16_C(0x4c01)) ||
            !emit_u16(encoder, UINT16_C(0x0c02)) ||
            !emit_u16(encoder, UINT16_C(0x3002)) || /* move.w d2,d0 */
            !emit_u16(encoder, UINT16_C(0x4840))) { /* swap d0 */
            return 0;
        }
    } else if (set_condition != 0U) {
        if (!emit_u16(encoder, UINT16_C(0xb081)) || /* cmp.l d1,d0 */
            !emit_u16(encoder, set_condition) ||
            !emit_word_long(encoder, UINT16_C(0x0280), 1U)) {
            return 0;
        }
    } else {
        return fail(encoder, instruction,
                    "instruction is outside the bootstrap encoder subset");
    }
    if (set_condition == 0U &&
        !emit_normalize(encoder, instruction->type)) {
        return fail(encoder, instruction,
                    "direct encoder cannot normalize arithmetic type");
    }
    return emit_u16(encoder, UINT16_C(0x2f00)); /* move.l d0,-(sp) */
}

static int emit_instruction(
    struct encoder *encoder, const struct miga80_ir_function *function,
    const struct miga80_ir_instruction *instruction)
{
    const unsigned int frame_index =
        function->parameter_count + instruction->operand;
    const uint16_t frame_offset = (uint16_t)(0U - ((frame_index + 1U) * 4U));

    switch (instruction->opcode) {
    case MIGA80_IR_PUSH_I32:
    case MIGA80_IR_PUSH_FIX:
    case MIGA80_IR_PUSH_BOOL:
        return emit_word_long(encoder, UINT16_C(0x2f3c),
                              instruction->operand);
    case MIGA80_IR_PUSH_PARAMETER_I32:
    case MIGA80_IR_PUSH_PARAMETER_BOOL:
        return emit_u16(encoder, UINT16_C(0x2f2e)) &&
               emit_u16(encoder,
                        (uint16_t)(0U - ((instruction->operand + 1U) * 4U)));
    case MIGA80_IR_PUSH_LOCAL_I32:
    case MIGA80_IR_PUSH_LOCAL_BOOL:
        return emit_u16(encoder, UINT16_C(0x2f2e)) &&
               emit_u16(encoder, frame_offset);
    case MIGA80_IR_STORE_LOCAL_I32:
    case MIGA80_IR_STORE_LOCAL_BOOL:
        return emit_u16(encoder, UINT16_C(0x201f)) &&
               emit_u16(encoder, UINT16_C(0x2d40)) &&
               emit_u16(encoder, frame_offset);
    case MIGA80_IR_NEG_I32:
        return emit_u16(encoder, UINT16_C(0x201f)) &&
               emit_u16(encoder, UINT16_C(0x4480)) &&
               emit_normalize(encoder, instruction->type) &&
               emit_u16(encoder, UINT16_C(0x2f00));
    case MIGA80_IR_NORMALIZE_INTEGER:
        return emit_u16(encoder, UINT16_C(0x201f)) &&
               emit_normalize(encoder, instruction->type) &&
               emit_u16(encoder, UINT16_C(0x2f00));
    case MIGA80_IR_ADD_I32:
    case MIGA80_IR_SUB_I32:
    case MIGA80_IR_MUL_I32:
    case MIGA80_IR_MUL_FIX:
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
    case MIGA80_IR_GE_U32:
        return emit_binary(encoder, instruction);
    case MIGA80_IR_CALL_LAYER:
        return emit_u16(encoder, UINT16_C(0x201f)) &&
               emit_u16(encoder, UINT16_C(0x206d)) &&
               emit_u16(encoder, MIGA80_ABI_RUNTIME_LAYER_HANDLER_OFFSET) &&
               emit_u16(encoder, UINT16_C(0x4e90));
    case MIGA80_IR_CALL_LINE:
        /* Keep all five evaluated arguments on stack across the setup call. */
        return emit_u16(encoder, UINT16_C(0x202f)) && emit_u16(encoder, 16U) &&
               emit_u16(encoder, UINT16_C(0x222f)) && emit_u16(encoder, 12U) &&
               emit_u16(encoder, UINT16_C(0x2417)) &&
               emit_u16(encoder, UINT16_C(0x206d)) &&
               emit_u16(encoder, MIGA80_ABI_RUNTIME_LINE_START_HANDLER_OFFSET) &&
               emit_u16(encoder, UINT16_C(0x4e90)) &&
               emit_u16(encoder, UINT16_C(0x202f)) && emit_u16(encoder, 8U) &&
               emit_u16(encoder, UINT16_C(0x222f)) && emit_u16(encoder, 4U) &&
               emit_u16(encoder, UINT16_C(0x206d)) &&
               emit_u16(encoder, MIGA80_ABI_RUNTIME_LINE_END_HANDLER_OFFSET) &&
               emit_u16(encoder, UINT16_C(0x4e90)) &&
               emit_u16(encoder, UINT16_C(0x4fef)) && emit_u16(encoder, 20U);
    case MIGA80_IR_CALL_PSET:
        return emit_u16(encoder, UINT16_C(0x241f)) && /* color -> d2 */
               emit_u16(encoder, UINT16_C(0x221f)) && /* y -> d1 */
               emit_u16(encoder, UINT16_C(0x201f)) && /* x -> d0 */
               emit_u16(encoder, UINT16_C(0x206d)) &&
               emit_u16(encoder,
                        MIGA80_ABI_RUNTIME_PSET_HANDLER_OFFSET) &&
               emit_u16(encoder, UINT16_C(0x4e90)); /* jsr (a0) */
    case MIGA80_IR_BRANCH_FALSE:
        return emit_u16(encoder, UINT16_C(0x201f)) &&
               emit_u16(encoder, UINT16_C(0x4a80)) &&
               emit_branch(encoder, UINT16_C(0x6700),
                           instruction->operand);
    case MIGA80_IR_JUMP:
        return emit_branch(encoder, UINT16_C(0x6000),
                           instruction->operand);
    case MIGA80_IR_RETURN:
        if (instruction->type != MIGA80_TYPE_VOID &&
            !emit_u16(encoder, UINT16_C(0x201f))) {
            return 0;
        }
        return emit_u16(encoder, UINT16_C(0x4e5e)) &&
               emit_u16(encoder, UINT16_C(0x4e75));
    default:
        return fail(encoder, instruction,
                    "instruction is outside the bootstrap encoder subset");
    }
}

static int emit_parameter_copies(
    struct encoder *encoder, const struct miga80_ir_function *function)
{
    unsigned int parameter;

    for (parameter = 0U; parameter < function->parameter_count; ++parameter) {
        if (miga80_type_is_address(function->parameter_types[parameter]) ||
            parameter >= MIGA80_ABI_MAX_SCALAR_ARGUMENTS) {
            return fail(encoder, NULL,
                        "bootstrap encoder supports scalar parameters only");
        }
        if (!emit_u16(encoder, (uint16_t)(UINT16_C(0x2d40) + parameter)) ||
            !emit_u16(
                encoder,
                (uint16_t)(0U - ((parameter + 1U) * 4U)))) {
            return 0;
        }
    }
    return 1;
}

static int patch_branches(struct encoder *encoder,
                          unsigned int block_count)
{
    unsigned int index;

    for (index = 0U; index < encoder->fixup_count; ++index) {
        const struct branch_fixup *fixup = &encoder->fixups[index];
        int64_t displacement;

        if (fixup->target_block >= block_count ||
            encoder->block_offsets[fixup->target_block] == SIZE_MAX) {
            return fail(encoder, NULL,
                        "direct encoder branch target is missing");
        }
        displacement =
            (int64_t)encoder->block_offsets[fixup->target_block] -
            (int64_t)fixup->displacement_offset;
        if (displacement < INT16_MIN || displacement > INT16_MAX) {
            return fail(encoder, NULL,
                        "direct encoder branch exceeds signed word range");
        }
        encoder->bytes[fixup->displacement_offset] =
            (uint8_t)((uint16_t)displacement >> 8);
        encoder->bytes[fixup->displacement_offset + 1U] =
            (uint8_t)displacement;
    }
    return 1;
}

int miga80_encode_m68k_o0(uint8_t *bytes, size_t capacity,
                          const struct miga80_ir_function *function,
                          size_t *encoded_size,
                          struct miga80_diagnostic *diagnostic)
{
    struct encoder *encoder = NULL;
    unsigned int instruction_index;
    unsigned int block;
    const unsigned int frame_size =
        function == NULL
            ? 0U
            : (function->parameter_count + function->local_count) * 4U;
    int success = 0;

    if (bytes == NULL || function == NULL || encoded_size == NULL ||
        diagnostic == NULL || capacity == 0U) {
        return 0;
    }
    (void)memset(diagnostic, 0, sizeof(*diagnostic));
    if (!miga80_validate_ir(function, diagnostic)) {
        return 0;
    }
    if (!miga80_abi_frame_size_is_valid(frame_size)) {
        diagnostic->line = 0U;
        diagnostic->column = 0U;
        (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                       "direct encoder frame violates native ABI");
        return 0;
    }
    encoder = (struct encoder *)malloc(sizeof(*encoder));
    if (encoder == NULL) {
        (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                       "unable to allocate direct encoder workspace");
        return 0;
    }
    (void)memset(encoder, 0, sizeof(*encoder));
    encoder->bytes = bytes;
    encoder->capacity = capacity;
    encoder->diagnostic = diagnostic;
    for (block = 0U; block < MIGA80_MAX_BASIC_BLOCKS; ++block) {
        encoder->block_offsets[block] = SIZE_MAX;
    }

    if (!emit_u16(encoder, UINT16_C(0x4e56)) ||
        !emit_u16(encoder, (uint16_t)(0U - frame_size)) ||
        !emit_parameter_copies(encoder, function)) {
        goto done;
    }
    for (instruction_index = 0U;
         instruction_index < function->instruction_count;
         ++instruction_index) {
        const struct miga80_ir_instruction *instruction =
            &function->instructions[instruction_index];

        for (block = 0U; block < function->block_count; ++block) {
            if (function->blocks[block].first_instruction ==
                instruction_index) {
                encoder->block_offsets[block] = encoder->size;
            }
        }
        if (!emit_instruction(encoder, function, instruction)) {
            if (diagnostic->message[0] == '\0') {
                (void)fail(encoder, instruction,
                           "unable to emit direct 68020 instruction");
            }
            goto done;
        }
    }
    if (!patch_branches(encoder, function->block_count)) {
        goto done;
    }
    *encoded_size = encoder->size;
    success = 1;

done:
    free(encoder);
    return success;
}

static int o1_fail(struct encoder *encoder,
                   const struct miga80_value_instruction *value,
                   const char *message)
{
    encoder->diagnostic->line = value == NULL ? 0U : value->line;
    encoder->diagnostic->column = value == NULL ? 0U : value->column;
    (void)snprintf(encoder->diagnostic->message,
                   sizeof(encoder->diagnostic->message), "%s", message);
    return 0;
}

static unsigned int o1_spill_offset(const struct allocation_plan *plan,
                                    unsigned int source)
{
    return (plan->spill_slots[source] + 1U) * 4U;
}

static uint16_t o1_frame_displacement(unsigned int offset)
{
    return (uint16_t)(0U - offset);
}

static int o1_emit_move(struct encoder *encoder,
                        const struct miga80_value_function *function,
                        const struct allocation_plan *plan,
                        unsigned int source, int destination)
{
    const struct miga80_value_instruction *value = &function->values[source];

    if (value->opcode == MIGA80_VALUE_CONSTANT) {
        const uint32_t constant = value->immediate;

        if (value->type == MIGA80_TYPE_STRING) {
            return o1_fail(encoder, value,
                           "direct O1 string addresses are not implemented");
        }
        if (constant <= UINT32_C(127) ||
            constant >= UINT32_C(0xffffff80)) {
            return emit_u16(
                encoder,
                (uint16_t)(UINT16_C(0x7000) |
                           ((unsigned int)destination << 9) |
                           (constant & UINT32_C(0xff))));
        }
        return emit_word_long(
            encoder,
            (uint16_t)(UINT16_C(0x203c) |
                       ((unsigned int)destination << 9)),
            constant);
    }
    if (plan->registers[source] == destination) {
        return 1;
    }
    if (plan->registers[source] != MIGA80_NO_REGISTER) {
        return emit_u16(
            encoder,
            (uint16_t)(UINT16_C(0x2000) |
                       ((unsigned int)destination << 9) |
                       (unsigned int)plan->registers[source]));
    }
    if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        return emit_u16(
                   encoder,
                   (uint16_t)(UINT16_C(0x202e) |
                              ((unsigned int)destination << 9))) &&
               emit_u16(encoder, o1_frame_displacement(
                                     o1_spill_offset(plan, source)));
    }
    return o1_fail(encoder, value, "direct O1 source has no location");
}

static int o1_emit_register_source(
    struct encoder *encoder, uint16_t opcode,
    const struct allocation_plan *plan, unsigned int source,
    int destination)
{
    if (plan->registers[source] != MIGA80_NO_REGISTER) {
        return emit_u16(
            encoder,
            (uint16_t)(opcode | ((unsigned int)destination << 9) |
                       (unsigned int)plan->registers[source]));
    }
    if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        return emit_u16(
                   encoder,
                   (uint16_t)(opcode | ((unsigned int)destination << 9) |
                              UINT16_C(0x002e))) &&
               emit_u16(encoder, o1_frame_displacement(
                                     o1_spill_offset(plan, source)));
    }
    return o1_fail(encoder, NULL, "direct O1 operand has no location");
}

static int o1_emit_normalize(struct encoder *encoder, enum miga80_type type,
                             int reg)
{
    if (type == MIGA80_TYPE_I8) {
        return emit_u16(encoder,
                        (uint16_t)(UINT16_C(0x49c0) | (unsigned int)reg));
    }
    if (type == MIGA80_TYPE_U8) {
        return emit_word_long(
            encoder, (uint16_t)(UINT16_C(0x0280) | (unsigned int)reg),
            UINT32_C(0xff));
    }
    if (type == MIGA80_TYPE_I16) {
        return emit_u16(encoder,
                        (uint16_t)(UINT16_C(0x48c0) | (unsigned int)reg));
    }
    if (type == MIGA80_TYPE_U16) {
        return emit_word_long(
            encoder, (uint16_t)(UINT16_C(0x0280) | (unsigned int)reg),
            UINT32_C(0xffff));
    }
    return type == MIGA80_TYPE_I32 || type == MIGA80_TYPE_FIX;
}

static int o1_store_spilled_result(struct encoder *encoder,
                                   const struct allocation_plan *plan,
                                   unsigned int index, int source)
{
    if (plan->spill_slots[index] == MIGA80_NO_SPILL_SLOT) {
        return 1;
    }
    return emit_u16(encoder,
                    (uint16_t)(UINT16_C(0x2d40) |
                               (unsigned int)source)) &&
           emit_u16(encoder, o1_frame_displacement(
                                 o1_spill_offset(plan, index)));
}

static unsigned int o1_power_of_two_shift(uint32_t constant)
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

static int o1_emit_add_immediate(struct encoder *encoder,
                                 uint32_t constant, int destination)
{
    if (constant >= 1U && constant <= 8U) {
        return emit_u16(
            encoder,
            (uint16_t)(UINT16_C(0x5080) |
                       ((constant & 7U) << 9) |
                       (unsigned int)destination));
    }
    if (constant >= UINT32_C(0xfffffff8)) {
        const uint32_t magnitude = 0U - constant;

        return emit_u16(
            encoder,
            (uint16_t)(UINT16_C(0x5180) |
                       ((magnitude & 7U) << 9) |
                       (unsigned int)destination));
    }
    return emit_word_long(
        encoder,
        (uint16_t)(UINT16_C(0x0680) | (unsigned int)destination),
        constant);
}

static int o1_emit_sub_immediate(struct encoder *encoder,
                                 uint32_t constant, int destination)
{
    if (constant >= 1U && constant <= 8U) {
        return emit_u16(
            encoder,
            (uint16_t)(UINT16_C(0x5180) |
                       ((constant & 7U) << 9) |
                       (unsigned int)destination));
    }
    if (constant >= UINT32_C(0xfffffff8)) {
        const uint32_t magnitude = 0U - constant;

        return emit_u16(
            encoder,
            (uint16_t)(UINT16_C(0x5080) |
                       ((magnitude & 7U) << 9) |
                       (unsigned int)destination));
    }
    return emit_word_long(
        encoder,
        (uint16_t)(UINT16_C(0x0480) | (unsigned int)destination),
        constant);
}

static int o1_emit_muls(struct encoder *encoder,
                        const struct miga80_value_function *function,
                        const struct allocation_plan *plan,
                        unsigned int source, int destination,
                        int wide)
{
    const struct miga80_value_instruction *operand = &function->values[source];
    const uint16_t extension =
        (uint16_t)(UINT16_C(0x0800) |
                   ((unsigned int)destination << 12) |
                   (wide ? UINT16_C(0x0407) : 0U));

    if (operand->opcode == MIGA80_VALUE_CONSTANT) {
        return emit_u16(encoder, UINT16_C(0x4c3c)) &&
               emit_u16(encoder, extension) &&
               emit_u32(encoder, operand->immediate);
    }
    if (plan->registers[source] != MIGA80_NO_REGISTER) {
        return emit_u16(
                   encoder,
                   (uint16_t)(UINT16_C(0x4c00) |
                              (unsigned int)plan->registers[source])) &&
               emit_u16(encoder, extension);
    }
    if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        return emit_u16(encoder, UINT16_C(0x4c2e)) &&
               emit_u16(encoder, extension) &&
               emit_u16(encoder, o1_frame_displacement(
                                     o1_spill_offset(plan, source)));
    }
    return o1_fail(encoder, operand, "direct O1 multiply has no source");
}

static int o1_opcode_is_commutative(enum miga80_value_opcode opcode)
{
    return opcode == MIGA80_VALUE_ADD || opcode == MIGA80_VALUE_MUL ||
           opcode == MIGA80_VALUE_MUL_FIX ||
           opcode == MIGA80_VALUE_EQ || opcode == MIGA80_VALUE_NE;
}

static uint16_t o1_condition_opcode(enum miga80_value_opcode opcode,
                                    int destination)
{
    uint16_t base = UINT16_C(0x57c0);

    if (opcode == MIGA80_VALUE_NE) {
        base = UINT16_C(0x56c0);
    } else if (opcode == MIGA80_VALUE_LT_I32) {
        base = UINT16_C(0x5dc0);
    } else if (opcode == MIGA80_VALUE_LE_I32) {
        base = UINT16_C(0x5fc0);
    } else if (opcode == MIGA80_VALUE_GT_I32) {
        base = UINT16_C(0x5ec0);
    } else if (opcode == MIGA80_VALUE_GE_I32) {
        base = UINT16_C(0x5cc0);
    } else if (opcode == MIGA80_VALUE_LT_U32) {
        base = UINT16_C(0x55c0);
    } else if (opcode == MIGA80_VALUE_LE_U32) {
        base = UINT16_C(0x53c0);
    } else if (opcode == MIGA80_VALUE_GT_U32) {
        base = UINT16_C(0x52c0);
    } else if (opcode == MIGA80_VALUE_GE_U32) {
        base = UINT16_C(0x54c0);
    }
    return (uint16_t)(base | (unsigned int)destination);
}

static int o1_emit_comparison(
    struct encoder *encoder, const struct miga80_value_function *function,
    const struct allocation_plan *plan,
    const struct miga80_value_instruction *value,
    unsigned int source, int destination)
{
    const struct miga80_value_instruction *operand = &function->values[source];
    int compared;

    if (operand->opcode == MIGA80_VALUE_CONSTANT) {
        if (operand->type == MIGA80_TYPE_STRING) {
            return o1_fail(encoder, value,
                           "direct O1 string comparison is not implemented");
        }
        compared = emit_word_long(
            encoder,
            (uint16_t)(UINT16_C(0x0c80) | (unsigned int)destination),
            operand->immediate);
    } else {
        compared = o1_emit_register_source(
            encoder, UINT16_C(0xb080), plan, source, destination);
    }
    return compared &&
           emit_u16(encoder, o1_condition_opcode(value->opcode,
                                                  destination)) &&
           emit_word_long(
               encoder,
               (uint16_t)(UINT16_C(0x0280) |
                          (unsigned int)destination),
               1U);
}

static int o1_emit_call_argument(
    struct encoder *encoder, const struct miga80_value_function *function,
    const struct allocation_plan *plan, unsigned int source)
{
    const struct miga80_value_instruction *value = &function->values[source];

    if (value->opcode == MIGA80_VALUE_CONSTANT) {
        return emit_word_long(encoder, UINT16_C(0x2f3c),
                              value->immediate);
    }
    if (plan->registers[source] != MIGA80_NO_REGISTER) {
        return emit_u16(
            encoder,
            (uint16_t)(UINT16_C(0x2f00) |
                       (unsigned int)plan->registers[source]));
    }
    if (plan->spill_slots[source] != MIGA80_NO_SPILL_SLOT) {
        return emit_u16(encoder, UINT16_C(0x2f2e)) &&
               emit_u16(encoder, o1_frame_displacement(
                                     o1_spill_offset(plan, source)));
    }
    return o1_fail(encoder, value,
                   "direct O1 call argument has no location");
}

static int o1_emit_runtime_call(struct encoder *encoder,
                               const struct miga80_value_function *function,
                               const struct allocation_plan *plan,
                               const struct miga80_value_instruction *value)
{
    const unsigned int arguments[3] = {value->left, value->right, value->third};
    const unsigned int count = miga80_value_call_arguments(value->opcode);
    unsigned int index;

    for (index = 0U; index < count; ++index) {
        if (!o1_emit_call_argument(encoder, function, plan, arguments[index])) {
            return 0;
        }
    }
    for (index = count; index > 0U; --index) {
        if (!emit_u16(encoder, (uint16_t)(0x201fU | ((index - 1U) << 9)))) {
            return 0;
        }
    }
    return emit_u16(encoder, UINT16_C(0x206d)) &&
           emit_u16(encoder, (uint16_t)miga80_value_call_offset(value->opcode)) &&
           emit_u16(encoder, UINT16_C(0x4e90));
}

static int o1_emit_value(struct encoder *encoder,
                         const struct miga80_value_function *function,
                         const struct allocation_plan *plan,
                         unsigned int index)
{
    const struct miga80_value_instruction *value = &function->values[index];
    const int destination = plan->registers[index] != MIGA80_NO_REGISTER
                                ? plan->registers[index]
                                : 7;
    unsigned int source = value->right;
    int emitted;

    if (miga80_value_call_arguments(value->opcode) != 0U) {
        return o1_emit_runtime_call(encoder, function, plan, value);
    }
    if (value->opcode == MIGA80_VALUE_NEG) {
        emitted = o1_emit_move(encoder, function, plan, value->left,
                               destination) &&
                  emit_u16(encoder,
                           (uint16_t)(UINT16_C(0x4480) |
                                      (unsigned int)destination)) &&
                  o1_emit_normalize(encoder, value->type, destination);
        return emitted &&
               o1_store_spilled_result(encoder, plan, index, destination);
    }
    if (value->opcode == MIGA80_VALUE_NORMALIZE_INTEGER) {
        emitted = o1_emit_move(encoder, function, plan, value->left,
                               destination) &&
                  o1_emit_normalize(encoder, value->type, destination);
        return emitted &&
               o1_store_spilled_result(encoder, plan, index, destination);
    }
    if (value->opcode == MIGA80_VALUE_MUL_FIX) {
        const int fix_destination =
            plan->registers[index] != MIGA80_NO_REGISTER
                ? plan->registers[index]
                : 6;

        if (plan->registers[value->right] == fix_destination &&
            function->values[value->right].opcode !=
                MIGA80_VALUE_CONSTANT) {
            source = value->left;
        } else if (!o1_emit_move(encoder, function, plan, value->left,
                                 fix_destination)) {
            return 0;
        }
        emitted = o1_emit_muls(encoder, function, plan, source,
                               fix_destination, 1) &&
                  emit_u16(
                      encoder,
                      (uint16_t)(UINT16_C(0x3007) |
                                 ((unsigned int)fix_destination << 9))) &&
                  emit_u16(
                      encoder,
                      (uint16_t)(UINT16_C(0x4840) |
                                 (unsigned int)fix_destination));
        return emitted &&
               o1_store_spilled_result(encoder, plan, index,
                                       fix_destination);
    }
    if (value->opcode == MIGA80_VALUE_DIV_FIX ||
        value->opcode == MIGA80_VALUE_DIV ||
        value->opcode == MIGA80_VALUE_DIV_U ||
        value->opcode == MIGA80_VALUE_FIX_FROM_I32 ||
        value->opcode == MIGA80_VALUE_I32_FROM_FIX) {
        return o1_fail(encoder, value,
                       "instruction is outside the direct O1 subset");
    }

    if (o1_opcode_is_commutative(value->opcode) &&
        plan->registers[value->right] == destination &&
        function->values[value->right].opcode != MIGA80_VALUE_CONSTANT) {
        source = value->left;
    } else if (!o1_emit_move(encoder, function, plan, value->left,
                             destination)) {
        return 0;
    }
    if ((value->opcode >= MIGA80_VALUE_EQ &&
         value->opcode <= MIGA80_VALUE_GE_I32) ||
        (value->opcode >= MIGA80_VALUE_LT_U32 &&
         value->opcode <= MIGA80_VALUE_GE_U32)) {
        emitted = o1_emit_comparison(encoder, function, plan, value,
                                     source, destination);
        return emitted &&
               o1_store_spilled_result(encoder, plan, index, destination);
    }
    if (function->values[source].opcode == MIGA80_VALUE_CONSTANT) {
        if (value->opcode == MIGA80_VALUE_ADD) {
            emitted = o1_emit_add_immediate(
                          encoder, function->values[source].immediate,
                          destination) &&
                      o1_emit_normalize(encoder, value->type, destination);
            return emitted && o1_store_spilled_result(
                                  encoder, plan, index, destination);
        }
        if (value->opcode == MIGA80_VALUE_SUB) {
            emitted = o1_emit_sub_immediate(
                          encoder, function->values[source].immediate,
                          destination) &&
                      o1_emit_normalize(encoder, value->type, destination);
            return emitted && o1_store_spilled_result(
                                  encoder, plan, index, destination);
        }
    }
    if (value->opcode == MIGA80_VALUE_ADD) {
        emitted = o1_emit_register_source(
            encoder, UINT16_C(0xd080), plan, source, destination);
    } else if (value->opcode == MIGA80_VALUE_SUB) {
        emitted = o1_emit_register_source(
            encoder, UINT16_C(0x9080), plan, source, destination);
    } else if (value->opcode == MIGA80_VALUE_MUL) {
        const struct miga80_value_instruction *operand =
            &function->values[source];
        const unsigned int shift =
            operand->opcode == MIGA80_VALUE_CONSTANT
                ? o1_power_of_two_shift(operand->immediate)
                : 0U;

        if (operand->opcode == MIGA80_VALUE_CONSTANT &&
            operand->immediate == 2U) {
            emitted = emit_u16(
                encoder,
                (uint16_t)(UINT16_C(0xd080) |
                           ((unsigned int)destination << 9) |
                           (unsigned int)destination));
        } else if (operand->opcode == MIGA80_VALUE_CONSTANT &&
                   operand->immediate == 3U &&
                   destination != plan->registers[value->left]) {
            emitted = emit_u16(
                          encoder,
                          (uint16_t)(UINT16_C(0xd080) |
                                     ((unsigned int)destination << 9) |
                                     (unsigned int)destination)) &&
                      o1_emit_register_source(
                          encoder, UINT16_C(0xd080), plan, value->left,
                          destination);
        } else if (shift >= 1U && shift <= 8U) {
            emitted = emit_u16(
                encoder,
                (uint16_t)(UINT16_C(0xe188) |
                           ((shift & 7U) << 9) |
                           (unsigned int)destination));
        } else {
            emitted = o1_emit_muls(encoder, function, plan, source,
                                   destination, 0);
        }
    } else {
        return o1_fail(encoder, value,
                       "instruction is outside the direct O1 subset");
    }
    emitted = emitted &&
              o1_emit_normalize(encoder, value->type, destination);
    return emitted &&
           o1_store_spilled_result(encoder, plan, index, destination);
}

struct o1_phi_copy {
    unsigned int phi_index;
    unsigned int source;
    unsigned int source_slot;
    int pending;
};

static int o1_phi_source_for_edge(
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

static int o1_emit_phi_copy(struct encoder *encoder,
                            const struct miga80_value_function *function,
                            const struct allocation_plan *plan,
                            const struct o1_phi_copy *copy)
{
    const unsigned int destination = o1_spill_offset(plan, copy->phi_index);

    if (copy->source == MIGA80_INVALID_VALUE) {
        if (plan->phi_temporary_slot == MIGA80_NO_SPILL_SLOT) {
            return o1_fail(encoder, NULL,
                           "direct O1 phi temporary is missing");
        }
        return emit_u16(encoder, UINT16_C(0x2e2e)) &&
               emit_u16(encoder, o1_frame_displacement(
                                     (plan->phi_temporary_slot + 1U) * 4U)) &&
               emit_u16(encoder, UINT16_C(0x2d47)) &&
               emit_u16(encoder, o1_frame_displacement(destination));
    }
    if (function->values[copy->source].opcode == MIGA80_VALUE_CONSTANT) {
        if (function->values[copy->source].type == MIGA80_TYPE_STRING) {
            return o1_fail(encoder, &function->values[copy->source],
                           "direct O1 string phi is not implemented");
        }
        return emit_u16(encoder, UINT16_C(0x2d7c)) &&
               emit_u32(encoder,
                        function->values[copy->source].immediate) &&
               emit_u16(encoder, o1_frame_displacement(destination));
    }
    if (plan->registers[copy->source] != MIGA80_NO_REGISTER) {
        return emit_u16(
                   encoder,
                   (uint16_t)(UINT16_C(0x2d40) |
                              (unsigned int)plan->registers[copy->source])) &&
               emit_u16(encoder, o1_frame_displacement(destination));
    }
    if (plan->spill_slots[copy->source] != MIGA80_NO_SPILL_SLOT) {
        return emit_u16(encoder, UINT16_C(0x2e2e)) &&
               emit_u16(encoder, o1_frame_displacement(
                                     o1_spill_offset(plan, copy->source))) &&
               emit_u16(encoder, UINT16_C(0x2d47)) &&
               emit_u16(encoder, o1_frame_displacement(destination));
    }
    return o1_fail(encoder, NULL, "direct O1 phi source has no location");
}

static int o1_emit_phi_edge(struct encoder *encoder,
                            const struct miga80_value_function *function,
                            const struct allocation_plan *plan,
                            unsigned int predecessor,
                            unsigned int successor)
{
    const struct miga80_value_basic_block *block =
        &function->blocks[successor];
    struct o1_phi_copy copies[MIGA80_MAX_LOCALS];
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
        if (!o1_phi_source_for_edge(phi, predecessor, &source) ||
            plan->spill_slots[phi_index] == MIGA80_NO_SPILL_SLOT) {
            return o1_fail(encoder, phi, "direct O1 phi edge is invalid");
        }
        source_slot = plan->spill_slots[source];
        if (source_slot == plan->spill_slots[phi_index]) {
            continue;
        }
        if (copy_count == MIGA80_MAX_LOCALS) {
            return o1_fail(encoder, phi,
                           "direct O1 phi copy limit exceeded");
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
            if (!o1_emit_phi_copy(encoder, function, plan, &copies[safe])) {
                return 0;
            }
            copies[safe].pending = 0;
            --remaining;
            continue;
        }
        if (plan->phi_temporary_slot == MIGA80_NO_SPILL_SLOT) {
            return o1_fail(encoder, NULL,
                           "direct O1 cyclic phi has no temporary");
        }
        for (copy_index = 0U; copy_index < copy_count; ++copy_index) {
            if (copies[copy_index].pending) {
                unsigned int other;
                const unsigned int destination_slot =
                    plan->spill_slots[copies[copy_index].phi_index];
                int replaced = 0;

                if (!emit_u16(encoder, UINT16_C(0x2e2e)) ||
                    !emit_u16(encoder, o1_frame_displacement(
                                          (destination_slot + 1U) * 4U)) ||
                    !emit_u16(encoder, UINT16_C(0x2d47)) ||
                    !emit_u16(encoder, o1_frame_displacement(
                                          (plan->phi_temporary_slot + 1U) *
                                          4U))) {
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
                    return o1_fail(encoder, NULL,
                                   "direct O1 cyclic phi is invalid");
                }
                break;
            }
        }
    }
    return 1;
}

static int o1_emit_jump_edge(struct encoder *encoder,
                             const struct miga80_value_function *function,
                             const struct allocation_plan *plan,
                             unsigned int predecessor,
                             unsigned int successor, int branch_required)
{
    if (plan->guarded &&
        miga80_m68k_o1_backward_edge(function, predecessor, successor)) {
        const struct miga80_value_basic_block *block =
            &function->blocks[predecessor];

        /* Success changes only CCR. A zero budget faults before decrement,
         * so it cannot wrap to UINT32_MAX and grant another full budget. */
        if (!emit_u16(encoder, UINT16_C(0x4aad)) || /* tst.l budget(a5) */
            !emit_u16(encoder, MIGA80_ABI_RUNTIME_BUDGET_OFFSET) ||
            !emit_u16(encoder, (uint16_t)(UINT16_C(0x6600) |
                (6U + (block->line <= 127U ? 2U : 6U) +
                 (block->column <= 127U ? 2U : 6U)))) ||
            !emit_u16(encoder, UINT16_C(0x7000) |
                                  MIGA80_ABI_FAULT_EXECUTION_BUDGET) ||
            !(block->line <= 127U
                  ? emit_u16(encoder, (uint16_t)(0x7200U | block->line))
                  : emit_word_long(encoder, 0x223cU, block->line)) ||
            !(block->column <= 127U
                  ? emit_u16(encoder, (uint16_t)(0x7400U | block->column))
                  : emit_word_long(encoder, 0x243cU, block->column)) ||
            !emit_u16(encoder, UINT16_C(0x2055)) || /* movea.l (a5),a0 */
            !emit_u16(encoder, UINT16_C(0x4ed0)) || /* jmp (a0) */
            !emit_u16(encoder, UINT16_C(0x53ad)) || /* subq.l #1,budget(a5) */
            !emit_u16(encoder, MIGA80_ABI_RUNTIME_BUDGET_OFFSET)) {
            return 0;
        }
    }
    return o1_emit_phi_edge(encoder, function, plan, predecessor,
                            successor) &&
           (!branch_required ||
            emit_branch(encoder, UINT16_C(0x6000), successor));
}

static int o1_patch_local_branch(struct encoder *encoder,
                                 size_t displacement_offset,
                                 size_t target_offset)
{
    const int64_t displacement =
        (int64_t)target_offset - (int64_t)displacement_offset;

    if (displacement < INT16_MIN || displacement > INT16_MAX) {
        return o1_fail(encoder, NULL,
                       "direct O1 local branch exceeds word range");
    }
    encoder->bytes[displacement_offset] =
        (uint8_t)((uint16_t)displacement >> 8);
    encoder->bytes[displacement_offset + 1U] = (uint8_t)displacement;
    return 1;
}

static int o1_emit_branch(struct encoder *encoder,
                          const struct miga80_value_function *function,
                          const struct allocation_plan *plan,
                          unsigned int block_index)
{
    const struct miga80_value_basic_block *block =
        &function->blocks[block_index];
    const struct miga80_value_instruction *condition =
        &function->values[block->condition];
    size_t false_displacement;

    if (condition->opcode == MIGA80_VALUE_CONSTANT) {
        const unsigned int successor =
            condition->immediate != 0U ? block->successors[0]
                                       : block->successors[1];

        return o1_emit_jump_edge(encoder, function, plan, block_index,
                                 successor, 1);
    }
    if (plan->registers[block->condition] != MIGA80_NO_REGISTER) {
        if (!emit_u16(
                encoder,
                (uint16_t)(UINT16_C(0x4a80) |
                           (unsigned int)plan->registers[block->condition]))) {
            return 0;
        }
    } else if (plan->spill_slots[block->condition] != MIGA80_NO_SPILL_SLOT) {
        if (!emit_u16(encoder, UINT16_C(0x2e2e)) ||
            !emit_u16(encoder, o1_frame_displacement(
                                  o1_spill_offset(plan, block->condition))) ||
            !emit_u16(encoder, UINT16_C(0x4a87))) {
            return 0;
        }
    } else {
        return o1_fail(encoder, condition,
                       "direct O1 branch condition has no location");
    }
    if (!emit_u16(encoder, UINT16_C(0x6700))) {
        return 0;
    }
    false_displacement = encoder->size;
    if (!emit_u16(encoder, 0U) ||
        !o1_emit_jump_edge(encoder, function, plan, block_index,
                           block->successors[0], 1) ||
        !o1_patch_local_branch(encoder, false_displacement, encoder->size)) {
        return 0;
    }
    return o1_emit_jump_edge(encoder, function, plan, block_index,
                             block->successors[1], 1);
}

static unsigned int o1_parameter_class_index(
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

static int o1_emit_parameter_copies(
    struct encoder *encoder, const struct miga80_value_function *function,
    const struct allocation_plan *plan)
{
    unsigned int index;

    for (index = 0U; index < function->value_count; ++index) {
        const struct miga80_value_instruction *value =
            &function->values[index];

        if (value->live && value->opcode == MIGA80_VALUE_PARAMETER) {
            enum miga80_abi_register abi_register;
            const int address = miga80_type_is_address(value->type);
            const unsigned int class_index = o1_parameter_class_index(
                function->parameter_types, value->parameter_index, address);
            uint16_t opcode;

            if (!(address
                      ? miga80_abi_address_argument_register(class_index,
                                                             &abi_register)
                      : miga80_abi_scalar_argument_register(class_index,
                                                            &abi_register)) ||
                plan->registers[index] == MIGA80_NO_REGISTER) {
                return o1_fail(encoder, value,
                               "direct O1 parameter copy is invalid");
            }
            if (!address &&
                plan->registers[index] ==
                    (int)(abi_register - MIGA80_ABI_D0)) {
                continue;
            }
            opcode = (uint16_t)(UINT16_C(0x2000) |
                                ((unsigned int)plan->registers[index] << 9));
            if (address) {
                opcode = (uint16_t)(opcode | UINT16_C(0x0008) |
                                    (unsigned int)(abi_register -
                                                   MIGA80_ABI_A0));
            } else {
                opcode = (uint16_t)(opcode |
                                    (unsigned int)(abi_register -
                                                   MIGA80_ABI_D0));
            }
            if (!emit_u16(encoder, opcode)) {
                return 0;
            }
        }
    }
    return 1;
}

static uint16_t o1_saved_push_mask(const struct allocation_plan *plan)
{
    uint16_t mask = 0U;
    unsigned int reg;

    for (reg = 3U; reg < MIGA80_DATA_REGISTER_COUNT; ++reg) {
        if (plan->saved_registers[reg]) {
            mask |= (uint16_t)(UINT16_C(1) << (15U - reg));
        }
    }
    return mask;
}

static uint16_t o1_saved_pop_mask(const struct allocation_plan *plan)
{
    uint16_t mask = 0U;
    unsigned int reg;

    for (reg = 3U; reg < MIGA80_DATA_REGISTER_COUNT; ++reg) {
        if (plan->saved_registers[reg]) {
            mask |= (uint16_t)(UINT16_C(1) << reg);
        }
    }
    return mask;
}

static int o1_emit_epilogue(struct encoder *encoder,
                            const struct allocation_plan *plan,
                            unsigned int frame_size)
{
    const uint16_t pop_mask = o1_saved_pop_mask(plan);

    if (pop_mask != 0U &&
        (!emit_u16(encoder, UINT16_C(0x4cdf)) ||
         !emit_u16(encoder, pop_mask))) {
        return 0;
    }
    if (frame_size != 0U && !emit_u16(encoder, UINT16_C(0x4e5e))) {
        return 0;
    }
    return emit_u16(encoder, UINT16_C(0x4e75));
}

static int encode_m68k_o1(uint8_t *bytes, size_t capacity,
                          const struct miga80_value_function *function,
                          size_t *encoded_size,
                          size_t *required_stack_bytes, int guarded,
                          struct miga80_diagnostic *diagnostic)
{
    struct encoder *encoder = NULL;
    struct allocation_plan *plan = NULL;
    unsigned int frame_size;
    unsigned int block;
    unsigned int order_index;
    int success = 0;

    if (bytes == NULL || function == NULL || encoded_size == NULL ||
        diagnostic == NULL || capacity == 0U) {
        return 0;
    }
    (void)memset(diagnostic, 0, sizeof(*diagnostic));
    if (!miga80_prepare_m68k_o1_plan(function, &plan, diagnostic)) {
        return 0;
    }
    plan->guarded = guarded;
    encoder = (struct encoder *)malloc(sizeof(*encoder));
    if (encoder == NULL) {
        diagnostic->line = 0U;
        diagnostic->column = 0U;
        (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                       "unable to allocate direct encoder workspace");
        goto done;
    }
    (void)memset(encoder, 0, sizeof(*encoder));
    encoder->bytes = bytes;
    encoder->capacity = capacity;
    encoder->diagnostic = diagnostic;
    for (block = 0U; block < MIGA80_MAX_BASIC_BLOCKS; ++block) {
        encoder->block_offsets[block] = SIZE_MAX;
    }
    frame_size = plan->spill_slot_count * 4U;
    if (!miga80_abi_frame_size_is_valid(frame_size)) {
        (void)o1_fail(encoder, NULL,
                      "direct O1 frame violates native ABI");
        goto done;
    }
    if (frame_size != 0U &&
        (!emit_u16(encoder, UINT16_C(0x4e56)) ||
         !emit_u16(encoder, o1_frame_displacement(frame_size)))) {
        goto done;
    }
    {
        const uint16_t push_mask = o1_saved_push_mask(plan);

        if (push_mask != 0U &&
            (!emit_u16(encoder, UINT16_C(0x48e7)) ||
             !emit_u16(encoder, push_mask))) {
            goto done;
        }
    }
    if (!o1_emit_parameter_copies(encoder, function, plan)) {
        goto done;
    }

    for (order_index = 0U; order_index < function->block_order_count;
         ++order_index) {
        const unsigned int block_index = function->block_order[order_index];
        const struct miga80_value_basic_block *value_block =
            &function->blocks[block_index];
        unsigned int offset;

        encoder->block_offsets[block_index] = encoder->size;
        for (offset = 0U; offset < value_block->value_count; ++offset) {
            const unsigned int index = value_block->first_value + offset;
            const struct miga80_value_instruction *value =
                &function->values[index];

            if (value->live && value->opcode != MIGA80_VALUE_CONSTANT &&
                value->opcode != MIGA80_VALUE_PARAMETER &&
                value->opcode != MIGA80_VALUE_PHI &&
                !o1_emit_value(encoder, function, plan, index)) {
                if (diagnostic->message[0] == '\0') {
                    (void)o1_fail(encoder, value,
                                  "unable to encode direct O1 value");
                }
                goto done;
            }
        }
        if (value_block->terminator == MIGA80_VALUE_BRANCH) {
            if (!o1_emit_branch(encoder, function, plan, block_index)) {
                goto done;
            }
        } else if (value_block->terminator == MIGA80_VALUE_JUMP) {
            const int branch_required =
                order_index + 1U == function->block_order_count ||
                function->block_order[order_index + 1U] !=
                    value_block->successors[0];

            if (!o1_emit_jump_edge(encoder, function, plan, block_index,
                                   value_block->successors[0],
                                   branch_required)) {
                goto done;
            }
        } else {
            if (function->result_type == MIGA80_TYPE_STRING) {
                (void)o1_fail(encoder, NULL,
                              "direct O1 string return is not implemented");
                goto done;
            }
            if (function->result_type != MIGA80_TYPE_VOID &&
                !o1_emit_move(encoder, function, plan, function->result,
                              0)) {
                goto done;
            }
            if (!o1_emit_epilogue(encoder, plan, frame_size)) {
                goto done;
            }
        }
    }
    if (!patch_branches(encoder, function->block_count)) {
        goto done;
    }
    if ((encoder->size & 3U) != 0U && !emit_u16(encoder, 0U)) {
        goto done;
    }
    *encoded_size = encoder->size;
    if (required_stack_bytes != NULL) {
        unsigned int reg;

        /* Conservative transient allowance: return PC, LINK's saved A6,
         * three staged pset arguments and its return PC, plus edge scratch.
         * Ordinary user calls are outside this encoder's accepted subset. */
        *required_stack_bytes = (size_t)frame_size + 64U;
        /* Layer/line services enter the bounded C drawing runtime. Plain
         * pset-only programs retain the original assembly fast-path bound. */
        for (reg = 0U; reg < function->value_count; ++reg) {
            if (function->values[reg].live &&
                miga80_value_call_arguments(function->values[reg].opcode) != 0U &&
                function->values[reg].opcode != MIGA80_VALUE_CALL_PSET) {
                *required_stack_bytes += 1024U;
                break;
            }
        }
        for (reg = 3U; reg < MIGA80_DATA_REGISTER_COUNT; ++reg) {
            if (plan->saved_registers[reg]) {
                *required_stack_bytes += 4U;
            }
        }
    }
    success = 1;

done:
    free(encoder);
    miga80_release_m68k_o1_plan(plan);
    return success;
}

int miga80_encode_m68k_o1(uint8_t *bytes, size_t capacity,
                          const struct miga80_value_function *function,
                          size_t *encoded_size,
                          struct miga80_diagnostic *diagnostic)
{
    return encode_m68k_o1(bytes, capacity, function, encoded_size, NULL, 0,
                           diagnostic);
}

int miga80_encode_m68k_o1_guarded(
    uint8_t *bytes, size_t capacity,
    const struct miga80_value_function *function, size_t *encoded_size,
    size_t *required_stack_bytes, struct miga80_diagnostic *diagnostic)
{
    if (required_stack_bytes == NULL) {
        return 0;
    }
    return encode_m68k_o1(bytes, capacity, function, encoded_size,
                           required_stack_bytes, 1, diagnostic);
}
