#include "compiler/backend_m68k/backend.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "compiler/abi/abi.h"

static int output_line(FILE *output, const char *format, ...)
{
    int written;
    va_list arguments;

    va_start(arguments, format);
    written = vfprintf(output, format, arguments);
    va_end(arguments);
    return written >= 0;
}

static int output_failure(struct miga80_diagnostic *diagnostic,
                          const struct miga80_ir_instruction *instruction)
{
    diagnostic->line = instruction == NULL ? 0U : instruction->line;
    diagnostic->column = instruction == NULL ? 0U : instruction->column;
    (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                   "unable to write GNU m68k assembly");
    return 0;
}

static int output_fault_immediate(FILE *output, unsigned int value,
                                  const char *reg)
{
    if (value <= 127U) {
        return output_line(output, "        moveq   #%u,%s\n", value, reg);
    }
    return output_line(output, "        move.l  #%u,%s\n", value, reg);
}

int miga80_emit_gnu_m68k_normalize_integer(FILE *output,
                                           enum miga80_type type,
                                           const char *reg)
{
    if (type == MIGA80_TYPE_I8) {
        return output_line(output, "        extb.l  %s\n", reg);
    }
    if (type == MIGA80_TYPE_U8) {
        return output_line(output, "        and.l   #0xff,%s\n", reg);
    }
    if (type == MIGA80_TYPE_I16) {
        return output_line(output, "        ext.l   %s\n", reg);
    }
    if (type == MIGA80_TYPE_U16) {
        return output_line(output, "        and.l   #0xffff,%s\n", reg);
    }
    return type == MIGA80_TYPE_I32 || type == MIGA80_TYPE_FIX;
}

int miga80_emit_gnu_m68k_string_address(FILE *output,
                                        const char *function_name,
                                        unsigned int pool_index,
                                        const char *address_register)
{
    return output != NULL && function_name != NULL &&
           address_register != NULL &&
           output_line(output, "        lea     .L_%s_string_%u(%%pc),%s\n",
                       function_name, pool_index, address_register);
}

