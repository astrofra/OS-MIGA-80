#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/abi/abi.h"
#include "m68k.h"
#include "memory.h"

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))
#define TRACE_CAPACITY 16U
#define DEFAULT_INSTRUCTION_LIMIT 4096U
#define PSET_INSTRUCTION_LIMIT 50000000U
#define MAX_FIXTURE_SIZE \
    ((size_t)(MIGA68K_CODE_END - MIGA68K_CODE_START - 8U))
#define STACK_POISON UINT8_C(0xa5)
#define STACK_GUARD_SIZE 16U
#define TEST_STACK_TOP (MIGA68K_STACK_END - STACK_GUARD_SIZE)
#define RETURN_SENTINEL (MIGA68K_CODE_END - 2U)
#define FAULT_SENTINEL (MIGA68K_CODE_END - 4U)
#define PSET_SENTINEL (MIGA68K_CODE_END - 6U)
#define TEST_RUNTIME_CONTEXT MIGA68K_DATA_START
#define TEST_PIXEL_BUFFER (MIGA68K_DATA_START + UINT32_C(0x100))
#define TEST_PIXEL_WIDTH 256U
#define TEST_PIXEL_HEIGHT 256U
#define TEST_PIXEL_BYTES (TEST_PIXEL_WIDTH * TEST_PIXEL_HEIGHT)

struct trace_buffer {
    uint32_t pcs[TRACE_CAPACITY];
    size_t count;
    size_t next;
};

struct test_case {
    const char *name;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t expected;
};

enum stop_reason {
    STOP_RETURNED,
    STOP_RUNTIME_FAULT,
    STOP_MEMORY_FAULT,
    STOP_BAD_PC,
    STOP_INSTRUCTION_LIMIT
};

/*
 * Reviewed encoding of tests/execute/mul_add.s:
 *
 *   move.l d0,d2
 *   add.l  d0,d0
 *   add.l  d2,d0
 *   add.l  d1,d0
 *   rts
 */
static const uint8_t mul_add_code[] = {
    0x24, 0x00, 0xd0, 0x80, 0xd0, 0x82, 0xd0, 0x81, 0x4e, 0x75
};
static uint8_t loaded_program_code[MAX_FIXTURE_SIZE];
static const uint8_t *active_program_code = mul_add_code;
static size_t active_program_code_size = sizeof(mul_add_code);

/* MOVE.L (A0),D0; RTS -- used to prove that unmapped reads are rejected. */
static const uint8_t invalid_read_code[] = {0x20, 0x10, 0x4e, 0x75};

/* BRA.S * -- used to prove that non-returning code is budget-stopped. */
static const uint8_t infinite_loop_code[] = {0x60, 0xfe};

/* MOVEQ #0,D3; RTS -- proves that ABI 0.6 saved-register damage is caught. */
static const uint8_t saved_register_clobber_code[] = {0x76, 0x00, 0x4e, 0x75};

static struct trace_buffer *active_trace;
static uint32_t active_stack_entry;
static uint32_t active_stack_low;
static unsigned int active_instruction_limit = DEFAULT_INSTRUCTION_LIMIT;
static int active_pset_mode;
static int active_pset_failed;
static uint32_t active_pset_calls;

static m68k_register_t musashi_register(enum miga80_abi_register reg)
{
    if (reg <= MIGA80_ABI_D7) {
        return (m68k_register_t)(M68K_REG_D0 + reg);
    }
    return (m68k_register_t)(M68K_REG_A0 + (reg - MIGA80_ABI_A0));
}

static uint32_t preserved_register_value(enum miga80_abi_register reg)
{
    unsigned int byte;

    if (reg == MIGA80_ABI_RUNTIME_CONTEXT_REGISTER) {
        return TEST_RUNTIME_CONTEXT;
    }
    if (reg <= MIGA80_ABI_D7) {
        byte = 0xd0U + (unsigned int)reg;
    } else {
        byte = 0xa0U + (unsigned int)(reg - MIGA80_ABI_A0);
    }
    return (uint32_t)byte * UINT32_C(0x01010101);
}

