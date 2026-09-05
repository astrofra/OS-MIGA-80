#include "compiler/ir/ir.h"

#include <stdio.h>
#include <string.h>

#define MIGA80_MAX_EVALUATED_BLOCKS (MIGA80_MAX_BASIC_BLOCKS * 131072U)

static int fail(struct miga80_diagnostic *diagnostic, unsigned int line,
                unsigned int column, const char *message)
{
    diagnostic->line = line;
    diagnostic->column = column;
    (void)snprintf(diagnostic->message, sizeof(diagnostic->message), "%s",
                   message);
    return 0;
}

static int emit_instruction(struct miga80_ir_function *ir,
                            enum miga80_ir_opcode opcode,
                            enum miga80_type type, uint32_t operand,
                            unsigned int line, unsigned int column,
                            struct miga80_diagnostic *diagnostic)
{
    struct miga80_ir_instruction *instruction;

    if (ir->instruction_count == MIGA80_MAX_IR_INSTRUCTIONS) {
        return fail(diagnostic, line, column,
                    "typed IR instruction limit exceeded");
    }
    instruction = &ir->instructions[ir->instruction_count++];
    instruction->opcode = opcode;
    instruction->type = type;
    instruction->operand = operand;
    instruction->line = line;
    instruction->column = column;
    return 1;
}

static int lower_node(const struct miga80_ast_function *ast, int node_index,
                      struct miga80_ir_function *ir,
                      struct miga80_diagnostic *diagnostic)
{
    const struct miga80_ast_node *node;
    enum miga80_ir_opcode opcode;

    if (node_index < 0 || (unsigned int)node_index >= ast->node_count) {
        return fail(diagnostic, 0U, 0U, "invalid AST node reference");
    }
    node = &ast->nodes[node_index];
    switch (node->kind) {
    case MIGA80_AST_LITERAL_I32:
        return emit_instruction(ir, MIGA80_IR_PUSH_I32, node->type,
                                node->value,
                                node->line, node->column, diagnostic);
    case MIGA80_AST_LITERAL_FIX:
        return emit_instruction(ir, MIGA80_IR_PUSH_FIX, MIGA80_TYPE_FIX,
                                node->value,
                                node->line, node->column, diagnostic);
    case MIGA80_AST_LITERAL_BOOL:
        return emit_instruction(ir, MIGA80_IR_PUSH_BOOL, MIGA80_TYPE_BOOL,
                                node->value,
                                node->line, node->column, diagnostic);
    case MIGA80_AST_LITERAL_STRING:
        return emit_instruction(ir, MIGA80_IR_PUSH_STRING,
                                MIGA80_TYPE_STRING, node->symbol_index,
                                node->line, node->column, diagnostic);
    case MIGA80_AST_LITERAL_SYMBOL:
        return emit_instruction(ir, MIGA80_IR_PUSH_SYMBOL,
                                MIGA80_TYPE_SYMBOL, node->symbol_index,
                                node->line, node->column, diagnostic);
    case MIGA80_AST_PARAMETER_I32:
        return emit_instruction(ir, MIGA80_IR_PUSH_PARAMETER_I32,
                                node->type, node->symbol_index, node->line,
                                node->column, diagnostic);
    case MIGA80_AST_PARAMETER_BOOL:
        return emit_instruction(ir, MIGA80_IR_PUSH_PARAMETER_BOOL,
                                MIGA80_TYPE_BOOL, node->symbol_index, node->line,
                                node->column, diagnostic);
    case MIGA80_AST_LOCAL_I32:
        return emit_instruction(ir, MIGA80_IR_PUSH_LOCAL_I32,
                                node->type, node->symbol_index, node->line,
                                node->column, diagnostic);
    case MIGA80_AST_LOCAL_BOOL:
        return emit_instruction(ir, MIGA80_IR_PUSH_LOCAL_BOOL,
                                MIGA80_TYPE_BOOL, node->symbol_index, node->line,
                                node->column, diagnostic);
    case MIGA80_AST_NORMALIZE_INTEGER:
        if (!lower_node(ast, node->left, ir, diagnostic)) {
            return 0;
        }
        return emit_instruction(ir, MIGA80_IR_NORMALIZE_INTEGER, node->type,
                                0U, node->line, node->column, diagnostic);
    case MIGA80_AST_NEG_I32:
        if (!lower_node(ast, node->left, ir, diagnostic)) {
            return 0;
        }
        return emit_instruction(ir, MIGA80_IR_NEG_I32, node->type, 0U,
                                node->line, node->column, diagnostic);
    case MIGA80_AST_FIX_FROM_I32:
        if (!lower_node(ast, node->left, ir, diagnostic)) {
            return 0;
        }
        return emit_instruction(ir, MIGA80_IR_FIX_FROM_I32,
                                MIGA80_TYPE_FIX, 0U, node->line,
                                node->column, diagnostic);
    case MIGA80_AST_I32_FROM_FIX:
        if (!lower_node(ast, node->left, ir, diagnostic)) {
            return 0;
        }
        return emit_instruction(ir, MIGA80_IR_I32_FROM_FIX,
                                MIGA80_TYPE_I32, 0U, node->line,
                                node->column, diagnostic);
    case MIGA80_AST_ADD_I32:
        opcode = MIGA80_IR_ADD_I32;
        break;
    case MIGA80_AST_SUB_I32:
        opcode = MIGA80_IR_SUB_I32;
        break;
    case MIGA80_AST_MUL_I32:
        opcode = MIGA80_IR_MUL_I32;
        break;
    case MIGA80_AST_MUL_FIX:
        opcode = MIGA80_IR_MUL_FIX;
        break;
    case MIGA80_AST_DIV_FIX:
        opcode = MIGA80_IR_DIV_FIX;
        break;
    case MIGA80_AST_DIV_I32:
        opcode = MIGA80_IR_DIV_I32;
        break;
    case MIGA80_AST_DIV_U32:
        opcode = MIGA80_IR_DIV_U32;
        break;
    case MIGA80_AST_EQ:
        if (node->left < 0 || (unsigned int)node->left >= ast->node_count) {
            return fail(diagnostic, node->line, node->column,
                        "invalid AST comparison operand");
        }
        opcode = ast->nodes[node->left].type == MIGA80_TYPE_BOOL
                     ? MIGA80_IR_EQ_BOOL
                     : MIGA80_IR_EQ_I32;
        break;
    case MIGA80_AST_NE:
        if (node->left < 0 || (unsigned int)node->left >= ast->node_count) {
            return fail(diagnostic, node->line, node->column,
                        "invalid AST comparison operand");
        }
        opcode = ast->nodes[node->left].type == MIGA80_TYPE_BOOL
                     ? MIGA80_IR_NE_BOOL
                     : MIGA80_IR_NE_I32;
        break;
    case MIGA80_AST_LT_I32:
        opcode = MIGA80_IR_LT_I32;
        break;
    case MIGA80_AST_LE_I32:
        opcode = MIGA80_IR_LE_I32;
        break;
    case MIGA80_AST_GT_I32:
        opcode = MIGA80_IR_GT_I32;
        break;
    case MIGA80_AST_GE_I32:
        opcode = MIGA80_IR_GE_I32;
        break;
    case MIGA80_AST_LT_U32:
        opcode = MIGA80_IR_LT_U32;
        break;
    case MIGA80_AST_LE_U32:
        opcode = MIGA80_IR_LE_U32;
        break;
    case MIGA80_AST_GT_U32:
        opcode = MIGA80_IR_GT_U32;
        break;
    case MIGA80_AST_GE_U32:
        opcode = MIGA80_IR_GE_U32;
        break;
    default:
        return fail(diagnostic, node->line, node->column,
                    "unknown AST node kind");
    }

