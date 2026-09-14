#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dos/dos.h>
#include <devices/inputevent.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <exec/tasks.h>
#include <graphics/displayinfo.h>
#include <graphics/copper.h>
#include <graphics/gfxmacros.h>
#include <graphics/gfx.h>
#include <graphics/gfxbase.h>
#include <graphics/modeid.h>
#include <graphics/rastport.h>
#include <graphics/videocontrol.h>
#include <graphics/view.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <utility/tagitem.h>

#include "graphics/c2p_reference.h"
#include "graphics/c2p4_reference.h"
#include "compiler/abi/runtime.h"
#include "compiler/backend_m68k/encoder.h"
#include "compiler/frontend/frontend.h"
#include "compiler/ir/ir.h"
#include "compiler/value_ir/value_ir.h"
#include "demo/supervisor.h"
#include "demo/stop_test.h"
#include "demo/file_picker.h"
#include "demo/intro.h"
#include "demo/music_host.h"
#include "demo/drawing_host.h"
#include "drawing_test_data.h"
#include "ui/palette.h"
#include "ui/source_view.h"

#define DEMO_SCREEN_WIDTH 256U
#define DEMO_SCREEN_HEIGHT 256U
#define DEMO_SCREEN_DEPTH 8U
#define DEMO_DISPLAY_ID (PAL_MONITOR_ID | LORESDPF_KEY)
#define DEMO_PALETTE_COLORS 32U
#define DEMO_CHUNKY_BYTES (DEMO_SCREEN_WIDTH * DEMO_SCREEN_HEIGHT)
#define DEMO_SOURCE_CAPACITY 4096U
#define DEMO_CODE_CAPACITY 4096U
#define DEMO_COMPILER_STACK_BYTES 32768U
#define DEMO_RUNTIME_STACK_BYTES 4096U
#define DEMO_RUNTIME_STACK_TOTAL_BYTES \
    (DEMO_RUNTIME_STACK_BYTES + 2U * DEMO_STACK_GUARD_BYTES)
#define DEMO_EXECUTION_BUDGET 1000000U
#define DEMO_STACK_BOUNDARY_RESERVE_BYTES 256U
#define DEMO_STACK_GUARD_BYTES 256U
#define DEMO_COMPILER_STACK_TOTAL_BYTES \
    (DEMO_COMPILER_STACK_BYTES + \
     (2U * DEMO_STACK_BOUNDARY_RESERVE_BYTES) + \
     (2U * DEMO_STACK_GUARD_BYTES))
#define DEMO_STACK_LOWER_GUARD 0x5aU
#define DEMO_STACK_UPPER_GUARD 0xa5U
#define DEMO_DEFAULT_SOURCE "SYS:demos/default.lua"
#define DEMO_DEFAULT_REPORT "RAM:MIGA80-BOOTED.TXT"
#define DEMO_RAWKEY_ESCAPE 0x45U
#define DEMO_RAWKEY_F5 0x54U

struct GfxBase *GfxBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library *KeymapBase = NULL;

static char source_buffer[DEMO_SOURCE_CAPACITY + 1U];
static char pending_source[DEMO_SOURCE_CAPACITY + 1U];
static const char *interactive_source_path;
static ULONG amiga_palette[1U + (DEMO_PALETTE_COLORS * 3U) + 1U];
static ULONG palette_readback[DEMO_PALETTE_COLORS * 3U];
static UBYTE *compiler_stack_allocation;
static struct StackSwapStruct compiler_stack_swap;

struct demo_compile_request {
    struct Screen *screen;
    uint8_t *chunky;
    const struct Miga80SourceViewMetrics *metrics;
    const char *report_path;
    char *error_status;
    size_t error_capacity;
};

static struct demo_compile_request compiler_request;
static int compiler_run_result;
static struct miga80_drawing_context last_drawing_context;
#define last_runtime last_drawing_context.runtime
static uint32_t last_planar_checksum;
static ULONG last_planar_lines, last_planar_triangles;
static ULONG last_animation_frames, last_animation_elapsed;
static struct miga80_music_stats last_music;
static enum miga80_c2p_backend c2p_backend = MIGA80_C2P_DEFAULT;
static struct miga80_c2p_stats last_c2p;
static struct {
    ULONG frames, elapsed;
    struct miga80_c2p_stats c2p;
} cube_samples[2];
static uint32_t last_framebuffer_checksum;
static uint32_t execution_budget = DEMO_EXECUTION_BUDGET;
static int force_runtime_fault;
static const char *last_failure;
static struct Window *active_window;
static int supervisor_enabled = 1;
static int escape_held;
static struct miga80_supervisor_observation last_supervisor;
static int test_graphics;
static int last_graphics_readback;
static int test_escape;
static ULONG test_escape_delay = 100000U;
static int test_unguarded;
static int test_stalled_service;
static unsigned int compile_attempts;

typedef char runtime_context_layout_check[
    sizeof(struct miga80_runtime_context) ==
            MIGA80_ABI_RUNTIME_GUARDED_CONTEXT_SIZE &&
    offsetof(struct miga80_runtime_context, budget) ==
            MIGA80_ABI_RUNTIME_BUDGET_OFFSET &&
    offsetof(struct miga80_runtime_context, fault_column) ==
            MIGA80_ABI_RUNTIME_FAULT_COLUMN_OFFSET ? 1 : -1];

typedef char drawing_context_layout_check[
    sizeof(struct miga80_drawing_context) == MIGA80_ABI_RUNTIME_DRAWING_CONTEXT_SIZE &&
    offsetof(struct miga80_drawing_context, drawing_state) ==
        MIGA80_ABI_RUNTIME_DRAWING_STATE_OFFSET &&
    offsetof(struct miga80_draw_surface, layer) == 0U &&
    offsetof(struct miga80_draw_surface, pixel_written) == 4U ? 1 : -1];

extern ULONG miga80_execute_generated(APTR code, APTR runtime_context);
extern void miga80_runtime_pset(void);
extern void miga80_runtime_draw_pset(void);
extern void miga80_runtime_sin(void);
extern void miga80_runtime_cos(void);
extern void miga80_runtime_time(void);
extern void miga80_runtime_cls(void);
extern void miga80_runtime_flip(void);
extern void miga80_runtime_layer(void);
extern void miga80_runtime_line_start(void);
extern void miga80_runtime_line_end(void);
extern void miga80_runtime_tri_middle(void);
extern void miga80_runtime_tri_end(void);
extern void miga80_runtime_fault(void);
extern void miga80_runtime_test_fault(void);
extern void miga80_runtime_test_stall(void);

static size_t text_length(const char *text)
{
    const char *end = text;

    while (*end != '\0') {
        ++end;
    }
    return (size_t)(end - text);
}

static int region_has_value(const UBYTE *region, size_t size, UBYTE value)
{
    size_t index;

    for (index = 0U; index < size; ++index) {
        if (region[index] != value) {
            return 0;
        }
    }
    return 1;
}

static int compiler_stack_guards_intact(void)
{
    const UBYTE *upper_guard;

    if (compiler_stack_allocation == NULL) {
        return 0;
    }
    upper_guard = compiler_stack_allocation + DEMO_STACK_GUARD_BYTES +
                  DEMO_STACK_BOUNDARY_RESERVE_BYTES +
                  DEMO_COMPILER_STACK_BYTES +
                  DEMO_STACK_BOUNDARY_RESERVE_BYTES;

    return region_has_value(compiler_stack_allocation,
                            DEMO_STACK_GUARD_BYTES,
                            DEMO_STACK_LOWER_GUARD) &&
           region_has_value(upper_guard, DEMO_STACK_GUARD_BYTES,
                            DEMO_STACK_UPPER_GUARD);
}

static int prepare_compiler_stack(void)
{
    UBYTE *usable;

    compiler_stack_allocation = (UBYTE *)AllocMem(
        DEMO_COMPILER_STACK_TOTAL_BYTES, MEMF_PUBLIC);
    if (compiler_stack_allocation == NULL) {
        return 0;
    }
    (void)memset(compiler_stack_allocation, DEMO_STACK_LOWER_GUARD,
                 DEMO_STACK_GUARD_BYTES);
    (void)memset(compiler_stack_allocation + DEMO_STACK_GUARD_BYTES, 0xcd,
                 DEMO_COMPILER_STACK_BYTES +
                     (2U * DEMO_STACK_BOUNDARY_RESERVE_BYTES));
    usable = compiler_stack_allocation + DEMO_STACK_GUARD_BYTES +
             DEMO_STACK_BOUNDARY_RESERVE_BYTES;
    (void)memset(usable + DEMO_COMPILER_STACK_BYTES +
                     DEMO_STACK_BOUNDARY_RESERVE_BYTES,
                 DEMO_STACK_UPPER_GUARD, DEMO_STACK_GUARD_BYTES);
    compiler_stack_swap.stk_Lower =
        usable - DEMO_STACK_BOUNDARY_RESERVE_BYTES;
    compiler_stack_swap.stk_Upper =
        (ULONG)(uintptr_t)(usable + DEMO_COMPILER_STACK_BYTES +
                           DEMO_STACK_BOUNDARY_RESERVE_BYTES);
    compiler_stack_swap.stk_Pointer =
        (APTR)(uintptr_t)(usable + DEMO_COMPILER_STACK_BYTES);
    return 1;
}

static void release_compiler_stack(void)
{
    if (compiler_stack_allocation != NULL) {
        FreeMem(compiler_stack_allocation,
                DEMO_COMPILER_STACK_TOTAL_BYTES);
        compiler_stack_allocation = NULL;
    }
}

static int write_bytes(BPTR output, const char *bytes, size_t size)
{
    if (output == (BPTR)0 || size > 0x7fffffffUL) {
        return 0;
    }
    return Write(output, (APTR)bytes, (LONG)size) == (LONG)size;
}

static int write_text(BPTR output, const char *text)
{
    return write_bytes(output, text, text_length(text));
}

static int write_decimal(BPTR output, uint64_t value)
{
    char digits[20];
    size_t count = 0U;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U && count < sizeof(digits));
    while (count > 0U) {
        --count;
        if (!write_bytes(output, &digits[count], 1U)) {
            return 0;
        }
    }
    return 1;
}

static int write_hex32(BPTR output, uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    char text[8];
    unsigned int index;

    for (index = 0U; index < 8U; ++index) {
        const unsigned int shift = (7U - index) * 4U;

        text[index] = digits[(value >> shift) & 0x0fU];
    }
    return write_bytes(output, text, sizeof(text));
}

