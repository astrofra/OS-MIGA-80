#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/backend_m68k/encoder.h"
#include "compiler/frontend/frontend.h"
#include "compiler/ir/ir.h"

#define TEST_SOURCE_CAPACITY 4096U
#define TEST_CODE_CAPACITY 4096U
#define TEST_WIDTH 256U
#define TEST_HEIGHT 256U

struct pixel_oracle {
    uint8_t pixels[TEST_WIDTH * TEST_HEIGHT];
    uint32_t calls;
};

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

int main(int argc, char **argv)
{
    char source[TEST_SOURCE_CAPACITY + 1U];
    uint8_t code[TEST_CODE_CAPACITY];
    struct miga80_ast_function *ast;
    struct miga80_ir_function *ir;
    struct miga80_diagnostic diagnostic;
    struct pixel_oracle oracle;
    struct miga80_ir_runtime runtime;
    uint32_t result = UINT32_MAX;
    size_t source_size;
    size_t code_size;
    int success = 0;

    if (argc != 3) {
        fprintf(stderr, "usage: %s source.lua output.bin\n", argv[0]);
        return 2;
    }
    ast = (struct miga80_ast_function *)malloc(sizeof(*ast));
    ir = (struct miga80_ir_function *)malloc(sizeof(*ir));
    if (ast == NULL || ir == NULL ||
        !read_source(argv[1], source, &source_size)) {
        fprintf(stderr, "unable to prepare compiler encoder test\n");
        goto cleanup;
    }
    if (!miga80_parse_function(source, source_size, ast, &diagnostic) ||
        !miga80_lower_function(ast, ir, &diagnostic) ||
        !miga80_encode_m68k_o0(code, sizeof(code), ir, &code_size,
                               &diagnostic)) {
        fprintf(stderr, "%u:%u: %s\n", diagnostic.line,
                diagnostic.column, diagnostic.message);
        goto cleanup;
    }
    (void)memset(&oracle, 0, sizeof(oracle));
    runtime.context = &oracle;
    runtime.pset = oracle_pset;
    if (!miga80_evaluate_ir_with_runtime(ir, NULL, 0U, &result, &runtime,
                                         &diagnostic)) {
        fprintf(stderr, "%u:%u: %s\n", diagnostic.line,
                diagnostic.column, diagnostic.message);
        goto cleanup;
    }
    if (!write_code(argv[2], code, code_size)) {
        fprintf(stderr, "unable to write direct encoder output\n");
        goto cleanup;
    }

    printf("source_bytes=%lu\n", (unsigned long)source_size);
    printf("ast_nodes=%u\n", ast->node_count);
    printf("ast_statements=%u\n", ast->statement_count);
    printf("ir_instructions=%u\n", ir->instruction_count);
    printf("ir_blocks=%u\n", ir->block_count);
    printf("encoded_bytes=%lu\n", (unsigned long)code_size);
    printf("encoded_checksum=%08x\n", (unsigned int)checksum(code, code_size));
    printf("oracle_pset_calls=%u\n", (unsigned int)oracle.calls);
    printf("oracle_framebuffer_checksum=%08x\n",
           (unsigned int)checksum(oracle.pixels, sizeof(oracle.pixels)));
    printf("result=%s\n", result == 0U ? "pass" : "fail");
    success = result == 0U;

cleanup:
    free(ir);
    free(ast);
    return success ? 0 : 1;
}
