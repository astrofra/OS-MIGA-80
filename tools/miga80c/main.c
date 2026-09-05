#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/backend_m68k/backend.h"
#include "compiler/frontend/frontend.h"
#include "compiler/ir/ir.h"
#include "compiler/value_ir/value_ir.h"

#define MIGA80_MAX_SOURCE_SIZE (64U * 1024U)

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s source.lua [-O0|-O1] -S -o output.s\n"
            "       %s source.lua --eval [typed-argument ...]\n",
            program, program);
}

static int read_source(const char *path, char **source, size_t *source_size)
{
    FILE *input = fopen(path, "rb");
    long file_size;
    size_t read_size;
    int close_result;
    int read_error;

    if (input == NULL) {
        fprintf(stderr, "%s: unable to open source\n", path);
        return 0;
    }
    if (fseek(input, 0L, SEEK_END) != 0 ||
        (file_size = ftell(input)) < 0L ||
        (unsigned long)file_size > MIGA80_MAX_SOURCE_SIZE ||
        fseek(input, 0L, SEEK_SET) != 0) {
        fprintf(stderr, "%s: source is unreadable or exceeds %u bytes\n",
                path, MIGA80_MAX_SOURCE_SIZE);
        (void)fclose(input);
        return 0;
    }

    *source = (char *)malloc((size_t)file_size + 1U);
    if (*source == NULL) {
        fprintf(stderr, "%s: unable to allocate source buffer\n", path);
        (void)fclose(input);
        return 0;
    }
    read_size = fread(*source, 1U, (size_t)file_size, input);
    read_error = ferror(input);
    close_result = fclose(input);
    if (read_size != (size_t)file_size || read_error || close_result != 0) {
        fprintf(stderr, "%s: unable to read source\n", path);
        free(*source);
        *source = NULL;
        return 0;
    }
    (*source)[read_size] = '\0';
    *source_size = read_size;
    return 1;
}