static int write_running_report(const char *path)
{
    BPTR output;
    int success;

    if (path == NULL) {
        return 1;
    }
    output = Open((STRPTR)path, MODE_NEWFILE);

    if (output == (BPTR)0) {
        return 0;
    }
    success = write_text(output, "miga80_source_view_report=1\nresult=running\n");
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int write_failure_report(const char *path, const char *stage)
{
    BPTR output;
    int success;

    if (path == NULL) {
        return 1;
    }
    output = Open((STRPTR)path, MODE_NEWFILE);

    if (output == (BPTR)0) {
        return 0;
    }
    success = write_text(output, "miga80_source_view_report=1\nfailure=") &&
              write_text(output, stage) &&
              write_text(output, "\nresult=fail\n");
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int write_success_report(
    const char *path, const struct Miga80SourceViewMetrics *metrics)
{
    BPTR output;
    int success;

    if (path == NULL) {
        return 1;
    }
    output = Open((STRPTR)path, MODE_NEWFILE);

    if (output == (BPTR)0) {
        return 0;
    }
    success =
        write_text(output,
                   "miga80_source_view_report=1\n"
                   "target=pal-a1200-68020-aga\n"
                   "screen=256x256x8-dual-playfield\n"
                   "font=4x8\nsource_bytes=") &&
        write_decimal(output, metrics->source_bytes) &&
        write_text(output, "\nsource_lines=") &&
        write_decimal(output, metrics->source_lines) &&
        write_text(output, "\nmaximum_columns=") &&
        write_decimal(output, metrics->maximum_columns) &&
        write_text(output, "\nsource_checksum=") &&
        write_hex32(output, metrics->source_checksum) &&
        write_text(output, "\nframebuffer_checksum=") &&
        write_hex32(output, metrics->framebuffer_checksum) &&
        write_text(output,
                   "\npalette=workbench-sunset\n"
                   "source_playfield=pf1\n"
                   "palette_roundtrip=pass\n"
                   "source_view=pass\n"
                   "result=pass\n");
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int write_run_report(
    const char *path, const struct Miga80SourceViewMetrics *metrics,
    const struct miga80_ast_function *ast,
    const struct miga80_ir_function *ir,
    const struct miga80_value_function *value_ir, const uint8_t *code,
    size_t code_size, uint32_t framebuffer_checksum)
{
    BPTR output;
    int success;

    if (path == NULL) {
        return 1;
    }
    output = Open((STRPTR)path, MODE_NEWFILE);

    if (output == (BPTR)0) {
        return 0;
    }
    success =
        write_text(output,
                   "miga80_source_view_report=2\n"
                   "target=pal-a1200-68020-aga\n"
                   "screen=256x256x8-dual-playfield\n"
                   "font=4x8\nsource_bytes=") &&
        write_decimal(output, metrics->source_bytes) &&
        write_text(output, "\nsource_lines=") &&
        write_decimal(output, metrics->source_lines) &&
        write_text(output, "\nmaximum_columns=") &&
        write_decimal(output, metrics->maximum_columns) &&
        write_text(output, "\nsource_checksum=") &&
        write_hex32(output, metrics->source_checksum) &&
        write_text(output, "\nast_nodes=") &&
        write_decimal(output, ast->node_count) &&
        write_text(output, "\nast_statements=") &&
        write_decimal(output, ast->statement_count) &&
        write_text(output, "\nir_instructions=") &&
        write_decimal(output, ir->instruction_count) &&
        write_text(output, "\nir_blocks=") &&
        write_decimal(output, ir->block_count) &&
        write_text(output, "\nvalue_instructions=") &&
        write_decimal(output, value_ir->value_count) &&
        write_text(output, "\nencoded_bytes=") &&
        write_decimal(output, code_size) &&
        write_text(output, "\nencoded_checksum=") &&
        write_hex32(output,
                    miga80_source_view_checksum(code, code_size)) &&
        write_text(output, "\nframebuffer_checksum=") &&
        write_hex32(output, framebuffer_checksum) &&
        write_text(output,
                   "\ncompiler=typed-value-ir-direct-o1-guarded\n"
                   "palette=workbench-sunset\n"
                   "source_playfield=pf1\n"
                   "native_execution=pass\n"
                   "runtime_stack_guards=pass\n"
                   "runtime_fault=0\n") &&
        write_text(output, "execution_budget_remaining=") &&
        write_decimal(output, last_runtime.budget) &&
        write_text(output, "\nsupervision=") &&
        write_text(output, supervisor_enabled ? "exec-task" : "disabled") &&
        write_text(output, "\n");
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int append_stack_restore_report(const char *path)
{
    BPTR output = Open((STRPTR)path, MODE_READWRITE);
    int success;

    if (output == (BPTR)0 || Seek(output, 0L, OFFSET_END) < 0L) {
        if (output != (BPTR)0) {
            (void)Close(output);
        }
        return 0;
    }
    success = write_text(output,
                         "compiler_stack_restore=pass\n"
                         "result=pass\n");
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int write_stopped_report(const char *path)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;

    if (output == (BPTR)0) {
        return 0;
    }
    success = write_text(output,
        "miga80_source_view_report=3\nsupervision=exec-task\n"
        "native_execution=stopped\nruntime_fault=4\n"
        "runtime_stack_guards=pass\nworker_stack_guards=pass\n"
        "compiler_stack_restore=pass\nresult=stopped\n");
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int read_source(const char *path, char *buffer, size_t *source_size)
{
    BPTR input = Open((STRPTR)path, MODE_OLDFILE);
    LONG count;

    if (input == (BPTR)0) {
        return 0;
    }
    count = Read(input, buffer, (LONG)sizeof(source_buffer));
    if (!Close(input) || count < 0 || (size_t)count > DEMO_SOURCE_CAPACITY) {
        return 0;
    }
    buffer[count] = '\0';
    *source_size = (size_t)count;
    return 1;
}

static int load_source(const char *path, size_t *source_size)
{
    return read_source(path, source_buffer, source_size);
}

static ULONG expand_nibble(ULONG value)
{
    return (value & 0x0fU) * 0x11111111UL;
}

static void prepare_palette(void)
{
    ULONG index;

    amiga_palette[0] = DEMO_PALETTE_COLORS << 16;
    for (index = 0U; index < DEMO_PALETTE_COLORS; ++index) {
        const uint16_t rgb12 = miga80_workbench_sunset_rgb12[index & 15U];

        amiga_palette[1U + (index * 3U)] =
            expand_nibble((ULONG)(rgb12 >> 8));
        amiga_palette[2U + (index * 3U)] =
            expand_nibble((ULONG)(rgb12 >> 4));
        amiga_palette[3U + (index * 3U)] = expand_nibble((ULONG)rgb12);
    }
    amiga_palette[1U + (DEMO_PALETTE_COLORS * 3U)] = 0U;
}

static int install_display_palette(struct Screen *screen)
{
    struct UCopList *list = AllocMem(sizeof(*list), MEMF_PUBLIC | MEMF_CLEAR);
    volatile struct Custom *registers = (volatile struct Custom *)0xdff000UL;
    if (list == NULL) {
        return 0;
    }
    if (UCopperListInit(list, 4U) == NULL) {
        if (list->FirstCopList != NULL) {
            FreeCopList(list->FirstCopList);
        }
        FreeMem(list, sizeof(*list));
        return 0;
    }
    /* Kickstart 3.0 can report the requested ColorMap bases while emitting
     * PF2OF=7 and BPLAM=16. Override them at viewport line zero, after the
     * system's palette loads: PF1 uses 0..15, PF2 uses 16..31. Keep the
     * hosted 140 ns sprite resolution and sprite palette bases intact. */
    CWAIT(list, 0, 0);
    CMOVE(list, registers->bplcon3, 0x1040U);
    CMOVE(list, registers->bplcon4, 0x0011U);
    CEND(list);
    Forbid();
    screen->ViewPort.UCopIns = list;
    Permit();
    /* CloseScreen releases this user list together with the display lists. */
    return RethinkDisplay() == 0U;
}

/* Inspect the actual merged non-interlaced display, not just ColorMap tags.
 * This test is specific to our exclusive screen without raster effects. */
static int verify_display_palette(void)
{
    UWORD high[DEMO_PALETTE_COLORS];
    UWORD low[DEMO_PALETTE_COLORS];
    UWORD bplcon0 = 0U, bplcon2 = 0xffffU;
    UWORD bplcon3 = 0xffffU, bplcon4 = 0xffffU;
    const struct cprlist *list;
    ULONG index;
    int ended = 0;

    for (index = 0U; index < DEMO_PALETTE_COLORS; ++index) {
        high[index] = low[index] = 0xffffU;
    }
    /* Keep the list and its allocation bound together while Intuition is
     * prevented from rebuilding/freeing it. No I/O or waiting in this span. */
    Forbid();
    list = GfxBase->ActiView != NULL ? GfxBase->ActiView->LOFCprList : NULL;
    if (list != NULL && list->start != NULL &&
        list->start == GfxBase->LOFlist &&
        list->MaxCount > 0 && list->MaxCount <= 4096) {
        for (index = 0U; index < (ULONG)list->MaxCount; ++index) {
            const UWORD reg = list->start[index * 2U];
            const UWORD value = list->start[index * 2U + 1U];
            if (reg == 0xffffU && value == 0xfffeU) {
                ended = 1;
                break;
            }
            if ((reg & 1U) != 0U) {
                continue;
            }
            if (reg == 0x100U) { bplcon0 = value; }
            else if (reg == 0x104U) { bplcon2 = value; }
            else if (reg == 0x106U) { bplcon3 = value; }
            else if (reg == 0x10cU) { bplcon4 = value; }
            else if (reg >= 0x180U && reg <= 0x1beU &&
                     (bplcon3 & 0xe000U) == 0U) {
                const ULONG color = (reg - 0x180U) / 2U;
                if ((bplcon3 & 0x0200U) != 0U) {
                    low[color] = value & 0x0fffU;
                } else {
                    high[color] = value & 0x0fffU;
                }
            }
        }
    }
    Permit();
    /* Eight planes, dual playfield, PF1 priority, PF2 offset 16, no XOR. */
    if (!ended || (bplcon0 & 0xfe14U) != 0x0610U ||
        (bplcon2 & 0x0040U) != 0U || (bplcon3 & 0x1c00U) != 0x1000U ||
        (bplcon4 & 0xff00U) != 0U) {
        return 0;
    }
    for (index = 0U; index < DEMO_PALETTE_COLORS; ++index) {
        const UWORD expected = miga80_workbench_sunset_rgb12[index & 15U];
        if (high[index] != expected || low[index] != expected) {
            return 0;
        }
    }
    return 1;
}

static int verify_palette_bases(struct ColorMap *color_map)
{
    struct TagItem query[] = {
        {VTAG_PF1_BASE_GET, 0U},
        {VTAG_PF2_BASE_GET, 0U},
        {TAG_DONE, 0U}
    };

    if (color_map == NULL || VideoControl(color_map, query) != 0U) {
        return 0;
    }
    return query[0].ti_Tag == VTAG_PF1_BASE_SET &&
           query[0].ti_Data == 0U &&
           query[1].ti_Tag == VTAG_PF2_BASE_SET &&
           query[1].ti_Data == 16U;
}

static int verify_palette(struct ViewPort *view_port)
{
    ULONG index;

    if (view_port->ColorMap == NULL ||
        view_port->ColorMap->Count < DEMO_PALETTE_COLORS) {
        return 0;
    }
    GetRGB32(view_port->ColorMap, 0U, DEMO_PALETTE_COLORS,
             palette_readback);
    for (index = 0U; index < DEMO_PALETTE_COLORS; ++index) {
        const uint16_t rgb12 = miga80_workbench_sunset_rgb12[index & 15U];

        if ((palette_readback[index * 3U] >> 28) !=
                ((ULONG)(rgb12 >> 8) & 0x0fU) ||
            (palette_readback[1U + (index * 3U)] >> 28) !=
                ((ULONG)(rgb12 >> 4) & 0x0fU) ||
            (palette_readback[2U + (index * 3U)] >> 28) !=
                ((ULONG)rgb12 & 0x0fU)) {
            return 0;
        }
    }
    return 1;
}

static int convert_source_view(struct Screen *screen, const uint8_t *chunky)
{
    struct BitMap *bitmap = screen->RastPort.BitMap;
    uint8_t *planes[MIGA80_C2P_PLANE_COUNT];
    size_t plane;

    if (bitmap == NULL || GetBitMapAttr(bitmap, BMA_DEPTH) !=
                              DEMO_SCREEN_DEPTH) {
        return 0;
    }
    for (plane = 0U; plane < MIGA80_C2P_PLANE_COUNT; ++plane) {
        if (bitmap->Planes[plane] == NULL) {
            return 0;
        }
        planes[plane] = (uint8_t *)bitmap->Planes[plane];
    }
    return miga80_c2p_reference(chunky, DEMO_SCREEN_WIDTH,
                                DEMO_SCREEN_HEIGHT, DEMO_SCREEN_WIDTH, planes,
                                (size_t)bitmap->BytesPerRow) == MIGA80_C2P_OK;
}

static void place_in_playfield_one(uint8_t *chunky)
{
    size_t pixel;

    for (pixel = 0U; pixel < DEMO_CHUNKY_BYTES; ++pixel) {
        chunky[pixel] = (uint8_t)(chunky[pixel] << 4);
    }
}

static int publish_canonical(struct Screen *screen, uint8_t *chunky)
{
    place_in_playfield_one(chunky);
    if (!convert_source_view(screen, chunky)) {
        return 0;
    }
    WaitTOF();
    WaitTOF();
    return 1;
}

/* Interactive UI is canonical colour4, so it can use the selected fast
 * default without the historical eight-plane reference conversion. */
static int publish_ui(struct Screen *screen, uint8_t *chunky)
{
    struct BitMap *bitmap = screen->RastPort.BitMap;
    uint8_t *planes[4];
    unsigned int plane;
    if (bitmap == NULL || GetBitMapAttr(bitmap, BMA_DEPTH) != 8U) { return 0; }
    for (plane = 0U; plane < 4U; ++plane) {
        if (bitmap->Planes[plane * 2U] == NULL ||
            bitmap->Planes[plane * 2U + 1U] == NULL) { return 0; }
        planes[plane] = bitmap->Planes[plane * 2U];
    }
    WaitBlit();
    for (plane = 0U; plane < 4U; ++plane) {
        (void)memset(bitmap->Planes[plane * 2U + 1U], 0,
                     (size_t)bitmap->BytesPerRow * DEMO_SCREEN_HEIGHT);
    }
    if (miga80_c2p4_kalms_color4(chunky, DEMO_SCREEN_WIDTH, DEMO_SCREEN_HEIGHT,
            DEMO_SCREEN_WIDTH, planes, bitmap->BytesPerRow) != MIGA80_C2P4_OK) {
        return 0;
    }
    /* Keep the source readback/checksum representation used by the workflow. */
    place_in_playfield_one(chunky);
    WaitTOF();
    return 1;
}

static UBYTE spread_four_bits(UBYTE value)
{
    value &= 0x0fU;
    return (UBYTE)((value & 0x01U) | ((value & 0x02U) << 1) |
                   ((value & 0x04U) << 2) | ((value & 0x08U) << 3));
}

static int verify_pixel(struct RastPort *rast_port, const uint8_t *chunky,
                        ULONG x, ULONG y)
{
    const ULONG actual = ReadPixel(rast_port, (LONG)x, (LONG)y);
    const UBYTE expected = spread_four_bits(
        (UBYTE)(chunky[(y * DEMO_SCREEN_WIDTH) + x] >> 4));

    return actual != 0xffffffffUL && (UBYTE)actual == expected;
}

static int verify_source_view(struct Screen *screen, const uint8_t *chunky)
{
    return verify_pixel(&screen->RastPort, chunky, 0U, 0U) &&
           verify_pixel(&screen->RastPort, chunky, 3U, 7U) &&
           verify_pixel(&screen->RastPort, chunky, 8U, 12U) &&
           verify_pixel(&screen->RastPort, chunky, 96U, 120U) &&
           verify_pixel(&screen->RastPort, chunky, 4U, 252U);
}

static void draw_interactive_title(uint8_t *chunky)
{
    char title[65];
    (void)snprintf(title, sizeof(title), "%-54.54s [OPEN F2]",
                   FilePart((STRPTR)interactive_source_path));
    miga80_source_view_draw_row(chunky, DEMO_SCREEN_WIDTH, 0U, title, 9U, 2U);
}

static int show_source_status(struct Screen *screen, uint8_t *chunky,
                              const char *status)
{
    struct Miga80SourceViewMetrics scratch;

    if (miga80_source_view_render_with_status(
            chunky, DEMO_SCREEN_WIDTH, source_buffer, text_length(source_buffer),
            status, &scratch) != MIGA80_SOURCE_VIEW_OK) { return 0; }
    if (interactive_source_path != NULL) {
        draw_interactive_title(chunky);
        return publish_ui(screen, chunky);
    }
    return publish_canonical(screen, chunky);
}

static void format_compile_error(char *status, size_t capacity,
                                 const struct miga80_diagnostic *diagnostic)
{
    (void)snprintf(status, capacity, "ERROR L%u:C%u %.40s",
                   diagnostic->line, diagnostic->column,
                   diagnostic->message);
}

static int verify_drawing(struct Screen *screen, struct miga80_host_drawing *drawing)
{
    const struct miga80_draw_surface *surface = miga80_host_drawing_surface(drawing);
    unsigned int x, y;
    if (screen == NULL) {
        return 0;
    }
    for (y = 0U; y < MIGA80_DRAW_HEIGHT; ++y) {
        for (x = 0U; x < MIGA80_DRAW_WIDTH; ++x) {
            const unsigned int pixel = surface->pixels[y * MIGA80_DRAW_WIDTH + x];
            const unsigned int planar = miga80_draw_planar_pixel(surface, x, y);
            unsigned int bit;
            ULONG expected = 0U;
            for (bit = 0U; bit < 4U; ++bit) {
                expected |= ((pixel >> bit) & 1U) << (bit * 2U);
                expected |= ((planar >> bit) & 1U) << (bit * 2U + 1U);
            }
            if (ReadPixel(&screen->RastPort, (LONG)x, (LONG)y) != expected) {
                return 0;
            }
        }
    }
    return 1;
}

struct demo_run_events {
    struct miga80_supervisor_events drawing;
    struct miga80_supervisor_events music;
    struct miga80_supervisor_events input_test;
};

static int service_run_events(void *data, ULONG signals)
{
    struct demo_run_events *events = data;
    if ((signals & events->input_test.signals) != 0U &&
        !events->input_test.service(events->input_test.data, signals)) {
        return 0;
    }
    if ((signals & events->music.signals) != 0U &&
        !events->music.service(events->music.data, signals)) { return 0; }
    return (signals & events->drawing.signals) == 0U ||
           events->drawing.service(events->drawing.data, signals);
}

static int __attribute__((noinline)) compile_and_run_on_current_stack(void)
{
    struct Screen *screen = compiler_request.screen;
    uint8_t *chunky = compiler_request.chunky;
    const struct Miga80SourceViewMetrics *metrics = compiler_request.metrics;
    const char *report_path = compiler_request.report_path;
    char *error_status = compiler_request.error_status;
    const size_t error_capacity = compiler_request.error_capacity;
    struct miga80_ast_function *ast = NULL;
    struct miga80_ir_function *ir = NULL;
    struct miga80_value_function *value_ir = NULL;
    struct miga80_diagnostic diagnostic;
    uint8_t *code = NULL;
    UBYTE *runtime_stack = NULL;
    struct miga80_host_drawing *drawing = NULL;
    struct miga80_host_music *music = NULL;
    struct demo_run_events run_events = {0};
    const ULONG runtime_allocation_bytes = DEMO_RUNTIME_STACK_TOTAL_BYTES +
        (supervisor_enabled ? MIGA80_SUPERVISOR_STACK_TOTAL_BYTES : 0U);
    size_t code_size = 0U;
    size_t required_stack_bytes = 0U;
    uint32_t framebuffer_checksum;
    const char *failure = "compiler_memory";
    int success = 0;

    last_failure = NULL;
    ++compile_attempts;
    last_framebuffer_checksum = 0U;
    last_planar_checksum = 0U;
    last_planar_lines = last_planar_triangles = 0U;
    last_animation_frames = last_animation_elapsed = 0U;
    (void)memset(&last_music, 0, sizeof(last_music));
    (void)memset(&last_c2p, 0, sizeof(last_c2p));
    last_graphics_readback = 0;
    (void)memset(&last_runtime, 0, sizeof(last_runtime));
    error_status[0] = '\0';
    if (!show_source_status(screen, chunky, "COMPILING - PARSE")) {
        failure = "display_compile_parse";
        goto cleanup;
    }
    ast = (struct miga80_ast_function *)AllocMem(
        (ULONG)sizeof(*ast), MEMF_PUBLIC | MEMF_CLEAR);
    ir = (struct miga80_ir_function *)AllocMem(
        (ULONG)sizeof(*ir), MEMF_PUBLIC | MEMF_CLEAR);
    value_ir = (struct miga80_value_function *)AllocMem(
        (ULONG)sizeof(*value_ir), MEMF_PUBLIC | MEMF_CLEAR);
    code = (uint8_t *)AllocMem(DEMO_CODE_CAPACITY,
                              MEMF_PUBLIC | MEMF_CLEAR);
    if (ast == NULL || ir == NULL || value_ir == NULL || code == NULL) {
        goto cleanup;
    }
    if (!miga80_parse_function(source_buffer, metrics->source_bytes, ast,
                               &diagnostic)) {
        failure = "compiler_parse";
        format_compile_error(error_status, error_capacity, &diagnostic);
        goto cleanup;
    }
    if (ast->parameter_count != 0U || ast->result_type != MIGA80_TYPE_VOID) {
        failure = "entry_signature";
        (void)snprintf(error_status, error_capacity,
                       "ERROR - ENTRY MUST HAVE NO ARGS AND RETURN VOID");
        goto cleanup;
    }
    if (!show_source_status(screen, chunky, "COMPILING - LOWER TYPED IR")) {
        failure = "display_compile_lower";
        goto cleanup;
    }
    if (!miga80_lower_function(ast, ir, &diagnostic)) {
        failure = "compiler_lower";
        format_compile_error(error_status, error_capacity, &diagnostic);
        goto cleanup;
    }
    if (!show_source_status(screen, chunky, "COMPILING - OPTIMIZE O1")) {
        failure = "display_compile_optimize";
        goto cleanup;
    }
    if (!miga80_build_value_ir(ir, value_ir, &diagnostic)) {
        failure = "compiler_optimize";
        format_compile_error(error_status, error_capacity, &diagnostic);
        goto cleanup;
    }
    if (!show_source_status(screen, chunky, "COMPILING - ENCODE O1 68020")) {
        failure = "display_compile_encode";
        goto cleanup;
    }
    if (!miga80_encode_m68k_o1_guarded(
            code, DEMO_CODE_CAPACITY, value_ir, &code_size,
            &required_stack_bytes, &diagnostic) ||
        (test_unguarded && !miga80_encode_m68k_o1(
            code, DEMO_CODE_CAPACITY, value_ir, &code_size, &diagnostic))) {
        failure = "compiler_encode";
        format_compile_error(error_status, error_capacity, &diagnostic);
        goto cleanup;
    }
    if (required_stack_bytes > DEMO_RUNTIME_STACK_BYTES) {
        failure = "runtime_stack_budget";
        goto cleanup;
    }
    runtime_stack = (UBYTE *)AllocMem(runtime_allocation_bytes,
                                      MEMF_PUBLIC);
    if (runtime_stack == NULL) {
        failure = "runtime_stack_memory";
        goto cleanup;
    }
    (void)memset(runtime_stack, DEMO_STACK_LOWER_GUARD,
                 DEMO_STACK_GUARD_BYTES);
    (void)memset(runtime_stack + DEMO_STACK_GUARD_BYTES, 0xcd,
                 DEMO_RUNTIME_STACK_BYTES);
    (void)memset(runtime_stack + DEMO_STACK_GUARD_BYTES +
                     DEMO_RUNTIME_STACK_BYTES,
                 DEMO_STACK_UPPER_GUARD, DEMO_STACK_GUARD_BYTES);

    if (!show_source_status(screen, chunky, supervisor_enabled
                ? "RUNNING - ESC STOP" : "RUNNING - BUDGET GUARD")) {
        failure = "display_run_status";
        goto cleanup;
    }

    CacheClearE((APTR)code, (ULONG)code_size, CACRF_ClearI);
    (void)memset(chunky, 0, DEMO_CHUNKY_BYTES);
    drawing = miga80_host_drawing_create(chunky, &last_drawing_context,
                                          &run_events.drawing);
    if (drawing == NULL) {
        failure = "drawing_memory";
        goto cleanup;
    }
    miga80_host_drawing_backend(drawing, c2p_backend);
    if (!miga80_host_music_create(ast, &last_drawing_context, &run_events.music,
                                  &music, error_status, error_capacity)) {
        failure = "music_setup";
        goto cleanup;
    }
    {
        unsigned int value;
        int animated = 0;
        for (value = 0U; value < value_ir->value_count; ++value) {
            if (value_ir->values[value].live &&
                (value_ir->values[value].opcode == MIGA80_VALUE_CALL_FLIP ||
                 value_ir->values[value].opcode == MIGA80_VALUE_CALL_TIME)) { animated = 1; }
        }
        if (animated && !miga80_host_drawing_animate(drawing, screen, &run_events.drawing)) {
            failure = "animation_memory";
            goto cleanup;
        }
    }
    last_drawing_context.sin_handler = (uint32_t)(uintptr_t)miga80_runtime_sin;
    last_drawing_context.cos_handler = (uint32_t)(uintptr_t)miga80_runtime_cos;
    last_drawing_context.time_handler = (uint32_t)(uintptr_t)miga80_runtime_time;
    last_drawing_context.cls_handler = (uint32_t)(uintptr_t)miga80_runtime_cls;
    last_drawing_context.flip_handler = (uint32_t)(uintptr_t)miga80_runtime_flip;
    last_drawing_context.layer_handler = (uint32_t)(uintptr_t)miga80_runtime_layer;
    last_drawing_context.line_start_handler = (uint32_t)(uintptr_t)miga80_runtime_line_start;
    last_drawing_context.line_end_handler = (uint32_t)(uintptr_t)miga80_runtime_line_end;
    last_drawing_context.tri_middle_handler = (uint32_t)(uintptr_t)miga80_runtime_tri_middle;
    last_drawing_context.tri_end_handler = (uint32_t)(uintptr_t)miga80_runtime_tri_end;
    last_runtime.fault_handler = (uint32_t)(uintptr_t)miga80_runtime_fault;
    last_runtime.pset_handler = (uint32_t)(uintptr_t)(test_stalled_service
                                    ? miga80_runtime_test_stall
                                    : force_runtime_fault
                                    ? miga80_runtime_test_fault
                                    : miga80_runtime_draw_pset);
    last_runtime.pixel_buffer = (uint32_t)(uintptr_t)chunky;
    last_runtime.budget = execution_budget;
    last_runtime.stack_top = (uint32_t)(uintptr_t)(
        runtime_stack + DEMO_STACK_GUARD_BYTES + DEMO_RUNTIME_STACK_BYTES);
    (void)memset(&last_supervisor, 0, sizeof(last_supervisor));
    if (supervisor_enabled) {
        struct miga80_supervisor_events events;
        struct miga80_stop_injector *injector = NULL;
        int supervised;

        if (test_escape) {
            injector = miga80_stop_injector_start(&run_events.input_test, test_escape_delay);
            if (injector == NULL) {
                failure = "stop_injector_setup";
                goto cleanup;
            }
        }
        events.signals = run_events.drawing.signals | run_events.input_test.signals | run_events.music.signals;
        events.service = service_run_events;
        events.data = &run_events;
        supervised = miga80_supervise_generated(
            (APTR)code, &last_runtime, active_window->UserPort,
            runtime_stack + DEMO_STACK_GUARD_BYTES,
            runtime_stack + DEMO_RUNTIME_STACK_TOTAL_BYTES, &escape_held,
            &events, &last_supervisor);
        if (injector != NULL &&
            !miga80_stop_injector_finish(injector,
                                          last_supervisor.interrupted)) {
            supervised = 0;
        }
        if (!supervised) {
            failure = "runtime_supervisor";
            goto cleanup;
        }
    } else {
        (void)miga80_execute_generated((APTR)code, (APTR)&last_runtime);
    }
    miga80_host_music_destroy(music, &last_music);
    music = NULL;
    if (!region_has_value(runtime_stack, DEMO_STACK_GUARD_BYTES,
                           DEMO_STACK_LOWER_GUARD) ||
        !region_has_value(runtime_stack + DEMO_STACK_GUARD_BYTES +
                              DEMO_RUNTIME_STACK_BYTES,
                           DEMO_STACK_GUARD_BYTES, DEMO_STACK_UPPER_GUARD)) {
        failure = "runtime_stack_guard";
        goto cleanup;
    }
    if (last_runtime.fault_code != 0U) {
        if (last_runtime.fault_code == MIGA80_ABI_FAULT_USER_STOP) {
            failure = "runtime_stopped";
            (void)snprintf(error_status, error_capacity, "STOPPED BY ESC");
            goto cleanup;
        }
        failure = "runtime_fault";
        (void)snprintf(error_status, error_capacity, "ERROR L%lu:C%lu %s",
                       (unsigned long)last_runtime.fault_line,
                       (unsigned long)last_runtime.fault_column,
                       last_runtime.fault_code ==
                               MIGA80_ABI_FAULT_EXECUTION_BUDGET
                           ? "EXECUTION BUDGET EXHAUSTED"
                           : "CONTROLLED RUNTIME FAULT");
        goto cleanup;
    }
    miga80_host_drawing_flush(drawing);
    miga80_host_drawing_finish(drawing);
    framebuffer_checksum = miga80_source_view_checksum(
        chunky, DEMO_CHUNKY_BYTES);
    last_framebuffer_checksum = framebuffer_checksum;
    if (miga80_source_view_draw_status(
            chunky, DEMO_SCREEN_WIDTH,
            "RUN COMPLETE - ESC SOURCE") != MIGA80_SOURCE_VIEW_OK ||
        !miga80_host_drawing_publish(drawing, screen->RastPort.BitMap)) {
        failure = "display_run_result";
        goto cleanup;
    }
    if (test_graphics) {
        last_graphics_readback = verify_drawing(screen, drawing);
        if (!last_graphics_readback) {
            failure = "drawing_readback";
            goto cleanup;
        }
    }
    if (report_path != NULL &&
        !write_run_report(report_path, metrics, ast, ir, value_ir, code,
                           code_size, framebuffer_checksum)) {
        failure = "write_run_report";
        goto cleanup;
    }
    success = 1;

cleanup:
    miga80_host_music_destroy(music, &last_music);
    if (drawing != NULL) {
        const struct miga80_draw_surface *surface;
        miga80_host_drawing_finish(drawing);
        last_animation_frames = miga80_host_drawing_frames(drawing);
        last_animation_elapsed = miga80_host_drawing_elapsed(drawing);
        miga80_host_drawing_c2p_stats(drawing, &last_c2p);
        surface = miga80_host_drawing_surface(drawing);
        last_planar_checksum = miga80_source_view_checksum(surface->planes[0],
                                                          4U * MIGA80_DRAW_PLANE_BYTES);
        last_planar_lines = miga80_host_drawing_lines(drawing);
        last_planar_triangles = miga80_host_drawing_triangles(drawing);
        miga80_host_drawing_destroy(drawing);
    }
    last_failure = success ? NULL : failure;
    if (!success) {
        if (error_status[0] == '\0') {
            (void)snprintf(error_status, error_capacity, "ERROR - %.48s",
                           failure);
        }
        if (report_path != NULL && strcmp(failure, "runtime_stopped") != 0) {
            (void)write_failure_report(report_path, failure);
        }
    }
    if (runtime_stack != NULL) {
        FreeMem(runtime_stack, runtime_allocation_bytes);
    }
    if (code != NULL) {
        FreeMem(code, DEMO_CODE_CAPACITY);
    }
    if (ir != NULL) {
        FreeMem(ir, (ULONG)sizeof(*ir));
    }
    if (value_ir != NULL) {
        FreeMem(value_ir, (ULONG)sizeof(*value_ir));
    }
    if (ast != NULL) {
        FreeMem(ast, (ULONG)sizeof(*ast));
    }
    return success;
}

static void __attribute__((noinline)) run_compiler_on_dedicated_stack(void)
{
    StackSwap(&compiler_stack_swap);
    /*
     * Keep this worker call argument-free.  GCC may defer popping stacked
     * arguments until the function epilogue; doing that after the second
     * StackSwap would pop them from the restored system stack instead of the
     * dedicated compiler stack and corrupt the caller's return address.
     */
    compiler_run_result = compile_and_run_on_current_stack();
    StackSwap(&compiler_stack_swap);
}

static int compile_and_run(struct Screen *screen, uint8_t *chunky,
                           const struct Miga80SourceViewMetrics *metrics,
                           const char *report_path, char *error_status,
                           size_t error_capacity)
{
    int guards_intact;

    last_failure = NULL;
    (void)memset(&last_runtime, 0, sizeof(last_runtime));
    if ((SysBase->AttnFlags & AFF_68020) == 0U) {
        (void)snprintf(error_status, error_capacity,
                       "ERROR - 68020 CPU REQUIRED");
        last_failure = "compiler_requires_68020";
        (void)write_failure_report(report_path,
                                   "compiler_requires_68020");
        return 0;
    }
    if (!prepare_compiler_stack()) {
        (void)snprintf(error_status, error_capacity,
                       "ERROR - COMPILER STACK MEMORY");
        last_failure = "compiler_stack_memory";
        (void)write_failure_report(report_path,
                                   "compiler_stack_memory");
        return 0;
    }

    compiler_request.screen = screen;
    compiler_request.chunky = chunky;
    compiler_request.metrics = metrics;
    compiler_request.report_path = report_path;
    compiler_request.error_status = error_status;
    compiler_request.error_capacity = error_capacity;
    compiler_run_result = 0;
    run_compiler_on_dedicated_stack();

    guards_intact = compiler_stack_guards_intact();
    release_compiler_stack();
    if (!guards_intact) {
        (void)snprintf(error_status, error_capacity,
                       "ERROR - COMPILER STACK GUARD");
        last_failure = "compiler_stack_guard";
        (void)write_failure_report(report_path,
                                   "compiler_stack_guard");
        return 0;
    }
    if (compiler_run_result && report_path != NULL &&
        !append_stack_restore_report(report_path)) {
        (void)snprintf(error_status, error_capacity,
                       "ERROR - STACK RESTORE REPORT");
        last_failure = "write_stack_restore_report";
        (void)write_failure_report(report_path,
                                   "write_stack_restore_report");
        return 0;
    }
    if (report_path != NULL && last_failure != NULL &&
        strcmp(last_failure, "runtime_stopped") == 0 &&
        !write_stopped_report(report_path)) {
        last_failure = "write_stopped_report";
        (void)snprintf(error_status, error_capacity,
                       "ERROR - WRITE STOP REPORT");
        (void)write_failure_report(report_path, last_failure);
    }
    return compiler_run_result;
}

enum demo_state { DEMO_STATE_SOURCE, DEMO_STATE_RESULT, DEMO_STATE_ERROR };

static int is_quit_key(UWORD code, UWORD qualifiers)
{
    struct InputEvent event = {0};
    char translated[8];

    if ((code & IECODE_UP_PREFIX) != 0U ||
        (qualifiers & IEQUALIFIER_CONTROL) == 0U ||
        (qualifiers & (IEQUALIFIER_REPEAT | IEQUALIFIER_LALT |
                       IEQUALIFIER_RALT | IEQUALIFIER_LCOMMAND |
                       IEQUALIFIER_RCOMMAND)) != 0U) {
        return 0;
    }
    /* Translate the chord with the current keymap, not a physical Q key.
     * This command does not inherit dead-key composition from text input. */
    event.ie_Class = IECLASS_RAWKEY;
    event.ie_Code = code;
    event.ie_Qualifier = qualifiers;
    return MapRawKey(&event, translated, sizeof(translated), NULL) == 1 &&
           translated[0] == 0x11; /* Ctrl-Q */
}

static const struct miga80_supervisor_events *intro_test_events;

static int poll_intro_input(void *data)
{
    struct Window *window = data;
    struct IntuiMessage *message;
    if (intro_test_events != NULL &&
        !intro_test_events->service(intro_test_events->data, intro_test_events->signals)) {
        return MIGA80_INTRO_FAILED;
    }
    while ((message = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL) {
        const ULONG kind = message->Class;
        const UWORD code = message->Code, qualifiers = message->Qualifier;
        ReplyMsg((struct Message *)message);
        if (kind == IDCMP_RAWKEY) {
            if (is_quit_key(code, qualifiers)) { return MIGA80_INTRO_QUIT; }
            if (code == (DEMO_RAWKEY_ESCAPE | 0x80U)) { escape_held = 0; }
            if (code == DEMO_RAWKEY_ESCAPE && (qualifiers & IEQUALIFIER_REPEAT) == 0U) {
                escape_held = 1;
                return MIGA80_INTRO_SKIPPED;
            }
        } else if (kind == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
            return MIGA80_INTRO_SKIPPED;
        }
    }
    return 0;
}

/* Shared by real raw-key input and the on-target workflow regression. */
static int workflow_key(struct Screen *screen, uint8_t *chunky,
                         const struct Miga80SourceViewMetrics *metrics,
                         const char *report_path, enum demo_state *state,
                         UWORD code, UWORD qualifiers)
{
    if (code == (DEMO_RAWKEY_ESCAPE | 0x80U)) {
        escape_held = 0;
    }
    if ((code & 0x80U) != 0U) {
        return 0;
    }
    if (is_quit_key(code, qualifiers)) {
        return 1;
    }
    if (code == DEMO_RAWKEY_F5 && *state == DEMO_STATE_SOURCE) {
        char error_status[MIGA80_SOURCE_VIEW_COLUMNS + 1U];

        if (escape_held) {
            return 0;
        }
        error_status[0] = '\0';
        *state = compile_and_run(screen, chunky, metrics, report_path,
                                  error_status, sizeof(error_status))
                     ? DEMO_STATE_RESULT : DEMO_STATE_ERROR;
        if (last_failure != NULL &&
            strcmp(last_failure, "runtime_stopped") == 0) {
            *state = DEMO_STATE_SOURCE;
            return show_source_status(screen, chunky,
                       "STOPPED - F5 RUN - CTRL-Q EXIT") ? 0 : -1;
        }
        if (*state == DEMO_STATE_ERROR &&
            !show_source_status(screen, chunky, error_status)) {
            return -1;
        }
    } else if (code == DEMO_RAWKEY_ESCAPE) {
        if (escape_held) {
            return 0;
        }
        if (*state == DEMO_STATE_SOURCE) {
            return 0;
        }
        if (!show_source_status(screen, chunky,
                                 "SOURCE READY - F5 RUN - CTRL-Q EXIT")) {
            return -1;
        }
        *state = DEMO_STATE_SOURCE;
    }
    return 0;
}

struct demo_ui {
    struct miga80_file_picker picker;
    struct Miga80SourceViewMetrics metrics;
    enum demo_state state;
    int browsing;
    char source_path[MIGA80_PICKER_PATH_SIZE];
};

static int ui_source(struct demo_ui *ui, struct Screen *screen, uint8_t *chunky)
{
    ui->browsing = 0;
    ui->state = DEMO_STATE_SOURCE;
    return show_source_status(screen, chunky,
                              "SOURCE READY - F2 OPEN - F5 RUN - CTRL-Q EXIT");
}

static int ui_picker(struct demo_ui *ui, struct Screen *screen, uint8_t *chunky)
{
    miga80_file_picker_render(&ui->picker, chunky);
    return publish_ui(screen, chunky);
}

static int ui_load(struct demo_ui *ui, struct Screen *screen, uint8_t *chunky)
{
    char path[MIGA80_PICKER_PATH_SIZE];
    size_t size;
    struct Miga80SourceViewMetrics metrics;
    enum Miga80SourceViewStatus status;
    if (!miga80_file_picker_selected_path(&ui->picker, path) ||
        !read_source(path, pending_source, &size)) {
        (void)strcpy(ui->picker.status, "CANNOT LOAD FILE (READ ERROR OR MORE THAN 4096 BYTES)");
        return ui_picker(ui, screen, chunky);
    }
    status = miga80_source_view_render(chunky, DEMO_SCREEN_WIDTH,
                                      pending_source, size, &metrics);
    if (status != MIGA80_SOURCE_VIEW_OK) {
        const char *reason = status == MIGA80_SOURCE_VIEW_TOO_MANY_LINES
            ? "SOURCE EXCEEDS 30 LINES" : status == MIGA80_SOURCE_VIEW_LINE_TOO_LONG
            ? "SOURCE EXCEEDS 64 COLUMNS" : "SOURCE MUST BE PRINTABLE ASCII WITH LF NEWLINES";
        (void)snprintf(ui->picker.status, sizeof(ui->picker.status), "%s", reason);
        return ui_picker(ui, screen, chunky);
    }
    /* No source or compiler metrics change until the complete file validates. */
    (void)memcpy(source_buffer, pending_source, size + 1U);
    ui->metrics = metrics;
    (void)strcpy(ui->source_path, path);
    return ui_source(ui, screen, chunky);
}

/* Real IDCMP and the on-target selector regression share this dispatch. */
static int ui_event(struct demo_ui *ui, struct Screen *screen, uint8_t *chunky,
    const char *report_path, ULONG message_class, UWORD code, UWORD qualifiers,
    WORD x, WORD y, ULONG seconds, ULONG micros)
{
    enum miga80_picker_action action;
    if (message_class == IDCMP_RAWKEY && is_quit_key(code, qualifiers)) { return 1; }
    if (message_class == IDCMP_RAWKEY && code == (DEMO_RAWKEY_ESCAPE | 0x80U)) {
        escape_held = 0;
    }
    if (ui->browsing) {
        if (message_class == IDCMP_MOUSEBUTTONS && code == SELECTDOWN) {
            action = miga80_file_picker_mouse(&ui->picker, x, y, seconds, micros);
        } else if (message_class == IDCMP_RAWKEY) {
            action = miga80_file_picker_key(&ui->picker, code);
        } else { return 0; }
        if (action == MIGA80_PICKER_LOAD) { return ui_load(ui, screen, chunky) ? 0 : -1; }
        if (action == MIGA80_PICKER_CANCEL) { return ui_source(ui, screen, chunky) ? 0 : -1; }
        if (action == MIGA80_PICKER_REDRAW) { return ui_picker(ui, screen, chunky) ? 0 : -1; }
        return 0;
    }
    if ((ui->state == DEMO_STATE_SOURCE || ui->state == DEMO_STATE_ERROR) &&
        ((message_class == IDCMP_RAWKEY && code == 0x51U &&
          (qualifiers & IEQUALIFIER_REPEAT) == 0U) ||
         (message_class == IDCMP_MOUSEBUTTONS && code == SELECTDOWN &&
          y >= 0 && y < 8 && x >= 220 && x < 256))) {
        ui->browsing = 1;
        (void)miga80_file_picker_scan(&ui->picker, ui->picker.path);
        return ui_picker(ui, screen, chunky) ? 0 : -1;
    }
    if (message_class == IDCMP_RAWKEY) {
        return workflow_key(screen, chunky, &ui->metrics, report_path,
                             &ui->state, code, qualifiers);
    }
    return 0;
}

static int write_browser_ready_report(const char *path)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int written;
    if (output == (BPTR)0) { return 0; }
    written = write_text(output, "miga80_startup_report=1\nbrowser=ready\nresult=pass\n");
    return Close(output) && written;
}

static int run_event_loop(struct Window *window, struct Screen *screen,
    uint8_t *chunky, const struct Miga80SourceViewMetrics *metrics,
    const char *report_path, const char *source_path, int browse)
{
    struct demo_ui ui;
    int result = 0;
    (void)memset(&ui, 0, sizeof(ui));
    miga80_file_picker_init(&ui.picker);
    (void)strcpy(ui.picker.path, "SYS:demos");
    ui.metrics = *metrics;
    (void)snprintf(ui.source_path, sizeof(ui.source_path), "%s", browse ? "(no source loaded)" : source_path);
    interactive_source_path = ui.source_path;
    ui.browsing = browse;
    if (browse) {
        (void)miga80_file_picker_scan(&ui.picker, ui.picker.path);
        if (!ui_picker(&ui, screen, chunky)) { result = -1; }
    } else {
        size_t pixel;
        /* Preserve the startup status, including autoboot compile errors. */
        for (pixel = 0U; pixel < DEMO_CHUNKY_BYTES; ++pixel) { chunky[pixel] >>= 4; }
        draw_interactive_title(chunky);
        if (!publish_ui(screen, chunky)) { result = -1; }
    }
    if (result == 0) {
        /* Keep the intro pointer hidden through the scan and first UI frame. */
        ClearPointer(window);
        if (browse && !write_browser_ready_report(report_path)) { result = -1; }
    }
    while (result == 0) {
        struct IntuiMessage *message;
        WaitPort(window->UserPort);
        while ((message = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL) {
            const ULONG message_class = message->Class;
            const UWORD code = message->Code, qualifiers = message->Qualifier;
            const WORD x = message->MouseX, y = message->MouseY;
            const ULONG seconds = message->Seconds, micros = message->Micros;
            ReplyMsg((struct Message *)message);
            result = ui_event(&ui, screen, chunky, report_path, message_class,
                               code, qualifiers, x, y, seconds, micros);
            if (result != 0) { break; }
        }
    }
    miga80_file_picker_free(&ui.picker);
    interactive_source_path = NULL;
    return result > 0;
}

static const char *browser_failure;

static int browser_find(const struct demo_ui *ui, const char *name)
{
    int i;
    for (i = 0; i < ui->picker.count; ++i) {
        if (strcmp(ui->picker.entries[i].name, name) == 0) { return i; }
    }
    return -1;
}

static int run_browser_regression(struct Screen *screen, uint8_t *chunky,
    const struct Miga80SourceViewMetrics *metrics)
{
    struct demo_ui ui;
    int passed = 0, i, round;
    ULONG baseline = 0U;
    const ULONG signals = FindTask(NULL)->tc_SigAlloc;
    const char *bad[] = {"Broken.lua", "Huge.lua", "Long.lua", "Rows.lua", "Gone.lua"};
    (void)memset(&ui, 0, sizeof(ui));
    miga80_file_picker_init(&ui.picker);
    ui.metrics = *metrics;
    (void)strcpy(ui.source_path, "SYS:demos/default.lua");
    interactive_source_path = ui.source_path;
#define BROWSER_CHECK(label, condition) \
    do { browser_failure = (label); if (!(condition)) { goto done; } } while (0)
#define BROWSER_KEY(code) ui_event(&ui, screen, chunky, NULL, IDCMP_RAWKEY, (code), 0U, 0, 0, 0U, 0U)
#define BROWSER_CLICK(x, y, seconds, micros) \
    ui_event(&ui, screen, chunky, NULL, IDCMP_MOUSEBUTTONS, SELECTDOWN, 0U, (x), (y), (seconds), (micros))
    BROWSER_CHECK("scan_demos", miga80_file_picker_scan(&ui.picker, "SYS:demos") &&
        ui.picker.count == 6 && strcmp(ui.picker.entries[0].name, "cube-chunky.lua") == 0 &&
        strcmp(ui.picker.entries[1].name, "cube-solid-chunky.lua") == 0 &&
        strcmp(ui.picker.entries[2].name, "cube-solid.lua") == 0 &&
        strcmp(ui.picker.entries[3].name, "cube.lua") == 0 &&
        strcmp(ui.picker.entries[4].name, "default.lua") == 0 &&
        strcmp(ui.picker.entries[5].name, "layers.lua") == 0);
    ui.browsing = 1;
    BROWSER_CHECK("render_picker", ui_picker(&ui, screen, chunky) && verify_source_view(screen, chunky));
    BROWSER_CHECK("single_click", BROWSER_CLICK(40, 44, 10U, 0U) == 0 &&
        ui.browsing && ui.picker.selected == 0 && ui.metrics.source_checksum == metrics->source_checksum);
    BROWSER_CHECK("different_row_not_double_click", BROWSER_CLICK(40, 60, 10U, 100000U) == 0 &&
        ui.browsing && ui.picker.selected == 1);
    BROWSER_CHECK("double_click_load", BROWSER_CLICK(40, 60, 10U, 200000U) == 0 &&
        !ui.browsing && strcmp(ui.source_path, "SYS:demos/cube-solid-chunky.lua") == 0 &&
        ui.metrics.source_checksum != metrics->source_checksum && verify_source_view(screen, chunky));
    /* Every shipped demo loads through the real dispatcher; loading never runs it. */
    for (i = 0; i < 6; ++i) {
        BROWSER_CHECK("f2_reopen", BROWSER_KEY(0x51U) == 0 && ui.browsing);
        BROWSER_CHECK("single_select", BROWSER_CLICK(40, (WORD)(44 + i * 16), 20U + (ULONG)i, 0U) == 0 && ui.browsing);
        BROWSER_CHECK("return_or_open_load",
            (i == 0 ? BROWSER_CLICK(140, 235, 25U, 0U) : BROWSER_KEY(0x44U)) == 0 &&
            !ui.browsing && ui.state == DEMO_STATE_SOURCE);
        BROWSER_CHECK("source_bytes", miga80_source_view_checksum(source_buffer, strlen(source_buffer)) == ui.metrics.source_checksum);
    }
    BROWSER_CHECK("loaded_source_f5", BROWSER_KEY(DEMO_RAWKEY_F5) == 0 && ui.state == DEMO_STATE_RESULT &&
        last_runtime.fault_code == 0U && last_planar_lines > 0U);
    BROWSER_CHECK("result_escape", BROWSER_KEY(DEMO_RAWKEY_ESCAPE) == 0 && ui.state == DEMO_STATE_SOURCE &&
        verify_source_view(screen, chunky));
    BROWSER_CHECK("mouse_open_button", BROWSER_CLICK(235, 4, 30U, 0U) == 0 && ui.browsing);
    BROWSER_CHECK("picker_f5_ignored", BROWSER_KEY(DEMO_RAWKEY_F5) == 0 && ui.browsing);
    BROWSER_CHECK("scan_fixtures", miga80_file_picker_scan(&ui.picker, "SYS:picker-test") &&
        ui.picker.count == 20 && ui.picker.entries[0].directory && ui.picker.entries[1].directory &&
        browser_find(&ui, "note.txt") < 0);
    BROWSER_CHECK("next_page", BROWSER_CLICK(80, 235, 40U, 0U) == 0 && ui.picker.first == 12 && ui.picker.selected == -1);
    BROWSER_CHECK("previous_page", BROWSER_CLICK(10, 235, 40U, 100000U) == 0 && ui.picker.first == 0);
    for (i = 0; i < 14; ++i) { BROWSER_CHECK("keyboard_scroll", BROWSER_KEY(0x4dU) == 0); }
    BROWSER_CHECK("keyboard_page_boundary", ui.picker.selected == 13 && ui.picker.first == 12);
    for (i = 0; i < 5; ++i) {
        const uint32_t checksum = ui.metrics.source_checksum;
        ui.picker.selected = browser_find(&ui, bad[i]);
        BROWSER_CHECK("bad_file_found", ui.picker.selected >= 0);
        if (i == 4) { BROWSER_CHECK("delete_test_fixture", DeleteFile("SYS:picker-test/Gone.lua")); }
        BROWSER_CHECK("bad_file_preserves_source", BROWSER_KEY(0x44U) == 0 && ui.browsing &&
            ui.metrics.source_checksum == checksum &&
            miga80_source_view_checksum(source_buffer, strlen(source_buffer)) == checksum &&
            strcmp(ui.source_path, "SYS:demos/layers.lua") == 0);
    }
    BROWSER_CHECK("scan_failure_preserves_directory",
        !miga80_file_picker_scan(&ui.picker, "SYS:missing-directory") &&
        strcmp(ui.picker.path, "SYS:picker-test") == 0 && ui.picker.count == 20);
    ui.picker.selected = browser_find(&ui, "empty");
    BROWSER_CHECK("empty_directory", BROWSER_KEY(0x44U) == 0 && ui.picker.count == 0 && ui.browsing);
    BROWSER_CHECK("empty_click", BROWSER_CLICK(40, 44, 50U, 0U) == 0 && ui.picker.selected == -1 && ui.browsing);
    BROWSER_CHECK("parent", BROWSER_KEY(0x41U) == 0 && strcmp(ui.picker.path, "SYS:picker-test") == 0);
    ui.picker.selected = browser_find(&ui, "nested");
    BROWSER_CHECK("nested_directory", BROWSER_KEY(0x44U) == 0 && ui.picker.count == 1 &&
        strcmp(ui.picker.entries[0].name, "inside.LuA") == 0);
    BROWSER_CHECK("sys_root", BROWSER_CLICK(10, 26, 51U, 0U) == 0 && strcmp(ui.picker.path, "SYS:") == 0);
    BROWSER_CHECK("root_parent", BROWSER_KEY(0x41U) == 0 && strcmp(ui.picker.path, "SYS:") == 0);
    BROWSER_CHECK("cancel_preserves_source", BROWSER_KEY(DEMO_RAWKEY_ESCAPE) == 0 && !ui.browsing &&
        strcmp(ui.source_path, "SYS:demos/layers.lua") == 0 && verify_source_view(screen, chunky));
    for (round = 0; round < 4; ++round) {
        for (i = 0; i < 4; ++i) {
            BROWSER_CHECK("repeat_scan", miga80_file_picker_scan(&ui.picker, "SYS:demos") &&
                miga80_file_picker_scan(&ui.picker, "SYS:picker-test/empty"));
        }
        miga80_file_picker_free(&ui.picker);
        BROWSER_CHECK("memory_no_growth", round == 0 || AvailMem(MEMF_PUBLIC) >= baseline);
        baseline = AvailMem(MEMF_PUBLIC);
    }
    ui.browsing = 1;
    BROWSER_CHECK("ctrl_q_exit", ui_event(&ui, screen, chunky, NULL, IDCMP_RAWKEY,
        0x10U, IEQUALIFIER_CONTROL, 0, 0, 0U, 0U) == 1);
    BROWSER_CHECK("signals_released", FindTask(NULL)->tc_SigAlloc == signals);
    passed = 1;
    browser_failure = NULL;
done:
    miga80_file_picker_free(&ui.picker);
    interactive_source_path = NULL;
#undef BROWSER_CHECK
#undef BROWSER_KEY
#undef BROWSER_CLICK
    return passed;
}

static int write_browser_report(const char *path, int passed)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int written;
    if (output == (BPTR)0) { return 0; }
    written = write_text(output, "miga80_browser_report=1\n") &&
        (passed ? write_text(output,
            "sys_demos_scan_sort=pass\nmouse_select_double_click=pass\n"
            "all_six_demos_load=pass\nloaded_source_f5=pass\nresult_escape=pass\n"
            "f2_mouse_reopen=pass\nfolders_filter_pagination=pass\n"
            "invalid_missing_large_preserve_source=pass\nempty_directory=pass\n"
            "parent_sys_root=pass\ncancel_preserves_source=pass\n"
            "memory_no_growth=pass\nctrl_q_exit=pass\nhosted_cleanup=pass\nresult=pass\n") :
            (write_text(output, "failure=") && write_text(output,
                browser_failure != NULL ? browser_failure : "browser_setup_or_cleanup") &&
             write_text(output, "\nresult=fail\n")));
    return Close(output) && written;
}

static int workflow_case(struct Screen *screen, uint8_t *chunky,
                          const char *source, const char *expected_failure,
                          uint32_t expected_fault)
{
    struct Miga80SourceViewMetrics metrics;
    enum demo_state state = DEMO_STATE_SOURCE;
    uint32_t source_checksum;

    (void)strcpy(source_buffer, source);
    if (miga80_source_view_render(chunky, DEMO_SCREEN_WIDTH, source_buffer,
                                   text_length(source_buffer), &metrics) !=
            MIGA80_SOURCE_VIEW_OK || !publish_canonical(screen, chunky)) {
        return 0;
    }
    source_checksum = miga80_source_view_checksum(chunky, DEMO_CHUNKY_BYTES);
    if (workflow_key(screen, chunky, &metrics, NULL, &state,
                       DEMO_RAWKEY_F5, 0U) != 0 ||
        state != (expected_failure == NULL ? DEMO_STATE_RESULT
                                          : DEMO_STATE_ERROR) ||
        (expected_failure != NULL &&
         (last_failure == NULL ||
          strcmp(last_failure, expected_failure) != 0)) ||
        last_runtime.fault_code != expected_fault ||
        (expected_failure == NULL &&
         last_framebuffer_checksum != UINT32_C(0xc4604fc7)) ||
        (expected_fault != 0U &&
         (last_runtime.fault_line == 0U || last_runtime.fault_column == 0U))) {
        return 0;
    }
    /* F5 in result/error is ignored; key release is ignored as well. */
    if (workflow_key(screen, chunky, &metrics, NULL, &state,
                       DEMO_RAWKEY_F5, 0U) != 0 ||
        workflow_key(screen, chunky, &metrics, NULL, &state,
                       DEMO_RAWKEY_ESCAPE | 0x80U, 0U) != 0 ||
        workflow_key(screen, chunky, &metrics, NULL, &state,
                       DEMO_RAWKEY_ESCAPE, 0U) != 0 ||
        state != DEMO_STATE_SOURCE ||
        miga80_source_view_checksum(chunky, DEMO_CHUNKY_BYTES) !=
            source_checksum || !verify_source_view(screen, chunky)) {
        return 0;
    }
    /* The test ADF boots with the default US keymap (Q=0x10, A=0x20).
     * Esc, plain Q, Ctrl-A, key-up and repeats must not close the editor. */
    return workflow_key(screen, chunky, &metrics, NULL, &state,
                          DEMO_RAWKEY_ESCAPE, 0U) == 0 &&
           workflow_key(screen, chunky, &metrics, NULL, &state, 0x10U, 0U) == 0 &&
           workflow_key(screen, chunky, &metrics, NULL, &state,
                          0x20U, IEQUALIFIER_CONTROL) == 0 &&
           workflow_key(screen, chunky, &metrics, NULL, &state,
                          0x90U, IEQUALIFIER_CONTROL) == 0 &&
           workflow_key(screen, chunky, &metrics, NULL, &state,
                          0x10U, IEQUALIFIER_CONTROL | IEQUALIFIER_REPEAT) == 0 &&
           workflow_key(screen, chunky, &metrics, NULL, &state,
                          0x10U, IEQUALIFIER_CONTROL) == 1;
}

#define DEMO_WORKFLOW_ROUNDS 3U
static const char *workflow_failure;

static int run_graphics_regression(struct Screen *screen, uint8_t *chunky)
{
    unsigned int round;
    ULONG baseline = 0U;
    test_graphics = 1;
    for (round = 0U; round < 3U; ++round) {
        struct Miga80SourceViewMetrics metrics;
        enum demo_state state = DEMO_STATE_SOURCE;
        uint32_t source_checksum;
        const ULONG signals = FindTask(NULL)->tc_SigAlloc;
        workflow_failure = "graphics_source";
        (void)strcpy(source_buffer, draw_test_source);
        if (miga80_source_view_render(chunky, DEMO_SCREEN_WIDTH, source_buffer,
                text_length(source_buffer), &metrics) != MIGA80_SOURCE_VIEW_OK ||
            !publish_canonical(screen, chunky)) {
            return 0;
        }
        source_checksum = miga80_source_view_checksum(chunky, DEMO_CHUNKY_BYTES);
        workflow_failure = "graphics_source_palette";
        if (!verify_display_palette()) {
            return 0;
        }
        workflow_failure = "graphics_native";
        if (workflow_key(screen, chunky, &metrics, NULL, &state,
                           DEMO_RAWKEY_F5, 0U) != 0 || state != DEMO_STATE_RESULT ||
            last_framebuffer_checksum != DRAW_TEST_PIXEL ||
            last_planar_checksum != DRAW_TEST_PLANAR ||
            last_planar_lines != DRAW_TEST_LINES || !last_graphics_readback ||
            last_runtime.fault_code != 0U) {
            return 0;
        }
        workflow_failure = "graphics_result_palette";
        if (!verify_display_palette()) {
            return 0;
        }
        workflow_failure = "graphics_source_return";
        if (workflow_key(screen, chunky, &metrics, NULL, &state,
                           DEMO_RAWKEY_ESCAPE, 0U) != 0 || state != DEMO_STATE_SOURCE ||
            miga80_source_view_checksum(chunky, DEMO_CHUNKY_BYTES) != source_checksum ||
            !verify_source_view(screen, chunky) || !verify_display_palette() ||
            workflow_key(screen, chunky, &metrics, NULL, &state,
                           0x10U, IEQUALIFIER_CONTROL) != 1) {
            return 0;
        }
        workflow_failure = "graphics_resources";
        if (FindTask(NULL)->tc_SigAlloc != signals ||
            (round != 0U && AvailMem(MEMF_PUBLIC) < baseline)) {
            return 0;
        }
        baseline = AvailMem(MEMF_PUBLIC);
    }
    test_graphics = 0;
    workflow_failure = NULL;
    return 1;
}

static int write_graphics_report(const char *path, int passed)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;
    if (output == (BPTR)0) {
        return 0;
    }
    success = write_text(output, "miga80_graphics_report=1\n") &&
        write_text(output, "pixel_checksum=") && write_hex32(output, last_framebuffer_checksum) &&
        write_text(output, "\nplanar_checksum=") && write_hex32(output, last_planar_checksum) &&
        write_text(output, "\nplanar_lines=") && write_decimal(output, last_planar_lines) &&
        (passed ? write_text(output,
            "\nrounds=3\npixel_pf1=pass\nplanar_pf2=pass\n"
            "copper_palette_banks=pass\ncopper_palette_rgb=pass\n"
            "direct_blitter=pass\nclipping_octants_erase=pass\n"
            "source_return=pass\nctrl_q_exit=pass\nresources_released=pass\n"
            "hosted_cleanup=pass\nresult=pass\n")
            : (write_text(output, "\nfailure=") &&
               write_text(output, last_failure != NULL ? last_failure :
                   workflow_failure != NULL ? workflow_failure : "graphics_setup") &&
               write_text(output, "\nresult=fail\n")));
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int run_workflow_regression(struct Screen *screen, uint8_t *chunky)
{
    static const char invalid_source[] =
        "function main(): void\n  local x: i32 =\nend\n";
    static const char infinite_source[] =
        "function main(): void\n  while true do\n    continue\n  end\nend\n";
    static const char fault_source[] =
        "function main(): void\n  pset(0, 0, 1)\nend\n";
    char *default_source;
    ULONG baseline;
    unsigned int round;
    int success = 0;

    workflow_failure = "selftest_memory";
    default_source = (char *)AllocMem(DEMO_SOURCE_CAPACITY + 1U, MEMF_PUBLIC);
    if (default_source == NULL) {
        return 0;
    }
    (void)strcpy(default_source, source_buffer);
    /* One warmup includes first-use library/compiler allocations. */
    workflow_failure = "selftest_warmup";
    if (!workflow_case(screen, chunky, default_source, NULL, 0U)) {
        goto done;
    }
    baseline = AvailMem(MEMF_PUBLIC);
    for (round = 0U; round < DEMO_WORKFLOW_ROUNDS; ++round) {
        workflow_failure = "selftest_compile_error";
        if (!workflow_case(screen, chunky, invalid_source, "compiler_parse",
                            0U)) {
            goto done;
        }
        execution_budget = 8U;
        workflow_failure = "selftest_budget";
        if (!workflow_case(screen, chunky, infinite_source, "runtime_fault",
                            MIGA80_ABI_FAULT_EXECUTION_BUDGET) ||
            last_runtime.budget != 0U) {
            goto done;
        }
        execution_budget = DEMO_EXECUTION_BUDGET;
        force_runtime_fault = 1;
        workflow_failure = "selftest_forced_fault";
        if (!workflow_case(screen, chunky, fault_source, "runtime_fault",
                            MIGA80_ABI_FAULT_DIVISION_BY_ZERO)) {
            goto done;
        }
        force_runtime_fault = 0;
        workflow_failure = "selftest_recovery";
        if (!workflow_case(screen, chunky, default_source, NULL, 0U)) {
            goto done;
        }
        workflow_failure = "selftest_memory_growth";
        if (AvailMem(MEMF_PUBLIC) < baseline) {
            goto done;
        }
    }
    success = 1;
    workflow_failure = NULL;

done:
    execution_budget = DEMO_EXECUTION_BUDGET;
    force_runtime_fault = 0;
    (void)strcpy(source_buffer, default_source);
    FreeMem(default_source, DEMO_SOURCE_CAPACITY + 1U);
    return success;
}

static int write_workflow_report(const char *path, int passed)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;

    if (output == (BPTR)0) {
        return 0;
    }
    success = write_text(output, "miga80_workflow_report=1\n") &&
        (passed
            ? write_text(output,
                "rounds=3\ncompile_run_cycles=13\n"
                "compile_error_recovery=pass\nbudget_recovery=pass\n"
                "forced_fault_recovery=pass\nsource_return=pass\n"
                "ctrl_q_exit=pass\nescape_keeps_source=pass\n"
                "repeated_mandelbrot=pass\nmemory_no_growth=pass\n"
                "hosted_cleanup=pass\nresult=pass\n")
            : (write_text(output, "failure=") &&
               write_text(output, workflow_failure != NULL
                                      ? workflow_failure : "selftest_setup") &&
               write_text(output, "\nresult=fail\n")));
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

/* This exercises the actual input.device -> Intuition -> supervisor path.
 * The unguarded and stalled-service cases cannot reach a compiler stop poll. */
static int stop_case(struct Screen *screen, uint8_t *chunky,
                      const char *source, int unguarded, int stalled_service,
                      const char *report_path)
{
    struct Miga80SourceViewMetrics metrics;
    enum demo_state state = DEMO_STATE_SOURCE;
    struct IntuiMessage *message;
    const unsigned int attempts_before = compile_attempts;
    const ULONG signals_before = FindTask(NULL)->tc_SigAlloc;
    const size_t source_pixels = DEMO_SCREEN_WIDTH * (DEMO_SCREEN_HEIGHT - 8U);
    uint32_t source_checksum;
    int key_result;

    (void)strcpy(source_buffer, source);
    if (miga80_source_view_render(chunky, DEMO_SCREEN_WIDTH, source_buffer,
                                   text_length(source_buffer), &metrics) !=
            MIGA80_SOURCE_VIEW_OK || !publish_canonical(screen, chunky)) {
        return 0;
    }
    source_checksum = miga80_source_view_checksum(chunky, source_pixels);
    test_escape = 1;
    test_unguarded = unguarded;
    test_stalled_service = stalled_service;
    key_result = workflow_key(screen, chunky, &metrics, report_path, &state,
                                DEMO_RAWKEY_F5, 0U);
    test_escape = 0;
    test_unguarded = 0;
    test_stalled_service = 0;
    if (key_result != 0 || state != DEMO_STATE_SOURCE ||
        last_failure == NULL || strcmp(last_failure, "runtime_stopped") != 0 ||
        last_runtime.fault_code != MIGA80_ABI_FAULT_USER_STOP ||
        last_runtime.fault_line != 0U || last_runtime.fault_column != 0U ||
        last_runtime.budget == 0U || !last_supervisor.started ||
        last_supervisor.finished || !last_supervisor.interrupted ||
        !last_supervisor.stack_intact ||
        (unguarded && last_runtime.budget != execution_budget)) {
        return 0;
    }
    while ((message = (struct IntuiMessage *)GetMsg(active_window->UserPort)) !=
            NULL) {
        const ULONG message_class = message->Class;
        const UWORD key = message->Code;
        const UWORD qualifiers = message->Qualifier;

        ReplyMsg((struct Message *)message);
        if (message_class == IDCMP_RAWKEY &&
            workflow_key(screen, chunky, &metrics, NULL, &state,
                           key, qualifiers) != 0) {
            return 0;
        }
    }
    return state == DEMO_STATE_SOURCE && !escape_held &&
        compile_attempts == attempts_before + 1U &&
        FindTask(NULL)->tc_SigAlloc == signals_before &&
        miga80_source_view_checksum(chunky, source_pixels) == source_checksum &&
        verify_source_view(screen, chunky);
}

static int write_stop_progress(const char *path, const char *phase)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;

    if (output == (BPTR)0) {
        return 0;
    }
    success = write_text(output, "miga80_stop_report=1\nphase=") &&
              write_text(output, phase) &&
              write_text(output, "\nresult=running\n");
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int run_stop_regression(struct Screen *screen, uint8_t *chunky,
                                const char *report_path)
{
    static const char infinite_source[] =
        "function main(): void\n  while true do\n    continue\n  end\nend\n";
    static const char planar_source[] =
        "function main(): void\n  layer(PLANAR)\n  while true do\n"
        "    line(0, 0, 255, 255, 15)\n  end\nend\n";
    static const char call_source[] =
        "function main(): void\n  pset(0, 0, 1)\nend\n";
    char *default_source;
    ULONG baseline;
    unsigned int round;
    int success = 0;

    workflow_failure = "stoptest_requires_supervisor";
    if (!supervisor_enabled) {
        return 0;
    }
    workflow_failure = "stoptest_memory";
    default_source = AllocMem(DEMO_SOURCE_CAPACITY + 1U, MEMF_PUBLIC);
    if (default_source == NULL) {
        return 0;
    }
    (void)strcpy(default_source, source_buffer);
    workflow_failure = "stoptest_warmup";
    if (!write_stop_progress(report_path, "warmup") ||
        !stop_case(screen, chunky, infinite_source, 0, 0, report_path)) {
        goto done;
    }
    if (!write_stop_progress(report_path, "repeated_stops")) {
        goto done;
    }
    baseline = AvailMem(MEMF_PUBLIC);
    for (round = 0U; round < 3U; ++round) {
        workflow_failure = "stoptest_guarded_loop";
        if (!stop_case(screen, chunky, infinite_source, 0, 0, NULL)) {
            goto done;
        }
        workflow_failure = "stoptest_unguarded_loop";
        if (!stop_case(screen, chunky, infinite_source, 1, 0, NULL)) {
            goto done;
        }
        workflow_failure = "stoptest_stalled_service";
        if (!stop_case(screen, chunky, call_source, 0, 1, NULL)) {
            goto done;
        }
        workflow_failure = "stoptest_planar_blitter";
        if (!stop_case(screen, chunky, planar_source, 1, 0, NULL) ||
            last_planar_lines == 0U) {
            goto done;
        }
        workflow_failure = "stoptest_memory_growth";
        if (AvailMem(MEMF_PUBLIC) < baseline) {
            goto done;
        }
    }
    workflow_failure = "stoptest_mandelbrot_recovery";
    if (!write_stop_progress(report_path, "mandelbrot_recovery") ||
        !workflow_case(screen, chunky, default_source, NULL, 0U)) {
        goto done;
    }
    success = 1;
    workflow_failure = NULL;
done:
    (void)strcpy(source_buffer, default_source);
    FreeMem(default_source, DEMO_SOURCE_CAPACITY + 1U);
    return success;
}

static int write_stop_report(const char *path, int passed)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;

    if (output == (BPTR)0) {
        return 0;
    }
    success = write_text(output, "miga80_stop_report=1\n") &&
        (passed ? write_text(output,
            "supervision=exec-task\nstopped_runs=13\n"
            "input_device_escape=pass\nguarded_loop=pass\n"
            "unguarded_loop=pass\nstalled_service=pass\nplanar_blitter=pass\n"
            "held_escape=pass\nrun_key_not_replayed=pass\n"
            "source_return=pass\nstopped_report=pass\nsignals_released=pass\n"
            "memory_no_growth=pass\nmandelbrot_recovery=pass\n"
            "hosted_cleanup=pass\nresult=pass\n")
         : (write_text(output, "failure=") &&
            write_text(output, workflow_failure != NULL
                                   ? workflow_failure : "stoptest_setup") &&
            write_text(output, "\nresult=fail\n")));
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int run_cube_regression(struct Screen *screen, uint8_t *chunky,
    const struct Miga80SourceViewMetrics *metrics, int pixel, int solid)
{
    char *source = AllocMem(DEMO_SOURCE_CAPACITY + 1U, MEMF_PUBLIC);
    ULONG baseline = 0U;
    unsigned int round;
    int success = 0;
    if (source == NULL) { return 0; }
    (void)strcpy(source, source_buffer);
    if (solid) {
        workflow_failure = miga80_host_music_selftest();
        if (workflow_failure != NULL) { goto done; }
    }
    test_escape_delay = pixel ? 6000000U : solid ? 2000000U : 500000U;
    /* Stop after several displayed frames, then repeat without resource growth. */
    for (round = 0U; round < 3U; ++round) {
        workflow_failure = "cube_escape";
        if (!stop_case(screen, chunky, source, 1, 0, NULL) ||
            last_animation_frames < 2U) { goto done; }
        if (solid && (last_music.starts != 1U || last_music.ticks < 50U ||
            !miga80_intro_audio_available())) { workflow_failure = "cube_music_escape";goto done; }
        if (round == 0U) { baseline = AvailMem(MEMF_PUBLIC); }
        else if (AvailMem(MEMF_PUBLIC) < baseline) {
            workflow_failure = "cube_stop_memory_growth"; goto done;
        }
    }
    test_graphics = pixel; /* Full PF1/PF2 readback after timing is captured. */
    for (round = 0U; round < 2U; ++round) {
        enum demo_state state = DEMO_STATE_SOURCE;
        workflow_failure = "cube_native";
        if (workflow_key(screen, chunky, metrics, NULL, &state, DEMO_RAWKEY_F5, 0U) != 0 ||
            state != DEMO_STATE_RESULT || last_animation_frames < 2U ||
            last_planar_lines != (pixel || solid ? 0U : last_animation_frames * 12U) ||
            (solid && !pixel && (last_planar_triangles < last_animation_frames * 2U ||
                                last_planar_triangles > last_animation_frames * 6U)) ||
            (pixel && (!last_graphics_readback ||
                last_framebuffer_checksum == UINT32_C(0x5e509dc5) ||
                last_planar_checksum != UINT32_C(0xefb69dc5))) ||
            last_animation_elapsed < 10U * 65536U || last_animation_elapsed >= 11U * 65536U ||
            !verify_display_palette()) { goto done; }
        if (pixel && (last_c2p.calls != last_animation_frames ||
            last_c2p.frequency == 0U || last_c2p.minimum == 0U)) { goto done; }
        if (solid && (last_music.starts != 1U || last_music.ticks < 490U || last_music.ticks > 550U ||
            last_music.position < 80U || last_music.position > 92U || last_music.dma_seen != 15U ||
            !miga80_intro_audio_available())) { workflow_failure = "cube_music_timing";goto done; }
        cube_samples[round].frames = last_animation_frames;
        cube_samples[round].elapsed = last_animation_elapsed;
        cube_samples[round].c2p = last_c2p;
        workflow_failure = "cube_source_return";
        if (workflow_key(screen, chunky, metrics, NULL, &state, DEMO_RAWKEY_ESCAPE, 0U) != 0 ||
            state != DEMO_STATE_SOURCE || !verify_source_view(screen, chunky)) { goto done; }
        workflow_failure = "cube_memory_growth";
        if (AvailMem(MEMF_PUBLIC) < baseline) { goto done; }
        if (round == 1U && workflow_key(screen, chunky, metrics, NULL, &state,
                0x10U, IEQUALIFIER_CONTROL) != 1) { goto done; }
    }
    workflow_failure = NULL;
    success = 1;
done:
    test_graphics = 0;
    test_escape_delay = 100000U;
    FreeMem(source, DEMO_SOURCE_CAPACITY + 1U);
    return success;
}

static int write_cube_report(const char *path, int success, int pixel, int solid)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int written;
    if (output == 0) { return 0; }
    written = write_text(output, "miga80_cube_report=1\n");
    if (pixel && success) {
        unsigned int sample;
        written = written && write_text(output, "c2p_backend=") &&
            write_text(output, miga80_c2p_backend_name(c2p_backend)) && write_text(output, "\n");
        for (sample = 0U; sample < 2U && written; ++sample) {
            char prefix[16];
            const struct miga80_c2p_stats *s = &cube_samples[sample].c2p;
            const char *names[] = {"frames", "elapsed_q16", "c2p_calls", "c2p_ticks",
                "eclock_hz", "c2p_min_ticks", "c2p_max_ticks"};
            const uint64_t values[] = {cube_samples[sample].frames, cube_samples[sample].elapsed,
                s->calls, s->ticks, s->frequency, s->minimum, s->maximum};
            unsigned int field;
            (void)snprintf(prefix, sizeof(prefix), "sample%u_", sample);
            for (field = 0U; field < sizeof(values) / sizeof(values[0]) && written; ++field) {
                written = write_text(output, prefix) && write_text(output, names[field]) &&
                    write_text(output, "=") && write_decimal(output, values[field]) && write_text(output, "\n");
            }
        }
    }
    if (solid) {
        written = written && write_text(output, "music_ticks=") && write_decimal(output, last_music.ticks) &&
            write_text(output, "\nmusic_position=") && write_decimal(output, last_music.position) &&
            write_text(output, "\nmusic_dma_seen=") && write_decimal(output, last_music.dma_seen) &&
            write_text(output, "\nmusic_starts=") && write_decimal(output, last_music.starts) &&
            write_text(output, "\n");
    }
    written = written &&
        write_text(output, "frames=") && write_decimal(output, last_animation_frames) &&
        write_text(output, "\nelapsed_q16=") && write_decimal(output, last_animation_elapsed) &&
        (success ? (write_text(output, "\nlua_native=pass\ndouble_buffer=pass\n") &&
            write_text(output, pixel
                ? "cpu_drawing=pass\npixel_pf1=pass\nplanar_empty=pass\nc2p_readback=pass\n"
                : solid ? "blitter_filled_triangles=pass\n" : "blitter_edges=pass\n") && write_text(output,
            "clock_ten_seconds=pass\nescape_animation=pass\nreplay=pass\nmemory_no_growth=pass\n"
            "source_return=pass\nctrl_q_exit=pass\n"
            "hosted_cleanup=pass\nresult=pass\n")) :
            (write_text(output, "\nfailure=") &&
             write_text(output, last_failure != NULL ? last_failure :
                 workflow_failure != NULL ? workflow_failure : "cube_setup") &&
             write_text(output, "\nresult=fail\n")));
    return Close(output) && written;
}

static struct miga80_intro_stats intro_sample;
static const char *intro_failure;

static int intro_quit_probe(void *data)
{
    unsigned int *polls = data;
    return ++*polls >= 12U ? MIGA80_INTRO_QUIT : 0;
}

static int run_intro_regression(struct Screen *screen, uint8_t *chunky)
{
    struct BitMap *original = screen->RastPort.BitMap;
    struct miga80_intro_stats stats;
    struct miga80_supervisor_events events;
    struct miga80_stop_injector *injector = NULL;
    const ULONG signals = FindTask(NULL)->tc_SigAlloc;
    ULONG baseline = 0U;
    unsigned int round, polls = 0U;
    int result, success = 0;
    for (round = 0U; round < 2U; ++round) {
        intro_failure = "intro_complete";
        result = miga80_intro_run(screen, poll_intro_input, active_window, &stats);
        if (result != MIGA80_INTRO_COMPLETE || stats.frames < 10U ||
            stats.elapsed_q16 < 209715U || stats.elapsed_q16 > 235930U ||
            screen->RastPort.BitMap != original) { goto done; }
        intro_sample = stats;
        intro_failure = "paula_pcm_playback";
        if (stats.audio_mask == 0U || stats.audio_completed != 2U ||
            stats.audio_errors != 0U || !stats.dma_seen || stats.pcm_checksum == 0U ||
            !miga80_intro_audio_available()) { goto done; }
        intro_failure = "intro_memory_growth";
        if (round != 0U && AvailMem(MEMF_PUBLIC) < baseline) { goto done; }
        baseline = AvailMem(MEMF_PUBLIC);
    }
    intro_failure = "intro_escape_setup";
    injector = miga80_stop_injector_start(&events, 100000U);
    if (injector == NULL) { goto done; }
    intro_test_events = &events;
    result = miga80_intro_run(screen, poll_intro_input, active_window, &stats);
    intro_test_events = NULL;
    intro_failure = "intro_escape";
    success = miga80_stop_injector_finish(injector, 1);
    injector = NULL;
    if (!success || result != MIGA80_INTRO_SKIPPED || stats.audio_errors != 0U ||
        screen->RastPort.BitMap != original || !miga80_intro_audio_available()) {
        success = 0; goto done;
    }
    success = 0;
    (void)poll_intro_input(active_window); /* Drain the injector's Escape release. */
    escape_held = 0;
    intro_failure = "intro_quit_cleanup";
    if (miga80_intro_run(screen, intro_quit_probe, &polls, &stats) != MIGA80_INTRO_QUIT ||
        screen->RastPort.BitMap != original || !miga80_intro_audio_available() ||
        FindTask(NULL)->tc_SigAlloc != signals) { goto done; }
    intro_failure = "intro_source_restore";
    prepare_palette();
    LoadRGB32(&screen->ViewPort, amiga_palette);
    if (!show_source_status(screen, chunky, "SOURCE READY - F5 RUN - CTRL-Q EXIT") ||
        !verify_source_view(screen, chunky) || !verify_palette(&screen->ViewPort)) { goto done; }
    intro_failure = NULL;
    success = 1;
done:
    intro_test_events = NULL;
    if (injector != NULL) { (void)miga80_stop_injector_finish(injector, 0); }
    return success;
}

static int write_intro_report(const char *path, int passed)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int written;
    if (output == (BPTR)0) { return 0; }
    written = write_text(output, "miga80_intro_report=1\n") &&
        write_text(output, "pcm_checksum=") && write_hex32(output, intro_sample.pcm_checksum) &&
        (passed ? write_text(output,
            "\ncentered_logo_glitch=pass\nclock_duration=pass\ndouble_buffer=pass\n"
            "paula_dma_stereo=pass\npcm_completed=pass\nrepeat_no_growth=pass\n"
            "escape_input_device=pass\nquit_cleanup=pass\naudio_channels_released=pass\n"
            "source_palette_restore=pass\nhosted_cleanup=pass\nresult=pass\n") :
            (write_text(output, "\nfailure=") && write_text(output,
                intro_failure != NULL ? intro_failure : "intro_setup_or_cleanup") &&
             write_text(output, "\nresult=fail\n")));
    return Close(output) && written;
}

int main(int argc, char **argv)
{
    static struct TagItem video_control[] = {
        {VTAG_PF1_BASE_SET, 0U},
        {VTAG_PF2_BASE_SET, 16U},
        {VTAG_FULLPALETTE_SET, TRUE},
        {TAG_DONE, 0U}
    };
    struct TagItem screen_tags[] = {
        {SA_DisplayID, DEMO_DISPLAY_ID},
        {SA_Width, DEMO_SCREEN_WIDTH},
        {SA_Height, DEMO_SCREEN_HEIGHT},
        {SA_Depth, DEMO_SCREEN_DEPTH},
        {SA_Type, CUSTOMSCREEN},
        {SA_Quiet, TRUE},
        {SA_ShowTitle, FALSE},
        {SA_Draggable, FALSE},
        {SA_Exclusive, TRUE},
        {SA_AutoScroll, FALSE},
        {SA_Interleaved, FALSE},
        {SA_ColorMapEntries, DEMO_PALETTE_COLORS},
        {SA_FullPalette, TRUE},
        {SA_VideoControl, (ULONG)(APTR)video_control},
        {TAG_DONE, 0U}
    };
    struct TagItem window_tags[] = {
        {WA_CustomScreen, 0U},
        {WA_Left, 0U},
        {WA_Top, 0U},
        {WA_Width, DEMO_SCREEN_WIDTH},
        {WA_Height, DEMO_SCREEN_HEIGHT},
        {WA_Backdrop, TRUE},
        {WA_Borderless, TRUE},
        {WA_Activate, TRUE},
        {WA_RMBTrap, TRUE},
        {WA_SimpleRefresh, TRUE},
        {WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS},
        {TAG_DONE, 0U}
    };
    const char *source_path =
        argc > 1 && argv[1][0] != '\0' ? argv[1] : DEMO_DEFAULT_SOURCE;
    const char *report_path =
        argc > 2 && argv[2][0] != '\0' ? argv[2] : DEMO_DEFAULT_REPORT;
    const int browse = argc == 1 || (argc > 3 && strcmp(argv[3], "BROWSE") == 0);
    const int introtest = argc > 3 && strcmp(argv[3], "INTROTEST") == 0;
    const int browsertest = argc > 3 && strcmp(argv[3], "BROWSERTEST") == 0;
    const int solidtest = argc > 3 && (strcmp(argv[3], "SOLIDTEST") == 0 ||
                                           strcmp(argv[3], "SOLIDPIXELTEST") == 0);
    const int cubepixeltest = argc > 3 && (strcmp(argv[3], "CUBEPIXELTEST") == 0 ||
                                         strcmp(argv[3], "SOLIDPIXELTEST") == 0);
    const int cubetest = solidtest || cubepixeltest || (argc > 3 && strcmp(argv[3], "CUBETEST") == 0);
    const int cube = argc > 3 && strcmp(argv[3], "CUBE") == 0;
    const int selftest = argc > 3 && strcmp(argv[3], "SELFTEST") == 0;
    const int graphicstest = argc > 3 && strcmp(argv[3], "GRAPHICSTEST") == 0;
    const int stoptest = argc > 3 && strcmp(argv[3], "STOPTEST") == 0;
    struct DisplayInfo display_info = {0};
    DisplayInfoHandle display_handle;
    struct Miga80SourceViewMetrics metrics;
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    UWORD *blank_pointer = NULL;
    uint8_t *chunky = NULL;
    size_t source_size = 0U;
    ULONG chip_revision;
    const char *failure = NULL;
    int success = 0;
    int argument;
    int splash = !introtest && !browsertest && !cubetest && !selftest &&
        !graphicstest && !stoptest && !(argc > 3 && strcmp(argv[3], "AUTORUN") == 0);

    for (argument = 3; argument < argc; ++argument) {
        if (strcmp(argv[argument], "NOSPLASH") == 0) {
            splash = 0;
        } else if (strcmp(argv[argument], "NOSUPERVISOR") == 0) {
            supervisor_enabled = 0;
        } else if (strcmp(argv[argument], "SUPERVISOR") == 0) {
            supervisor_enabled = 1;
        } else if (strcmp(argv[argument], "C2P=MASK32") == 0) {
            c2p_backend = MIGA80_C2P_MASK32;
        } else if (strcmp(argv[argument], "C2P=KALMS") == 0) {
            c2p_backend = MIGA80_C2P_KALMS;
        } else if (strcmp(argv[argument], "C2P=REFERENCE") == 0) {
            c2p_backend = MIGA80_C2P_REFERENCE;
        } else if (strncmp(argv[argument], "C2P=", 4U) == 0) {
            (void)fprintf(stderr, "Unknown C2P backend: %s\n", argv[argument]);
            return 20;
        }
    }

    (void)write_running_report(report_path);
    if (!browse && !load_source(source_path, &source_size)) {
        failure = "load_default_source";
        goto cleanup;
    }

    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39U);
    if (GfxBase == NULL) {
        failure = "open_graphics_v39";
        goto cleanup;
    }
    IntuitionBase =
        (struct IntuitionBase *)OpenLibrary("intuition.library", 39U);
    if (IntuitionBase == NULL) {
        failure = "open_intuition_v39";
        goto cleanup;
    }
    KeymapBase = OpenLibrary("keymap.library", 36U);
    if (KeymapBase == NULL) {
        failure = "open_keymap_v36";
        goto cleanup;
    }

    chip_revision = GfxBase->ChipRevBits0;
    if ((chip_revision & (GFXF_AA_ALICE | GFXF_AA_LISA)) !=
        (GFXF_AA_ALICE | GFXF_AA_LISA)) {
        chip_revision = SetChipRev(SETCHIPREV_BEST);
    }
    if ((chip_revision & (GFXF_AA_ALICE | GFXF_AA_LISA)) !=
        (GFXF_AA_ALICE | GFXF_AA_LISA)) {
        failure = "aga_chipset";
        goto cleanup;
    }

    display_handle = FindDisplayInfo(DEMO_DISPLAY_ID);
    if (display_handle == NULL ||
        GetDisplayInfoData(display_handle, &display_info,
                           (ULONG)sizeof(display_info), DTAG_DISP,
                           DEMO_DISPLAY_ID) == 0U ||
        display_info.NotAvailable != 0U ||
        (display_info.PropertyFlags & (DIPF_IS_PAL | DIPF_IS_DUALPF)) !=
            (DIPF_IS_PAL | DIPF_IS_DUALPF) ||
        ModeNotAvailable(DEMO_DISPLAY_ID) != 0L) {
        failure = "pal_aga_dual_playfield";
        goto cleanup;
    }

    screen = OpenScreenTagList(NULL, screen_tags);
    if (screen == NULL) {
        failure = "open_source_screen";
        goto cleanup;
    }
    if (!verify_palette_bases(screen->ViewPort.ColorMap)) {
        failure = "source_palette_bases";
        goto cleanup;
    }
    window_tags[0].ti_Data = (ULONG)(APTR)screen;
    window = OpenWindowTagList(NULL, window_tags);
    if (window == NULL || window->UserPort == NULL) {
        failure = "open_source_window";
        goto cleanup;
    }
    active_window = window;
    if (splash) {
        /* One transparent sprite row, plus two leading/trailing control words.
         * Keep its Chip RAM alive until the window and screen are closed. */
        blank_pointer = (UWORD *)AllocMem(6U * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
        if (blank_pointer != NULL) {
            SetPointer(window, blank_pointer, 1, 16, 0, 0);
        } else {
            splash = 0;
        }
    }

    chunky = (uint8_t *)AllocMem((ULONG)DEMO_CHUNKY_BYTES,
                                 MEMF_PUBLIC | MEMF_CLEAR);
    if (chunky == NULL) {
        failure = "alloc_source_framebuffer";
        goto cleanup;
    }
    if (miga80_source_view_render(chunky, DEMO_SCREEN_WIDTH, source_buffer,
                                  source_size, &metrics) !=
        MIGA80_SOURCE_VIEW_OK) {
        failure = "render_source_view";
        goto cleanup;
    }

    /*
     * AGA dual playfield interleaves PF1 on bitplanes 1/3/5/7 and PF2 on
     * bitplanes 2/4/6/8.  Put the source view in PF1 explicitly: its direct
     * 0..15 colour registers are selected by the palette Copper list, while
     * zero remains transparent and reveals the PF2/backdrop colour.
     */
    place_in_playfield_one(chunky);

    if (!install_display_palette(screen)) {
        failure = "source_palette_copper";
        goto cleanup;
    }
    if (splash) {
        struct miga80_intro_stats stats;
        const int intro_result = miga80_intro_run(screen, poll_intro_input, window, &stats);
        if (intro_result == MIGA80_INTRO_QUIT) { success = 1; goto cleanup; }
        /* Allocation failure may skip the intro; it must not prevent opening files. */
    }
    prepare_palette();
    LoadRGB32(&screen->ViewPort, amiga_palette);
    if (!verify_palette(&screen->ViewPort)) {
        failure = "source_palette_roundtrip";
        goto cleanup;
    }
    if (!browse) {
        if (!convert_source_view(screen, chunky)) {
            failure = "source_view_c2p";
            goto cleanup;
        }
        WaitTOF();
        WaitTOF();
        if (blank_pointer != NULL) { ClearPointer(window); }
        if (!verify_source_view(screen, chunky)) {
            failure = "source_view_readback";
            goto cleanup;
        }
        if (!write_success_report(report_path, &metrics)) {
            failure = "write_boot_report";
            goto cleanup;
        }
    }

    success = 1;
    if (introtest) {
        success = run_intro_regression(screen, chunky);
    } else if (browsertest) {
        success = run_browser_regression(screen, chunky, &metrics);
    } else if (cubetest) {
        success = run_cube_regression(screen, chunky, &metrics, cubepixeltest, solidtest);
    } else if (cube) {
        char error_status[MIGA80_SOURCE_VIEW_COLUMNS + 1U];
        const int completed = compile_and_run(screen, chunky, &metrics, report_path,
            error_status, sizeof(error_status));
        success = show_source_status(screen, chunky, completed
            ? "CUBE FINISHED - F5 REPLAY - CTRL-Q EXIT" : error_status) &&
            run_event_loop(window, screen, chunky, &metrics, report_path, source_path,
                           browse);
    } else if (graphicstest) {
        success = run_graphics_regression(screen, chunky);
    } else if (stoptest) {
        success = run_stop_regression(screen, chunky, report_path);
    } else if (selftest) {
        success = run_workflow_regression(screen, chunky);
    } else if (argc > 3 && strcmp(argv[3], "AUTORUN") == 0) {
        char error_status[MIGA80_SOURCE_VIEW_COLUMNS + 1U];

        error_status[0] = '\0';
        if (!compile_and_run(screen, chunky, &metrics, report_path,
                             error_status, sizeof(error_status))) {
            success = 0;
        }
    } else {
        success = run_event_loop(window, screen, chunky, &metrics, report_path, source_path,
                           browse);
    }

cleanup:
    if (chunky != NULL) {
        FreeMem(chunky, (ULONG)DEMO_CHUNKY_BYTES);
    }
    if (window != NULL) {
        CloseWindow(window);
        active_window = NULL;
    }
    if (screen != NULL) {
        if (!CloseScreen(screen)) {
            failure = "close_source_screen";
            workflow_failure = failure;
            success = 0;
        }
    }
    if (blank_pointer != NULL) {
        FreeMem(blank_pointer, 6U * sizeof(UWORD));
    }
    if (KeymapBase != NULL) {
        CloseLibrary(KeymapBase);
        KeymapBase = NULL;
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
    if (GfxBase != NULL) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    if (!success && failure != NULL) {
        (void)write_failure_report(report_path, failure);
    }
    if (selftest && !write_workflow_report(report_path, success)) {
        success = 0;
    }
    if (stoptest && !write_stop_report(report_path, success)) {
        success = 0;
    }
    if (graphicstest && !write_graphics_report(report_path, success)) {
        success = 0;
    }
    if (introtest && !write_intro_report(report_path, success)) { success = 0; }
    if (browsertest && !write_browser_report(report_path, success)) { success = 0; }
    if (cubetest && !write_cube_report(report_path, success, cubepixeltest, solidtest)) { success = 0; }
    return success ? RETURN_OK : RETURN_FAIL;
}
