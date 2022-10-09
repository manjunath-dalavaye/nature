#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "lir.h"
#include "src/debug/debug.h"
#include "src/semantic/analysis.h"
#include "utils/error.h"

lir_operand *set_indirect_addr(lir_operand *operand) {
    if (operand->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *var = operand->value;
        var->indirect_addr = true;
        return operand;
    } else if (operand->type == LIR_OPERAND_TYPE_ADDR) {
        lir_operand_addr *addr = operand->value;
        addr->indirect_addr = true;
        return operand;
    }

    error_exit("[set_indirect_addr] operand_type != LIR_OPERAND_TYPE_VAR or LIR_OPERAND_TYPE_ADDR, actual %d",
               operand->type);
    return NULL;
}

lir_operand *lir_new_addr_operand(lir_operand *base, int offset, type_base_t infer_size_type) {
    lir_operand_addr *addr_operand = malloc(sizeof(lir_operand_addr));
    addr_operand->base = base;
    addr_operand->offset = offset;
    addr_operand->infer_size_type = infer_size_type;

    lir_operand *operand = NEW(lir_operand);
    operand->type = LIR_OPERAND_TYPE_ADDR;
    operand->value = addr_operand;
    return operand;
}

lir_op *lir_op_runtime_call(char *name, lir_operand *result, int arg_count, ...) {
    lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
    params_operand->count = 0;

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand *param = va_arg(args, lir_operand*);
        params_operand->list[params_operand->count++] = param;
    }
    va_end(args);
    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);
    return lir_op_new(LIR_OPCODE_RUNTIME_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

lir_op *lir_op_builtin_call(char *name, lir_operand *result, int arg_count, ...) {
    lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
    params_operand->count = 0;

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand *param = va_arg(args, lir_operand*);
        params_operand->list[params_operand->count++] = param;
    }
    va_end(args);
    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);
    return lir_op_new(LIR_OPCODE_BUILTIN_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

lir_op *lir_op_call(char *name, lir_operand *result, int arg_count, ...) {
    lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
    params_operand->count = 0;

    va_list args;
    va_start(args, arg_count); // 初始化参数
    for (int i = 0; i < arg_count; ++i) {
        lir_operand *param = va_arg(args, lir_operand*);
        params_operand->list[params_operand->count++] = param;
    }
    va_end(args);
    lir_operand *call_params_operand = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_ACTUAL_PARAM, params_operand);
    return lir_op_new(LIR_OPCODE_CALL, lir_new_label_operand(name, false), call_params_operand, result);
}

/**
 * 临时变量是否影响变量入栈？
 * @param type
 * @return
 */
lir_operand *lir_new_temp_var_operand(closure *c, type_t type) {
    string unique_ident = LIR_UNIQUE_NAME(TEMP_IDENT);

    symbol_set_temp_ident(unique_ident, type);
    lir_local_var_decl *local = lir_new_local_var_decl(c, unique_ident, type);
    list_push(c->local_var_decls, local);

    return LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, lir_new_var_operand(c, unique_ident));
}

lir_operand *lir_new_label_operand(char *ident, bool is_local) {
    lir_operand_symbol_label *label = NEW(lir_operand_symbol_label);
    label->ident = ident;
    label->is_local = is_local;

    lir_operand *operand = NEW(lir_operand);
    operand->type = LIR_OPERAND_TYPE_SYMBOL_LABEL;
    operand->value = label;
    return operand;
}

lir_op *lir_op_label(char *ident, bool is_local) {
    return lir_op_new(LIR_OPCODE_LABEL, NULL, NULL, lir_new_label_operand(ident, is_local));
}

lir_op *lir_op_unique_label(char *ident) {
    return lir_op_label(LIR_UNIQUE_NAME(ident), true);
}

lir_op *lir_op_bal(lir_operand *label) {
    return lir_op_new(LIR_OPCODE_BAL, NULL, NULL, label);
}

lir_op *lir_op_move(lir_operand *dst, lir_operand *src) {
    return lir_op_new(LIR_OPCODE_MOVE, src, NULL, dst);
}

lir_op *lir_op_new(lir_op_type type, lir_operand *first, lir_operand *second, lir_operand *result) {
    // 变量 copy,避免寄存器分配时相互粘连
    if (first != NULL && first->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *operand_var = first->value;
        first = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, LIR_COPY_VAR_OPERAND(operand_var));
    }

    if (second != NULL && second->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *operand_var = second->value;
        second = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, LIR_COPY_VAR_OPERAND(operand_var));
    }

    if (result != NULL && result->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *operand_var = result->value;

        result = LIR_NEW_OPERAND(LIR_OPERAND_TYPE_VAR, LIR_COPY_VAR_OPERAND(operand_var));
    }

    lir_op *op = NEW(lir_op);
    op->code = type;
    op->first = first;
    op->second = second;
    op->result = result;

#ifdef DEBUG_COMPILER_LIR
    debug_lir(lir_line, op);
#endif

    return op;
}