    if (!lower_node(ast, node->left, ir, diagnostic) ||
        !lower_node(ast, node->right, ir, diagnostic)) {
        return 0;
    }
    return emit_instruction(ir, opcode,
                            (node->kind >= MIGA80_AST_EQ &&
                             node->kind <= MIGA80_AST_GE_U32)
                                ? ast->nodes[node->left].type
                                : node->type,
                            0U, node->line, node->column,
                            diagnostic);
}

enum lower_loop_control_kind {
    LOWER_LOOP_BREAK,
    LOWER_LOOP_CONTINUE
};

struct lower_edge {
    uint16_t block;
    uint16_t instruction;
    unsigned char successor_index;
    unsigned char kind;
};

struct lower_context {
    const struct miga80_ast_function *ast;
    struct miga80_ir_function *ir;
    struct miga80_diagnostic *diagnostic;
    struct lower_edge control_edges[MIGA80_MAX_STATEMENTS];
    unsigned int control_edge_count;
    uint32_t active_loops;
    unsigned int loop_depth;
    unsigned int visited_statements;
    int returned;
};

static unsigned int create_block(struct lower_context *context)
{
    struct miga80_ir_basic_block *block;
    unsigned int index;

    if (context->ir->block_count == MIGA80_MAX_BASIC_BLOCKS) {
        (void)fail(context->diagnostic, 0U, 0U,
                   "typed IR basic-block limit exceeded");
        return MIGA80_INVALID_BLOCK;
    }
    index = context->ir->block_count++;
    block = &context->ir->blocks[index];
    (void)memset(block, 0, sizeof(*block));
    block->first_instruction = MIGA80_INVALID_BLOCK;
    block->successors[0] = MIGA80_INVALID_BLOCK;
    block->successors[1] = MIGA80_INVALID_BLOCK;
    context->ir->block_loop_membership[index] = context->active_loops;
    return index;
}

static void begin_block(struct lower_context *context, unsigned int block)
{
    context->ir->blocks[block].first_instruction =
        context->ir->instruction_count;
}

static int finish_block(struct lower_context *context, unsigned int block,
                        unsigned int first_successor,
                        unsigned int second_successor,
                        unsigned int successor_count)
{
    struct miga80_ir_basic_block *result = &context->ir->blocks[block];

    if (result->first_instruction == MIGA80_INVALID_BLOCK ||
        successor_count > MIGA80_MAX_BLOCK_SUCCESSORS) {
        return fail(context->diagnostic, 0U, 0U,
                    "unable to finish typed IR basic block");
    }
    result->instruction_count =
        context->ir->instruction_count - result->first_instruction;
    result->successor_count = successor_count;
    if (successor_count >= 1U) {
        result->successors[0] = first_successor;
    }
    if (successor_count >= 2U) {
        result->successors[1] = second_successor;
    }
    return 1;
}

static int patch_edge(struct lower_context *context,
                      const struct lower_edge *edge, unsigned int target)
{
    struct miga80_ir_basic_block *block;
    struct miga80_ir_instruction *instruction;

    if (edge->block >= context->ir->block_count ||
        edge->instruction >= context->ir->instruction_count ||
        target >= context->ir->block_count) {
        return fail(context->diagnostic, 0U, 0U,
                    "unable to patch typed IR control edge");
    }
    block = &context->ir->blocks[edge->block];
    instruction = &context->ir->instructions[edge->instruction];
    if (edge->successor_index >= block->successor_count ||
        block->successors[edge->successor_index] != MIGA80_INVALID_BLOCK ||
        instruction->operand != MIGA80_INVALID_BLOCK ||
        (instruction->opcode != MIGA80_IR_JUMP &&
         instruction->opcode != MIGA80_IR_BRANCH_FALSE)) {
        return fail(context->diagnostic, instruction->line,
                    instruction->column,
                    "typed IR control edge was already patched");
    }
    block->successors[edge->successor_index] = target;
    instruction->operand = target;
    return 1;
}

static int finish_open_jump(struct lower_context *context,
                            unsigned int block, unsigned int line,
                            unsigned int column, struct lower_edge *edge)
{
    const unsigned int instruction = context->ir->instruction_count;

    if (!emit_instruction(context->ir, MIGA80_IR_JUMP, MIGA80_TYPE_NONE,
                          MIGA80_INVALID_BLOCK, line, column,
                          context->diagnostic) ||
        !finish_block(context, block, MIGA80_INVALID_BLOCK,
                      MIGA80_INVALID_BLOCK, 1U)) {
        return 0;
    }
    edge->block = (uint16_t)block;
    edge->instruction = (uint16_t)instruction;
    edge->successor_index = 0U;
    return 1;
}

static int record_loop_control(struct lower_context *context,
                               unsigned int block,
                               enum lower_loop_control_kind kind,
                               unsigned int line, unsigned int column)
{
    struct lower_edge *edge;

    if (context->loop_depth == 0U) {
        return fail(context->diagnostic, line, column,
                    "loop control is outside while");
    }
    if (context->control_edge_count == MIGA80_MAX_STATEMENTS) {
        return fail(context->diagnostic, line, column,
                    "too many pending loop-control edges");
    }
    edge = &context->control_edges[context->control_edge_count];
    if (!finish_open_jump(context, block, line, column, edge)) {
        return 0;
    }
    edge->kind = (unsigned char)kind;
    ++context->control_edge_count;
    return 1;
}

static int add_funnel_edge(struct lower_context *context,
                           const struct lower_edge *edge,
                           struct lower_edge *pending, int *has_pending,
                           unsigned int line, unsigned int column)
{
    unsigned int merge_block;

    if (!*has_pending) {
        *pending = *edge;
        *has_pending = 1;
        return 1;
    }
    merge_block = create_block(context);
    if (merge_block == MIGA80_INVALID_BLOCK ||
        !patch_edge(context, pending, merge_block) ||
        !patch_edge(context, edge, merge_block)) {
        return 0;
    }
    begin_block(context, merge_block);
    return finish_open_jump(context, merge_block, line, column, pending);
}

