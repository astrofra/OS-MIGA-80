#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dos/dos.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/displayinfo.h>
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
#include <utility/tagitem.h>

#include "graphics/c2p_reference.h"
#include "compiler/backend_m68k/encoder.h"
#include "compiler/frontend/frontend.h"
#include "compiler/ir/ir.h"
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
#define DEMO_DEFAULT_SOURCE "MIGA80:DATA/DEFAULT.LUA"
#define DEMO_DEFAULT_REPORT "MIGA80:BOOTED.TXT"
#define DEMO_RAWKEY_ESCAPE 0x45U
#define DEMO_RAWKEY_F5 0x54U

struct GfxBase *GfxBase = NULL;
struct IntuitionBase *IntuitionBase = NULL;

static char source_buffer[DEMO_SOURCE_CAPACITY + 1U];
static ULONG amiga_palette[1U + (DEMO_PALETTE_COLORS * 3U) + 1U];
static ULONG palette_readback[DEMO_PALETTE_COLORS * 3U];

extern void miga80_execute_generated(APTR code, APTR runtime_context);
extern void miga80_runtime_pset(void);

static size_t text_length(const char *text)
{
    const char *end = text;

    while (*end != '\0') {
        ++end;
    }
    return (size_t)(end - text);
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

static int write_decimal(BPTR output, size_t value)
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
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;

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
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;

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
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;

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
    const struct miga80_ir_function *ir, const uint8_t *code,
    size_t code_size, uint32_t framebuffer_checksum)
{
    BPTR output = Open((STRPTR)path, MODE_NEWFILE);
    int success;

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
        write_text(output, "\nencoded_bytes=") &&
        write_decimal(output, code_size) &&
        write_text(output, "\nencoded_checksum=") &&
        write_hex32(output,
                    miga80_source_view_checksum(code, code_size)) &&
        write_text(output, "\nframebuffer_checksum=") &&
        write_hex32(output, framebuffer_checksum) &&
        write_text(output,
                   "\ncompiler=typed-ir-direct-o0\n"
                   "palette=workbench-sunset\n"
                   "source_playfield=pf1\n"
                   "native_execution=pass\n"
                   "result=pass\n");
    if (!Close(output)) {
        success = 0;
    }
    return success;
}

