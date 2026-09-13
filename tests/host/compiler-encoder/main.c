#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/backend_m68k/encoder.h"
#include "compiler/backend_m68k/backend.h"
#include "compiler/frontend/frontend.h"
#include "compiler/ir/ir.h"
#include "compiler/value_ir/value_ir.h"

#define TEST_SOURCE_CAPACITY 4096U
#define TEST_CODE_CAPACITY 4096U
#define TEST_WIDTH 256U
#define TEST_HEIGHT 256U

static char test_source[TEST_SOURCE_CAPACITY + 1U];
static uint8_t test_code_o0[TEST_CODE_CAPACITY];
static uint8_t test_code_o1[TEST_CODE_CAPACITY];

struct pixel_oracle {
    uint8_t pixels[TEST_WIDTH * TEST_HEIGHT];
    uint32_t calls;
};

static struct pixel_oracle test_oracle;

static uint32_t checksum(const void *bytes, size_t size)
{
    const uint8_t *cursor = (const uint8_t *)bytes;
    uint32_t hash = UINT32_C(2166136261);
    size_t index;

    for (index = 0U; index < size; ++index) {
        hash ^= cursor[index];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static int oracle_pset(void *context, uint32_t x, uint32_t y,
                       uint32_t color)
{
    struct pixel_oracle *oracle = (struct pixel_oracle *)context;

    if (oracle == NULL || x >= TEST_WIDTH || y >= TEST_HEIGHT ||
        color > 15U) {
        return 0;
    }
    oracle->pixels[(y * TEST_WIDTH) + x] = (uint8_t)color;
    ++oracle->calls;
    return 1;
}

static int read_source(const char *path, char *source, size_t *source_size)
{
    FILE *input = fopen(path, "rb");
    size_t size;
    int extra;

    if (input == NULL) {
        return 0;
    }
    size = fread(source, 1U, TEST_SOURCE_CAPACITY, input);
    extra = fgetc(input);
    if (ferror(input) || extra != EOF || fclose(input) != 0) {
        return 0;
    }
    source[size] = '\0';
    *source_size = size;
    return 1;
}

static int write_code(const char *path, const uint8_t *code,
                      size_t code_size)
{
    FILE *output = fopen(path, "wb");
    int success;

    if (output == NULL) {
        return 0;
    }
    success = fwrite(code, 1U, code_size, output) == code_size;
    if (fclose(output) != 0) {
        success = 0;
    }
    return success;
}

static int encode_o1_fixture(const char *source_path,
                             const char *output_path, const char *assembly_path)
{
    struct miga80_ast_function *ast;
    struct miga80_ir_function *ir;
    struct miga80_value_function *value_ir;
    struct miga80_diagnostic diagnostic;
    size_t source_size;
    size_t code_size;
    size_t required_stack_bytes = 0U;
    unsigned int live_values = 0U;
    unsigned int value_index;
    int success = 0;

    ast = (struct miga80_ast_function *)malloc(sizeof(*ast));
    ir = (struct miga80_ir_function *)malloc(sizeof(*ir));
    value_ir =
        (struct miga80_value_function *)malloc(sizeof(*value_ir));
    if (ast == NULL || ir == NULL || value_ir == NULL ||
        !read_source(source_path, test_source, &source_size)) {
        fprintf(stderr, "unable to prepare O1 encoder fixture\n");
        goto cleanup;
    }
    if (!miga80_parse_function(test_source, source_size, ast, &diagnostic) ||
        !miga80_lower_function(ast, ir, &diagnostic) ||
        !miga80_build_value_ir(ir, value_ir, &diagnostic) ||
        !(assembly_path != NULL
              ? miga80_encode_m68k_o1_guarded(
                    test_code_o1, sizeof(test_code_o1), value_ir, &code_size,
                    &required_stack_bytes, &diagnostic)
              : miga80_encode_m68k_o1(test_code_o1, sizeof(test_code_o1),
                                       value_ir, &code_size, &diagnostic))) {
        fprintf(stderr, "%u:%u: %s\n", diagnostic.line,
                diagnostic.column, diagnostic.message);
        goto cleanup;
    }
    if (!write_code(output_path, test_code_o1, code_size)) {
        fprintf(stderr, "unable to write O1 encoder fixture\n");
        goto cleanup;
    }
    if (assembly_path != NULL) {
        FILE *assembly = fopen(assembly_path, "w");
        int emitted;

        if (assembly == NULL) {
            goto cleanup;
        }
        emitted = miga80_emit_gnu_m68k_o1_guarded(assembly, value_ir,
                                                 &diagnostic);
        if (fclose(assembly) != 0 || !emitted) {
            goto cleanup;
        }
        /* A failed bounded emission must leave caller-owned guards intact. */
        (void)memset(test_code_o0, 0xa5, sizeof(test_code_o0));
        if (miga80_encode_m68k_o1_guarded(
                test_code_o0 + 4U, code_size - 1U, value_ir, &source_size,
                &required_stack_bytes, &diagnostic) ||
            test_code_o0[3] != 0xa5 ||
            test_code_o0[code_size + 3U] != 0xa5) {
            fprintf(stderr, "guarded encoder capacity check failed\n");
            goto cleanup;
        }
        printf("required_stack_bytes=%lu\n",
               (unsigned long)required_stack_bytes);
    }
    for (value_index = 0U; value_index < value_ir->value_count;
         ++value_index) {
        if (value_ir->values[value_index].live) {
            ++live_values;
        }
    }
    printf("source_bytes=%lu\n", (unsigned long)source_size);
    printf("ir_instructions=%u\n", ir->instruction_count);
    printf("ir_blocks=%u\n", ir->block_count);
    printf("value_instructions=%u\n", value_ir->value_count);
    printf("value_live_instructions=%u\n", live_values);
    printf("o1_encoded_bytes=%lu\n", (unsigned long)code_size);
    printf("o1_encoded_checksum=%08x\n",
           (unsigned int)checksum(test_code_o1, code_size));
    success = 1;

cleanup:
    free(value_ir);
    free(ir);
    free(ast);
    return success;
}

int main(int argc, char **argv)
{
    struct miga80_ast_function *ast;
    struct miga80_ir_function *ir;
    struct miga80_value_function *value_ir;
    struct miga80_diagnostic diagnostic;
    struct miga80_ir_runtime runtime = {0};
    uint32_t result = UINT32_MAX;
    size_t source_size;
    size_t code_o0_size;
    size_t code_o1_size;
    unsigned int live_values = 0U;
    unsigned int value_index;
    int success = 0;

    if (argc == 4 && strcmp(argv[1], "--o1") == 0) {
        return encode_o1_fixture(argv[2], argv[3], NULL) ? 0 : 1;
    }
    if (argc == 5 && strcmp(argv[1], "--guarded") == 0) {
        return encode_o1_fixture(argv[2], argv[3], argv[4]) ? 0 : 1;
    }
    if (argc != 4) {
        fprintf(stderr,
                "usage: %s source.lua output-o0.bin output-o1.bin\n"
                "       %s --o1 source.lua output.bin\n",
                argv[0], argv[0]);
        return 2;
    }
    ast = (struct miga80_ast_function *)malloc(sizeof(*ast));
    ir = (struct miga80_ir_function *)malloc(sizeof(*ir));
    value_ir =
        (struct miga80_value_function *)malloc(sizeof(*value_ir));
    if (ast == NULL || ir == NULL || value_ir == NULL ||
        !read_source(argv[1], test_source, &source_size)) {
        fprintf(stderr, "unable to prepare compiler encoder test\n");
        goto cleanup;
    }
    if (!miga80_parse_function(test_source, source_size, ast, &diagnostic) ||
        !miga80_lower_function(ast, ir, &diagnostic) ||
        !miga80_build_value_ir(ir, value_ir, &diagnostic) ||
        !miga80_encode_m68k_o0(test_code_o0, sizeof(test_code_o0), ir,
                               &code_o0_size, &diagnostic) ||
        !miga80_encode_m68k_o1(test_code_o1, sizeof(test_code_o1), value_ir,
                               &code_o1_size, &diagnostic)) {
        fprintf(stderr, "%u:%u: %s\n", diagnostic.line,
                diagnostic.column, diagnostic.message);
        goto cleanup;
    }
    (void)memset(&test_oracle, 0, sizeof(test_oracle));
    runtime.context = &test_oracle;
    runtime.pset = oracle_pset;
    if (!miga80_evaluate_ir_with_runtime(ir, NULL, 0U, &result, &runtime,
                                         &diagnostic)) {
        fprintf(stderr, "%u:%u: %s\n", diagnostic.line,
                diagnostic.column, diagnostic.message);
        goto cleanup;
    }
    if (!write_code(argv[2], test_code_o0, code_o0_size) ||
        !write_code(argv[3], test_code_o1, code_o1_size)) {
        fprintf(stderr, "unable to write direct encoder output\n");
        goto cleanup;
    }

    printf("source_bytes=%lu\n", (unsigned long)source_size);
    printf("ast_nodes=%u\n", ast->node_count);
    printf("ast_statements=%u\n", ast->statement_count);
    printf("ir_instructions=%u\n", ir->instruction_count);
    printf("ir_blocks=%u\n", ir->block_count);
    for (value_index = 0U; value_index < value_ir->value_count;
         ++value_index) {
        if (value_ir->values[value_index].live) {
            ++live_values;
        }
    }
    printf("value_instructions=%u\n", value_ir->value_count);
    printf("value_live_instructions=%u\n", live_values);
    printf("o0_encoded_bytes=%lu\n", (unsigned long)code_o0_size);
    printf("o0_encoded_checksum=%08x\n",
           (unsigned int)checksum(test_code_o0, code_o0_size));
    printf("o1_encoded_bytes=%lu\n", (unsigned long)code_o1_size);
    printf("o1_encoded_checksum=%08x\n",
           (unsigned int)checksum(test_code_o1, code_o1_size));
    printf("oracle_pset_calls=%u\n", (unsigned int)test_oracle.calls);
    printf("oracle_framebuffer_checksum=%08x\n",
           (unsigned int)checksum(test_oracle.pixels,
                                  sizeof(test_oracle.pixels)));
    printf("result=%s\n",
           result == 0U && code_o1_size < code_o0_size ? "pass" : "fail");
    success = result == 0U && code_o1_size < code_o0_size;

cleanup:
    free(value_ir);
    free(ir);
    free(ast);
    return success ? 0 : 1;
}