static int build_control_funnel(
    struct lower_context *context, unsigned int control_start,
    enum lower_loop_control_kind kind, const struct lower_edge *implicit_edge,
    int has_implicit_edge, unsigned int target, unsigned int line,
    unsigned int column)
{
    struct lower_edge pending;
    unsigned int index;
    int has_pending = 0;

    for (index = control_start; index < context->control_edge_count; ++index) {
        if (context->control_edges[index].kind == kind &&
            !add_funnel_edge(context, &context->control_edges[index],
                             &pending, &has_pending, line, column)) {
            return 0;
        }
    }
    if (has_implicit_edge &&
        !add_funnel_edge(context, implicit_edge, &pending, &has_pending,
                         line, column)) {
        return 0;
    }
    if (!has_pending) {
        return fail(context->diagnostic, line, column,
                    "loop-control funnel has no incoming edge");
    }
    return patch_edge(context, &pending, target);
}

static int lower_statement_list(struct lower_context *context,
                                unsigned int statement_index,
                                unsigned int *current_block)
{
    while (statement_index != MIGA80_INVALID_STATEMENT) {
        const struct miga80_ast_statement *statement;

        if (statement_index >= context->ast->statement_count ||
            context->visited_statements++ == context->ast->statement_count) {
            return fail(context->diagnostic, 0U, 0U,
                        "invalid or cyclic AST statement list");
        }
        statement = &context->ast->statements[statement_index];
        if (context->returned) {
            return fail(context->diagnostic, statement->line,
                        statement->column, "AST statement follows return");
        }
        if (statement->kind == MIGA80_AST_LOCAL_INITIALIZE ||
            statement->kind == MIGA80_AST_LOCAL_ASSIGN) {
            enum miga80_ir_opcode store_opcode;

            if (statement->local_index >= context->ast->local_count ||
                !lower_node(context->ast, statement->expression, context->ir,
                            context->diagnostic)) {
                if (statement->local_index >= context->ast->local_count) {
                    return fail(context->diagnostic, statement->line,
                                statement->column,
                                "AST local statement index is invalid");
                }
                return 0;
            }
            store_opcode =
                context->ast->local_types[statement->local_index] ==
                        MIGA80_TYPE_BOOL
                    ? MIGA80_IR_STORE_LOCAL_BOOL
                    : MIGA80_IR_STORE_LOCAL_I32;
            if (!emit_instruction(
                    context->ir, store_opcode,
                    context->ast->local_types[statement->local_index],
                    statement->local_index, statement->line,
                                  statement->column, context->diagnostic)) {
                return 0;
            }
        } else if (statement->kind == MIGA80_AST_CALL_PSET) {
            unsigned int argument;

            if (statement->argument_count != MIGA80_MAX_INTRINSIC_ARGUMENTS) {
                return fail(context->diagnostic, statement->line,
                            statement->column,
                            "pset AST argument count is invalid");
            }
            for (argument = 0U;
                 argument < MIGA80_MAX_INTRINSIC_ARGUMENTS; ++argument) {
                if (!lower_node(context->ast,
                                statement->arguments[argument],
                                context->ir, context->diagnostic)) {
                    return 0;
                }
            }
            if (!emit_instruction(context->ir, MIGA80_IR_CALL_PSET,
                                  MIGA80_TYPE_VOID,
                                  MIGA80_MAX_INTRINSIC_ARGUMENTS,
                                  statement->line, statement->column,
                                  context->diagnostic)) {
                return 0;
            }
        } else if (statement->kind == MIGA80_AST_IF) {
            unsigned int then_block;
            unsigned int else_block;
            unsigned int then_end;
            unsigned int else_end;
            struct lower_edge then_edge;
            struct lower_edge else_edge;
            int then_falls_through;
            int else_falls_through;

            if (!lower_node(context->ast, statement->expression, context->ir,
                            context->diagnostic)) {
                return 0;
            }
            then_block = create_block(context);
            else_block = create_block(context);
            if (then_block == MIGA80_INVALID_BLOCK ||
                else_block == MIGA80_INVALID_BLOCK ||
                !emit_instruction(context->ir, MIGA80_IR_BRANCH_FALSE,
                                  MIGA80_TYPE_BOOL, else_block, statement->line,
                                  statement->column, context->diagnostic) ||
                !finish_block(context, *current_block, then_block, else_block,
                              2U)) {
                return 0;
            }

            begin_block(context, then_block);
            then_end = then_block;
            if (!lower_statement_list(context, statement->then_statement,
                                      &then_end)) {
                return 0;
            }
            then_falls_through = then_end != MIGA80_INVALID_BLOCK;
            if (then_falls_through &&
                !finish_open_jump(context, then_end, statement->line,
                                  statement->column, &then_edge)) {
                return 0;
            }

            begin_block(context, else_block);
            else_end = else_block;
            if (!lower_statement_list(context, statement->else_statement,
                                      &else_end)) {
                return 0;
            }
            else_falls_through = else_end != MIGA80_INVALID_BLOCK;
            if (else_falls_through &&
                !finish_open_jump(context, else_end, statement->line,
                                  statement->column, &else_edge)) {
                return 0;
            }
            if (then_falls_through || else_falls_through) {
                const unsigned int join_block = create_block(context);

                if (join_block == MIGA80_INVALID_BLOCK ||
                    (then_falls_through &&
                     !patch_edge(context, &then_edge, join_block)) ||
                    (else_falls_through &&
                     !patch_edge(context, &else_edge, join_block))) {
                    return 0;
                }
                begin_block(context, join_block);
                *current_block = join_block;
            } else {
                *current_block = MIGA80_INVALID_BLOCK;
            }
        } else if (statement->kind == MIGA80_AST_WHILE) {
            unsigned int header_block;
            unsigned int body_block;
            unsigned int exit_block;
            unsigned int body_end;
            unsigned int latch_block = MIGA80_INVALID_BLOCK;
            const unsigned int control_start = context->control_edge_count;
            const uint32_t outer_loops = context->active_loops;
            struct lower_edge header_exit_edge;
            struct lower_edge body_edge;
            unsigned int control_index;
            unsigned int continue_count = 0U;

            header_block = create_block(context);
            body_block = create_block(context);
            if (header_block == MIGA80_INVALID_BLOCK ||
                body_block == MIGA80_INVALID_BLOCK ||
                !emit_instruction(context->ir, MIGA80_IR_JUMP,
                                  MIGA80_TYPE_NONE, header_block, statement->line,
                                  statement->column, context->diagnostic) ||
                !finish_block(context, *current_block, header_block,
                              MIGA80_INVALID_BLOCK, 1U)) {
                return 0;
            }
            context->active_loops |= UINT32_C(1) << header_block;
            context->ir->block_loop_membership[header_block] =
                context->active_loops;
            context->ir->block_loop_membership[body_block] =
                context->active_loops;

            begin_block(context, header_block);
            if (!lower_node(context->ast, statement->expression,
                            context->ir, context->diagnostic) ||
                !emit_instruction(context->ir, MIGA80_IR_BRANCH_FALSE,
                                  MIGA80_TYPE_BOOL, MIGA80_INVALID_BLOCK, statement->line,
                                  statement->column, context->diagnostic) ||
                !finish_block(context, header_block, body_block,
                              MIGA80_INVALID_BLOCK,
                              2U)) {
                return 0;
            }
            header_exit_edge.block = (uint16_t)header_block;
            header_exit_edge.instruction =
                (uint16_t)(context->ir->instruction_count - 1U);
            header_exit_edge.successor_index = 1U;

            begin_block(context, body_block);
            body_end = body_block;
            ++context->loop_depth;
            if (!lower_statement_list(context, statement->then_statement,
                                      &body_end)) {
                --context->loop_depth;
                return 0;
            }
            --context->loop_depth;
            if (body_end != MIGA80_INVALID_BLOCK &&
                !finish_open_jump(context, body_end, statement->line,
                                  statement->column, &body_edge)) {
                return 0;
            }
            for (control_index = control_start;
                 control_index < context->control_edge_count;
                 ++control_index) {
                if (context->control_edges[control_index].kind ==
                    LOWER_LOOP_CONTINUE) {
                    ++continue_count;
                }
            }
            if (body_end != MIGA80_INVALID_BLOCK) {
                ++continue_count;
            }
            if (continue_count != 0U) {
                latch_block = create_block(context);
            }
            exit_block = create_block(context);
            if ((continue_count != 0U &&
                 latch_block == MIGA80_INVALID_BLOCK) ||
                exit_block == MIGA80_INVALID_BLOCK) {
                return 0;
            }
            context->ir->block_loop_membership[exit_block] = outer_loops;

            if (continue_count != 0U) {
                if (!build_control_funnel(
                        context, control_start, LOWER_LOOP_CONTINUE,
                        &body_edge, body_end != MIGA80_INVALID_BLOCK,
                        latch_block, statement->line, statement->column)) {
                    return 0;
                }
                begin_block(context, latch_block);
                if (!emit_instruction(context->ir, MIGA80_IR_JUMP,
                                      MIGA80_TYPE_NONE, header_block, statement->line,
                                      statement->column,
                                      context->diagnostic) ||
                    !finish_block(context, latch_block, header_block,
                                  MIGA80_INVALID_BLOCK, 1U)) {
                    return 0;
                }
            }
            if (!build_control_funnel(
                    context, control_start, LOWER_LOOP_BREAK,
                    &header_exit_edge, 1, exit_block, statement->line,
                    statement->column)) {
                return 0;
            }
            context->control_edge_count = control_start;
            context->active_loops = outer_loops;
            begin_block(context, exit_block);
            *current_block = exit_block;
        } else if (statement->kind == MIGA80_AST_BREAK ||
                   statement->kind == MIGA80_AST_CONTINUE) {
            const enum lower_loop_control_kind kind =
                statement->kind == MIGA80_AST_BREAK ? LOWER_LOOP_BREAK
                                                   : LOWER_LOOP_CONTINUE;

            if (!record_loop_control(context, *current_block, kind,
                                     statement->line, statement->column)) {
                return 0;
            }
            *current_block = MIGA80_INVALID_BLOCK;
        } else if (statement->kind == MIGA80_AST_RETURN) {
            if (statement->next_statement != MIGA80_INVALID_STATEMENT ||
                (context->ast->result_type != MIGA80_TYPE_VOID &&
                 !lower_node(context->ast, statement->expression,
                             context->ir, context->diagnostic)) ||
                !emit_instruction(context->ir, MIGA80_IR_RETURN,
                                  context->ast->result_type, 0U,
                                  statement->line, statement->column,
                                  context->diagnostic) ||
                !finish_block(context, *current_block,
                              MIGA80_INVALID_BLOCK, MIGA80_INVALID_BLOCK,
                              0U)) {
                return 0;
            }
            context->returned = 1;
        } else {
            return fail(context->diagnostic, statement->line,
                        statement->column, "unknown AST statement kind");
        }
        statement_index = statement->next_statement;
        if (*current_block == MIGA80_INVALID_BLOCK &&
            statement_index != MIGA80_INVALID_STATEMENT) {
            const struct miga80_ast_statement *next =
                &context->ast->statements[statement_index];

            return fail(context->diagnostic, next->line, next->column,
                        "AST statement follows terminating control flow");
        }
    }
    return 1;
}