static int load_source(const char *path, size_t *source_size)
{
    BPTR input = Open((STRPTR)path, MODE_OLDFILE);
    LONG count;

    if (input == (BPTR)0) {
        return 0;
    }
    count = Read(input, source_buffer, (LONG)sizeof(source_buffer));
    if (!Close(input) || count < 0 || (size_t)count > DEMO_SOURCE_CAPACITY) {
        return 0;
    }
    source_buffer[count] = '\0';
    *source_size = (size_t)count;
    return 1;
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

static int show_source_status(struct Screen *screen, uint8_t *chunky,
                              const char *status)
{
    struct Miga80SourceViewMetrics scratch;

    return miga80_source_view_render_with_status(
               chunky, DEMO_SCREEN_WIDTH, source_buffer,
               text_length(source_buffer), status, &scratch) ==
               MIGA80_SOURCE_VIEW_OK &&
           publish_canonical(screen, chunky);
}

static void format_compile_error(char *status, size_t capacity,
                                 const struct miga80_diagnostic *diagnostic)
{
    (void)snprintf(status, capacity, "ERROR L%u:C%u %.40s",
                   diagnostic->line, diagnostic->column,
                   diagnostic->message);
}

static int compile_and_run(struct Screen *screen, uint8_t *chunky,
                           const struct Miga80SourceViewMetrics *metrics,
                           const char *report_path, char *error_status,
                           size_t error_capacity)
{
    struct miga80_ast_function *ast = NULL;
    struct miga80_ir_function *ir = NULL;
    struct miga80_diagnostic diagnostic;
    uint8_t *code = NULL;
    APTR runtime_context[3];
    size_t code_size = 0U;
    uint32_t framebuffer_checksum;
    const char *failure = "compiler_memory";
    int success = 0;

    if (!show_source_status(screen, chunky, "COMPILING - PARSE")) {
        failure = "display_compile_parse";
        goto cleanup;
    }
    ast = (struct miga80_ast_function *)AllocMem(
        (ULONG)sizeof(*ast), MEMF_PUBLIC | MEMF_CLEAR);
    ir = (struct miga80_ir_function *)AllocMem(
        (ULONG)sizeof(*ir), MEMF_PUBLIC | MEMF_CLEAR);
    code = (uint8_t *)AllocMem(DEMO_CODE_CAPACITY,
                              MEMF_PUBLIC | MEMF_CLEAR);
    if (ast == NULL || ir == NULL || code == NULL) {
        goto cleanup;
    }
    if (!miga80_parse_function(source_buffer, metrics->source_bytes, ast,
                               &diagnostic)) {
        failure = "compiler_parse";
        format_compile_error(error_status, error_capacity, &diagnostic);
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
    if (!show_source_status(screen, chunky, "COMPILING - ENCODE 68020")) {
        failure = "display_compile_encode";
        goto cleanup;
    }
    if (!miga80_encode_m68k_o0(code, DEMO_CODE_CAPACITY, ir, &code_size,
                               &diagnostic)) {
        failure = "compiler_encode";
        format_compile_error(error_status, error_capacity, &diagnostic);
        goto cleanup;
    }

    CacheClearE((APTR)code, (ULONG)code_size, CACRF_ClearI);
    (void)memset(chunky, 0, DEMO_CHUNKY_BYTES);
    runtime_context[0] = NULL;
    runtime_context[1] = (APTR)miga80_runtime_pset;
    runtime_context[2] = (APTR)chunky;
    miga80_execute_generated((APTR)code, (APTR)runtime_context);
    framebuffer_checksum = miga80_source_view_checksum(
        chunky, DEMO_CHUNKY_BYTES);
    if (miga80_source_view_draw_status(
            chunky, DEMO_SCREEN_WIDTH,
            "RUN COMPLETE - ESC SOURCE") != MIGA80_SOURCE_VIEW_OK ||
        !publish_canonical(screen, chunky)) {
        failure = "display_run_result";
        goto cleanup;
    }
    if (!write_run_report(report_path, metrics, ast, ir, code, code_size,
                          framebuffer_checksum)) {
        failure = "write_run_report";
        goto cleanup;
    }
    success = 1;

cleanup:
    if (!success) {
        if (error_status[0] == '\0') {
            (void)snprintf(error_status, error_capacity, "ERROR - %.48s",
                           failure);
        }
        (void)write_failure_report(report_path, failure);
    }
    if (code != NULL) {
        FreeMem(code, DEMO_CODE_CAPACITY);
    }
    if (ir != NULL) {
        FreeMem(ir, (ULONG)sizeof(*ir));
    }
    if (ast != NULL) {
        FreeMem(ast, (ULONG)sizeof(*ast));
    }
    return success;
}

static void run_event_loop(struct Window *window, struct Screen *screen,
                           uint8_t *chunky,
                           const struct Miga80SourceViewMetrics *metrics,
                           const char *report_path)
{
    enum demo_state { DEMO_STATE_SOURCE, DEMO_STATE_RESULT,
                      DEMO_STATE_ERROR } state = DEMO_STATE_SOURCE;
    int finished = 0;

    while (!finished) {
        struct IntuiMessage *message;

        WaitPort(window->UserPort);
        while ((message =
                    (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL) {
            const ULONG message_class = message->Class;
            const UWORD code = message->Code;

            ReplyMsg((struct Message *)message);
            if (message_class != IDCMP_RAWKEY || (code & 0x80U) != 0U) {
                continue;
            }
            if ((code & 0x7fU) == DEMO_RAWKEY_F5 &&
                state == DEMO_STATE_SOURCE) {
                char error_status[MIGA80_SOURCE_VIEW_COLUMNS + 1U];

                error_status[0] = '\0';
                state = compile_and_run(screen, chunky, metrics,
                                        report_path, error_status,
                                        sizeof(error_status))
                            ? DEMO_STATE_RESULT
                            : DEMO_STATE_ERROR;
                if (state == DEMO_STATE_ERROR) {
                    (void)show_source_status(screen, chunky, error_status);
                }
            } else if ((code & 0x7fU) == DEMO_RAWKEY_ESCAPE) {
                if (state == DEMO_STATE_SOURCE) {
                    finished = 1;
                } else {
                    (void)show_source_status(
                        screen, chunky, "SOURCE READY - F5 RUN - ESC EXIT");
                    state = DEMO_STATE_SOURCE;
                }
            }
        }
    }
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
        {WA_IDCMP, IDCMP_RAWKEY},
        {TAG_DONE, 0U}
    };
    const char *source_path =
        argc > 1 && argv[1][0] != '\0' ? argv[1] : DEMO_DEFAULT_SOURCE;
    const char *report_path =
        argc > 2 && argv[2][0] != '\0' ? argv[2] : DEMO_DEFAULT_REPORT;
    struct DisplayInfo display_info = {0};
    DisplayInfoHandle display_handle;
    struct Miga80SourceViewMetrics metrics;
    struct Screen *screen = NULL;
    struct Window *window = NULL;
    uint8_t *chunky = NULL;
    size_t source_size = 0U;
    ULONG chip_revision;
    const char *failure = NULL;
    int success = 0;

    (void)write_running_report(report_path);
    if (!load_source(source_path, &source_size)) {
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
     * 0..15 colour registers are stable, while zero remains transparent and
     * reveals the PF2/backdrop colour.  The earlier low-nibble placement put
     * the editor in PF2 and exposed an emulator-visible PF2 colour-bank
     * mismatch even though ColorMap readback itself succeeded.
     */
    place_in_playfield_one(chunky);

    prepare_palette();
    LoadRGB32(&screen->ViewPort, amiga_palette);
    if (!verify_palette(&screen->ViewPort)) {
        failure = "source_palette_roundtrip";
        goto cleanup;
    }
    if (!convert_source_view(screen, chunky)) {
        failure = "source_view_c2p";
        goto cleanup;
    }
    WaitTOF();
    WaitTOF();
    if (!verify_source_view(screen, chunky)) {
        failure = "source_view_readback";
        goto cleanup;
    }
    if (!write_success_report(report_path, &metrics)) {
        failure = "write_boot_report";
        goto cleanup;
    }

    success = 1;
    if (argc > 3 && strcmp(argv[3], "AUTORUN") == 0) {
        char error_status[MIGA80_SOURCE_VIEW_COLUMNS + 1U];

        error_status[0] = '\0';
        if (!compile_and_run(screen, chunky, &metrics, report_path,
                             error_status, sizeof(error_status))) {
            success = 0;
        }
    } else {
        run_event_loop(window, screen, chunky, &metrics, report_path);
    }

cleanup:
    if (!success && failure != NULL) {
        (void)write_failure_report(report_path, failure);
    }
    if (chunky != NULL) {
        FreeMem(chunky, (ULONG)DEMO_CHUNKY_BYTES);
    }
    if (window != NULL) {
        CloseWindow(window);
    }
    if (screen != NULL) {
        (void)CloseScreen(screen);
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
    if (GfxBase != NULL) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    return success ? RETURN_OK : RETURN_FAIL;
}
