#include "compiler/backend_m68k/encoder.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "compiler/abi/abi.h"

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
    struct encoder encoder;
    unsigned int instruction_index;
    unsigned int block;
    const unsigned int frame_size =
        function == NULL
            ? 0U
            : (function->parameter_count + function->local_count) * 4U;

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
    (void)memset(&encoder, 0, sizeof(encoder));
    encoder.bytes = bytes;
    encoder.capacity = capacity;
    encoder.diagnostic = diagnostic;
    for (block = 0U; block < MIGA80_MAX_BASIC_BLOCKS; ++block) {
        encoder.block_offsets[block] = SIZE_MAX;
    }

    if (!emit_u16(&encoder, UINT16_C(0x4e56)) ||
        !emit_u16(&encoder, (uint16_t)(0U - frame_size)) ||
        !emit_parameter_copies(&encoder, function)) {
        return 0;
    }
    for (instruction_index = 0U;
         instruction_index < function->instruction_count;
         ++instruction_index) {
        const struct miga80_ir_instruction *instruction =
            &function->instructions[instruction_index];

        for (block = 0U; block < function->block_count; ++block) {
            if (function->blocks[block].first_instruction ==
                instruction_index) {
                encoder.block_offsets[block] = encoder.size;
            }
        }
        if (!emit_instruction(&encoder, function, instruction)) {
            if (diagnostic->message[0] == '\0') {
                return fail(&encoder, instruction,
                            "unable to emit direct 68020 instruction");
            }
            return 0;
        }
    }
    if (!patch_branches(&encoder, function->block_count)) {
        return 0;
    }
    *encoded_size = encoder.size;
    return 1;
}
