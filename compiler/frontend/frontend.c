#include "compiler/frontend/frontend.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum token_kind {
    TOKEN_EOF,
    TOKEN_INVALID,
    TOKEN_IDENTIFIER,
    TOKEN_INTEGER,
    TOKEN_FIX_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_FUNCTION,
    TOKEN_LOCAL,
    TOKEN_IF,
    TOKEN_THEN,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_DO,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_RETURN,
    TOKEN_END,
    TOKEN_I32,
    TOKEN_BOOL,
    TOKEN_I8,
    TOKEN_U8,
    TOKEN_I16,
    TOKEN_U16,
    TOKEN_FIX,
    TOKEN_STRING,
    TOKEN_SYMBOL,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    TOKEN_COLON,
    TOKEN_COMMA,
    TOKEN_EQUAL,
    TOKEN_EQUAL_EQUAL,
    TOKEN_NOT_EQUAL,
    TOKEN_LESS,
    TOKEN_LESS_EQUAL,
    TOKEN_GREATER,
    TOKEN_GREATER_EQUAL,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_SLASH_EQUAL
};

struct token {
    enum token_kind kind;
    const char *start;
    size_t length;
    unsigned int line;
    unsigned int column;
    uint32_t integer;
};

struct lexer {
    const char *source;
    size_t size;
    size_t offset;
    unsigned int line;
    unsigned int column;
    struct miga80_diagnostic *diagnostic;
};

struct parser {
    struct lexer lexer;
    struct token current;
    struct miga80_ast_function *function;
    struct miga80_diagnostic *diagnostic;
    unsigned int loop_depth;
    int failed;
};

static void set_diagnostic(struct miga80_diagnostic *diagnostic,
                           unsigned int line, unsigned int column,
                           const char *format, ...)
{
    va_list arguments;

    if (diagnostic->message[0] != '\0') {
        return;
    }
    diagnostic->line = line;
    diagnostic->column = column;
    va_start(arguments, format);
    (void)vsnprintf(diagnostic->message, sizeof(diagnostic->message), format,
                    arguments);
    va_end(arguments);
}

static int identifier_start(char character)
{
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') || character == '_';
}

static int identifier_continue(char character)
{
    return identifier_start(character) ||
           (character >= '0' && character <= '9');
}

static char peek(const struct lexer *lexer, size_t distance)
{
    if (lexer->offset + distance >= lexer->size) {
        return '\0';
    }
    return lexer->source[lexer->offset + distance];
}

static char advance(struct lexer *lexer)
{
    const char character = lexer->source[lexer->offset++];

    if (character == '\n') {
        ++lexer->line;
        lexer->column = 1U;
    } else {
        ++lexer->column;
    }
    return character;
}

static void skip_trivia(struct lexer *lexer)
{
    for (;;) {
        const char character = peek(lexer, 0U);

        if (character == ' ' || character == '\t' || character == '\r' ||
            character == '\n') {
            (void)advance(lexer);
        } else if (character == '-' && peek(lexer, 1U) == '-') {
            while (peek(lexer, 0U) != '\0' && peek(lexer, 0U) != '\n') {
                (void)advance(lexer);
            }
        } else {
            return;
        }
    }
}

static int token_is_word(const struct token *token, const char *word)
{
    const size_t length = strlen(word);

    return token->length == length &&
           memcmp(token->start, word, length) == 0;
}

static struct token next_token(struct lexer *lexer)
{
    struct token token;
    char character;

    skip_trivia(lexer);
    (void)memset(&token, 0, sizeof(token));
    token.start = &lexer->source[lexer->offset];
    token.line = lexer->line;
    token.column = lexer->column;
    if (lexer->offset == lexer->size) {
        token.kind = TOKEN_EOF;
        return token;
    }

    character = advance(lexer);
    token.length = 1U;
    if (identifier_start(character)) {
        while (identifier_continue(peek(lexer, 0U))) {
            (void)advance(lexer);
        }
        token.length = (size_t)(&lexer->source[lexer->offset] - token.start);
        token.kind = TOKEN_IDENTIFIER;
        if (token_is_word(&token, "function")) {
            token.kind = TOKEN_FUNCTION;
        } else if (token_is_word(&token, "local")) {
            token.kind = TOKEN_LOCAL;
        } else if (token_is_word(&token, "if")) {
            token.kind = TOKEN_IF;
        } else if (token_is_word(&token, "then")) {
            token.kind = TOKEN_THEN;
        } else if (token_is_word(&token, "else")) {
            token.kind = TOKEN_ELSE;
        } else if (token_is_word(&token, "while")) {
            token.kind = TOKEN_WHILE;
        } else if (token_is_word(&token, "do")) {
            token.kind = TOKEN_DO;
        } else if (token_is_word(&token, "break")) {
            token.kind = TOKEN_BREAK;
        } else if (token_is_word(&token, "continue")) {
            token.kind = TOKEN_CONTINUE;
        } else if (token_is_word(&token, "return")) {
            token.kind = TOKEN_RETURN;
        } else if (token_is_word(&token, "end")) {
            token.kind = TOKEN_END;
        } else if (token_is_word(&token, "i32")) {
            token.kind = TOKEN_I32;
        } else if (token_is_word(&token, "bool")) {
            token.kind = TOKEN_BOOL;
        } else if (token_is_word(&token, "i8")) {
            token.kind = TOKEN_I8;
        } else if (token_is_word(&token, "u8")) {
            token.kind = TOKEN_U8;
        } else if (token_is_word(&token, "i16")) {
            token.kind = TOKEN_I16;
        } else if (token_is_word(&token, "u16")) {
            token.kind = TOKEN_U16;
        } else if (token_is_word(&token, "fix")) {
            token.kind = TOKEN_FIX;
        } else if (token_is_word(&token, "string")) {
            token.kind = TOKEN_STRING;
        } else if (token_is_word(&token, "symbol")) {
            token.kind = TOKEN_SYMBOL;
        } else if (token_is_word(&token, "true")) {
            token.kind = TOKEN_TRUE;
        } else if (token_is_word(&token, "false")) {
            token.kind = TOKEN_FALSE;
        }
        return token;
    }

    if (character >= '0' && character <= '9') {
        uint64_t value = (uint64_t)(character - '0');
        size_t offset;

        while (peek(lexer, 0U) >= '0' && peek(lexer, 0U) <= '9') {
            (void)advance(lexer);
        }
        if (peek(lexer, 0U) == '.') {
            (void)advance(lexer);
            if (peek(lexer, 0U) < '0' || peek(lexer, 0U) > '9') {
                set_diagnostic(lexer->diagnostic, token.line, token.column,
                               "fix literal requires digits after '.'");
                token.kind = TOKEN_INVALID;
                return token;
            }
            while (peek(lexer, 0U) >= '0' && peek(lexer, 0U) <= '9') {
                (void)advance(lexer);
            }
            token.length =
                (size_t)(&lexer->source[lexer->offset] - token.start);
            if (!miga80_parse_fix_literal(token.start, token.length,
                                           &token.integer)) {
                set_diagnostic(
                    lexer->diagnostic, token.line, token.column,
                    "fix literal is out of range or exceeds 9 fractional digits");
                token.kind = TOKEN_INVALID;
                return token;
            }
            token.kind = TOKEN_FIX_LITERAL;
            return token;
        }
        token.length =
            (size_t)(&lexer->source[lexer->offset] - token.start);
        for (offset = 1U; offset < token.length; ++offset) {
            const uint64_t digit = (uint64_t)(token.start[offset] - '0');

            if (value > (UINT32_C(2147483647) - digit) / 10U) {
                set_diagnostic(lexer->diagnostic, token.line, token.column,
                               "i32 literal is out of range");
                token.kind = TOKEN_INVALID;
                return token;
            }
            value = value * 10U + digit;
        }
        token.kind = TOKEN_INTEGER;
        token.integer = (uint32_t)value;
        return token;
    }

    if (character == '\'' || character == '"') {
        const char quote = character;

        token.kind = TOKEN_STRING_LITERAL;
        token.start = &lexer->source[lexer->offset];
        for (;;) {
            character = peek(lexer, 0U);
            if (character == '\0' || character == '\n' ||
                character == '\r') {
                token.kind = TOKEN_INVALID;
                set_diagnostic(lexer->diagnostic, token.line, token.column,
                               "unterminated string literal");
                return token;
            }
            if (character == quote) {
                token.length =
                    (size_t)(&lexer->source[lexer->offset] - token.start);
                (void)advance(lexer);
                return token;
            }
            (void)advance(lexer);
            if (character == '\\') {
                character = peek(lexer, 0U);
                if (character == '\0' || character == '\n' ||
                    character == '\r') {
                    token.kind = TOKEN_INVALID;
                    set_diagnostic(lexer->diagnostic, token.line,
                                   token.column,
                                   "unterminated string escape");
                    return token;
                }
                (void)advance(lexer);
            }
        }
    }

