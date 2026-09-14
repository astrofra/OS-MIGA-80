#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "graphics/drawing.h"
#include "compiler/backend_m68k/backend.h"
#include "compiler/backend_m68k/encoder.h"

void triangle_tests(void);

static uint8_t pixels[65536];
static uint8_t planes[32768];
static uint8_t expected[65536];
static struct miga80_draw_surface surface;
static uint32_t trace = UINT32_C(0x811c9dc5);
static unsigned int line_count;

static uint32_t checksum(const uint8_t *bytes, size_t size)
{
    uint32_t hash = UINT32_C(2166136261);
    while (size-- != 0U) {
        hash = (hash ^ *bytes++) * UINT32_C(16777619);
    }
    return hash;
}

static void trace_value(uint32_t value)
{
    trace = ((trace << 5) | (trace >> 27)) ^ value;
}

static int oracle_pset(void *context, uint32_t x, uint32_t y, uint32_t color)
{
    trace_value(4U); trace_value(x); trace_value(y); trace_value(color);
    miga80_draw_pset(context, x, y, color);
    return 1;
}

static int oracle_layer(void *context, uint32_t layer)
{
    trace_value(36U); trace_value(layer);
    miga80_draw_select(context, layer);
    return 1;
}

static int oracle_line(void *context, uint32_t x0, uint32_t y0,
                        uint32_t x1, uint32_t y1, uint32_t color)
{
    struct miga80_draw_surface *target = context;
    struct miga80_draw_line line = {(int32_t)x0, (int32_t)y0,
                                   (int32_t)x1, (int32_t)y1, color};
    trace_value(40U); trace_value(x0); trace_value(y0); trace_value(color);
    trace_value(44U); trace_value(x1); trace_value(y1);
    if (target->layer == MIGA80_LAYER_PLANAR && miga80_draw_clip_line(&line)) {
        ++line_count;
    }
    miga80_draw_line_start(target, x0, y0, color);
    miga80_draw_line_end(target, x1, y1);
    return 1;
}

static int oracle_tri(void *context, uint32_t x0, uint32_t y0, uint32_t x1,
    uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color)
{
    trace_value(40U); trace_value(x0); trace_value(y0); trace_value(color);
    trace_value(72U); trace_value(x1); trace_value(y1);
    trace_value(76U); trace_value(x2); trace_value(y2);
    miga80_draw_line_start(context, x0, y0, color);
    miga80_draw_tri_middle(context, x1, y1);
    miga80_draw_tri_end(context, x2, y2);
    return 1;
}

static void initialize(void)
{
    unsigned int plane;
    memset(&surface, 0, sizeof(surface));
    memset(pixels, 0, sizeof(pixels));
    memset(planes, 0, sizeof(planes));
    surface.layer = MIGA80_LAYER_PIXEL;
    surface.pixels = pixels;
    for (plane = 0U; plane < 4U; ++plane) {
        surface.planes[plane] = planes + plane * 8192U;
    }
}