int miga80_lower_function(const struct miga80_ast_function *ast,
                          struct miga80_ir_function *ir,
                          struct miga80_diagnostic *diagnostic)
{
    struct lower_context context;
    unsigned int current_block;

    if (ast == NULL || ir == NULL || diagnostic == NULL) {
        return 0;
    }
    (void)memset(ir, 0, sizeof(*ir));
    (void)memset(diagnostic, 0, sizeof(*diagnostic));
    if (ast->parameter_count > MIGA80_MAX_PARAMETERS ||
        ast->local_count > MIGA80_MAX_LOCALS ||
        ast->node_count > MIGA80_MAX_AST_NODES ||
        ast->statement_count > MIGA80_MAX_STATEMENTS) {
        return fail(diagnostic, 0U, 0U,
                    "AST function exceeds bounded storage");
    }
    (void)memcpy(ir->name, ast->name, sizeof(ir->name));
    (void)memcpy(ir->parameter_types, ast->parameter_types,
                 sizeof(ir->parameter_types));
    (void)memcpy(ir->local_types, ast->local_types,
                 sizeof(ir->local_types));
    ir->parameter_count = ast->parameter_count;
    ir->local_count = ast->local_count;
    ir->result_type = ast->result_type;
    (void)memcpy(&ir->pool, &ast->pool, sizeof(ir->pool));
    (void)memset(&context, 0, sizeof(context));
    context.ast = ast;
    context.ir = ir;
    context.diagnostic = diagnostic;
    current_block = create_block(&context);
    if (current_block == MIGA80_INVALID_BLOCK) {
        return 0;
    }
    ir->entry_block = current_block;
    begin_block(&context, current_block);
    if (!lower_statement_list(&context, ast->first_statement,
                              &current_block) ||
        !context.returned ||
        context.visited_statements != ast->statement_count) {
        if (diagnostic->message[0] == '\0') {
            return fail(diagnostic, 0U, 0U,
                        "AST function has invalid statement coverage");
        }
        return 0;
    }
    return miga80_validate_ir(ir, diagnostic);
}

static int validate_block_stack(const struct miga80_ir_function *ir,
                                const struct miga80_ir_basic_block *block,
                                struct miga80_diagnostic *diagnostic)
{
    enum miga80_type stack[MIGA80_MAX_IR_STACK];
    unsigned int stack_size = 0U;
    unsigned int offset;