int miga80_emit_gnu_m68k_constant_pool(
    FILE *output, const char *function_name,
    const struct miga80_constant_pool *pool)
{
    unsigned int index;

    if (output == NULL || function_name == NULL ||
        !miga80_validate_constant_pool(pool)) {
        return 0;
    }
    for (index = 0U; index < pool->entry_count; ++index) {
        const struct miga80_pool_entry *entry = &pool->entries[index];
        const unsigned char *bytes;
        unsigned int offset;

        if (entry->type != MIGA80_TYPE_STRING) {
            continue;
        }
        bytes = miga80_pool_entry_bytes(pool, index);
        if (bytes == NULL ||
            !output_line(output,
                         "        .balign 4\n"
                         ".L_%s_string_%u:\n"
                         "        .long   %u\n",
                         function_name, index,
                         (unsigned int)entry->length)) {
            return 0;
        }
        for (offset = 0U; offset < entry->length; ++offset) {
            if (offset % 12U == 0U &&
                !output_line(output, "        .byte   ")) {
                return 0;
            }
            if (!output_line(output, "%s0x%02x",
                             offset % 12U == 0U ? "" : ",",
                             (unsigned int)bytes[offset])) {
                return 0;
            }
            if ((offset + 1U) % 12U == 0U ||
                offset + 1U == entry->length) {
                if (!output_line(output, "\n")) {
                    return 0;
                }
            }
        }
    }
    return 1;
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

int miga80_emit_gnu_m68k_fault_site(FILE *output, const char *function_name,
                                    unsigned int site, unsigned int line,
                                    unsigned int column)
{
    return output_line(output, ".L_%s_divzero_%u:\n", function_name, site) &&
           output_fault_immediate(output, line, "%d1") &&
           output_fault_immediate(output, column, "%d2") &&
           output_line(output, "        bra     .L_%s_fault_divzero\n",
                       function_name);
}

int miga80_emit_gnu_m68k_fault_tail(FILE *output,
                                    const char *function_name)
{
    return output_line(
        output,
        ".L_%s_fault_divzero:\n"
        "        moveq   #%u,%%d0\n"
        "        movea.l %u(%%a5),%%a0\n"
        "        jmp     (%%a0)\n",
        function_name, MIGA80_ABI_FAULT_DIVISION_BY_ZERO,
        MIGA80_ABI_RUNTIME_FAULT_HANDLER_OFFSET);
}

int miga80_emit_gnu_m68k_conversion_fault_site(
    FILE *output, const char *function_name, unsigned int site,
    unsigned int line, unsigned int column)
{
    return output_line(output, ".L_%s_conversion_%u:\n", function_name,
                       site) &&
           output_fault_immediate(output, line, "%d1") &&
           output_fault_immediate(output, column, "%d2") &&
           output_line(output, "        bra     .L_%s_fault_conversion\n",
                       function_name);
}

int miga80_emit_gnu_m68k_conversion_fault_tail(
    FILE *output, const char *function_name)
{
    return output_line(
        output,
        ".L_%s_fault_conversion:\n"
        "        moveq   #%u,%%d0\n"
        "        movea.l %u(%%a5),%%a0\n"
        "        jmp     (%%a0)\n",
        function_name, MIGA80_ABI_FAULT_CONVERSION_OUT_OF_RANGE,
        MIGA80_ABI_RUNTIME_FAULT_HANDLER_OFFSET);
}

int miga80_emit_gnu_m68k(FILE *output,
                         const struct miga80_ir_function *function,
                         struct miga80_diagnostic *diagnostic)
{
    unsigned int index;
    unsigned int frame_size;

    if (output == NULL || function == NULL || diagnostic == NULL) {
        return 0;
    }
    (void)memset(diagnostic, 0, sizeof(*diagnostic));
    if (!miga80_validate_ir(function, diagnostic)) {
        return 0;
    }
    if (function->parameter_count > MIGA80_MAX_PARAMETERS) {
        diagnostic->line = 0U;
        diagnostic->column = 0U;
        (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                       "function exceeds the initial register ABI");
        return 0;
    }
    frame_size = (function->parameter_count + function->local_count) * 4U;
    if (!miga80_abi_frame_size_is_valid(frame_size)) {
        (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                       "function frame violates native ABI 0.6");
        return 0;
    }

    if (!output_line(output,
                     "/* Generated by miga80c -O0 for native ABI %u.%u; "
                     "development oracle only. */\n"
                     "        .text\n"
                     "        .even\n"
                     "        .globl  %s\n"
                     "%s:\n"
                     "        link.w  %%a6,#-%u\n",
                     MIGA80_ABI_VERSION_MAJOR, MIGA80_ABI_VERSION_MINOR,
                     function->name, function->name, frame_size)) {
        return output_failure(diagnostic, NULL);
    }
    for (index = 0; index < function->parameter_count; ++index) {
        enum miga80_abi_register argument_register;
        const char *argument_name;
        const int address_class =
            miga80_type_is_address(function->parameter_types[index]);
        const unsigned int class_index = parameter_class_index(
            function->parameter_types, index, address_class);

        if (!(address_class
                  ? miga80_abi_address_argument_register(class_index,
                                                         &argument_register)
                  : miga80_abi_scalar_argument_register(class_index,
                                                        &argument_register)) ||
            (argument_name =
                 miga80_abi_gnu_register_name(argument_register)) == NULL) {
            diagnostic->line = 0U;
            diagnostic->column = 0U;
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "unable to map argument through native ABI 0.6");
            return 0;
        }
        if (!output_line(output, "        move.l  %s,-%u(%%a6)\n",
                         argument_name, (index + 1U) * 4U)) {
            return output_failure(diagnostic, NULL);
        }
    }

    for (index = 0; index < function->instruction_count; ++index) {
        const struct miga80_ir_instruction *instruction =
            &function->instructions[index];
        unsigned int block_index;
        int success = 1;

        for (block_index = 0U; block_index < function->block_count;
             ++block_index) {
            if (function->blocks[block_index].first_instruction == index &&
                !output_line(output, ".L_%s_b%u:\n", function->name,
                             block_index)) {
                return output_failure(diagnostic, instruction);
            }
        }

        switch (instruction->opcode) {
        case MIGA80_IR_PUSH_I32:
        case MIGA80_IR_PUSH_FIX:
        case MIGA80_IR_PUSH_BOOL:
            success = output_line(output,
                                  "        move.l  #0x%08x,-(%%a7)\n",
                                  (unsigned int)instruction->operand);
            break;
        case MIGA80_IR_PUSH_STRING:
            success = miga80_emit_gnu_m68k_string_address(
                          output, function->name,
                          (unsigned int)instruction->operand, "%a0") &&
                      output_line(output,
                                  "        move.l  %%a0,-(%%a7)\n");
            break;
        case MIGA80_IR_PUSH_SYMBOL:
            success = output_line(
                output, "        move.l  #0x%08x,-(%%a7)\n",
                miga80_pool_symbol_id(
                    &function->pool,
                    (unsigned int)instruction->operand));
            break;
        case MIGA80_IR_PUSH_PARAMETER_I32:
        case MIGA80_IR_PUSH_PARAMETER_BOOL:
            if (instruction->operand >= function->parameter_count) {
                diagnostic->line = instruction->line;
                diagnostic->column = instruction->column;
                (void)snprintf(diagnostic->message,
                               sizeof(diagnostic->message),
                               "typed IR parameter index exceeds ABI");
                return 0;
            }
            success = output_line(output, "        move.l  -%u(%%a6),-(%%a7)\n",
                                  (instruction->operand + 1U) * 4U);
            break;
        case MIGA80_IR_PUSH_LOCAL_I32:
        case MIGA80_IR_PUSH_LOCAL_BOOL:
            if (instruction->operand >= function->local_count) {
                diagnostic->line = instruction->line;
                diagnostic->column = instruction->column;
                (void)snprintf(diagnostic->message,
                               sizeof(diagnostic->message),
                               "typed IR local index exceeds frame");
                return 0;
            }
            success = output_line(
                output, "        move.l  -%u(%%a6),-(%%a7)\n",
                (function->parameter_count + instruction->operand + 1U) *
                    4U);
            break;
        case MIGA80_IR_STORE_LOCAL_I32:
        case MIGA80_IR_STORE_LOCAL_BOOL:
            if (instruction->operand >= function->local_count) {
                diagnostic->line = instruction->line;
                diagnostic->column = instruction->column;
                (void)snprintf(diagnostic->message,
                               sizeof(diagnostic->message),
                               "typed IR local store exceeds frame");
                return 0;
            }
            success = output_line(
                output,
                "        move.l  (%%a7)+,%%d0\n"
                "        move.l  %%d0,-%u(%%a6)\n",
                (function->parameter_count + instruction->operand + 1U) *
                    4U);
            break;
        case MIGA80_IR_NEG_I32:
            success = output_line(output,
                                  "        move.l  (%%a7)+,%%d0\n"
                                  "        neg.l   %%d0\n");
            if (success) {
                success = miga80_emit_gnu_m68k_normalize_integer(
                    output, instruction->type, "%d0");
            }
            if (success) {
                success = output_line(output,
                                      "        move.l  %%d0,-(%%a7)\n");
            }
            break;
        case MIGA80_IR_NORMALIZE_INTEGER:
            success = output_line(output,
                                  "        move.l  (%%a7)+,%%d0\n");
            if (success) {
                success = miga80_emit_gnu_m68k_normalize_integer(
                    output, instruction->type, "%d0");
            }
            if (success) {
                success = output_line(output,
                                      "        move.l  %%d0,-(%%a7)\n");
            }
            break;
        case MIGA80_IR_FIX_FROM_I32:
            success = output_line(
                output,
                "        move.l  (%%a7)+,%%d0\n"
                "        add.l   #0x00008000,%%d0\n"
                "        cmp.l   #0x00010000,%%d0\n"
                "        bcc     .L_%s_conversion_%u\n"
                "        sub.l   #0x00008000,%%d0\n"
                "        swap    %%d0\n"
                "        clr.w   %%d0\n"
                "        move.l  %%d0,-(%%a7)\n",
                function->name, index);
            break;
        case MIGA80_IR_I32_FROM_FIX:
            success = output_line(
                output,
                "        move.l  (%%a7)+,%%d0\n"
                "        tst.l   %%d0\n"
                "        bpl     .L_%s_fix_to_i32_positive_%u\n"
                "        add.l   #0x0000ffff,%%d0\n"
                ".L_%s_fix_to_i32_positive_%u:\n"
                "        swap    %%d0\n"
                "        ext.l   %%d0\n"
                "        move.l  %%d0,-(%%a7)\n",
                function->name, index, function->name, index);
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
        case MIGA80_IR_GE_U32:
            success = output_line(output,
                                  "        move.l  (%%a7)+,%%d1\n"
                                  "        move.l  (%%a7)+,%%d0\n");
            if (success && instruction->opcode == MIGA80_IR_ADD_I32) {
                success = output_line(output, "        add.l   %%d1,%%d0\n");
            } else if (success &&
                       instruction->opcode == MIGA80_IR_SUB_I32) {
                success = output_line(output, "        sub.l   %%d1,%%d0\n");
            } else if (success &&
                       instruction->opcode == MIGA80_IR_MUL_I32) {
                success = output_line(output, "        muls.l  %%d1,%%d0\n");
            } else if (success &&
                       instruction->opcode == MIGA80_IR_MUL_FIX) {
                success = output_line(output,
                                      "        muls.l  %%d1,%%d2:%%d0\n"
                                      "        move.w  %%d2,%%d0\n"
                                      "        swap    %%d0\n");
            } else if (success &&
                       instruction->opcode == MIGA80_IR_DIV_FIX) {
                success = output_line(
                    output,
                    "        tst.l   %%d1\n"
                    "        beq     .L_%s_divzero_%u\n"
                    "        move.l  %%d0,%%d2\n"
                    "        eor.l   %%d1,%%d2\n"
                    "        move.l  %%d2,-(%%a7)\n"
                    "        tst.l   %%d0\n"
                    "        bpl     .L_%s_fixdiv_left_%u\n"
                    "        neg.l   %%d0\n"
                    ".L_%s_fixdiv_left_%u:\n"
                    "        tst.l   %%d1\n"
                    "        bpl     .L_%s_fixdiv_right_%u\n"
                    "        neg.l   %%d1\n"
                    ".L_%s_fixdiv_right_%u:\n"
                    "        move.l  %%d0,%%d2\n"
                    "        lsr.l   #8,%%d2\n"
                    "        lsr.l   #8,%%d2\n"
                    "        swap    %%d0\n"
                    "        clr.w   %%d0\n"
                    "        move.l  %%d0,-(%%a7)\n"
                    "        move.l  %%d2,%%d0\n"
                    "        moveq   #0,%%d2\n"
                    "        divul.l %%d1,%%d2:%%d0\n"
                    "        move.l  (%%a7)+,%%d0\n"
                    "        divu.l  %%d1,%%d2:%%d0\n"
                    "        move.l  (%%a7)+,%%d2\n"
                    "        tst.l   %%d2\n"
                    "        bpl     .L_%s_fixdiv_done_%u\n"
                    "        neg.l   %%d0\n"
                    ".L_%s_fixdiv_done_%u:\n",
                    function->name, index, function->name, index,
                    function->name, index, function->name, index,
                    function->name, index, function->name, index,
                    function->name, index);
            } else if (success &&
                       instruction->opcode == MIGA80_IR_DIV_I32) {
                success = output_line(
                    output,
                    "        tst.l   %%d1\n"
                    "        beq     .L_%s_divzero_%u\n"
                    "        divs.l  %%d1,%%d0\n",
                    function->name, index);
            } else if (success &&
                       instruction->opcode == MIGA80_IR_DIV_U32) {
                success = output_line(
                    output,
                    "        tst.l   %%d1\n"
                    "        beq     .L_%s_divzero_%u\n"
                    "        divu.l  %%d1,%%d0\n",
                    function->name, index);
            } else if (success) {
                const char *condition = "seq";

                if (instruction->opcode == MIGA80_IR_NE_I32 ||
                    instruction->opcode == MIGA80_IR_NE_BOOL) {
                    condition = "sne";
                } else if (instruction->opcode == MIGA80_IR_LT_I32) {
                    condition = "slt";
                } else if (instruction->opcode == MIGA80_IR_LE_I32) {
                    condition = "sle";
                } else if (instruction->opcode == MIGA80_IR_GT_I32) {
                    condition = "sgt";
                } else if (instruction->opcode == MIGA80_IR_GE_I32) {
                    condition = "sge";
                } else if (instruction->opcode == MIGA80_IR_LT_U32) {
                    condition = "scs";
                } else if (instruction->opcode == MIGA80_IR_LE_U32) {
                    condition = "sls";
                } else if (instruction->opcode == MIGA80_IR_GT_U32) {
                    condition = "shi";
                } else if (instruction->opcode == MIGA80_IR_GE_U32) {
                    condition = "scc";
                }
                success = output_line(output,
                                      "        cmp.l   %%d1,%%d0\n"
                                      "        %s     %%d0\n"
                                      "        and.l   #1,%%d0\n",
                                      condition);
            }
            if (success &&
                (instruction->opcode == MIGA80_IR_ADD_I32 ||
                 instruction->opcode == MIGA80_IR_SUB_I32 ||
                 instruction->opcode == MIGA80_IR_MUL_I32 ||
                 instruction->opcode == MIGA80_IR_MUL_FIX ||
                 instruction->opcode == MIGA80_IR_DIV_FIX ||
                 instruction->opcode == MIGA80_IR_DIV_I32 ||
                 instruction->opcode == MIGA80_IR_DIV_U32)) {
                success = miga80_emit_gnu_m68k_normalize_integer(
                    output, instruction->type, "%d0");
            }
            if (success) {
                success =
                    output_line(output, "        move.l  %%d0,-(%%a7)\n");
            }
            break;
        case MIGA80_IR_BRANCH_FALSE:
            success = output_line(output,
                                  "        move.l  (%%a7)+,%%d0\n"
                                  "        tst.l   %%d0\n"
                                  "        beq     .L_%s_b%u\n",
                                  function->name,
                                  (unsigned int)instruction->operand);
            break;
        case MIGA80_IR_JUMP:
            if (function->blocks[instruction->operand].first_instruction ==
                index + 1U) {
                success = 1;
            } else {
                success = output_line(output, "        bra     .L_%s_b%u\n",
                                      function->name,
                                      (unsigned int)instruction->operand);
            }
            break;
        case MIGA80_IR_CALL_PSET:
            success = output_line(
                output,
                "        move.l  (%%a7)+,%%d2\n"
                "        move.l  (%%a7)+,%%d1\n"
                "        move.l  (%%a7)+,%%d0\n"
                "        movea.l %u(%%a5),%%a0\n"
                "        jsr     (%%a0)\n",
                MIGA80_ABI_RUNTIME_PSET_HANDLER_OFFSET);
            break;
        case MIGA80_IR_RETURN:
            if (instruction->type == MIGA80_TYPE_VOID) {
                success = output_line(output,
                                      "        unlk    %%a6\n"
                                      "        rts\n");
            } else if (instruction->type == MIGA80_TYPE_STRING) {
                success = output_line(output,
                                      "        movea.l (%%a7)+,%%a0\n"
                                      "        unlk    %%a6\n"
                                      "        rts\n");
            } else {
                success = output_line(output,
                                      "        move.l  (%%a7)+,%%d0\n"
                                      "        unlk    %%a6\n"
                                      "        rts\n");
            }
            break;
        default:
            diagnostic->line = instruction->line;
            diagnostic->column = instruction->column;
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "unknown typed IR instruction in m68k backend");
            return 0;
        }
        if (!success) {
            return output_failure(diagnostic, instruction);
        }
    }

    {
        int has_division = 0;

        for (index = 0U; index < function->instruction_count; ++index) {
            const struct miga80_ir_instruction *instruction =
                &function->instructions[index];

            if (instruction->opcode != MIGA80_IR_DIV_FIX &&
                instruction->opcode != MIGA80_IR_DIV_I32 &&
                instruction->opcode != MIGA80_IR_DIV_U32) {
                continue;
            }
            if (!miga80_emit_gnu_m68k_fault_site(
                    output, function->name, index, instruction->line,
                    instruction->column)) {
                return output_failure(diagnostic, instruction);
            }
            has_division = 1;
        }
        if (has_division &&
            !miga80_emit_gnu_m68k_fault_tail(output, function->name)) {
            return output_failure(diagnostic, NULL);
        }
    }

    {
        int has_conversion_fault = 0;

        for (index = 0U; index < function->instruction_count; ++index) {
            const struct miga80_ir_instruction *instruction =
                &function->instructions[index];

            if (instruction->opcode != MIGA80_IR_FIX_FROM_I32) {
                continue;
            }
            if (!miga80_emit_gnu_m68k_conversion_fault_site(
                    output, function->name, index, instruction->line,
                    instruction->column)) {
                return output_failure(diagnostic, instruction);
            }
            has_conversion_fault = 1;
        }
        if (has_conversion_fault &&
            !miga80_emit_gnu_m68k_conversion_fault_tail(
                output, function->name)) {
            return output_failure(diagnostic, NULL);
        }
    }

    if (!miga80_emit_gnu_m68k_constant_pool(output, function->name,
                                             &function->pool)) {
        return output_failure(diagnostic, NULL);
    }

    if (fflush(output) != 0 || ferror(output)) {
        return output_failure(diagnostic, NULL);
    }
    return 1;
}