    switch (character) {
    case '(':
        token.kind = TOKEN_LEFT_PAREN;
        break;
    case ')':
        token.kind = TOKEN_RIGHT_PAREN;
        break;
    case ':':
        token.kind = TOKEN_COLON;
        break;
    case ',':
        token.kind = TOKEN_COMMA;
        break;
    case '=':
        if (peek(lexer, 0U) == '=') {
            (void)advance(lexer);
            token.length = 2U;
            token.kind = TOKEN_EQUAL_EQUAL;
        } else {
            token.kind = TOKEN_EQUAL;
        }
        break;
    case '~':
        if (peek(lexer, 0U) == '=') {
            (void)advance(lexer);
            token.length = 2U;
            token.kind = TOKEN_NOT_EQUAL;
        } else {
            token.kind = TOKEN_INVALID;
            set_diagnostic(lexer->diagnostic, token.line, token.column,
                           "expected '=' after '~'");
        }
        break;
    case '!':
        if (peek(lexer, 0U) == '=') {
            (void)advance(lexer);
            token.length = 2U;
            token.kind = TOKEN_NOT_EQUAL;
        } else {
            token.kind = TOKEN_INVALID;
            set_diagnostic(lexer->diagnostic, token.line, token.column,
                           "expected '=' after '!'");
        }
        break;
    case '<':
        if (peek(lexer, 0U) == '=') {
            (void)advance(lexer);
            token.length = 2U;
            token.kind = TOKEN_LESS_EQUAL;
        } else {
            token.kind = TOKEN_LESS;
        }
        break;
    case '>':
        if (peek(lexer, 0U) == '=') {
            (void)advance(lexer);
            token.length = 2U;
            token.kind = TOKEN_GREATER_EQUAL;
        } else {
            token.kind = TOKEN_GREATER;
        }
        break;
    case '+':
        token.kind = TOKEN_PLUS;
        break;
    case '-':
        token.kind = TOKEN_MINUS;
        break;
    case '*':
        token.kind = TOKEN_STAR;
        break;
    case '/':
        if (peek(lexer, 0U) == '=') {
            (void)advance(lexer);
            token.length = 2U;
            token.kind = TOKEN_SLASH_EQUAL;
        } else {
            token.kind = TOKEN_SLASH;
        }
        break;
    default:
        token.kind = TOKEN_INVALID;
        set_diagnostic(lexer->diagnostic, token.line, token.column,
                       "unexpected character '%c'", character);
        break;
    }
    return token;
}

static const char *token_name(enum token_kind kind)
{
    switch (kind) {
    case TOKEN_EOF:
        return "end of file";
    case TOKEN_IDENTIFIER:
        return "identifier";
    case TOKEN_INTEGER:
        return "integer";
    case TOKEN_FIX_LITERAL:
        return "fix literal";
    case TOKEN_STRING_LITERAL:
        return "string literal";
    case TOKEN_FUNCTION:
        return "'function'";
    case TOKEN_LOCAL:
        return "'local'";
    case TOKEN_IF:
        return "'if'";
    case TOKEN_THEN:
        return "'then'";
    case TOKEN_ELSE:
        return "'else'";
    case TOKEN_WHILE:
        return "'while'";
    case TOKEN_DO:
        return "'do'";
    case TOKEN_BREAK:
        return "'break'";
    case TOKEN_CONTINUE:
        return "'continue'";
    case TOKEN_RETURN:
        return "'return'";
    case TOKEN_END:
        return "'end'";
    case TOKEN_I32:
        return "'i32'";
    case TOKEN_BOOL:
        return "'bool'";
    case TOKEN_I8:
        return "'i8'";
    case TOKEN_U8:
        return "'u8'";
    case TOKEN_I16:
        return "'i16'";
    case TOKEN_U16:
        return "'u16'";
    case TOKEN_FIX:
        return "'fix'";
    case TOKEN_STRING:
        return "'string'";
    case TOKEN_SYMBOL:
        return "'symbol'";
    case TOKEN_TRUE:
        return "'true'";
    case TOKEN_FALSE:
        return "'false'";
    case TOKEN_LEFT_PAREN:
        return "'('";
    case TOKEN_RIGHT_PAREN:
        return "')'";
    case TOKEN_COLON:
        return "':'";
    case TOKEN_COMMA:
        return "','";
    case TOKEN_EQUAL:
        return "'='";
    case TOKEN_EQUAL_EQUAL:
        return "'=='";
    case TOKEN_NOT_EQUAL:
        return "'~='";
    case TOKEN_LESS:
        return "'<'";
    case TOKEN_LESS_EQUAL:
        return "'<='";
    case TOKEN_GREATER:
        return "'>'";
    case TOKEN_GREATER_EQUAL:
        return "'>='";
    case TOKEN_PLUS:
        return "'+'";
    case TOKEN_MINUS:
        return "'-'";
    case TOKEN_STAR:
        return "'*'";
    case TOKEN_SLASH:
        return "'/'";
    case TOKEN_SLASH_EQUAL:
        return "'/='";
    default:
        return "valid token";
    }
}

static void parser_advance(struct parser *parser)
{
    parser->current = next_token(&parser->lexer);
    if (parser->current.kind == TOKEN_INVALID) {
        parser->failed = 1;
    }
}

static int expect(struct parser *parser, enum token_kind kind)
{
    if (parser->failed) {
        return 0;
    }
    if (parser->current.kind != kind) {
        set_diagnostic(parser->diagnostic, parser->current.line,
                       parser->current.column, "expected %s, found %s",
                       token_name(kind), token_name(parser->current.kind));
        parser->failed = 1;
        return 0;
    }
    parser_advance(parser);
    return 1;
}

static int copy_name(struct parser *parser, char *destination,
                     const struct token *token)
{
    if (token->length > MIGA80_MAX_NAME) {
        set_diagnostic(parser->diagnostic, token->line, token->column,
                       "identifier exceeds %u characters", MIGA80_MAX_NAME);
        parser->failed = 1;
        return 0;
    }
    (void)memcpy(destination, token->start, token->length);
    destination[token->length] = '\0';
    return 1;
}

static int add_node(struct parser *parser, enum miga80_ast_kind kind,
                    unsigned int line, unsigned int column, int left,
                    int right, uint32_t value, unsigned int symbol_index,
                    enum miga80_type type)
{
    struct miga80_ast_node *node;
    const unsigned int index = parser->function->node_count;

    if (index == MIGA80_MAX_AST_NODES) {
        set_diagnostic(parser->diagnostic, line, column,
                       "expression exceeds %u AST nodes",
                       MIGA80_MAX_AST_NODES);
        parser->failed = 1;
        return MIGA80_INVALID_NODE;
    }
    node = &parser->function->nodes[index];
    node->kind = kind;
    node->line = line;
    node->column = column;
    node->left = left;
    node->right = right;
    node->value = value;
    node->symbol_index = symbol_index;
    node->type = type;
    ++parser->function->node_count;
    return (int)index;
}