static void trace_add(struct trace_buffer *trace, uint32_t pc)
{
    trace->pcs[trace->next] = pc;
    trace->next = (trace->next + 1U) % TRACE_CAPACITY;
    if (trace->count < TRACE_CAPACITY) {
        ++trace->count;
    }
}

static void instruction_hook(unsigned int pc)
{
    const uint32_t stack_pointer = m68k_get_reg(NULL, M68K_REG_A7);

    if (active_trace != NULL) {
        trace_add(active_trace, pc);
        if (stack_pointer < active_stack_low) {
            active_stack_low = stack_pointer;
        }
    }
    m68k_end_timeslice();
}

static void finish_stack_measurement(unsigned int *stack_bytes)
{
    if (stack_bytes != NULL) {
        *stack_bytes = (unsigned int)(active_stack_entry - active_stack_low);
    }
}

static void print_trace(const struct trace_buffer *trace)
{
    const size_t first =
        (trace->next + TRACE_CAPACITY - trace->count) % TRACE_CAPACITY;
    size_t index;

    fprintf(stderr, "Last %lu instruction(s):\n",
            (unsigned long)trace->count);
    for (index = 0; index < trace->count; ++index) {
        char instruction[128];
        const uint32_t pc = trace->pcs[(first + index) % TRACE_CAPACITY];

        (void)m68k_disassemble(instruction, pc, M68K_CPU_TYPE_68EC020);
        fprintf(stderr, "  %06x  %s\n", (unsigned int)pc, instruction);
    }
}

static void print_registers(void)
{
    unsigned int index;

    for (index = 0; index < 8U; ++index) {
        fprintf(stderr, "D%u=%08x%c", index,
                m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + index)),
                index == 7U ? '\n' : ' ');
    }
    for (index = 0; index < 8U; ++index) {
        fprintf(stderr, "A%u=%08x%c", index,
                m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + index)),
                index == 7U ? '\n' : ' ');
    }
    fprintf(stderr, "PC=%08x SR=%04x\n",
            m68k_get_reg(NULL, M68K_REG_PC),
            m68k_get_reg(NULL, M68K_REG_SR));
}

static int preserved_registers_are_valid(void)
{
    enum miga80_abi_register reg;

    for (reg = MIGA80_ABI_D0; reg < MIGA80_ABI_REGISTER_COUNT;
         reg = (enum miga80_abi_register)(reg + 1)) {
        if (miga80_abi_register_is_callee_saved(reg) &&
            m68k_get_reg(NULL, musashi_register(reg)) !=
                preserved_register_value(reg)) {
            return 0;
        }
    }
    return 1;
}

static int load_program_image(const char *path)
{
    FILE *fixture = fopen(path, "rb");
    size_t size;
    int extra_byte;

    if (fixture == NULL) {
        fprintf(stderr, "Unable to open 68k fixture: %s\n", path);
        return 0;
    }
    size = fread(loaded_program_code, 1U, sizeof(loaded_program_code), fixture);
    extra_byte = fgetc(fixture);
    if (ferror(fixture) || size == 0U || extra_byte != EOF) {
        fprintf(stderr, "Invalid or oversized 68k fixture: %s\n", path);
        (void)fclose(fixture);
        return 0;
    }
    if (fclose(fixture) != 0) {
        fprintf(stderr, "Unable to close 68k fixture: %s\n", path);
        return 0;
    }

    active_program_code = loaded_program_code;
    active_program_code_size = size;
    return 1;
}

