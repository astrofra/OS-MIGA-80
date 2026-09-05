#ifndef MIGA80_COMPILER_FRONTEND_H
#define MIGA80_COMPILER_FRONTEND_H

#include <stddef.h>
#include <stdint.h>
#include <limits.h>

#include "compiler/abi/abi.h"

#define MIGA80_MAX_NAME 31U
#define MIGA80_MAX_PARAMETERS MIGA80_ABI_MAX_ARGUMENTS
#define MIGA80_MAX_LOCALS 16U
#define MIGA80_MAX_STATEMENTS 32U
#define MIGA80_MAX_AST_NODES 128U
#define MIGA80_MAX_POOL_ENTRIES 32U
#define MIGA80_MAX_POOL_BYTES 1024U
#define MIGA80_MAX_INTRINSIC_ARGUMENTS 3U
#define MIGA80_INVALID_NODE (-1)
#define MIGA80_INVALID_STATEMENT UINT_MAX

enum miga80_type {
    MIGA80_TYPE_I32,
    MIGA80_TYPE_BOOL,
    MIGA80_TYPE_I8,
    MIGA80_TYPE_U8,
    MIGA80_TYPE_I16,
    MIGA80_TYPE_U16,
    MIGA80_TYPE_FIX,
    MIGA80_TYPE_STRING,
    MIGA80_TYPE_SYMBOL,
    MIGA80_TYPE_VOID,
    MIGA80_TYPE_NONE
};

struct miga80_pool_entry {
    enum miga80_type type;
    uint16_t offset;
    uint16_t length;
};

struct miga80_constant_pool {
    struct miga80_pool_entry entries[MIGA80_MAX_POOL_ENTRIES];
    unsigned char bytes[MIGA80_MAX_POOL_BYTES];
    uint16_t entry_count;
    uint16_t bytes_used;
};

struct miga80_diagnostic {
    unsigned int line;
    unsigned int column;
    char message[160];
};

enum miga80_ast_kind {
    MIGA80_AST_LITERAL_I32,
    MIGA80_AST_LITERAL_FIX,
    MIGA80_AST_LITERAL_BOOL,
    MIGA80_AST_LITERAL_STRING,
    MIGA80_AST_LITERAL_SYMBOL,
    MIGA80_AST_PARAMETER_I32,
    MIGA80_AST_PARAMETER_BOOL,
    MIGA80_AST_LOCAL_I32,
    MIGA80_AST_LOCAL_BOOL,
    MIGA80_AST_NEG_I32,
    MIGA80_AST_ADD_I32,
    MIGA80_AST_SUB_I32,
    MIGA80_AST_MUL_I32,
    MIGA80_AST_MUL_FIX,
    MIGA80_AST_DIV_FIX,
    MIGA80_AST_DIV_I32,
    MIGA80_AST_DIV_U32,
    MIGA80_AST_FIX_FROM_I32,
    MIGA80_AST_I32_FROM_FIX,
    MIGA80_AST_EQ,
    MIGA80_AST_NE,
    MIGA80_AST_LT_I32,
    MIGA80_AST_LE_I32,
    MIGA80_AST_GT_I32,
    MIGA80_AST_GE_I32,
    MIGA80_AST_LT_U32,
    MIGA80_AST_LE_U32,
    MIGA80_AST_GT_U32,
    MIGA80_AST_GE_U32,
    MIGA80_AST_NORMALIZE_INTEGER
};

struct miga80_ast_node {
    enum miga80_ast_kind kind;
    unsigned int line;
    unsigned int column;
    int left;
    int right;
    uint32_t value;
    unsigned int symbol_index;
    enum miga80_type type;
};

enum miga80_ast_statement_kind {
    MIGA80_AST_LOCAL_INITIALIZE,
    MIGA80_AST_LOCAL_ASSIGN,
    MIGA80_AST_IF,
    MIGA80_AST_WHILE,
    MIGA80_AST_BREAK,
    MIGA80_AST_CONTINUE,
    MIGA80_AST_CALL_PSET,
    MIGA80_AST_RETURN
};

struct miga80_ast_statement {
    enum miga80_ast_statement_kind kind;
    unsigned int local_index;
    int expression;
    unsigned int line;
    unsigned int column;
    unsigned int next_statement;
    unsigned int then_statement;
    unsigned int else_statement;
    int arguments[MIGA80_MAX_INTRINSIC_ARGUMENTS];
    unsigned int argument_count;
};

struct miga80_ast_function {
    char name[MIGA80_MAX_NAME + 1U];
    char parameter_names[MIGA80_MAX_PARAMETERS][MIGA80_MAX_NAME + 1U];
    enum miga80_type parameter_types[MIGA80_MAX_PARAMETERS];
    unsigned int parameter_count;
    char local_names[MIGA80_MAX_LOCALS][MIGA80_MAX_NAME + 1U];
    enum miga80_type local_types[MIGA80_MAX_LOCALS];
    unsigned int local_count;
    struct miga80_ast_node nodes[MIGA80_MAX_AST_NODES];
    unsigned int node_count;
    struct miga80_ast_statement statements[MIGA80_MAX_STATEMENTS];
    unsigned int statement_count;
    unsigned int first_statement;
    enum miga80_type result_type;
    int result;
    struct miga80_constant_pool pool;
};

int miga80_parse_function(const char *source, size_t source_size,
                          struct miga80_ast_function *function,
                          struct miga80_diagnostic *diagnostic);
int miga80_divide_i32(uint32_t dividend, uint32_t divisor,
                      uint32_t *quotient);
int miga80_parse_fix_literal(const char *text, size_t length,
                             uint32_t *value);
uint32_t miga80_multiply_fix(uint32_t left, uint32_t right);
int miga80_divide_fix(uint32_t dividend, uint32_t divisor,
                      uint32_t *quotient);
int miga80_convert_i32_to_fix(uint32_t input, uint32_t *result);
uint32_t miga80_convert_fix_to_i32(uint32_t input);
const char *miga80_type_name(enum miga80_type type);
int miga80_type_is_integer(enum miga80_type type);
int miga80_type_is_signed_integer(enum miga80_type type);
int miga80_type_is_numeric(enum miga80_type type);
int miga80_type_is_signed_numeric(enum miga80_type type);
int miga80_type_is_scalar(enum miga80_type type);
int miga80_type_is_address(enum miga80_type type);
int miga80_type_is_value(enum miga80_type type);
uint32_t miga80_normalize_integer(enum miga80_type type, uint32_t value);
int miga80_integer_value_is_canonical(enum miga80_type type,
                                      uint32_t value);
int miga80_validate_constant_pool(const struct miga80_constant_pool *pool);
const unsigned char *miga80_pool_entry_bytes(
    const struct miga80_constant_pool *pool, unsigned int entry_index);
uint32_t miga80_pool_symbol_id(const struct miga80_constant_pool *pool,
                               unsigned int entry_index);

#endif