closure *lir_new_closure(ast_closure_t *ast) {
    closure *new = NEW(closure);
    new->name = ast->function->name;
    new->env_name = ast->env_name;
    new->parent = NULL;
    new->operations = NULL;
    new->entry = NULL;
    new->globals.count = 0;
    new->fixed_regs = slice_new(); // reg_t
    new->blocks = slice_new(); // lir_basic_block
    new->order_blocks = slice_new(); // lir_basic_block

    new->interval_table = table_new();

    new->local_var_decl_table = table_new();
    new->local_var_decls = list_new();
    new->formal_params = list_new();
    new->stack_length = 0;

    return new;
}

lir_basic_block *lir_new_basic_block() {
    lir_basic_block *basic_block = NEW(lir_basic_block);
    basic_block->operations = list_new();
    basic_block->preds = slice_new();
    basic_block->succs = slice_new();

    basic_block->forward_succs = slice_new();
    basic_block->incoming_forward_count = 0;
    basic_block->use.count = 0;
    basic_block->def.count = 0;
    basic_block->live_in.count = 0;
    basic_block->live_out.count = 0;
    basic_block->dom = slice_new();
    basic_block->df = slice_new();
    basic_block->be_idom = slice_new();
    basic_block->loop.tree_high = 0;
    basic_block->loop.index = 0;
    basic_block->loop.depth = 0;
    basic_block->loop.flag = 0;

    return basic_block;
}

bool lir_blocks_contains(slice_t *blocks, uint8_t label) {
    for (int i = 0; i < blocks->count; ++i) {
        if (((lir_basic_block *) blocks->take[i])->label == label) {
            return true;
        }
    }
    return false;
}

lir_operand *lir_new_phi_body(lir_operand_var *var, uint8_t count) {
    lir_operand *operand = NEW(lir_operand);

    lir_operand_phi_body *phi_body = NEW(lir_operand_phi_body);
    for (int i = 0; i < count; ++i) {
        phi_body->list[i] = LIR_NEW_VAR_OPERAND(var->ident);
    }

    operand->type = LIR_OPERAND_TYPE_PHI_BODY;
    operand->value = phi_body;
    return operand;
}

lir_operand_var *lir_new_var_operand(closure *c, char *ident) {
    lir_operand_var *var = NEW(lir_operand_var);
    var->ident = ident;
    var->old = ident;
    var->reg_id = 0;

    // 1. 读取符号信息
    lir_local_var_decl *local = table_get(c->local_var_decl_table, ident);
    var->decl = local;
    var->infer_size_type = local->ast_type.base;

    return var;
}

lir_local_var_decl *lir_new_local_var_decl(closure *c, char *ident, type_t type) {
    lir_local_var_decl *local = NEW(lir_local_var_decl);
    local->ast_type = type;
    local->stack_offset = NEW(uint16_t);
    *local->stack_offset = 0;
    local->ident = ident;
    table_set(c->local_var_decl_table, ident, local);
    return local;
}

lir_operand *lir_new_empty_operand() {
    lir_operand *operand = NEW(lir_operand);
    operand->type = 0;
    operand->value = NULL;
    return operand;
}

type_base_t lir_operand_type_base(lir_operand *operand) {
    if (operand->type == LIR_OPERAND_TYPE_VAR) {
        lir_operand_var *var = operand->value;
        return var->infer_size_type;
    }

    if (operand->type == LIR_OPERAND_TYPE_ADDR) {
        lir_operand_addr *addr = operand->value;
        return addr->infer_size_type;
    }

    if (operand->type == LIR_OPERAND_TYPE_SYMBOL_VAR) {
        lir_operand_symbol_var *s = operand->value;
        return s->type;
    }

    if (operand->type == LIR_OPERAND_TYPE_IMM) {
        lir_operand_immediate *imm = operand->value;
        return imm->type;
    }

    return TYPE_UNKNOWN;
}

uint8_t lir_operand_sizeof(lir_operand *operand) {
    return type_base_sizeof(lir_operand_type_base(operand));
}

lir_vars lir_vars_by_operand(lir_operand *operand) {
    lir_vars result = {0};
    if (!operand) {
        return result;
    }

    if (operand->type == LIR_OPERAND_TYPE_VAR) {
        result.list[result.count++] = operand->value;
    }

    if (operand->type == LIR_OPERAND_TYPE_ACTUAL_PARAM) {
        lir_operand_actual_param *operands = operand->value;
        for (int i = 0; i < operands->count; ++i) {
            lir_operand *o = operands->list[i];
            if (o->type == LIR_OPERAND_TYPE_ACTUAL_PARAM) {
                error_exit("[lir_vars_by_operand] ACTUAL_PARAM nesting is not allowed");
            }

            if (o->type == LIR_OPERAND_TYPE_VAR) {
                result.list[result.count++] = o->value;
            }
        }
    }

    return result;
}

lir_vars lir_input_vars(lir_op *op) {
    lir_vars result = lir_vars_by_operand(op->first);
    lir_vars temp = lir_vars_by_operand(op->second);
    for (int i = 0; i < temp.count; ++i) {
        result.list[result.count++] = temp.list[i];
    }
    return result;
}

lir_vars lir_output_vars(lir_op *op) {
    return lir_vars_by_operand(op->result);
}