static int parse_u32(const char *text, uint32_t *value)
{
    char *end;
    long long parsed;

    errno = 0;
    end = NULL;
    parsed = strtoll(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < (long long)INT32_MIN ||
        parsed > (long long)UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static int prepare_program(const char *name, const uint8_t *code,
                           size_t code_size, uint32_t d0, uint32_t d1,
                           uint32_t d2)
{
    enum miga80_abi_register reg;

    miga68k_memory_reset(STACK_POISON);
    if ((TEST_STACK_TOP - 4U) % MIGA80_ABI_STACK_ALIGNMENT != 0U ||
        !miga68k_memory_load_code(MIGA68K_CODE_START, code, code_size) ||
        !miga68k_memory_host_write_u32(MIGA68K_VECTOR_START,
                                      TEST_STACK_TOP - 4U) ||
        !miga68k_memory_host_write_u32(MIGA68K_VECTOR_START + 4U,
                                      MIGA68K_CODE_START) ||
        !miga68k_memory_host_write_u32(TEST_STACK_TOP - 4U,
                                      RETURN_SENTINEL) ||
        !miga68k_memory_host_write_u32(
            TEST_RUNTIME_CONTEXT + MIGA80_ABI_RUNTIME_FAULT_HANDLER_OFFSET,
            FAULT_SENTINEL) ||
        !miga68k_memory_host_write_u32(
            TEST_RUNTIME_CONTEXT + MIGA80_ABI_RUNTIME_PSET_HANDLER_OFFSET,
            PSET_SENTINEL) ||
        !miga68k_memory_host_write_u32(
            TEST_RUNTIME_CONTEXT + MIGA80_ABI_RUNTIME_PIXEL_BUFFER_OFFSET,
            TEST_PIXEL_BUFFER)) {
        fprintf(stderr, "TEST: %s\nUnable to initialize virtual memory.\n",
                name);
        return 0;
    }

    m68k_set_cpu_type(M68K_CPU_TYPE_68EC020);
    m68k_pulse_reset();
    (void)m68k_execute(1);
    m68k_set_reg(M68K_REG_D0, d0);
    m68k_set_reg(M68K_REG_D1, d1);
    m68k_set_reg(M68K_REG_D2, d2);
    for (reg = MIGA80_ABI_D0; reg < MIGA80_ABI_REGISTER_COUNT;
         reg = (enum miga80_abi_register)(reg + 1)) {
        if (miga80_abi_register_is_callee_saved(reg)) {
            m68k_set_reg(musashi_register(reg),
                         preserved_register_value(reg));
        }
    }
    return 1;
}

static enum stop_reason execute_program(struct trace_buffer *trace,
                                        unsigned int *instruction_count,
                                        unsigned int *stack_bytes)
{
    active_trace = trace;
    active_stack_entry = m68k_get_reg(NULL, M68K_REG_A7);
    active_stack_low = active_stack_entry;
    *instruction_count = 0;
    while (*instruction_count < active_instruction_limit) {
        const uint32_t pc = m68k_get_reg(NULL, M68K_REG_PC);

        if (pc == RETURN_SENTINEL) {
            finish_stack_measurement(stack_bytes);
            return STOP_RETURNED;
        }
        if (pc == FAULT_SENTINEL) {
            finish_stack_measurement(stack_bytes);
            return STOP_RUNTIME_FAULT;
        }
        if (pc == PSET_SENTINEL && active_pset_mode) {
            const uint32_t stack_pointer =
                m68k_get_reg(NULL, M68K_REG_A7);
            const uint32_t x = m68k_get_reg(NULL, M68K_REG_D0);
            const uint32_t y = m68k_get_reg(NULL, M68K_REG_D1);
            const uint32_t color = m68k_get_reg(NULL, M68K_REG_D2);
            uint32_t return_address;

            if (x >= TEST_PIXEL_WIDTH || y >= TEST_PIXEL_HEIGHT ||
                color > 15U ||
                !miga68k_memory_host_read_u32(stack_pointer,
                                               &return_address) ||
                !miga68k_memory_host_write_u8(
                    TEST_PIXEL_BUFFER + y * TEST_PIXEL_WIDTH + x,
                    (uint8_t)color)) {
                active_pset_failed = 1;
                finish_stack_measurement(stack_bytes);
                return STOP_MEMORY_FAULT;
            }
            ++active_pset_calls;
            m68k_set_reg(M68K_REG_D0, UINT32_C(0xdead0000));
            m68k_set_reg(M68K_REG_D1, UINT32_C(0xdead0001));
            m68k_set_reg(M68K_REG_D2, UINT32_C(0xdead0002));
            m68k_set_reg(M68K_REG_A0, UINT32_C(0xdead0008));
            m68k_set_reg(M68K_REG_A1, UINT32_C(0xdead0009));
            m68k_set_reg(M68K_REG_A7, stack_pointer + 4U);
            m68k_set_reg(M68K_REG_PC, return_address);
            continue;
        }
        if (!miga68k_memory_is_executable(pc)) {
            finish_stack_measurement(stack_bytes);
            return STOP_BAD_PC;
        }

        (void)m68k_execute(1000000);
        ++*instruction_count;
        if (miga68k_memory_has_fault()) {
            finish_stack_measurement(stack_bytes);
            return STOP_MEMORY_FAULT;
        }
    }
    finish_stack_measurement(stack_bytes);
    return STOP_INSTRUCTION_LIMIT;
}

static int run_case(const struct test_case *test,
                    unsigned int *executed_instructions,
                    unsigned int *maximum_stack_bytes)
{
    struct trace_buffer trace;
    unsigned int instruction_count;
    unsigned int stack_bytes = 0U;
    enum stop_reason stop;
    int passed = 1;
    const char *failure = NULL;

    (void)memset(&trace, 0, sizeof(trace));
    if (!prepare_program(test->name, active_program_code,
                         active_program_code_size, test->a, test->b,
                         test->c)) {
        active_trace = NULL;
        return 0;
    }

    stop = execute_program(&trace, &instruction_count, &stack_bytes);
    if (executed_instructions != NULL) {
        *executed_instructions = instruction_count;
    }
    if (maximum_stack_bytes != NULL) {
        *maximum_stack_bytes = stack_bytes;
    }
    if (stop == STOP_INSTRUCTION_LIMIT) {
        failure = "instruction limit reached";
    } else if (stop == STOP_RUNTIME_FAULT) {
        failure = "unexpected controlled runtime fault";
    } else if (stop == STOP_BAD_PC) {
        failure = "execution left the code segment";
    } else if (stop == STOP_MEMORY_FAULT) {
        failure = miga68k_memory_fault();
    } else if (failure == NULL &&
               m68k_get_reg(NULL, M68K_REG_D0) != test->expected) {
        failure = "unexpected return value";
    } else if (failure == NULL &&
               m68k_get_reg(NULL, M68K_REG_A7) != TEST_STACK_TOP) {
        failure = "unbalanced stack";
    } else if (failure == NULL && !preserved_registers_are_valid()) {
        failure = "callee-saved register was modified";
    } else if (failure == NULL &&
               (!miga68k_memory_range_is(MIGA68K_STACK_START,
                                         STACK_GUARD_SIZE, STACK_POISON) ||
                !miga68k_memory_range_is(TEST_STACK_TOP, STACK_GUARD_SIZE,
                                         STACK_POISON))) {
        failure = "stack guard was modified";
    }

    if (failure != NULL) {
        fprintf(stderr,
                "TEST: %s\nRESULT: %s; expected D0=%08x, got %08x\n"
                "INSTRUCTIONS: %u\nSTACK BYTES: %u\n",
                test->name, failure, (unsigned int)test->expected,
                m68k_get_reg(NULL, M68K_REG_D0), instruction_count,
                stack_bytes);
        print_registers();
        print_trace(&trace);
        passed = 0;
    }
    active_trace = NULL;
    return passed;
}

static int run_fault_case(const struct test_case *test,
                          uint32_t expected_fault,
                          uint32_t expected_line,
                          uint32_t expected_column,
                          unsigned int *executed_instructions,
                          unsigned int *maximum_stack_bytes)
{
    struct trace_buffer trace;
    unsigned int instruction_count;
    unsigned int stack_bytes = 0U;
    enum stop_reason stop;
    int passed;

    (void)memset(&trace, 0, sizeof(trace));
    if (!prepare_program(test->name, active_program_code,
                         active_program_code_size, test->a, test->b,
                         test->c)) {
        active_trace = NULL;
        return 0;
    }
    stop = execute_program(&trace, &instruction_count, &stack_bytes);
    if (executed_instructions != NULL) {
        *executed_instructions = instruction_count;
    }
    if (maximum_stack_bytes != NULL) {
        *maximum_stack_bytes = stack_bytes;
    }
    passed = stop == STOP_RUNTIME_FAULT &&
             m68k_get_reg(NULL, M68K_REG_D0) == expected_fault &&
             m68k_get_reg(NULL, M68K_REG_D1) == expected_line &&
             m68k_get_reg(NULL, M68K_REG_D2) == expected_column &&
             m68k_get_reg(NULL, M68K_REG_A5) == TEST_RUNTIME_CONTEXT &&
             miga68k_memory_range_is(MIGA68K_STACK_START,
                                     STACK_GUARD_SIZE, STACK_POISON) &&
             miga68k_memory_range_is(TEST_STACK_TOP, STACK_GUARD_SIZE,
                                     STACK_POISON);
    if (!passed) {
        fprintf(stderr,
                "TEST: %s\nRESULT: expected controlled fault %u at %u:%u, "
                "got stop=%u D0=%08x D1=%08x D2=%08x\n"
                "INSTRUCTIONS: %u\nSTACK BYTES: %u\n",
                test->name, (unsigned int)expected_fault,
                (unsigned int)expected_line, (unsigned int)expected_column,
                (unsigned int)stop, m68k_get_reg(NULL, M68K_REG_D0),
                m68k_get_reg(NULL, M68K_REG_D1),
                m68k_get_reg(NULL, M68K_REG_D2), instruction_count,
                stack_bytes);
        print_registers();
        print_trace(&trace);
    }
    active_trace = NULL;
    return passed;
}

static int run_memory_guard_test(void)
{
    struct trace_buffer trace;
    unsigned int instruction_count;
    enum stop_reason stop;

    (void)memset(&trace, 0, sizeof(trace));
    if (!prepare_program("guard/invalid_read", invalid_read_code,
                         sizeof(invalid_read_code), 0U, 0U, 0U)) {
        return 0;
    }
    m68k_set_reg(M68K_REG_A0, MIGA68K_STACK_END);
    stop = execute_program(&trace, &instruction_count, NULL);
    active_trace = NULL;
    if (stop == STOP_MEMORY_FAULT && miga68k_memory_has_fault()) {
        return 1;
    }

    fprintf(stderr,
            "TEST: guard/invalid_read\nRESULT: unmapped read was not rejected\n"
            "INSTRUCTIONS: %u\n",
            instruction_count);
    print_registers();
    print_trace(&trace);
    return 0;
}

static int run_instruction_limit_test(void)
{
    struct trace_buffer trace;
    unsigned int instruction_count;
    enum stop_reason stop;

    (void)memset(&trace, 0, sizeof(trace));
    if (!prepare_program("guard/instruction_limit", infinite_loop_code,
                         sizeof(infinite_loop_code), 0U, 0U, 0U)) {
        return 0;
    }
    stop = execute_program(&trace, &instruction_count, NULL);
    active_trace = NULL;
    if (stop == STOP_INSTRUCTION_LIMIT &&
        instruction_count == DEFAULT_INSTRUCTION_LIMIT) {
        return 1;
    }

    fprintf(stderr,
            "TEST: guard/instruction_limit\n"
            "RESULT: non-returning code escaped its budget\n"
            "INSTRUCTIONS: %u\n",
            instruction_count);
    print_registers();
    print_trace(&trace);
    return 0;
}

static int run_pset_program(uint32_t expected_checksum,
                            uint32_t expected_calls,
                            unsigned int *executed_instructions,
                            unsigned int *maximum_stack_bytes)
{
    struct trace_buffer trace;
    unsigned int instruction_count;
    unsigned int stack_bytes = 0U;
    enum stop_reason stop;
    uint32_t checksum;
    int passed;

    (void)memset(&trace, 0, sizeof(trace));
    if (!prepare_program("mandelbrot/direct-encoder", active_program_code,
                         active_program_code_size, 0U, 0U, 0U) ||
        !miga68k_memory_host_fill(TEST_PIXEL_BUFFER, TEST_PIXEL_BYTES, 0U)) {
        return 0;
    }
    active_pset_mode = 1;
    active_pset_failed = 0;
    active_pset_calls = 0U;
    active_instruction_limit = PSET_INSTRUCTION_LIMIT;
    stop = execute_program(&trace, &instruction_count, &stack_bytes);
    checksum = miga68k_memory_host_checksum(TEST_PIXEL_BUFFER,
                                             TEST_PIXEL_BYTES);
    passed = stop == STOP_RETURNED && !active_pset_failed &&
             active_pset_calls == expected_calls &&
             checksum == expected_checksum &&
             m68k_get_reg(NULL, M68K_REG_A7) == TEST_STACK_TOP &&
             preserved_registers_are_valid() &&
             miga68k_memory_range_is(MIGA68K_STACK_START,
                                     STACK_GUARD_SIZE, STACK_POISON) &&
             miga68k_memory_range_is(TEST_STACK_TOP, STACK_GUARD_SIZE,
                                     STACK_POISON);
    if (executed_instructions != NULL) {
        *executed_instructions = instruction_count;
    }
    if (maximum_stack_bytes != NULL) {
        *maximum_stack_bytes = stack_bytes;
    }
    active_pset_mode = 0;
    active_instruction_limit = DEFAULT_INSTRUCTION_LIMIT;
    active_trace = NULL;
    if (!passed) {
        fprintf(stderr,
                "TEST: mandelbrot/direct-encoder\n"
                "RESULT: stop=%u pset_failed=%d calls=%u/%u "
                "checksum=%08x/%08x instructions=%u stack_bytes=%u\n",
                (unsigned int)stop, active_pset_failed,
                (unsigned int)active_pset_calls, (unsigned int)expected_calls,
                (unsigned int)checksum, (unsigned int)expected_checksum,
                instruction_count, stack_bytes);
        print_registers();
        print_trace(&trace);
    }
    return passed;
}

static int run_saved_register_guard_test(void)
{
    struct trace_buffer trace;
    unsigned int instruction_count;
    enum stop_reason stop;

    (void)memset(&trace, 0, sizeof(trace));
    if (!prepare_program("guard/saved_register", saved_register_clobber_code,
                         sizeof(saved_register_clobber_code), 0U, 0U, 0U)) {
        return 0;
    }
    stop = execute_program(&trace, &instruction_count, NULL);
    active_trace = NULL;
    if (stop == STOP_RETURNED && !preserved_registers_are_valid()) {
        return 1;
    }

    fprintf(stderr,
            "TEST: guard/saved_register\n"
            "RESULT: ABI 0.6 saved-register damage was not detected\n"
            "INSTRUCTIONS: %u\n",
            instruction_count);
    print_registers();
    print_trace(&trace);
    return 0;
}

int main(int argc, char **argv)
{
    static const struct test_case cases[] = {
        {"zero", 0U, 0U, 0U, 0U},
        {"positive", 7U, 5U, 0U, 26U},
        {"negative_a", UINT32_C(0xfffffffc), 1U, 0U,
         UINT32_C(0xfffffff5)},
        {"negative_b", 3U, UINT32_C(0xfffffff9), 0U, 2U},
        {"wrap_u32", UINT32_C(0x7fffffff), 4U, 0U,
         UINT32_C(0x80000001)}
    };
    struct test_case command_line_case;
    size_t index;

    if (argc == 5 && strcmp(argv[1], "--pset") == 0) {
        uint32_t expected_checksum;
        uint32_t expected_calls;
        unsigned int instruction_count;
        unsigned int stack_bytes;

        if (!load_program_image(argv[2]) ||
            !parse_u32(argv[3], &expected_checksum) ||
            !parse_u32(argv[4], &expected_calls)) {
            fprintf(stderr, "Invalid --pset arguments.\n");
            return 2;
        }
        m68k_init();
        m68k_set_instr_hook_callback(instruction_hook);
        if (!run_pset_program(expected_checksum, expected_calls,
                              &instruction_count, &stack_bytes)) {
            return 1;
        }
        printf("PASS  mandelbrot native framebuffer=%08x "
               "pset_calls=%u instructions=%u code_bytes=%lu "
               "stack_bytes=%u\n",
               (unsigned int)expected_checksum,
               (unsigned int)expected_calls, instruction_count,
               (unsigned long)active_program_code_size, stack_bytes);
        return 0;
    }

    if (argc == 8 && strcmp(argv[1], "--case") == 0) {
        unsigned int instruction_count;
        unsigned int stack_bytes;

        command_line_case.name = argv[3];
        if (!load_program_image(argv[2]) ||
            !parse_u32(argv[4], &command_line_case.a) ||
            !parse_u32(argv[5], &command_line_case.b) ||
            !parse_u32(argv[6], &command_line_case.c) ||
            !parse_u32(argv[7], &command_line_case.expected)) {
            fprintf(stderr, "Invalid --case arguments.\n");
            return 2;
        }
        m68k_init();
        m68k_set_instr_hook_callback(instruction_hook);
        if (!run_case(&command_line_case, &instruction_count,
                      &stack_bytes)) {
            return 1;
        }
        printf("PASS  %s D0=%08x instructions=%u code_bytes=%lu "
               "stack_bytes=%u\n",
               command_line_case.name,
               (unsigned int)command_line_case.expected, instruction_count,
               (unsigned long)active_program_code_size, stack_bytes);
        return 0;
    }

    if (argc == 10 && strcmp(argv[1], "--fault-case") == 0) {
        unsigned int instruction_count;
        unsigned int stack_bytes;
        uint32_t expected_fault;
        uint32_t expected_line;
        uint32_t expected_column;

        command_line_case.name = argv[3];
        if (!load_program_image(argv[2]) ||
            !parse_u32(argv[4], &command_line_case.a) ||
            !parse_u32(argv[5], &command_line_case.b) ||
            !parse_u32(argv[6], &command_line_case.c) ||
            !parse_u32(argv[7], &expected_fault) ||
            !parse_u32(argv[8], &expected_line) ||
            !parse_u32(argv[9], &expected_column)) {
            fprintf(stderr, "Invalid --fault-case arguments.\n");
            return 2;
        }
        m68k_init();
        m68k_set_instr_hook_callback(instruction_hook);
        if (!run_fault_case(&command_line_case, expected_fault,
                            expected_line, expected_column,
                            &instruction_count, &stack_bytes)) {
            return 1;
        }
        printf("PASS  %s fault=%u source=%u:%u instructions=%u "
               "code_bytes=%lu stack_bytes=%u\n",
               command_line_case.name, (unsigned int)expected_fault,
               (unsigned int)expected_line, (unsigned int)expected_column,
               instruction_count, (unsigned long)active_program_code_size,
               stack_bytes);
        return 0;
    }

    if (argc > 2 ||
        (argc == 2 && (strcmp(argv[1], "--case") == 0 ||
                       strcmp(argv[1], "--fault-case") == 0))) {
        fprintf(stderr,
                "Usage: %s [mul_add.bin]\n"
                "       %s --case image.bin name D0 D1 D2 expected\n"
                "       %s --fault-case image.bin name D0 D1 D2 "
                "fault line column\n"
                "       %s --pset image.bin framebuffer-checksum calls\n",
                argv[0], argv[0], argv[0], argv[0]);
        return 2;
    }
    if (argc == 2 && !load_program_image(argv[1])) {
        return 2;
    }

    m68k_init();
    m68k_set_instr_hook_callback(instruction_hook);
    for (index = 0; index < ARRAY_COUNT(cases); ++index) {
        if (!run_case(&cases[index], NULL, NULL)) {
            return 1;
        }
    }
    if (!run_memory_guard_test() || !run_instruction_limit_test() ||
        !run_saved_register_guard_test()) {
        return 1;
    }

    printf("PASS  miga68k-test Musashi 68EC020 mul_add (%lu cases; "
           "3 guard cases; ABI %u.%u)\n",
           (unsigned long)ARRAY_COUNT(cases), MIGA80_ABI_VERSION_MAJOR,
           MIGA80_ABI_VERSION_MINOR);
    return 0;
}