static void primitive_tests(void)
{
    int x, y;
    uint32_t seed = 1U;
    unsigned int trial;
    initialize();
    /* Independent rational nearest-pixel construction, every octant/tie. */
    for (y = -12; y <= 12; ++y) {
        for (x = -12; x <= 12; ++x) {
            struct miga80_draw_line line = {128, 128, 128+x, 128+y, 7};
            int dx, dy, steps, i;
            assert(miga80_draw_clip_line(&line));
            dx = line.x1-line.x0; dy = line.y1-line.y0;
            steps = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            memset(expected, 0, sizeof(expected));
            memset(pixels, 0, sizeof(pixels));
            for (i = 0; i <= steps; ++i) {
                const int px = line.x0 + (steps == 0 ? 0 :
                    (dx < 0 ? -1 : 1) * ((2*i*abs(dx)+steps)/(2*steps)));
                const int py = line.y0 + (steps == 0 ? 0 :
                    (dy < 0 ? -1 : 1) * ((2*i*abs(dy)+steps)/(2*steps)));
                expected[py*256+px] = 7U;
            }
            miga80_draw_cpu_line(&surface, &line);
            assert(memcmp(pixels, expected, sizeof(pixels)) == 0);
        }
    }
    for (trial = 0U; trial < 20000U; ++trial) {
        struct miga80_draw_line line;
        int32_t *coordinates[4] = {&line.x0, &line.y0, &line.x1, &line.y1};
        unsigned int index;
        for (index = 0U; index < 4U; ++index) {
            seed = seed * UINT32_C(1664525) + UINT32_C(1013904223);
            *coordinates[index] = (int32_t)seed;
        }
        line.color = 15U;
        if (miga80_draw_clip_line(&line)) {
            assert((uint32_t)line.x0 < 256U && (uint32_t)line.y0 < 256U &&
                   (uint32_t)line.x1 < 256U && (uint32_t)line.y1 < 256U);
            miga80_draw_cpu_line(&surface, &line);
        }
    }
    initialize();
    surface.layer = MIGA80_LAYER_PLANAR;
    for (trial = 0U; trial < 16U; ++trial) {
        miga80_draw_pset(&surface, 31U, 42U, trial);
        assert(miga80_draw_planar_pixel(&surface, 31U, 42U) == trial);
        assert(miga80_draw_planar_pixel(&surface, 30U, 42U) == 0U);
        assert(pixels[42U*256U+31U] == 0U);
    }
    miga80_draw_pset(&surface, UINT32_MAX, 0U, 5U);
    miga80_draw_pset(&surface, 31U, 42U, 16U);
    assert(miga80_draw_planar_pixel(&surface, 31U, 42U) == 15U);
    {
        struct miga80_draw_line extreme = {INT32_MIN, 42, INT32_MAX, 42, 3};
        assert(miga80_draw_clip_line(&extreme));
        assert(extreme.x0 == 0 && extreme.x1 == 255);
    }
    initialize();
}

static void rejection_tests(void)
{
    static const char *const invalid[] = {
        "function main(): void layer(1) end",
        "function main(): void layer(OBJECT) end",
        "function main(): void layer(PIXEL, PLANAR) end",
        "function main(): void line(1, 2, 3, 4) end",
        "function main(): void line(1, 2, 3, 4, 5, 6) end",
        "function main(): void line(true, 2, 3, 4, 5) end",
        "function main(): void line(1, 2, 3, 4, true) end",
        "function main(): void tri(1,2,3,4,5,6) end",
        "function main(): void tri(1,2,3,4,5,6,7,8) end",
        "function main(): void tri(1,2,3,true,5,6,7) end",
        "function main(): void tri(1,2,3,4,5,6,true) end"
    };
    static const char valid[] =
        "function main(): void layer(PLANAR) line(1, 2, 3, 4, 5) end";
    static struct miga80_ast_function ast;
    static struct miga80_ir_function ir;
    static struct miga80_value_function value;
    struct miga80_diagnostic diagnostic;
    unsigned int i;
    uint8_t bytes[1024];
    size_t size;
    for (i = 0U; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        assert(!miga80_parse_function(invalid[i], strlen(invalid[i]), &ast, &diagnostic));
        assert(diagnostic.line != 0U && diagnostic.column != 0U);
    }
    assert(miga80_parse_function(valid, strlen(valid), &ast, &diagnostic));
    assert(miga80_lower_function(&ast, &ir, &diagnostic));
    assert(miga80_build_value_ir(&ir, &value, &diagnostic));
    for (i = 0U; i < ir.instruction_count; ++i) {
        if (ir.instructions[i].opcode == MIGA80_IR_CALL_LINE) {
            ir.instructions[i].operand = 3U;
            assert(!miga80_validate_ir(&ir, &diagnostic));
            ir.instructions[i].operand = 5U;
            assert(miga80_validate_ir(&ir, &diagnostic));
        }
    }
    for (i = 0U; i < value.value_count; ++i) {
        if (value.values[i].opcode == MIGA80_VALUE_CALL_LINE_END) {
            value.values[i].left = MIGA80_INVALID_VALUE;
            assert(!miga80_encode_m68k_o1(bytes, sizeof(bytes), &value, &size, &diagnostic));
            break;
        }
    }
    assert(i != value.value_count);
}