    if (block->instruction_count == 0U) {
        return fail(diagnostic, 0U, 0U, "typed IR basic block is empty");
    }

    for (offset = 0U; offset < block->instruction_count; ++offset) {
        const struct miga80_ir_instruction *instruction =
            &ir->instructions[block->first_instruction + offset];
        const int terminal =
            instruction->opcode == MIGA80_IR_BRANCH_FALSE ||
            instruction->opcode == MIGA80_IR_JUMP ||
            instruction->opcode == MIGA80_IR_RETURN;

        if (terminal && offset + 1U != block->instruction_count) {
            return fail(diagnostic, instruction->line, instruction->column,
                        "typed IR terminator is not last in block");
        }
        if (instruction->opcode == MIGA80_IR_PUSH_I32 ||
            instruction->opcode == MIGA80_IR_PUSH_FIX ||
            instruction->opcode == MIGA80_IR_PUSH_BOOL ||
            instruction->opcode == MIGA80_IR_PUSH_STRING ||
            instruction->opcode == MIGA80_IR_PUSH_SYMBOL ||
            instruction->opcode == MIGA80_IR_PUSH_PARAMETER_I32 ||
            instruction->opcode == MIGA80_IR_PUSH_PARAMETER_BOOL ||
            instruction->opcode == MIGA80_IR_PUSH_LOCAL_I32 ||
            instruction->opcode == MIGA80_IR_PUSH_LOCAL_BOOL) {
            const enum miga80_type type = instruction->type;
            const int bool_opcode =
                instruction->opcode == MIGA80_IR_PUSH_BOOL ||
                instruction->opcode == MIGA80_IR_PUSH_PARAMETER_BOOL ||
                instruction->opcode == MIGA80_IR_PUSH_LOCAL_BOOL;
            const int string_literal =
                instruction->opcode == MIGA80_IR_PUSH_STRING;
            const int symbol_literal =
                instruction->opcode == MIGA80_IR_PUSH_SYMBOL;
            const int nonbool_opcode =
                instruction->opcode == MIGA80_IR_PUSH_I32 ||
                instruction->opcode == MIGA80_IR_PUSH_PARAMETER_I32 ||
                instruction->opcode == MIGA80_IR_PUSH_LOCAL_I32;

            if (stack_size == MIGA80_MAX_IR_STACK) {
                return fail(diagnostic, instruction->line,
                            instruction->column, "typed IR stack overflow");
            }
            if ((bool_opcode && type != MIGA80_TYPE_BOOL) ||
                (nonbool_opcode &&
                 (type == MIGA80_TYPE_BOOL || !miga80_type_is_value(type))) ||
                (string_literal && type != MIGA80_TYPE_STRING) ||
                (symbol_literal && type != MIGA80_TYPE_SYMBOL) ||
                (instruction->opcode == MIGA80_IR_PUSH_BOOL &&
                 instruction->operand > 1U) ||
                (instruction->opcode == MIGA80_IR_PUSH_I32 &&
                 !miga80_integer_value_is_canonical(type,
                                                     instruction->operand)) ||
                (instruction->opcode == MIGA80_IR_PUSH_FIX &&
                 type != MIGA80_TYPE_FIX) ||
                ((string_literal || symbol_literal) &&
                 (instruction->operand >= ir->pool.entry_count ||
                  ir->pool.entries[instruction->operand].type != type)) ||
                ((instruction->opcode == MIGA80_IR_PUSH_PARAMETER_I32 ||
                  instruction->opcode == MIGA80_IR_PUSH_PARAMETER_BOOL) &&
                 (instruction->operand >= ir->parameter_count ||
                  ir->parameter_types[instruction->operand] != type)) ||
                ((instruction->opcode == MIGA80_IR_PUSH_LOCAL_I32 ||
                  instruction->opcode == MIGA80_IR_PUSH_LOCAL_BOOL) &&
                 (instruction->operand >= ir->local_count ||
                  ir->local_types[instruction->operand] != type))) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR value load has invalid type or index");
            }
            stack[stack_size++] = type;
        } else if (instruction->opcode == MIGA80_IR_STORE_LOCAL_I32 ||
                   instruction->opcode == MIGA80_IR_STORE_LOCAL_BOOL) {
            const enum miga80_type type = instruction->type;

            if (stack_size != 1U || instruction->operand >= ir->local_count ||
                (instruction->opcode == MIGA80_IR_STORE_LOCAL_BOOL
                     ? type != MIGA80_TYPE_BOOL
                     : type == MIGA80_TYPE_BOOL ||
                           !miga80_type_is_value(type)) ||
                ir->local_types[instruction->operand] != type ||
                stack[0] != type) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR local store has invalid type or stack");
            }
            stack_size = 0U;
        } else if (instruction->opcode == MIGA80_IR_NEG_I32) {
            if (stack_size < 1U ||
                !miga80_type_is_signed_numeric(instruction->type) ||
                stack[stack_size - 1U] != instruction->type) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR unary operation has invalid type");
            }
        } else if (instruction->opcode == MIGA80_IR_NORMALIZE_INTEGER) {
            if (stack_size < 1U ||
                !miga80_type_is_integer(instruction->type) ||
                stack[stack_size - 1U] != MIGA80_TYPE_I32) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR constant conversion has invalid type");
            }
            stack[stack_size - 1U] = instruction->type;
        } else if (instruction->opcode == MIGA80_IR_FIX_FROM_I32) {
            if (stack_size < 1U || instruction->type != MIGA80_TYPE_FIX ||
                stack[stack_size - 1U] != MIGA80_TYPE_I32) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR i32-to-fix conversion is invalid");
            }
            stack[stack_size - 1U] = MIGA80_TYPE_FIX;
        } else if (instruction->opcode == MIGA80_IR_I32_FROM_FIX) {
            if (stack_size < 1U || instruction->type != MIGA80_TYPE_I32 ||
                stack[stack_size - 1U] != MIGA80_TYPE_FIX) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR fix-to-i32 conversion is invalid");
            }
            stack[stack_size - 1U] = MIGA80_TYPE_I32;
        } else if (instruction->opcode == MIGA80_IR_ADD_I32 ||
                   instruction->opcode == MIGA80_IR_SUB_I32 ||
                   instruction->opcode == MIGA80_IR_MUL_I32 ||
                   instruction->opcode == MIGA80_IR_MUL_FIX ||
                   instruction->opcode == MIGA80_IR_DIV_FIX ||
                   instruction->opcode == MIGA80_IR_DIV_I32 ||
                   instruction->opcode == MIGA80_IR_DIV_U32 ||
                   instruction->opcode == MIGA80_IR_EQ_I32 ||
                   instruction->opcode == MIGA80_IR_NE_I32 ||
                   instruction->opcode == MIGA80_IR_EQ_BOOL ||
                   instruction->opcode == MIGA80_IR_NE_BOOL ||
                   instruction->opcode == MIGA80_IR_LT_I32 ||
                   instruction->opcode == MIGA80_IR_LE_I32 ||
                   instruction->opcode == MIGA80_IR_GT_I32 ||
                   instruction->opcode == MIGA80_IR_GE_I32 ||
                   instruction->opcode == MIGA80_IR_LT_U32 ||
                   instruction->opcode == MIGA80_IR_LE_U32 ||
                   instruction->opcode == MIGA80_IR_GT_U32 ||
                   instruction->opcode == MIGA80_IR_GE_U32) {
            const int comparison =
                (instruction->opcode >= MIGA80_IR_EQ_I32 &&
                 instruction->opcode <= MIGA80_IR_GE_I32) ||
                (instruction->opcode >= MIGA80_IR_LT_U32 &&
                 instruction->opcode <= MIGA80_IR_GE_U32);
            const int bool_comparison =
                instruction->opcode == MIGA80_IR_EQ_BOOL ||
                instruction->opcode == MIGA80_IR_NE_BOOL;
            const int signed_division =
                instruction->opcode == MIGA80_IR_DIV_I32;
            const int fix_division =
                instruction->opcode == MIGA80_IR_DIV_FIX;
            const int signed_comparison =
                instruction->opcode >= MIGA80_IR_LT_I32 &&
                instruction->opcode <= MIGA80_IR_GE_I32;
            const int unsigned_operation =
                instruction->opcode == MIGA80_IR_DIV_U32 ||
                (instruction->opcode >= MIGA80_IR_LT_U32 &&
                 instruction->opcode <= MIGA80_IR_GE_U32);
            const enum miga80_type operand_type = instruction->type;

            const int nonbool_equality =
                instruction->opcode == MIGA80_IR_EQ_I32 ||
                instruction->opcode == MIGA80_IR_NE_I32;
            const int add_or_sub =
                instruction->opcode == MIGA80_IR_ADD_I32 ||
                instruction->opcode == MIGA80_IR_SUB_I32;

            if ((bool_comparison && operand_type != MIGA80_TYPE_BOOL) ||
                (nonbool_equality &&
                 (operand_type == MIGA80_TYPE_BOOL ||
                  !miga80_type_is_value(operand_type))) ||
                (add_or_sub && !miga80_type_is_numeric(operand_type)) ||
                (instruction->opcode == MIGA80_IR_MUL_I32 &&
                 !miga80_type_is_integer(operand_type)) ||
                (instruction->opcode == MIGA80_IR_MUL_FIX &&
                 operand_type != MIGA80_TYPE_FIX) ||
                (fix_division && operand_type != MIGA80_TYPE_FIX) ||
                (signed_division &&
                 !miga80_type_is_signed_integer(operand_type)) ||
                (signed_comparison &&
                 !miga80_type_is_signed_numeric(operand_type)) ||
                (unsigned_operation &&
                 (!miga80_type_is_integer(operand_type) ||
                  miga80_type_is_signed_integer(operand_type))) ||
                stack_size < 2U || stack[stack_size - 1U] != operand_type ||
                stack[stack_size - 2U] != operand_type) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR binary operation has invalid type");
            }
            --stack_size;
            stack[stack_size - 1U] =
                comparison ? MIGA80_TYPE_BOOL : operand_type;
        } else if (instruction->opcode == MIGA80_IR_CALL_PSET) {
            if (instruction->type != MIGA80_TYPE_VOID ||
                instruction->operand != MIGA80_MAX_INTRINSIC_ARGUMENTS ||
                stack_size != MIGA80_MAX_INTRINSIC_ARGUMENTS ||
                stack[0] != MIGA80_TYPE_I32 ||
                stack[1] != MIGA80_TYPE_I32 ||
                stack[2] != MIGA80_TYPE_U8) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR pset call has invalid arguments");
            }
            stack_size = 0U;
        } else if (instruction->opcode == MIGA80_IR_BRANCH_FALSE) {
            if (instruction->type != MIGA80_TYPE_BOOL ||
                stack_size != 1U || stack[0] != MIGA80_TYPE_BOOL ||
                block->successor_count != 2U ||
                instruction->operand != block->successors[1]) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR conditional branch is invalid");
            }
            stack_size = 0U;
        } else if (instruction->opcode == MIGA80_IR_JUMP) {
            if (instruction->type != MIGA80_TYPE_NONE || stack_size != 0U ||
                block->successor_count != 1U ||
                instruction->operand != block->successors[0]) {
                return fail(diagnostic, instruction->line,
                            instruction->column, "typed IR jump is invalid");
            }
        } else if (instruction->opcode == MIGA80_IR_RETURN) {
            const int valid_stack =
                ir->result_type == MIGA80_TYPE_VOID
                    ? stack_size == 0U
                    : (stack_size == 1U && stack[0] == ir->result_type);

            if (instruction->type != ir->result_type || !valid_stack ||
                block->successor_count != 0U) {
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "typed IR return has invalid type or stack");
            }
            stack_size = 0U;
        } else {
            return fail(diagnostic, instruction->line, instruction->column,
                        "unknown typed IR instruction");
        }
    }
    {
        const enum miga80_ir_opcode terminator =
            ir->instructions[block->first_instruction +
                             block->instruction_count - 1U]
                .opcode;

        if (terminator != MIGA80_IR_BRANCH_FALSE &&
            terminator != MIGA80_IR_JUMP &&
            terminator != MIGA80_IR_RETURN) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR basic block has no terminator");
        }
    }
    return 1;
}