static int parse_argument(const char *text, uint32_t *value)
{
    char *end = NULL;
    long long parsed;

    errno = 0;
    parsed = strtoll(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < (long long)INT32_MIN || parsed > (long long)UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_typed_argument(const struct miga80_ir_function *function,
                                unsigned int parameter_index,
                                const char *text, uint32_t *value)
{
    const enum miga80_type type =
        function->parameter_types[parameter_index];
    unsigned int entry;

    if (type == MIGA80_TYPE_BOOL) {
        if (strcmp(text, "false") == 0) {
            *value = MIGA80_ABI_BOOL_FALSE;
            return 1;
        }
        if (strcmp(text, "true") == 0) {
            *value = MIGA80_ABI_BOOL_TRUE;
            return 1;
        }
    }
    if (type == MIGA80_TYPE_FIX) {
        return miga80_parse_fix_literal(text, strlen(text), value);
    }
    if (type != MIGA80_TYPE_STRING && type != MIGA80_TYPE_SYMBOL) {
        return parse_argument(text, value);
    }
    for (entry = 0U; entry < function->pool.entry_count; ++entry) {
        const struct miga80_pool_entry *candidate =
            &function->pool.entries[entry];
        const size_t length = strlen(text);

        if (candidate->type == type && candidate->length == length &&
            memcmp(miga80_pool_entry_bytes(&function->pool, entry), text,
                   length) == 0) {
            *value = type == MIGA80_TYPE_STRING
                         ? entry + 1U
                         : miga80_pool_symbol_id(&function->pool, entry);
            return 1;
        }
    }
    return 0;
}

static void print_diagnostic(const char *path,
                             const struct miga80_diagnostic *diagnostic)
{
    if (diagnostic->line == 0U) {
        fprintf(stderr, "%s: error: %s\n", path, diagnostic->message);
    } else {
        fprintf(stderr, "%s:%u:%u: error: %s\n", path, diagnostic->line,
                diagnostic->column, diagnostic->message);
    }
}

static int write_assembly(const char *path,
                          const struct miga80_ir_function *function,
                          unsigned int optimization_level,
                          struct miga80_diagnostic *diagnostic)
{
    struct miga80_value_function *value_function = NULL;
    FILE *output;
    int success;

    if (optimization_level == 1U) {
        value_function =
            (struct miga80_value_function *)malloc(sizeof(*value_function));
        if (value_function == NULL) {
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "unable to allocate O1 value IR");
            diagnostic->line = 0U;
            diagnostic->column = 0U;
            return 0;
        }
        if (!miga80_build_value_ir(function, value_function, diagnostic)) {
            free(value_function);
            return 0;
        }
    }

    output = fopen(path, "wb");
    if (output == NULL) {
        fprintf(stderr, "%s: unable to create assembly output\n", path);
        free(value_function);
        return 0;
    }
    if (optimization_level == 0U) {
        success = miga80_emit_gnu_m68k(output, function, diagnostic);
    } else {
        success =
            miga80_emit_gnu_m68k_o1(output, value_function, diagnostic);
    }
    if (fclose(output) != 0) {
        success = 0;
        if (diagnostic->message[0] == '\0') {
            (void)snprintf(diagnostic->message,
                           sizeof(diagnostic->message),
                           "unable to close assembly output");
        }
    }
    free(value_function);
    return success;
}

int main(int argc, char **argv)
{
    char *source = NULL;
    size_t source_size = 0U;
    struct miga80_ast_function *ast = NULL;
    struct miga80_ir_function *ir = NULL;
    struct miga80_diagnostic diagnostic;
    unsigned int optimization_level = 1U;
    int option_index = 2;
    int exit_code = 1;

    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }
    if (!read_source(argv[1], &source, &source_size)) {
        return 1;
    }
    ast = (struct miga80_ast_function *)malloc(sizeof(*ast));
    ir = (struct miga80_ir_function *)malloc(sizeof(*ir));
    if (ast == NULL || ir == NULL) {
        fprintf(stderr, "%s: unable to allocate compiler arenas\n", argv[1]);
        free(ir);
        free(ast);
        free(source);
        return 1;
    }
    if (!miga80_parse_function(source, source_size, ast, &diagnostic) ||
        !miga80_lower_function(ast, ir, &diagnostic)) {
        print_diagnostic(argv[1], &diagnostic);
        free(ir);
        free(ast);
        free(source);
        return 1;
    }

    if (strcmp(argv[option_index], "-O0") == 0 ||
        strcmp(argv[option_index], "-O1") == 0) {
        optimization_level = argv[option_index][2] == '0' ? 0U : 1U;
        ++option_index;
    }

    if (argc == option_index + 3 && strcmp(argv[option_index], "-S") == 0 &&
        strcmp(argv[option_index + 1], "-o") == 0) {
        const char *output_path = argv[option_index + 2];

        if (!write_assembly(output_path, ir, optimization_level,
                            &diagnostic)) {
            print_diagnostic(output_path, &diagnostic);
        } else {
            exit_code = 0;
        }
    } else if (option_index == 2 && strcmp(argv[2], "--eval") == 0) {
        uint32_t arguments[MIGA80_MAX_PARAMETERS] = {0U, 0U, 0U};
        uint32_t result;
        unsigned int index;
        const unsigned int argument_count = (unsigned int)(argc - 3);

        if (argument_count != ir->parameter_count) {
            fprintf(stderr, "%s: expected %u argument(s), got %u\n", argv[1],
                    ir->parameter_count, argument_count);
        } else {
            for (index = 0; index < argument_count; ++index) {
                if (!parse_typed_argument(ir, index, argv[index + 3U],
                                          &arguments[index])) {
                    fprintf(stderr, "%s: invalid %s argument '%s'\n",
                            argv[1],
                            miga80_type_name(ir->parameter_types[index]),
                            argv[index + 3U]);
                    break;
                }
            }
            if (index == argument_count &&
                miga80_evaluate_ir(ir, arguments, argument_count, &result,
                                   &diagnostic)) {
                printf("0x%08x\n", (unsigned int)result);
                exit_code = 0;
            } else if (index == argument_count) {
                print_diagnostic(argv[1], &diagnostic);
            }
        }
    } else {
        usage(argv[0]);
        exit_code = 2;
    }

    free(ir);
    free(ast);
    free(source);
    return exit_code;
}