static void save(const char *directory, const char *name, const void *data, size_t size)
{
    char path[1024];
    FILE *file;
    assert(snprintf(path, sizeof(path), "%s/%s", directory, name) > 0);
    file = fopen(path, "wb");
    assert(file != NULL && fwrite(data, 1U, size, file) == size);
    assert(fclose(file) == 0);
}

int main(int argc, char **argv)
{
    static struct miga80_ast_function ast;
    static struct miga80_ir_function ir;
    static struct miga80_value_function value;
    struct miga80_diagnostic diagnostic;
    struct miga80_ir_runtime runtime = {&surface, oracle_pset, oracle_layer, oracle_line, NULL, NULL, NULL, oracle_tri, NULL, NULL, NULL, NULL};
    uint8_t code[4096];
    char source[4097];
    size_t size, code_size, bound;
    uint32_t result;
    unsigned int mode;
    FILE *input;
    assert(argc == 3);
    triangle_tests();
    primitive_tests();
    rejection_tests();
    input = fopen(argv[1], "rb");
    assert(input != NULL);
    size = fread(source, 1U, sizeof(source)-1U, input);
    assert(!ferror(input) && fclose(input) == 0);
    source[size] = '\0';
    if (!miga80_parse_function(source, size, &ast, &diagnostic) ||
        !miga80_lower_function(&ast, &ir, &diagnostic) ||
        !miga80_build_value_ir(&ir, &value, &diagnostic) ||
        !miga80_evaluate_ir_with_runtime(&ir, NULL, 0U, &result, &runtime, &diagnostic)) {
        fprintf(stderr, "%u:%u %s\n", diagnostic.line, diagnostic.column, diagnostic.message);
        return 1;
    }
    for (mode = 0U; mode < 3U; ++mode) {
        char path[1024], name[32];
        FILE *assembly;
        int success = mode == 0U
            ? miga80_encode_m68k_o0(code, sizeof(code), &ir, &code_size, &diagnostic)
            : mode == 1U
                ? miga80_encode_m68k_o1(code, sizeof(code), &value, &code_size, &diagnostic)
                : miga80_encode_m68k_o1_guarded(code, sizeof(code), &value, &code_size, &bound, &diagnostic);
        if (!success) {
            fprintf(stderr, "%u:%u %s\n", diagnostic.line, diagnostic.column, diagnostic.message);
            return 1;
        }
        snprintf(name, sizeof(name), "mode%u.bin", mode);
        save(argv[2], name, code, code_size);
        snprintf(path, sizeof(path), "%s/mode%u.s", argv[2], mode);
        assembly = fopen(path, "w");
        assert(assembly != NULL);
        assert(mode == 0U ? miga80_emit_gnu_m68k(assembly, &ir, &diagnostic) :
               mode == 1U ? miga80_emit_gnu_m68k_o1(assembly, &value, &diagnostic) :
                            miga80_emit_gnu_m68k_o1_guarded(assembly, &value, &diagnostic));
        assert(fclose(assembly) == 0);
    }
    assert(bound >= 1024U && bound <= 4096U);
    save(argv[2], "pixel.bin", pixels, sizeof(pixels));
    save(argv[2], "planar.bin", planes, sizeof(planes));
    printf("trace=%08x\npixel=%08x\nplanar=%08x\nplanar_lines=%u\nstack_bound=%lu\n",
        trace, checksum(pixels, sizeof(pixels)), checksum(planes, sizeof(planes)),
        line_count, (unsigned long)bound);
    return 0;
}