int miga80_validate_ir(const struct miga80_ir_function *ir,
                       struct miga80_diagnostic *diagnostic)
{
    unsigned char instruction_owners[MIGA80_MAX_IR_INSTRUCTIONS];
    uint32_t valid_loop_bits;
    unsigned int scalar_parameters = 0U;
    unsigned int address_parameters = 0U;
    unsigned int block_index;
    unsigned int index;

    if (ir == NULL || diagnostic == NULL) {
        return 0;
    }
    if (ir->parameter_count > MIGA80_MAX_PARAMETERS ||
        ir->local_count > MIGA80_MAX_LOCALS ||
        ir->instruction_count > MIGA80_MAX_IR_INSTRUCTIONS ||
        ir->block_count == 0U ||
        ir->block_count > MIGA80_MAX_BASIC_BLOCKS ||
        ir->entry_block >= ir->block_count) {
        return fail(diagnostic, 0U, 0U,
                    "typed IR function exceeds bounded storage");
    }
    if (ir->result_type != MIGA80_TYPE_VOID &&
        !miga80_type_is_value(ir->result_type)) {
        return fail(diagnostic, 0U, 0U,
                    "typed IR function has invalid result type");
    }
    for (index = 0U; index < ir->parameter_count; ++index) {
        if (!miga80_type_is_value(ir->parameter_types[index])) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR function has invalid parameter type");
        }
        if (miga80_type_is_address(ir->parameter_types[index])) {
            ++address_parameters;
        } else {
            ++scalar_parameters;
        }
    }
    if (scalar_parameters > MIGA80_ABI_MAX_SCALAR_ARGUMENTS ||
        address_parameters > MIGA80_ABI_MAX_ADDRESS_ARGUMENTS) {
        return fail(diagnostic, 0U, 0U,
                    "typed IR function exceeds register class ABI");
    }
    for (index = 0U; index < ir->local_count; ++index) {
        if (!miga80_type_is_value(ir->local_types[index])) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR function has invalid local type");
        }
    }
    if (!miga80_validate_constant_pool(&ir->pool)) {
        return fail(diagnostic, 0U, 0U,
                    "typed IR immutable pool is invalid");
    }
    valid_loop_bits =
        ir->block_count == MIGA80_MAX_BASIC_BLOCKS
            ? UINT32_MAX
            : (UINT32_C(1) << ir->block_count) - UINT32_C(1);
    (void)memset(instruction_owners, 0, sizeof(instruction_owners));
    for (block_index = 0U; block_index < ir->block_count; ++block_index) {
        const struct miga80_ir_basic_block *block = &ir->blocks[block_index];

        if (block->instruction_count == 0U ||
            block->first_instruction >= ir->instruction_count ||
            block->instruction_count >
                ir->instruction_count - block->first_instruction ||
            block->successor_count > MIGA80_MAX_BLOCK_SUCCESSORS) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR basic block has invalid bounds");
        }
        if ((ir->block_loop_membership[block_index] & ~valid_loop_bits) !=
            0U) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR block has invalid loop membership");
        }
        for (index = 0U; index < MIGA80_MAX_BLOCK_SUCCESSORS; ++index) {
            if ((index < block->successor_count &&
                 block->successors[index] >= ir->block_count) ||
                (index >= block->successor_count &&
                 block->successors[index] != MIGA80_INVALID_BLOCK)) {
                return fail(diagnostic, 0U, 0U,
                            "typed IR basic block has invalid successors");
            }
        }
        for (index = block->first_instruction;
             index < block->first_instruction + block->instruction_count;
             ++index) {
            if (instruction_owners[index] != 0U) {
                return fail(diagnostic, 0U, 0U,
                            "typed IR basic blocks overlap");
            }
            instruction_owners[index] = 1U;
        }
        if (!validate_block_stack(ir, block, diagnostic)) {
            return 0;
        }
        if (ir->instructions[block->first_instruction +
                             block->instruction_count - 1U]
                    .opcode == MIGA80_IR_BRANCH_FALSE &&
            ir->blocks[block->successors[0]].first_instruction !=
                block->first_instruction + block->instruction_count) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR true branch is not the fallthrough block");
        }
    }
    for (index = 0U; index < ir->instruction_count; ++index) {
        if (instruction_owners[index] == 0U) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR instruction is outside all blocks");
        }
    }
    return 1;
}