static int find_parameter(const struct miga80_ast_function *function,
                          const struct token *token)
{
    unsigned int index;

    for (index = 0; index < function->parameter_count; ++index) {
        if (strlen(function->parameter_names[index]) == token->length &&
            memcmp(function->parameter_names[index], token->start,
                   token->length) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static int find_local(const struct miga80_ast_function *function,
                      const struct token *token)
{
    unsigned int index;

    for (index = 0U; index < function->local_count; ++index) {
        if (strlen(function->local_names[index]) == token->length &&
            memcmp(function->local_names[index], token->start,
                   token->length) == 0) {
            return (int)index;
        }
    }
    return -1;
}

static int parse_expression(struct parser *parser);

int miga80_divide_i32(uint32_t dividend, uint32_t divisor,
                      uint32_t *quotient)
{
    uint32_t dividend_magnitude;
    uint32_t divisor_magnitude;
    uint32_t magnitude;
    const int negative = ((dividend ^ divisor) >> 31) != 0U;

    if (divisor == 0U || quotient == NULL) {
        return 0;
    }
    dividend_magnitude = (dividend >> 31) != 0U ? 0U - dividend : dividend;
    divisor_magnitude = (divisor >> 31) != 0U ? 0U - divisor : divisor;
    magnitude = dividend_magnitude / divisor_magnitude;
    *quotient = negative ? 0U - magnitude : magnitude;
    return 1;
}

int miga80_parse_fix_literal(const char *text, size_t length,
                             uint32_t *value)
{
    uint64_t integral = 0U;
    uint64_t fractional = 0U;
    uint64_t denominator = 1U;
    uint64_t magnitude;
    size_t offset = 0U;
    unsigned int fractional_digits = 0U;
    int negative = 0;

    if (text == NULL || value == NULL || length == 0U) {
        return 0;
    }
    if (text[offset] == '-') {
        negative = 1;
        if (++offset == length) {
            return 0;
        }
    }
    if (text[offset] < '0' || text[offset] > '9') {
        return 0;
    }
    while (offset < length && text[offset] >= '0' && text[offset] <= '9') {
        integral = integral * 10U + (uint64_t)(text[offset] - '0');
        if (integral > UINT64_C(32768)) {
            return 0;
        }
        ++offset;
    }
    if (offset == length || text[offset++] != '.') {
        return 0;
    }
    while (offset < length && text[offset] >= '0' && text[offset] <= '9') {
        if (fractional_digits == 9U) {
            return 0;
        }
        fractional = fractional * 10U + (uint64_t)(text[offset] - '0');
        denominator *= 10U;
        ++fractional_digits;
        ++offset;
    }
    if (offset != length || fractional_digits == 0U) {
        return 0;
    }
    magnitude = integral * UINT64_C(65536) +
                (fractional * UINT64_C(65536) + denominator / 2U) /
                    denominator;
    if ((!negative && magnitude > UINT64_C(0x7fffffff)) ||
        (negative && magnitude > UINT64_C(0x80000000))) {
        return 0;
    }
    *value = negative ? 0U - (uint32_t)magnitude : (uint32_t)magnitude;
    return 1;
}

uint32_t miga80_multiply_fix(uint32_t left, uint32_t right)
{
    const int negative = ((left ^ right) >> 31) != 0U;
    const uint32_t left_magnitude =
        (left >> 31) != 0U ? 0U - left : left;
    const uint32_t right_magnitude =
        (right >> 31) != 0U ? 0U - right : right;
    const uint64_t product =
        (uint64_t)left_magnitude * (uint64_t)right_magnitude;
    uint64_t scaled = product >> 16;

    if (negative && (product & UINT64_C(0xffff)) != 0U) {
        ++scaled;
    }
    return negative ? 0U - (uint32_t)scaled : (uint32_t)scaled;
}

int miga80_divide_fix(uint32_t dividend, uint32_t divisor,
                      uint32_t *quotient)
{
    const int negative = ((dividend ^ divisor) >> 31) != 0U;
    const uint32_t dividend_magnitude =
        (dividend >> 31) != 0U ? 0U - dividend : dividend;
    const uint32_t divisor_magnitude =
        (divisor >> 31) != 0U ? 0U - divisor : divisor;
    uint64_t magnitude;

    if (divisor == 0U || quotient == NULL) {
        return 0;
    }
    magnitude = ((uint64_t)dividend_magnitude << 16) /
                (uint64_t)divisor_magnitude;
    *quotient = negative ? 0U - (uint32_t)magnitude
                         : (uint32_t)magnitude;
    return 1;
}

int miga80_convert_i32_to_fix(uint32_t input, uint32_t *result)
{
    if (result == NULL || input + UINT32_C(0x8000) > UINT32_C(0xffff)) {
        return 0;
    }
    *result = input << 16;
    return 1;
}

uint32_t miga80_convert_fix_to_i32(uint32_t input)
{
    const int negative = (input >> 31) != 0U;
    const uint32_t magnitude = negative ? 0U - input : input;
    const uint32_t integral = magnitude >> 16;

    return negative ? 0U - integral : integral;
}

static int constant_i32(const struct miga80_ast_function *function,
                        int node_index, uint32_t *constant)
{
    const struct miga80_ast_node *node;
    uint32_t left;
    uint32_t right;

    if (node_index < 0 || (unsigned int)node_index >= function->node_count ||
        constant == NULL) {
        return 0;
    }
    node = &function->nodes[node_index];
    if (node->kind == MIGA80_AST_LITERAL_I32 ||
        node->kind == MIGA80_AST_LITERAL_FIX) {
        *constant = node->value;
        return 1;
    }
    if (node->kind == MIGA80_AST_NEG_I32) {
        if (!constant_i32(function, node->left, &left)) {
            return 0;
        }
        *constant = 0U - left;
        return 1;
    }
    if (node->kind == MIGA80_AST_NORMALIZE_INTEGER) {
        if (!constant_i32(function, node->left, &left)) {
            return 0;
        }
        *constant = miga80_normalize_integer(node->type, left);
        return 1;
    }
    if (node->kind == MIGA80_AST_FIX_FROM_I32) {
        if (!constant_i32(function, node->left, &left) ||
            !miga80_convert_i32_to_fix(left, constant)) {
            return 0;
        }
        return 1;
    }
    if (node->kind == MIGA80_AST_I32_FROM_FIX) {
        if (!constant_i32(function, node->left, &left)) {
            return 0;
        }
        *constant = miga80_convert_fix_to_i32(left);
        return 1;
    }
    if (node->kind != MIGA80_AST_ADD_I32 &&
        node->kind != MIGA80_AST_SUB_I32 &&
        node->kind != MIGA80_AST_MUL_I32 &&
        node->kind != MIGA80_AST_MUL_FIX &&
        node->kind != MIGA80_AST_DIV_FIX &&
        node->kind != MIGA80_AST_DIV_I32 &&
        node->kind != MIGA80_AST_DIV_U32) {
        return 0;
    }
    if (!constant_i32(function, node->left, &left) ||
        !constant_i32(function, node->right, &right)) {
        return 0;
    }
    if (node->kind == MIGA80_AST_ADD_I32) {
        *constant = left + right;
    } else if (node->kind == MIGA80_AST_SUB_I32) {
        *constant = left - right;
    } else if (node->kind == MIGA80_AST_MUL_I32) {
        *constant = left * right;
    } else if (node->kind == MIGA80_AST_MUL_FIX) {
        *constant = miga80_multiply_fix(left, right);
    } else if (node->kind == MIGA80_AST_DIV_FIX) {
        if (!miga80_divide_fix(left, right, constant)) {
            return 0;
        }
    } else if (node->kind == MIGA80_AST_DIV_I32) {
        if (!miga80_divide_i32(left, right, constant)) {
            return 0;
        }
    } else {
        if (right == 0U) {
            return 0;
        }
        *constant = left / right;
    }
    *constant = miga80_normalize_integer(node->type, *constant);
    return 1;
}

const char *miga80_type_name(enum miga80_type type)
{
    switch (type) {
    case MIGA80_TYPE_I32:
        return "i32";
    case MIGA80_TYPE_BOOL:
        return "bool";
    case MIGA80_TYPE_I8:
        return "i8";
    case MIGA80_TYPE_U8:
        return "u8";
    case MIGA80_TYPE_I16:
        return "i16";
    case MIGA80_TYPE_U16:
        return "u16";
    case MIGA80_TYPE_FIX:
        return "fix";
    case MIGA80_TYPE_STRING:
        return "string";
    case MIGA80_TYPE_SYMBOL:
        return "symbol";
    default:
        return "invalid";
    }
}

int miga80_type_is_integer(enum miga80_type type)
{
    return type == MIGA80_TYPE_I32 || type == MIGA80_TYPE_I8 ||
           type == MIGA80_TYPE_U8 || type == MIGA80_TYPE_I16 ||
           type == MIGA80_TYPE_U16;
}

int miga80_type_is_signed_integer(enum miga80_type type)
{
    return type == MIGA80_TYPE_I32 || type == MIGA80_TYPE_I8 ||
           type == MIGA80_TYPE_I16;
}

int miga80_type_is_numeric(enum miga80_type type)
{
    return type == MIGA80_TYPE_FIX || miga80_type_is_integer(type);
}

int miga80_type_is_signed_numeric(enum miga80_type type)
{
    return type == MIGA80_TYPE_FIX || miga80_type_is_signed_integer(type);
}

int miga80_type_is_scalar(enum miga80_type type)
{
    return type == MIGA80_TYPE_BOOL || type == MIGA80_TYPE_SYMBOL ||
           miga80_type_is_numeric(type);
}

int miga80_type_is_address(enum miga80_type type)
{
    return type == MIGA80_TYPE_STRING;
}

int miga80_type_is_value(enum miga80_type type)
{
    return miga80_type_is_scalar(type) || miga80_type_is_address(type);
}

uint32_t miga80_normalize_integer(enum miga80_type type, uint32_t value)
{
    if (type == MIGA80_TYPE_U8) {
        return value & UINT32_C(0xff);
    }
    if (type == MIGA80_TYPE_I8) {
        value &= UINT32_C(0xff);
        return (value & UINT32_C(0x80)) != 0U
                   ? value | UINT32_C(0xffffff00)
                   : value;
    }
    if (type == MIGA80_TYPE_U16) {
        return value & UINT32_C(0xffff);
    }
    if (type == MIGA80_TYPE_I16) {
        value &= UINT32_C(0xffff);
        return (value & UINT32_C(0x8000)) != 0U
                   ? value | UINT32_C(0xffff0000)
                   : value;
    }
    return value;
}

int miga80_integer_value_is_canonical(enum miga80_type type, uint32_t value)
{
    return miga80_type_is_integer(type) &&
           miga80_normalize_integer(type, value) == value;
}

const unsigned char *miga80_pool_entry_bytes(
    const struct miga80_constant_pool *pool, unsigned int entry_index)
{
    const struct miga80_pool_entry *entry;

    if (pool == NULL || entry_index >= pool->entry_count) {
        return NULL;
    }
    entry = &pool->entries[entry_index];
    if ((unsigned int)entry->offset + (unsigned int)entry->length >
        pool->bytes_used) {
        return NULL;
    }
    return &pool->bytes[entry->offset];
}

uint32_t miga80_pool_symbol_id(const struct miga80_constant_pool *pool,
                               unsigned int entry_index)
{
    uint32_t symbol_id = 0U;
    unsigned int index;

    if (pool == NULL || entry_index >= pool->entry_count ||
        pool->entries[entry_index].type != MIGA80_TYPE_SYMBOL) {
        return 0U;
    }
    for (index = 0U; index <= entry_index; ++index) {
        if (pool->entries[index].type == MIGA80_TYPE_SYMBOL) {
            ++symbol_id;
        }
    }
    return symbol_id;
}

int miga80_validate_constant_pool(const struct miga80_constant_pool *pool)
{
    unsigned int index;

    if (pool == NULL || pool->entry_count > MIGA80_MAX_POOL_ENTRIES ||
        pool->bytes_used > MIGA80_MAX_POOL_BYTES) {
        return 0;
    }
    for (index = 0U; index < pool->entry_count; ++index) {
        const struct miga80_pool_entry *entry = &pool->entries[index];
        const unsigned char *bytes =
            miga80_pool_entry_bytes(pool, index);
        unsigned int previous;

        if ((entry->type != MIGA80_TYPE_STRING &&
             entry->type != MIGA80_TYPE_SYMBOL) ||
            bytes == NULL) {
            return 0;
        }
        for (previous = 0U; previous < index; ++previous) {
            const struct miga80_pool_entry *candidate =
                &pool->entries[previous];

            if (candidate->type == entry->type &&
                candidate->length == entry->length &&
                memcmp(miga80_pool_entry_bytes(pool, previous), bytes,
                       entry->length) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int hex_digit(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static int decode_literal_byte(struct parser *parser,
                               const struct token *token,
                               unsigned int *source_offset,
                               unsigned int *byte)
{
    *byte = (unsigned char)token->start[*source_offset];
    if (*byte != (unsigned int)'\\') {
        return 1;
    }
    if (++*source_offset == token->length) {
        set_diagnostic(parser->diagnostic, token->line, token->column,
                       "unterminated string escape");
        parser->failed = 1;
        return 0;
    }
    {
        const char escape = token->start[*source_offset];

        if (escape == 'n') {
            *byte = (unsigned int)'\n';
        } else if (escape == 'r') {
            *byte = (unsigned int)'\r';
        } else if (escape == 't') {
            *byte = (unsigned int)'\t';
        } else if (escape == '0') {
            *byte = 0U;
        } else if (escape == '\\' || escape == '\'' || escape == '"') {
            *byte = (unsigned char)escape;
        } else if (escape == 'x') {
            int high;
            int low;

            if (*source_offset + 2U >= token->length ||
                (high = hex_digit(token->start[*source_offset + 1U])) < 0 ||
                (low = hex_digit(token->start[*source_offset + 2U])) < 0) {
                set_diagnostic(
                    parser->diagnostic, token->line, token->column,
                    "string escape requires two hexadecimal digits");
                parser->failed = 1;
                return 0;
            }
            *byte = (unsigned int)((high << 4) | low);
            *source_offset += 2U;
        } else {
            set_diagnostic(parser->diagnostic, token->line, token->column,
                           "unsupported string escape '\\%c'", escape);
            parser->failed = 1;
            return 0;
        }
    }
    return 1;
}

static int intern_pool_literal(struct parser *parser,
                               const struct token *token,
                               enum miga80_type type)
{
    struct miga80_constant_pool *pool = &parser->function->pool;
    unsigned int source_offset;
    unsigned int length = 0U;
    unsigned int index;

    for (source_offset = 0U; source_offset < token->length;
         ++source_offset) {
        unsigned int byte;

        if (!decode_literal_byte(parser, token, &source_offset, &byte)) {
            return -1;
        }
        if (length == MIGA80_MAX_POOL_BYTES) {
            set_diagnostic(parser->diagnostic, token->line, token->column,
                           "immutable literal exceeds %u bytes",
                           MIGA80_MAX_POOL_BYTES);
            parser->failed = 1;
            return -1;
        }
        ++length;
    }
    for (index = 0U; index < pool->entry_count; ++index) {
        const struct miga80_pool_entry *entry = &pool->entries[index];
        unsigned int decoded_offset = 0U;
        int equal = entry->type == type && entry->length == length;

        for (source_offset = 0U; equal && source_offset < token->length;
             ++source_offset) {
            unsigned int byte;

            if (!decode_literal_byte(parser, token, &source_offset, &byte)) {
                return -1;
            }
            if (pool->bytes[entry->offset + decoded_offset] !=
                (unsigned char)byte) {
                equal = 0;
            }
            ++decoded_offset;
        }
        if (equal) {
            return (int)index;
        }
    }
    if (pool->entry_count == MIGA80_MAX_POOL_ENTRIES) {
        set_diagnostic(parser->diagnostic, token->line, token->column,
                       "immutable pool exceeds %u entries",
                       MIGA80_MAX_POOL_ENTRIES);
        parser->failed = 1;
        return -1;
    }
    if (length > MIGA80_MAX_POOL_BYTES - pool->bytes_used) {
        set_diagnostic(parser->diagnostic, token->line, token->column,
                       "immutable pool exceeds %u bytes",
                       MIGA80_MAX_POOL_BYTES);
        parser->failed = 1;
        return -1;
    }
    index = pool->entry_count++;
    pool->entries[index].type = type;
    pool->entries[index].offset = pool->bytes_used;
    pool->entries[index].length = (uint16_t)length;
    for (source_offset = 0U; source_offset < token->length;
         ++source_offset) {
        unsigned int byte;

        if (!decode_literal_byte(parser, token, &source_offset, &byte)) {
            return -1;
        }
        pool->bytes[pool->bytes_used++] = (unsigned char)byte;
    }
    return (int)index;
}

static int parse_type(struct parser *parser, enum miga80_type *type)
{
    if (parser->current.kind == TOKEN_I32) {
        *type = MIGA80_TYPE_I32;
        parser_advance(parser);
        return 1;
    }
    if (parser->current.kind == TOKEN_BOOL) {
        *type = MIGA80_TYPE_BOOL;
        parser_advance(parser);
        return 1;
    }
    if (parser->current.kind == TOKEN_I8) {
        *type = MIGA80_TYPE_I8;
        parser_advance(parser);
        return 1;
    }
    if (parser->current.kind == TOKEN_U8) {
        *type = MIGA80_TYPE_U8;
        parser_advance(parser);
        return 1;
    }
    if (parser->current.kind == TOKEN_I16) {
        *type = MIGA80_TYPE_I16;
        parser_advance(parser);
        return 1;
    }
    if (parser->current.kind == TOKEN_U16) {
        *type = MIGA80_TYPE_U16;
        parser_advance(parser);
        return 1;
    }
    if (parser->current.kind == TOKEN_FIX) {
        *type = MIGA80_TYPE_FIX;
        parser_advance(parser);
        return 1;
    }
    if (parser->current.kind == TOKEN_STRING) {
        *type = MIGA80_TYPE_STRING;
        parser_advance(parser);
        return 1;
    }
    if (parser->current.kind == TOKEN_SYMBOL) {
        *type = MIGA80_TYPE_SYMBOL;
        parser_advance(parser);
        return 1;
    }
    set_diagnostic(parser->diagnostic, parser->current.line,
                   parser->current.column, "expected value type, found %s",
                   token_name(parser->current.kind));
    parser->failed = 1;
    return 0;
}

static int require_bootstrap_value_type(struct parser *parser,
                                        enum miga80_type type,
                                        unsigned int line,
                                        unsigned int column)
{
    if (miga80_type_is_value(type)) {
        return 1;
    }
    set_diagnostic(parser->diagnostic, line, column,
                   "unsupported bootstrap value type '%s'",
                   miga80_type_name(type));
    parser->failed = 1;
    return 0;
}

static int constant_fits_type(enum miga80_type type, uint32_t value)
{
    if (type == MIGA80_TYPE_I8) {
        return value <= UINT32_C(0x7f) || value >= UINT32_C(0xffffff80);
    }
    if (type == MIGA80_TYPE_U8) {
        return value <= UINT32_C(0xff);
    }
    if (type == MIGA80_TYPE_I16) {
        return value <= UINT32_C(0x7fff) || value >= UINT32_C(0xffff8000);
    }
    if (type == MIGA80_TYPE_U16) {
        return value <= UINT32_C(0xffff);
    }
    return type == MIGA80_TYPE_I32;
}

static int coerce_constant(struct parser *parser, int *node_index,
                           enum miga80_type expected)
{
    struct miga80_ast_node *node;
    uint32_t value;
    int converted;

    if (*node_index == MIGA80_INVALID_NODE ||
        !miga80_type_is_integer(expected)) {
        return 0;
    }
    node = &parser->function->nodes[*node_index];
    if (node->type == expected) {
        return 1;
    }
    if (node->type != MIGA80_TYPE_I32 ||
        !constant_i32(parser->function, *node_index, &value) ||
        !constant_fits_type(expected, value)) {
        return 0;
    }
    converted = add_node(parser, MIGA80_AST_NORMALIZE_INTEGER, node->line,
                         node->column, *node_index, MIGA80_INVALID_NODE, 0U,
                         0U, expected);
    if (converted == MIGA80_INVALID_NODE) {
        return 0;
    }
    *node_index = converted;
    return 1;
}

static int require_type(struct parser *parser, int *node_index,
                        enum miga80_type expected, unsigned int line,
                        unsigned int column, const char *context)
{
    if (*node_index == MIGA80_INVALID_NODE || parser->failed) {
        return 0;
    }
    if (parser->function->nodes[*node_index].type != expected &&
        !coerce_constant(parser, node_index, expected)) {
        set_diagnostic(parser->diagnostic, line, column,
                       "%s requires %s, found %s", context,
                       miga80_type_name(expected),
                       miga80_type_name(
                           parser->function->nodes[*node_index].type));
        parser->failed = 1;
        return 0;
    }
    return 1;
}

static int require_matching_numeric(struct parser *parser, int *left,
                                    int *right, unsigned int line,
                                    unsigned int column,
                                    const char *context,
                                    enum miga80_type *type)
{
    enum miga80_type left_type;
    enum miga80_type right_type;

    if (*left == MIGA80_INVALID_NODE || *right == MIGA80_INVALID_NODE ||
        parser->failed) {
        return 0;
    }
    left_type = parser->function->nodes[*left].type;
    right_type = parser->function->nodes[*right].type;
    if (left_type != right_type) {
        if (left_type == MIGA80_TYPE_I32 &&
            miga80_type_is_integer(right_type) &&
            coerce_constant(parser, left, right_type)) {
            left_type = right_type;
        } else if (right_type == MIGA80_TYPE_I32 &&
                   miga80_type_is_integer(left_type) &&
                   coerce_constant(parser, right, left_type)) {
            right_type = left_type;
        }
    }
    if (left_type != right_type || !miga80_type_is_numeric(left_type)) {
        if (miga80_type_is_integer(left_type) &&
            miga80_type_is_integer(right_type)) {
            set_diagnostic(
                parser->diagnostic, line, column,
                "%s requires matching integer operands, found %s and %s",
                context, miga80_type_name(left_type),
                miga80_type_name(right_type));
        } else {
            set_diagnostic(
                parser->diagnostic, line, column,
                "%s requires matching numeric operands, found %s and %s",
                context, miga80_type_name(left_type),
                miga80_type_name(right_type));
        }
        parser->failed = 1;
        return 0;
    }
    *type = left_type;
    return 1;
}

static int parse_primary(struct parser *parser)
{
    const struct token token = parser->current;

    if (token.kind == TOKEN_FIX || token.kind == TOKEN_I32) {
        const enum miga80_type target_type =
            token.kind == TOKEN_FIX ? MIGA80_TYPE_FIX : MIGA80_TYPE_I32;
        enum miga80_type source_type;
        uint32_t constant;
        uint32_t converted;
        int operand;

        parser_advance(parser);
        if (!expect(parser, TOKEN_LEFT_PAREN)) {
            return MIGA80_INVALID_NODE;
        }
        operand = parse_expression(parser);
        if (operand == MIGA80_INVALID_NODE ||
            !expect(parser, TOKEN_RIGHT_PAREN)) {
            return MIGA80_INVALID_NODE;
        }
        source_type = parser->function->nodes[operand].type;
        if ((target_type == MIGA80_TYPE_FIX &&
             source_type != MIGA80_TYPE_I32) ||
            (target_type == MIGA80_TYPE_I32 &&
             source_type != MIGA80_TYPE_FIX)) {
            set_diagnostic(parser->diagnostic, token.line, token.column,
                           "conversion to %s requires %s, found %s",
                           miga80_type_name(target_type),
                           target_type == MIGA80_TYPE_FIX ? "i32" : "fix",
                           miga80_type_name(source_type));
            parser->failed = 1;
            return MIGA80_INVALID_NODE;
        }
        if (target_type == MIGA80_TYPE_FIX &&
            constant_i32(parser->function, operand, &constant) &&
            !miga80_convert_i32_to_fix(constant, &converted)) {
            set_diagnostic(parser->diagnostic, token.line, token.column,
                           "conversion to fix is out of range");
            parser->failed = 1;
            return MIGA80_INVALID_NODE;
        }
        return add_node(parser,
                        target_type == MIGA80_TYPE_FIX
                            ? MIGA80_AST_FIX_FROM_I32
                            : MIGA80_AST_I32_FROM_FIX,
                        token.line, token.column, operand,
                        MIGA80_INVALID_NODE, 0U, 0U, target_type);
    }

    if (token.kind == TOKEN_STRING_LITERAL) {
        const int pool_index =
            intern_pool_literal(parser, &token, MIGA80_TYPE_STRING);

        parser_advance(parser);
        if (pool_index < 0) {
            return MIGA80_INVALID_NODE;
        }
        return add_node(parser, MIGA80_AST_LITERAL_STRING, token.line,
                        token.column, MIGA80_INVALID_NODE,
                        MIGA80_INVALID_NODE, 0U,
                        (unsigned int)pool_index, MIGA80_TYPE_STRING);
    }
    if (token.kind == TOKEN_SYMBOL) {
        struct token literal;
        int pool_index;

        parser_advance(parser);
        if (!expect(parser, TOKEN_LEFT_PAREN)) {
            return MIGA80_INVALID_NODE;
        }
        literal = parser->current;
        if (!expect(parser, TOKEN_STRING_LITERAL)) {
            return MIGA80_INVALID_NODE;
        }
        pool_index =
            intern_pool_literal(parser, &literal, MIGA80_TYPE_SYMBOL);
        if (pool_index < 0 || !expect(parser, TOKEN_RIGHT_PAREN)) {
            return MIGA80_INVALID_NODE;
        }
        return add_node(parser, MIGA80_AST_LITERAL_SYMBOL, token.line,
                        token.column, MIGA80_INVALID_NODE,
                        MIGA80_INVALID_NODE, 0U,
                        (unsigned int)pool_index, MIGA80_TYPE_SYMBOL);
    }

    if (token.kind == TOKEN_INTEGER) {
        parser_advance(parser);
        return add_node(parser, MIGA80_AST_LITERAL_I32, token.line,
                        token.column, MIGA80_INVALID_NODE,
                        MIGA80_INVALID_NODE, token.integer, 0U,
                        MIGA80_TYPE_I32);
    }
    if (token.kind == TOKEN_FIX_LITERAL) {
        parser_advance(parser);
        return add_node(parser, MIGA80_AST_LITERAL_FIX, token.line,
                        token.column, MIGA80_INVALID_NODE,
                        MIGA80_INVALID_NODE, token.integer, 0U,
                        MIGA80_TYPE_FIX);
    }
    if (token.kind == TOKEN_TRUE || token.kind == TOKEN_FALSE) {
        parser_advance(parser);
        return add_node(parser, MIGA80_AST_LITERAL_BOOL, token.line,
                        token.column, MIGA80_INVALID_NODE,
                        MIGA80_INVALID_NODE,
                        token.kind == TOKEN_TRUE ? 1U : 0U, 0U,
                        MIGA80_TYPE_BOOL);
    }
    if (token.kind == TOKEN_IDENTIFIER) {
        const int parameter = find_parameter(parser->function, &token);
        const int local = find_local(parser->function, &token);

        if (parameter < 0 && local < 0) {
            set_diagnostic(parser->diagnostic, token.line, token.column,
                           "unknown identifier '%.*s'", (int)token.length,
                           token.start);
            parser->failed = 1;
            return MIGA80_INVALID_NODE;
        }
        parser_advance(parser);
        if (parameter >= 0) {
            const enum miga80_type type =
                parser->function->parameter_types[parameter];

            return add_node(parser,
                            type == MIGA80_TYPE_BOOL
                                ? MIGA80_AST_PARAMETER_BOOL
                                : MIGA80_AST_PARAMETER_I32,
                            token.line,
                            token.column, MIGA80_INVALID_NODE,
                            MIGA80_INVALID_NODE, 0U,
                            (unsigned int)parameter, type);
        }
        return add_node(parser,
                        parser->function->local_types[local] == MIGA80_TYPE_BOOL
                            ? MIGA80_AST_LOCAL_BOOL
                            : MIGA80_AST_LOCAL_I32,
                        token.line,
                        token.column, MIGA80_INVALID_NODE,
                        MIGA80_INVALID_NODE, 0U, (unsigned int)local,
                        parser->function->local_types[local]);
    }
    if (token.kind == TOKEN_LEFT_PAREN) {
        int expression;

        parser_advance(parser);
        expression = parse_expression(parser);
        (void)expect(parser, TOKEN_RIGHT_PAREN);
        return expression;
    }

    set_diagnostic(parser->diagnostic, token.line, token.column,
                   "expected value expression, found %s",
                   token_name(token.kind));
    parser->failed = 1;
    return MIGA80_INVALID_NODE;
}

static int parse_unary(struct parser *parser)
{
    if (parser->current.kind == TOKEN_MINUS) {
        const struct token operation = parser->current;
        int operand;

        parser_advance(parser);
        operand = parse_unary(parser);
        if (operand == MIGA80_INVALID_NODE || parser->failed ||
            !miga80_type_is_signed_numeric(
                parser->function->nodes[operand].type)) {
            if (!parser->failed && operand != MIGA80_INVALID_NODE) {
                const enum miga80_type operand_type =
                    parser->function->nodes[operand].type;

                set_diagnostic(parser->diagnostic, operation.line,
                               operation.column,
                               miga80_type_is_integer(operand_type)
                                   ? "unary '-' requires a signed integer, found %s"
                                   : "unary '-' requires a signed numeric value, found %s",
                               miga80_type_name(operand_type));
                parser->failed = 1;
            }
            return MIGA80_INVALID_NODE;
        }
        return add_node(parser, MIGA80_AST_NEG_I32, operation.line,
                        operation.column, operand, MIGA80_INVALID_NODE, 0U,
                        0U, parser->function->nodes[operand].type);
    }
    return parse_primary(parser);
}

static int parse_multiply(struct parser *parser)
{
    int left = parse_unary(parser);

    while (!parser->failed &&
           (parser->current.kind == TOKEN_STAR ||
            parser->current.kind == TOKEN_SLASH)) {
        const struct token operation = parser->current;
        enum miga80_ast_kind kind = MIGA80_AST_MUL_I32;
        enum miga80_type type;
        int right;
        uint32_t right_constant;

        parser_advance(parser);
        right = parse_unary(parser);
        if (!require_matching_numeric(parser, &left, &right, operation.line,
                                      operation.column,
                                      "multiplicative operator", &type)) {
            return MIGA80_INVALID_NODE;
        }
        if (operation.kind == TOKEN_SLASH) {
            kind = type == MIGA80_TYPE_FIX
                       ? MIGA80_AST_DIV_FIX
                       : (miga80_type_is_signed_integer(type)
                              ? MIGA80_AST_DIV_I32
                              : MIGA80_AST_DIV_U32);
        } else if (type == MIGA80_TYPE_FIX) {
            kind = MIGA80_AST_MUL_FIX;
        }
        if (operation.kind == TOKEN_SLASH &&
            constant_i32(parser->function, right, &right_constant) &&
            right_constant == 0U) {
            set_diagnostic(parser->diagnostic, operation.line,
                           operation.column,
                           "division by zero in constant expression");
            parser->failed = 1;
            return MIGA80_INVALID_NODE;
        }
        left = add_node(parser, kind, operation.line, operation.column, left,
                        right, 0U, 0U, type);
    }
    return left;
}

static int parse_add(struct parser *parser)
{
    int left = parse_multiply(parser);

    while (!parser->failed &&
           (parser->current.kind == TOKEN_PLUS ||
            parser->current.kind == TOKEN_MINUS)) {
        const struct token operation = parser->current;
        const enum miga80_ast_kind kind =
            operation.kind == TOKEN_PLUS ? MIGA80_AST_ADD_I32
                                         : MIGA80_AST_SUB_I32;
        enum miga80_type type;
        int right;

        parser_advance(parser);
        right = parse_multiply(parser);
        if (!require_matching_numeric(parser, &left, &right, operation.line,
                                      operation.column,
                                      "arithmetic operator", &type)) {
            return MIGA80_INVALID_NODE;
        }
        left = add_node(parser, kind, operation.line, operation.column, left,
                        right, 0U, 0U, type);
    }
    return left;
}

static int comparison_token(enum token_kind kind)
{
    return kind == TOKEN_EQUAL_EQUAL || kind == TOKEN_NOT_EQUAL ||
           kind == TOKEN_LESS || kind == TOKEN_LESS_EQUAL ||
           kind == TOKEN_GREATER || kind == TOKEN_GREATER_EQUAL;
}

static int parse_expression(struct parser *parser)
{
    int left = parse_add(parser);

    while (!parser->failed && comparison_token(parser->current.kind)) {
        const struct token operation = parser->current;
        enum miga80_ast_kind kind = MIGA80_AST_EQ;
        enum miga80_type operand_type;
        int right;

        parser_advance(parser);
        right = parse_add(parser);
        if (left == MIGA80_INVALID_NODE || right == MIGA80_INVALID_NODE ||
            parser->failed) {
            return MIGA80_INVALID_NODE;
        }
        if (parser->function->nodes[left].type !=
            parser->function->nodes[right].type) {
            const enum miga80_type left_type =
                parser->function->nodes[left].type;
            const enum miga80_type right_type =
                parser->function->nodes[right].type;

            if (left_type == MIGA80_TYPE_I32 &&
                miga80_type_is_integer(right_type)) {
                (void)coerce_constant(parser, &left, right_type);
            } else if (right_type == MIGA80_TYPE_I32 &&
                       miga80_type_is_integer(left_type)) {
                (void)coerce_constant(parser, &right, left_type);
            }
            if (parser->function->nodes[left].type !=
                parser->function->nodes[right].type) {
                set_diagnostic(parser->diagnostic, operation.line,
                               operation.column,
                               "comparison operands have different types");
                parser->failed = 1;
                return MIGA80_INVALID_NODE;
            }
        }
        operand_type = parser->function->nodes[left].type;
        if (operation.kind != TOKEN_EQUAL_EQUAL &&
            operation.kind != TOKEN_NOT_EQUAL &&
            !miga80_type_is_numeric(operand_type)) {
            set_diagnostic(parser->diagnostic, operation.line,
                           operation.column,
                           "ordered comparison requires integer operands");
            parser->failed = 1;
            return MIGA80_INVALID_NODE;
        }
        if (operation.kind == TOKEN_NOT_EQUAL) {
            kind = MIGA80_AST_NE;
        } else if (operation.kind == TOKEN_LESS) {
            kind = miga80_type_is_signed_numeric(operand_type)
                       ? MIGA80_AST_LT_I32
                       : MIGA80_AST_LT_U32;
        } else if (operation.kind == TOKEN_LESS_EQUAL) {
            kind = miga80_type_is_signed_numeric(operand_type)
                       ? MIGA80_AST_LE_I32
                       : MIGA80_AST_LE_U32;
        } else if (operation.kind == TOKEN_GREATER) {
            kind = miga80_type_is_signed_numeric(operand_type)
                       ? MIGA80_AST_GT_I32
                       : MIGA80_AST_GT_U32;
        } else if (operation.kind == TOKEN_GREATER_EQUAL) {
            kind = miga80_type_is_signed_numeric(operand_type)
                       ? MIGA80_AST_GE_I32
                       : MIGA80_AST_GE_U32;
        }
        left = add_node(parser, kind, operation.line, operation.column, left,
                        right, 0U, 0U, MIGA80_TYPE_BOOL);
    }
    return left;
}

static int parse_parameter(struct parser *parser)
{
    const struct token name = parser->current;
    enum miga80_type type;
    unsigned int index;

    if (!expect(parser, TOKEN_IDENTIFIER)) {
        return 0;
    }
    if (parser->function->parameter_count == MIGA80_MAX_PARAMETERS) {
        set_diagnostic(parser->diagnostic, name.line, name.column,
                       "initial ABI supports at most %u parameters",
                       MIGA80_MAX_PARAMETERS);
        parser->failed = 1;
        return 0;
    }
    for (index = 0; index < parser->function->parameter_count; ++index) {
        if (strlen(parser->function->parameter_names[index]) == name.length &&
            memcmp(parser->function->parameter_names[index], name.start,
                   name.length) == 0) {
            set_diagnostic(parser->diagnostic, name.line, name.column,
                           "duplicate parameter '%.*s'", (int)name.length,
                           name.start);
            parser->failed = 1;
            return 0;
        }
    }
    index = parser->function->parameter_count;
    if (!copy_name(parser, parser->function->parameter_names[index], &name) ||
        !expect(parser, TOKEN_COLON) || !parse_type(parser, &type)) {
        return 0;
    }
    if (!require_bootstrap_value_type(parser, type, name.line, name.column)) {
        return 0;
    }
    {
        const int address_class = miga80_type_is_address(type);
        const unsigned int limit = address_class
                                       ? MIGA80_ABI_MAX_ADDRESS_ARGUMENTS
                                       : MIGA80_ABI_MAX_SCALAR_ARGUMENTS;
        unsigned int class_count = 0U;

        for (index = 0U; index < parser->function->parameter_count; ++index) {
            if (miga80_type_is_address(
                    parser->function->parameter_types[index]) ==
                address_class) {
                ++class_count;
            }
        }
        if (class_count == limit) {
            set_diagnostic(parser->diagnostic, name.line, name.column,
                           "initial ABI supports at most %u %s parameters",
                           limit, address_class ? "address" : "scalar");
            parser->failed = 1;
            return 0;
        }
    }
    parser->function->parameter_types[index] = type;
    ++parser->function->parameter_count;
    return 1;
}

struct statement_list {
    unsigned int first;
    unsigned int last;
};

static void initialize_statement_list(struct statement_list *list)
{
    list->first = MIGA80_INVALID_STATEMENT;
    list->last = MIGA80_INVALID_STATEMENT;
}

static unsigned int allocate_statement(
    struct parser *parser, enum miga80_ast_statement_kind kind,
    unsigned int local_index, int expression, unsigned int line,
    unsigned int column)
{
    struct miga80_ast_statement *statement;
    unsigned int index;

    if (parser->function->statement_count == MIGA80_MAX_STATEMENTS) {
        set_diagnostic(parser->diagnostic, line, column,
                       "function body exceeds %u statements",
                       MIGA80_MAX_STATEMENTS);
        parser->failed = 1;
        return MIGA80_INVALID_STATEMENT;
    }
    index = parser->function->statement_count++;
    statement = &parser->function->statements[index];
    statement->kind = kind;
    statement->local_index = local_index;
    statement->expression = expression;
    statement->line = line;
    statement->column = column;
    statement->next_statement = MIGA80_INVALID_STATEMENT;
    statement->then_statement = MIGA80_INVALID_STATEMENT;
    statement->else_statement = MIGA80_INVALID_STATEMENT;
    return index;
}

static int append_statement(struct parser *parser,
                            struct statement_list *list,
                            unsigned int statement)
{
    if (statement == MIGA80_INVALID_STATEMENT) {
        return 0;
    }
    if (list->first == MIGA80_INVALID_STATEMENT) {
        list->first = statement;
    } else {
        parser->function->statements[list->last].next_statement = statement;
    }
    list->last = statement;
    return 1;
}

static int local_name_is_available(struct parser *parser,
                                   const struct token *name)
{
    if (find_parameter(parser->function, name) >= 0 ||
        find_local(parser->function, name) >= 0) {
        set_diagnostic(parser->diagnostic, name->line, name->column,
                       "duplicate local '%.*s'", (int)name->length,
                       name->start);
        parser->failed = 1;
        return 0;
    }
    return 1;
}

static unsigned int parse_local_declaration(struct parser *parser)
{
    struct token name;
    enum miga80_type type;
    int expression;
    unsigned int local_index;

    parser_advance(parser);
    name = parser->current;
    if (!expect(parser, TOKEN_IDENTIFIER)) {
        return MIGA80_INVALID_STATEMENT;
    }
    if (parser->function->local_count == MIGA80_MAX_LOCALS) {
        set_diagnostic(parser->diagnostic, name.line, name.column,
                       "function exceeds %u local variables",
                       MIGA80_MAX_LOCALS);
        parser->failed = 1;
        return MIGA80_INVALID_STATEMENT;
    }
    if (!local_name_is_available(parser, &name) ||
        !expect(parser, TOKEN_COLON) || !parse_type(parser, &type) ||
        !expect(parser, TOKEN_EQUAL)) {
        return MIGA80_INVALID_STATEMENT;
    }
    if (!require_bootstrap_value_type(parser, type, name.line, name.column)) {
        return MIGA80_INVALID_STATEMENT;
    }
    expression = parse_expression(parser);
    if (!require_type(parser, &expression, type, name.line, name.column,
                      "local initializer")) {
        return MIGA80_INVALID_STATEMENT;
    }
    local_index = parser->function->local_count;
    if (!copy_name(parser, parser->function->local_names[local_index],
                   &name)) {
        return MIGA80_INVALID_STATEMENT;
    }
    parser->function->local_types[local_index] = type;
    ++parser->function->local_count;
    return allocate_statement(parser, MIGA80_AST_LOCAL_INITIALIZE,
                              local_index, expression, name.line,
                              name.column);
}

static unsigned int parse_assignment(struct parser *parser)
{
    const struct token name = parser->current;
    const int local = find_local(parser->function, &name);
    struct token operation;
    int expression;

    if (local < 0) {
        if (find_parameter(parser->function, &name) >= 0) {
            set_diagnostic(parser->diagnostic, name.line, name.column,
                           "cannot assign to parameter '%.*s'",
                           (int)name.length, name.start);
        } else {
            set_diagnostic(parser->diagnostic, name.line, name.column,
                           "unknown local assignment target '%.*s'",
                           (int)name.length, name.start);
        }
        parser->failed = 1;
        return MIGA80_INVALID_STATEMENT;
    }
    if (!expect(parser, TOKEN_IDENTIFIER)) {
        return MIGA80_INVALID_STATEMENT;
    }
    operation = parser->current;
    if (operation.kind != TOKEN_EQUAL &&
        operation.kind != TOKEN_SLASH_EQUAL) {
        set_diagnostic(parser->diagnostic, operation.line, operation.column,
                       "expected '=' or '/=', found %s",
                       token_name(operation.kind));
        parser->failed = 1;
        return MIGA80_INVALID_STATEMENT;
    }
    parser_advance(parser);
    expression = parse_expression(parser);
    if (operation.kind == TOKEN_SLASH_EQUAL) {
        int target;
        uint32_t right_constant;
        const enum miga80_type target_type =
            parser->function->local_types[local];

        if (!miga80_type_is_numeric(target_type)) {
            set_diagnostic(parser->diagnostic, operation.line,
                           operation.column,
                           "operator '/=' requires numeric target");
            parser->failed = 1;
            return MIGA80_INVALID_STATEMENT;
        }
        if (!require_type(parser, &expression, target_type,
                          operation.line, operation.column,
                          "operator '/='")) {
            return MIGA80_INVALID_STATEMENT;
        }
        if (constant_i32(parser->function, expression, &right_constant) &&
            right_constant == 0U) {
            set_diagnostic(parser->diagnostic, operation.line,
                           operation.column,
                           "division by zero in constant expression");
            parser->failed = 1;
            return MIGA80_INVALID_STATEMENT;
        }
        target = add_node(parser, MIGA80_AST_LOCAL_I32, name.line,
                          name.column, MIGA80_INVALID_NODE,
                          MIGA80_INVALID_NODE, 0U, (unsigned int)local,
                          target_type);
        expression = add_node(
            parser,
            target_type == MIGA80_TYPE_FIX
                ? MIGA80_AST_DIV_FIX
                : (miga80_type_is_signed_integer(target_type)
                       ? MIGA80_AST_DIV_I32
                       : MIGA80_AST_DIV_U32),
            operation.line, operation.column, target, expression, 0U, 0U,
            target_type);
        if (expression == MIGA80_INVALID_NODE) {
            return MIGA80_INVALID_STATEMENT;
        }
    } else if (!require_type(parser, &expression,
                             parser->function->local_types[local], name.line,
                             name.column, "assignment")) {
        return MIGA80_INVALID_STATEMENT;
    }
    return allocate_statement(parser, MIGA80_AST_LOCAL_ASSIGN,
                              (unsigned int)local, expression, name.line,
                              name.column);
}

static unsigned int parse_if_statement(struct parser *parser);
static unsigned int parse_while_statement(struct parser *parser);

static unsigned int parse_loop_control_statement(struct parser *parser)
{
    const struct token control = parser->current;
    const enum miga80_ast_statement_kind kind =
        control.kind == TOKEN_BREAK ? MIGA80_AST_BREAK
                                    : MIGA80_AST_CONTINUE;

    parser_advance(parser);
    if (parser->loop_depth == 0U) {
        set_diagnostic(parser->diagnostic, control.line, control.column,
                       "%.*s is only valid inside while",
                       (int)control.length, control.start);
        parser->failed = 1;
        return MIGA80_INVALID_STATEMENT;
    }
    return allocate_statement(parser, kind, 0U, MIGA80_INVALID_NODE,
                              control.line, control.column);
}

static int token_starts_control_statement(enum token_kind kind)
{
    return kind == TOKEN_IDENTIFIER || kind == TOKEN_IF ||
           kind == TOKEN_WHILE || kind == TOKEN_BREAK ||
           kind == TOKEN_CONTINUE;
}

static int parse_control_statement_list(struct parser *parser,
                                        struct statement_list *list,
                                        const char *owner)
{
    initialize_statement_list(list);
    while (!parser->failed &&
           token_starts_control_statement(parser->current.kind)) {
        unsigned int statement;
        enum miga80_ast_statement_kind statement_kind;

        if (parser->current.kind == TOKEN_IF) {
            statement = parse_if_statement(parser);
        } else if (parser->current.kind == TOKEN_WHILE) {
            statement = parse_while_statement(parser);
        } else if (parser->current.kind == TOKEN_BREAK ||
                   parser->current.kind == TOKEN_CONTINUE) {
            statement = parse_loop_control_statement(parser);
        } else {
            statement = parse_assignment(parser);
        }
        if (!append_statement(parser, list, statement)) {
            return 0;
        }
        statement_kind = parser->function->statements[statement].kind;
        if ((statement_kind == MIGA80_AST_BREAK ||
             statement_kind == MIGA80_AST_CONTINUE) &&
            token_starts_control_statement(parser->current.kind)) {
            set_diagnostic(parser->diagnostic, parser->current.line,
                           parser->current.column,
                           "statement follows loop control");
            parser->failed = 1;
            return 0;
        }
    }
    if (parser->current.kind == TOKEN_LOCAL) {
        set_diagnostic(parser->diagnostic, parser->current.line,
                       parser->current.column,
                       "local declarations inside %s are not implemented",
                       owner);
        parser->failed = 1;
        return 0;
    }
    if (parser->current.kind == TOKEN_RETURN) {
        set_diagnostic(parser->diagnostic, parser->current.line,
                       parser->current.column,
                       "return inside %s is not implemented", owner);
        parser->failed = 1;
        return 0;
    }
    return 1;
}

static unsigned int parse_if_statement(struct parser *parser)
{
    const struct token if_token = parser->current;
    struct statement_list then_list;
    struct statement_list else_list;
    unsigned int statement_index;
    int condition;

    parser_advance(parser);
    condition = parse_expression(parser);
    if (!require_type(parser, &condition, MIGA80_TYPE_BOOL, if_token.line,
                      if_token.column, "if condition") ||
        !expect(parser, TOKEN_THEN)) {
        return MIGA80_INVALID_STATEMENT;
    }
    statement_index =
        allocate_statement(parser, MIGA80_AST_IF, 0U, condition,
                           if_token.line, if_token.column);
    if (statement_index == MIGA80_INVALID_STATEMENT ||
        !parse_control_statement_list(parser, &then_list, "if") ||
        !expect(parser, TOKEN_ELSE) ||
        !parse_control_statement_list(parser, &else_list, "if") ||
        !expect(parser, TOKEN_END)) {
        return MIGA80_INVALID_STATEMENT;
    }
    parser->function->statements[statement_index].then_statement =
        then_list.first;
    parser->function->statements[statement_index].else_statement =
        else_list.first;
    return statement_index;
}

static unsigned int parse_while_statement(struct parser *parser)
{
    const struct token while_token = parser->current;
    struct statement_list body;
    unsigned int statement_index;
    int condition;

    parser_advance(parser);
    condition = parse_expression(parser);
    if (!require_type(parser, &condition, MIGA80_TYPE_BOOL,
                      while_token.line, while_token.column,
                      "while condition") ||
        !expect(parser, TOKEN_DO)) {
        return MIGA80_INVALID_STATEMENT;
    }
    statement_index =
        allocate_statement(parser, MIGA80_AST_WHILE, 0U, condition,
                           while_token.line, while_token.column);
    if (statement_index == MIGA80_INVALID_STATEMENT) {
        return MIGA80_INVALID_STATEMENT;
    }
    ++parser->loop_depth;
    if (!parse_control_statement_list(parser, &body, "while")) {
        --parser->loop_depth;
        return MIGA80_INVALID_STATEMENT;
    }
    --parser->loop_depth;
    if (!expect(parser, TOKEN_END)) {
        return MIGA80_INVALID_STATEMENT;
    }
    parser->function->statements[statement_index].then_statement =
        body.first;
    return statement_index;
}

int miga80_parse_function(const char *source, size_t source_size,
                          struct miga80_ast_function *function,
                          struct miga80_diagnostic *diagnostic)
{
    struct parser parser;
    struct statement_list body;
    struct token function_name;

    if (source == NULL || function == NULL || diagnostic == NULL) {
        return 0;
    }
    (void)memset(function, 0, sizeof(*function));
    (void)memset(diagnostic, 0, sizeof(*diagnostic));
    (void)memset(&parser, 0, sizeof(parser));
    parser.lexer.source = source;
    parser.lexer.size = source_size;
    parser.lexer.line = 1U;
    parser.lexer.column = 1U;
    parser.lexer.diagnostic = diagnostic;
    parser.function = function;
    parser.diagnostic = diagnostic;
    function->first_statement = MIGA80_INVALID_STATEMENT;
    initialize_statement_list(&body);
    parser_advance(&parser);

    if (!expect(&parser, TOKEN_FUNCTION)) {
        return 0;
    }
    function_name = parser.current;
    if (!expect(&parser, TOKEN_IDENTIFIER) ||
        !copy_name(&parser, function->name, &function_name) ||
        !expect(&parser, TOKEN_LEFT_PAREN)) {
        return 0;
    }
    if (parser.current.kind != TOKEN_RIGHT_PAREN) {
        if (!parse_parameter(&parser)) {
            return 0;
        }
        while (parser.current.kind == TOKEN_COMMA) {
            parser_advance(&parser);
            if (!parse_parameter(&parser)) {
                return 0;
            }
        }
    }
    if (!expect(&parser, TOKEN_RIGHT_PAREN) ||
        !expect(&parser, TOKEN_COLON) ||
        !parse_type(&parser, &function->result_type)) {
        return 0;
    }
    if (!require_bootstrap_value_type(
            &parser, function->result_type, function_name.line,
            function_name.column)) {
        return 0;
    }

    while (!parser.failed && (parser.current.kind == TOKEN_LOCAL ||
                              token_starts_control_statement(
                                  parser.current.kind))) {
        unsigned int statement;

        if (parser.current.kind == TOKEN_LOCAL) {
            statement = parse_local_declaration(&parser);
        } else if (parser.current.kind == TOKEN_IF) {
            statement = parse_if_statement(&parser);
        } else if (parser.current.kind == TOKEN_WHILE) {
            statement = parse_while_statement(&parser);
        } else if (parser.current.kind == TOKEN_BREAK ||
                   parser.current.kind == TOKEN_CONTINUE) {
            statement = parse_loop_control_statement(&parser);
        } else {
            statement = parse_assignment(&parser);
        }
        if (!append_statement(&parser, &body, statement)) {
            return 0;
        }
    }
    {
        const struct token return_token = parser.current;

        if (!expect(&parser, TOKEN_RETURN)) {
            return 0;
        }
        function->result = parse_expression(&parser);
        if (!require_type(&parser, &function->result, function->result_type,
                          return_token.line, return_token.column,
                          "function return") ||
            !append_statement(
                &parser, &body,
                allocate_statement(&parser, MIGA80_AST_RETURN, 0U,
                                   function->result, return_token.line,
                                   return_token.column))) {
            return 0;
        }
    }
    if (parser.failed || !expect(&parser, TOKEN_END) ||
        !expect(&parser, TOKEN_EOF)) {
        return 0;
    }
    function->first_statement = body.first;
    return function->result != MIGA80_INVALID_NODE;
}