int miga80_evaluate_ir_with_runtime(
    const struct miga80_ir_function *ir, const uint32_t *arguments,
    unsigned int argument_count, uint32_t *result,
    const struct miga80_ir_runtime *runtime,
    struct miga80_diagnostic *diagnostic)
{
    uint32_t stack[MIGA80_MAX_IR_STACK];
    uint32_t locals[MIGA80_MAX_LOCALS];
    unsigned char local_initialized[MIGA80_MAX_LOCALS];
    unsigned int stack_size = 0U;
    unsigned int current_block;
    unsigned int executed_blocks = 0U;
    unsigned int index;

    if (ir == NULL || result == NULL || diagnostic == NULL ||
        (arguments == NULL && argument_count != 0U)) {
        return 0;
    }
    (void)memset(diagnostic, 0, sizeof(*diagnostic));
    (void)memset(locals, 0, sizeof(locals));
    (void)memset(local_initialized, 0, sizeof(local_initialized));
    if (!miga80_validate_ir(ir, diagnostic)) {
        return 0;
    }
    if (argument_count != ir->parameter_count) {
        return fail(diagnostic, 0U, 0U, "argument count does not match ABI");
    }

    for (index = 0U; index < argument_count; ++index) {
        if (ir->parameter_types[index] == MIGA80_TYPE_BOOL &&
            arguments[index] > 1U) {
            return fail(diagnostic, 0U, 0U,
                        "bool argument is not canonical");
        }
        if (miga80_type_is_integer(ir->parameter_types[index]) &&
            !miga80_integer_value_is_canonical(ir->parameter_types[index],
                                                arguments[index])) {
            return fail(diagnostic, 0U, 0U,
                        "integer argument is not canonical");
        }
        if (ir->parameter_types[index] == MIGA80_TYPE_STRING &&
            (arguments[index] == 0U ||
             arguments[index] > ir->pool.entry_count ||
             ir->pool.entries[arguments[index] - 1U].type !=
                 MIGA80_TYPE_STRING)) {
            return fail(diagnostic, 0U, 0U,
                        "string argument is not in the immutable pool");
        }
        if (ir->parameter_types[index] == MIGA80_TYPE_SYMBOL) {
            unsigned int entry;
            int found = 0;

            for (entry = 0U; entry < ir->pool.entry_count; ++entry) {
                if (ir->pool.entries[entry].type == MIGA80_TYPE_SYMBOL &&
                    arguments[index] != 0U &&
                    miga80_pool_symbol_id(&ir->pool, entry) ==
                    arguments[index]) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                return fail(diagnostic, 0U, 0U,
                            "symbol argument is not interned");
            }
        }
    }
    current_block = ir->entry_block;
    for (;;) {
        const struct miga80_ir_basic_block *block =
            &ir->blocks[current_block];
        int transferred = 0;
        unsigned int offset;

        if (++executed_blocks > MIGA80_MAX_EVALUATED_BLOCKS) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR control-flow budget exceeded");
        }
        for (offset = 0U; offset < block->instruction_count; ++offset) {
            const struct miga80_ir_instruction *instruction =
                &ir->instructions[block->first_instruction + offset];
            uint32_t left;
            uint32_t right;

            switch (instruction->opcode) {
            case MIGA80_IR_PUSH_I32:
            case MIGA80_IR_PUSH_FIX:
            case MIGA80_IR_PUSH_BOOL:
                stack[stack_size++] = instruction->operand;
                break;
            case MIGA80_IR_PUSH_STRING:
                stack[stack_size++] = instruction->operand + 1U;
                break;
            case MIGA80_IR_PUSH_SYMBOL:
                stack[stack_size++] = miga80_pool_symbol_id(
                    &ir->pool, instruction->operand);
                break;
            case MIGA80_IR_PUSH_PARAMETER_I32:
            case MIGA80_IR_PUSH_PARAMETER_BOOL:
                stack[stack_size++] = arguments[instruction->operand];
                break;
            case MIGA80_IR_PUSH_LOCAL_I32:
            case MIGA80_IR_PUSH_LOCAL_BOOL:
                if (!local_initialized[instruction->operand]) {
                    return fail(diagnostic, instruction->line,
                                instruction->column,
                                "typed IR local is uninitialized");
                }
                stack[stack_size++] = locals[instruction->operand];
                break;
            case MIGA80_IR_STORE_LOCAL_I32:
            case MIGA80_IR_STORE_LOCAL_BOOL:
                locals[instruction->operand] = stack[--stack_size];
                local_initialized[instruction->operand] = 1U;
                break;
            case MIGA80_IR_NEG_I32:
                stack[stack_size - 1U] = miga80_normalize_integer(
                    instruction->type, 0U - stack[stack_size - 1U]);
                break;
            case MIGA80_IR_NORMALIZE_INTEGER:
                stack[stack_size - 1U] = miga80_normalize_integer(
                    instruction->type, stack[stack_size - 1U]);
                break;
            case MIGA80_IR_FIX_FROM_I32:
                if (!miga80_convert_i32_to_fix(
                        stack[stack_size - 1U],
                        &stack[stack_size - 1U])) {
                    return fail(diagnostic, instruction->line,
                                instruction->column,
                                "conversion out of range");
                }
                break;
            case MIGA80_IR_I32_FROM_FIX:
                stack[stack_size - 1U] = miga80_convert_fix_to_i32(
                    stack[stack_size - 1U]);
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
                right = stack[--stack_size];
                left = stack[stack_size - 1U];
                if (instruction->opcode == MIGA80_IR_ADD_I32) {
                    stack[stack_size - 1U] = miga80_normalize_integer(
                        instruction->type, left + right);
                } else if (instruction->opcode == MIGA80_IR_SUB_I32) {
                    stack[stack_size - 1U] = miga80_normalize_integer(
                        instruction->type, left - right);
                } else if (instruction->opcode == MIGA80_IR_MUL_I32) {
                    stack[stack_size - 1U] = miga80_normalize_integer(
                        instruction->type, left * right);
                } else if (instruction->opcode == MIGA80_IR_MUL_FIX) {
                    stack[stack_size - 1U] =
                        miga80_multiply_fix(left, right);
                } else if (instruction->opcode == MIGA80_IR_DIV_FIX) {
                    if (!miga80_divide_fix(left, right,
                                           &stack[stack_size - 1U])) {
                        return fail(diagnostic, instruction->line,
                                    instruction->column,
                                    "division by zero");
                    }
                } else if (instruction->opcode == MIGA80_IR_DIV_I32) {
                    if (!miga80_divide_i32(left, right,
                                           &stack[stack_size - 1U])) {
                        return fail(diagnostic, instruction->line,
                                    instruction->column,
                                    "division by zero");
                    }
                    stack[stack_size - 1U] = miga80_normalize_integer(
                        instruction->type, stack[stack_size - 1U]);
                } else if (instruction->opcode == MIGA80_IR_DIV_U32) {
                    if (right == 0U) {
                        return fail(diagnostic, instruction->line,
                                    instruction->column,
                                    "division by zero");
                    }
                    stack[stack_size - 1U] = miga80_normalize_integer(
                        instruction->type, left / right);
                } else if (instruction->opcode == MIGA80_IR_EQ_I32 ||
                           instruction->opcode == MIGA80_IR_EQ_BOOL) {
                    stack[stack_size - 1U] = left == right ? 1U : 0U;
                } else if (instruction->opcode == MIGA80_IR_NE_I32 ||
                           instruction->opcode == MIGA80_IR_NE_BOOL) {
                    stack[stack_size - 1U] = left != right ? 1U : 0U;
                } else if (instruction->opcode == MIGA80_IR_LT_I32) {
                    stack[stack_size - 1U] =
                        (left ^ UINT32_C(0x80000000)) <
                                (right ^ UINT32_C(0x80000000))
                            ? 1U
                            : 0U;
                } else if (instruction->opcode == MIGA80_IR_LE_I32) {
                    stack[stack_size - 1U] =
                        (left ^ UINT32_C(0x80000000)) <=
                                (right ^ UINT32_C(0x80000000))
                            ? 1U
                            : 0U;
                } else if (instruction->opcode == MIGA80_IR_GT_I32) {
                    stack[stack_size - 1U] =
                        (left ^ UINT32_C(0x80000000)) >
                                (right ^ UINT32_C(0x80000000))
                            ? 1U
                            : 0U;
                } else if (instruction->opcode == MIGA80_IR_GE_I32) {
                    stack[stack_size - 1U] =
                        (left ^ UINT32_C(0x80000000)) >=
                                (right ^ UINT32_C(0x80000000))
                            ? 1U
                            : 0U;
                } else if (instruction->opcode == MIGA80_IR_LT_U32) {
                    stack[stack_size - 1U] = left < right ? 1U : 0U;
                } else if (instruction->opcode == MIGA80_IR_LE_U32) {
                    stack[stack_size - 1U] = left <= right ? 1U : 0U;
                } else if (instruction->opcode == MIGA80_IR_GT_U32) {
                    stack[stack_size - 1U] = left > right ? 1U : 0U;
                } else {
                    stack[stack_size - 1U] = left >= right ? 1U : 0U;
                }
                break;
            case MIGA80_IR_BRANCH_FALSE:
                current_block = stack[--stack_size] != 0U
                                    ? block->successors[0]
                                    : block->successors[1];
                transferred = 1;
                break;
            case MIGA80_IR_JUMP:
                current_block = block->successors[0];
                transferred = 1;
                break;
            case MIGA80_IR_CALL_PSET:
                {
                    const uint32_t color = stack[--stack_size];
                    const uint32_t y = stack[--stack_size];
                    const uint32_t x = stack[--stack_size];

                    if (runtime == NULL || runtime->pset == NULL ||
                        !runtime->pset(runtime->context, x, y, color)) {
                        return fail(diagnostic, instruction->line,
                                    instruction->column,
                                    "pset runtime service failed");
                    }
                }
                break;
            case MIGA80_IR_RETURN:
                *result = ir->result_type == MIGA80_TYPE_VOID ? 0U
                                                               : stack[0];
                return 1;
            default:
                return fail(diagnostic, instruction->line,
                            instruction->column,
                            "unknown typed IR instruction");
            }
            if (transferred) {
                break;
            }
        }
        if (!transferred) {
            return fail(diagnostic, 0U, 0U,
                        "typed IR block has no control transfer");
        }
    }
}

int miga80_evaluate_ir(const struct miga80_ir_function *ir,
                       const uint32_t *arguments, unsigned int argument_count,
                       uint32_t *result,
                       struct miga80_diagnostic *diagnostic)
{
    return miga80_evaluate_ir_with_runtime(
        ir, arguments, argument_count, result, NULL, diagnostic);
}
