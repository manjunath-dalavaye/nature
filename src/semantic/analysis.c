#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#include "utils/helper.h"
#include "analysis.h"
#include "utils/error.h"
#include "utils/table.h"
#include "utils/slice.h"
#include "src/symbol/symbol.h"
#include "src/debug/debug.h"

static void analysis_block(module_t *m, slice_t *block) {
    for (int i = 0; i < block->count; ++i) {
        // switch 结构导向优化
#ifdef DEBUG_ANALYSIS
        debug_stmt("ANALYSIS", block->tack[i]);
#endif
        analysis_stmt(m, block->take[i]);
    }
}

static char *analysis_resolve_type(module_t *m, analysis_fndef_t *current, string ident) {
    local_scope_t *current_scope = current->current_scope;
    while (current_scope != NULL) {
        for (int i = 0; i < current_scope->idents->count; ++i) {
            local_ident_t *local = current_scope->idents->take[i];
            if (strcmp(ident, local->ident) == 0) {
                return local->unique_ident;
            }
        }

        current_scope = current_scope->parent;
    }

    if (current->parent == NULL) {
        error_type_not_found(m->analysis_line, ident);
    }

    return analysis_resolve_type(m, current->parent, ident);
}

/**
 * 类型的处理较为简单，不需要做将其引用的环境封闭。只需要类型为 typedef ident 时能够定位唯一名称即可
 * @param type
 */
static void analysis_typeuse(module_t *m, typeuse_t *type) {
    // 如果只是简单的 ident,又应该如何改写呢？
    // type foo = int
    // 'foo' is type_decl_ident
    if (type->kind == TYPE_IDENT) {
        // 向上查查查
        typeuse_ident_t *ident = type->ident;
        string unique_name = analysis_resolve_type(m, m->analysis_current, ident->literal);
        ident->literal = unique_name;
        return;
    }

    if (type->kind == TYPE_MAP) {
        typeuse_map_t *map_decl = type->map;
        analysis_typeuse(m, &map_decl->key_type);
        analysis_typeuse(m, &map_decl->value_type);
        return;
    }

    if (type->kind == TYPE_SET) {
        typeuse_set_t *set = type->set;
        analysis_typeuse(m, &set->key_type);
        return;
    }

    if (type->kind == TYPE_LIST) {
        typeuse_list_t *list_decl = type->list;
        analysis_typeuse(m, &list_decl->element_type);
        return;
    }

    if (type->kind == TYPE_TUPLE) {
        typeuse_tuple_t *tuple = type->tuple;
        for (int i = 0; i < tuple->elements->length; ++i) {
            typeuse_t *element_type = ct_list_value(tuple->elements, i);
            analysis_typeuse(m, element_type);
        }
        return;
    }

    if (type->kind == TYPE_FN) {
        typeuse_fn_t *type_fn = type->fn;
        analysis_typeuse(m, &type_fn->return_type);
        for (int i = 0; i < type_fn->formal_types->length; ++i) {
            typeuse_t *t = ct_list_value(type_fn->formal_types, i);
            analysis_typeuse(m, t);
        }
    }

    if (type->kind == TYPE_STRUCT) {
        typeuse_struct_t *struct_decl = type->struct_;
        for (int i = 0; i < struct_decl->properties->length; ++i) {
            struct_property_t *item = ct_list_value(struct_decl->properties, i);
            analysis_typeuse(m, &item->type);

            // 可选右值解析
            if (item->right) {
                ast_expr *expr = item->right;
                if (expr->assert_type == AST_FNDEF) {
                    ast_fndef_t *fndef = expr->value;
                    fndef->self_struct = type;
                }

                analysis_expr(m, item->right);
            }
        }
    }
}

/**
 * @param name
 * @return
 */
static char *analysis_unique_ident(module_t *m, char *name) {
    char *unique_name = LIR_UNIQUE_NAME(name);
    return ident_with_module(m->ident, unique_name);
}

/**
 * TODO 不同 module 直接有同名的 ident 怎么处理？
 * code 可能还是 var 等待推导,但是基础信息已经填充完毕了
 * @param type
 * @param ident
 * @return
 */
local_ident_t *local_ident_new(module_t *m, symbol_type type, void *decl, string ident) {
    // unique ident
    string unique_ident = analysis_unique_ident(m, ident);

    local_ident_t *local = malloc(sizeof(local_ident_t));
    local->ident = ident;
    local->unique_ident = unique_ident;
    local->scope_depth = m->analysis_current->scope_depth;
    local->decl = decl;
    local->type = type;

    // 添加 locals
    local_scope_t *current_scope = m->analysis_current->current_scope;
    slice_push(current_scope->idents, local);

    // 添加到全局符号表
    symbol_table_set(local->unique_ident, type, decl, true);

    return local;
}

static local_scope_t *local_scope_new(uint8_t scope_depth, local_scope_t *parent) {
    local_scope_t *new = malloc(sizeof(local_scope_t));
    new->idents = slice_new();
    new->scope_depth = scope_depth;
    new->parent = parent;
    return new;
}

/**
 * 块级作用域处理
 */
static void analysis_begin_scope(module_t *m) {
    m->analysis_current->scope_depth++;
    local_scope_t *current_scope = m->analysis_current->current_scope;
    m->analysis_current->current_scope = local_scope_new(m->analysis_current->scope_depth, current_scope);
}

static void analysis_end_scope(module_t *m) {
    local_scope_t *current_scope = m->analysis_current->current_scope;
    m->analysis_current->current_scope = current_scope->parent;
    m->analysis_current->scope_depth--;
}


static void analysis_if(module_t *m, ast_if_stmt *if_stmt) {
    analysis_expr(m, &if_stmt->condition);

    analysis_begin_scope(m);
    analysis_block(m, if_stmt->consequent);
    analysis_end_scope(m);

    analysis_begin_scope(m);
    analysis_block(m, if_stmt->alternate);
    analysis_end_scope(m);
}

static void analysis_assign(module_t *m, ast_assign_stmt *assign) {
    analysis_expr(m, &assign->left);
    analysis_expr(m, &assign->right);
}

static void analysis_throw(module_t *m, ast_throw_stmt *throw) {
    analysis_expr(m, &throw->error);
}

static void analysis_call(module_t *m, ast_call *call) {
    // 函数地址 unique 改写
    analysis_expr(m, &call->left);

    // 实参 unique 改写
    for (int i = 0; i < call->actual_params->length; ++i) {
        ast_expr *actual_param = ct_list_value(call->actual_params, i);
        analysis_expr(m, actual_param);
    }
}


/**
 * 检查当前作用域及当前 scope 是否重复定义了 ident
 * @param ident
 * @return
 */
static bool analysis_redeclare_check(module_t *m, char *ident) {
    local_scope_t *current_scope = m->analysis_current->current_scope;
    for (int i = 0; i < current_scope->idents->count; ++i) {
        local_ident_t *local = current_scope->idents->take[i];
        if (strcmp(ident, local->ident) == 0) {
            error_redeclare_ident(m->analysis_line, ident);
            return false;
        }
    }

    return true;
}

/**
 * 当子作用域中重新定义了变量，产生了同名变量时，则对变量进行重命名
 * @param var_decl
 */
static void analysis_var_decl(module_t *m, ast_var_decl *var_decl) {
    analysis_redeclare_check(m, var_decl->ident);

    analysis_typeuse(m, &var_decl->type);

    local_ident_t *local = local_ident_new(m, SYMBOL_VAR, var_decl, var_decl->ident);

    // 改写
    var_decl->ident = local->unique_ident;
}

static void analysis_vardef(module_t *m, ast_vardef_stmt *stmt) {
    analysis_expr(m, &stmt->right);
    analysis_redeclare_check(m, stmt->var_decl.ident);
    analysis_typeuse(m, &stmt->var_decl.type);
   
    local_ident_t *local = local_ident_new(m, SYMBOL_VAR, &stmt->var_decl, stmt->var_decl.ident);
    stmt->var_decl.ident = local->unique_ident;
}

static void analysis_var_tuple_destr(module_t *m, ast_tuple_destr *tuple_destr) {
    for (int i = 0; i < tuple_destr->elements->length; ++i) {
        ast_expr *expr = ct_list_value(tuple_destr->elements, i);
        // 要么是 ast_var_decl 要么是 ast_tuple_destr
        if (expr->assert_type == AST_VAR_DECL) {
            analysis_var_decl(m, expr->value);
        } else if (expr->assert_type == AST_STMT_VAR_TUPLE_DESTR) {
            analysis_var_tuple_destr(m, expr->value);
        } else {
            assertf(false, "var tuple destr expr type exception");
        }
    }
}

/**
 * var (a, b, (c, d)) = xxxx
 * @param m
 * @param stmt
 */
static void analysis_var_tuple_destr_stmt(module_t *m, ast_var_tuple_def_stmt *stmt) {
    analysis_expr(m, &stmt->right);
    analysis_var_tuple_destr(m, stmt->tuple_destr);
}

static void analysis_fndef_name(module_t *m, ast_fndef_t *fndef) {
    // 仅 fun 再次定义 as 才需要再次添加到符号表
    if (!str_equal(fndef->name, FN_MAIN_NAME)) {
        if (strlen(fndef->name) == 0) {
            // 如果没有函数名称，则添加匿名函数名称
            fndef->name = analysis_unique_ident(m, ANONYMOUS_FN_NAME);
        }

        analysis_redeclare_check(m, fndef->name);

        // 函数名称改写
        local_ident_t *local = local_ident_new(m, SYMBOL_FN, fndef, fndef->name);

        fndef->name = local->unique_ident;
    }
}

static void analysis_function_begin(module_t *m) {
    analysis_begin_scope(m);
}

static void analysis_function_end(module_t *m) {
    analysis_end_scope(m);

    m->analysis_current = m->analysis_current->parent;
}

static analysis_fndef_t *analysis_current_init(module_t *m, local_scope_t *scope, char *fn_name) {
    analysis_fndef_t *new = malloc(sizeof(analysis_fndef_t));
    new->frees = slice_new();
    new->free_table = table_new();
    new->delay_fndefs = ct_list_new(sizeof(delay_fndef_t));
    new->scope_depth = 0;
    new->fn_name = fn_name;
    new->current_scope = scope;

    // 继承关系
    new->parent = m->analysis_current;
    m->analysis_current = new;

    return m->analysis_current;
}


/**
 * env 中仅包含自由变量，不包含 function 原有的形参,且其还是形参的一部分
 *
 * fn<void(int, int)> foo = void (int a, int b) {
 *
 * }
 *
 * 函数此时有两个名称,所以需要添加两次符号表
 * 其中 foo 属于 var[code == fn]
 * bar 属于 label
 * fn(int, int) foo = fn bar(int a, int b) {
 *
 * }
 * @param fndef
 * @return
 */
static void analysis_fndef(module_t *m, ast_fndef_t *fndef, local_scope_t *scope) {
    fndef->envs = ct_list_new(sizeof(ast_expr));

    analysis_typeuse(m, &fndef->return_type);

    // 初始化
    analysis_current_init(m, scope, fndef->name);

    // 开启一个新的 function 作用域
    analysis_function_begin(m);

    // 函数形参处理
    for (int i = 0; i < fndef->formals->length; ++i) {
        ast_var_decl *param = ct_list_value(fndef->formals, i);

        // type 中引用了 typedef ident 到话,同样需要更新引用
        analysis_typeuse(m, &param->type);

        // 注册(param->ident 此时已经随 param->type 一起写入到了 symbol 中)
        local_ident_t *param_local = local_ident_new(m, SYMBOL_VAR, param, param->ident);

        // 将 ast 中到 param->ident 进行改写
        param->ident = param_local->unique_ident;
    }

    // 分析请求体 block, 其中进行了自由变量的捕获/改写和局部变量改写
    analysis_block(m, fndef->body);

    // 注意，环境中的自由变量捕捉是基于 current_function->parent 进行的
    // free 是在外部环境构建 env 的。
    // current_function->env_unique_name = unique_var_ident(ENV_IDENT);
//    closure->env_name = m->analysis_current->env_unique_name;

    // 构造 env
    for (int i = 0; i < m->analysis_current->frees->count; ++i) {
        free_ident_t *free_var = m->analysis_current->frees->take[i];
        ast_expr expr;

        // 调用函数中引用的逃逸变量就在当前作用域中
        if (free_var->is_local) {
            // ast_ident 表达式
            expr.assert_type = AST_EXPR_IDENT;
            expr.value = ast_new_ident(free_var->ident);
        } else {
            // ast_env_index 表达式
            expr.assert_type = AST_EXPR_ENV_ACCESS;
            ast_env_value *access_env = malloc(sizeof(ast_env_value));
            access_env->env = ast_new_ident(m->analysis_current->parent->fn_name);
            access_env->index = free_var->env_index;
            access_env->unique_ident = free_var->ident;
            expr.value = access_env;
        }

        ct_list_push(fndef->envs, &expr);
    }

    // 对当前 fndef 中对所有 sub fndef 进行 analysis
    for (int i = 0; i < m->analysis_current->delay_fndefs->length; ++i) {
        delay_fndef_t *d = ct_list_value(m->analysis_current->delay_fndefs, i);
        analysis_fndef(m, d->fndef, d->scope);
        // 将所有对 fndef 都加入到 flat fndefs 中, 且没有 parent 关系想关联。
        slice_push(m->ast_fndefs, d->fndef);
    }

    analysis_function_end(m); // 退出当前 current
}

/**
 * @param current
 * @param is_local
 * @param index
 * @param ident
 * @return
 */
static int analysis_push_free(analysis_fndef_t *current, bool is_local, int index, string ident) {
    // if exists, return exists index
    free_ident_t *free = table_get(current->free_table, ident);
    if (free) {
        return free->index;
    }

    // 新增 free
    free = NEW(local_ident_t);
    free->is_local = is_local;
    free->env_index = index;
    free->ident = ident;
    int free_index = slice_push(current->frees, free);
    free->index = free_index;
    table_set(current->free_table, ident, free);

    return free_index;
}

/**
 * 返回 top scope ident index
 * @param current
 * @param ident
 * @return
 */
int8_t analysis_resolve_free(analysis_fndef_t *current, string*ident) {
    if (current->parent == NULL) {
        return -1;
    }

    local_scope_t *scope = current->parent->current_scope;
    for (int i = 0; i < scope->idents->count; ++i) {
        local_ident_t *local = scope->idents->take[i];

        // 在父级作用域找到对应的 ident
        if (strcmp(*ident, local->ident) == 0) {
            local->is_capture = true;
            *ident = local->unique_ident;
            return (int8_t) analysis_push_free(current, true, i, *ident);
        }
    }

    // 一级 parent 没有找到，则继续向上 parent 递归查询
    int8_t parent_free_index = analysis_resolve_free(current->parent, ident);
    if (parent_free_index != -1) {
        // 在更高级的某个 parent 中找到了符号，则在 current 中添加对逃逸变量的引用处理
        return (int8_t) analysis_push_free(current, false, parent_free_index, *ident);
    }

    return -1;
}

/**
 * @param expr
 */
static void analysis_ident(module_t *m, ast_expr *expr) {
    ast_ident *ident = expr->value;

    // 在当前函数作用域中查找变量定义
    local_scope_t *current_scope = m->analysis_current->current_scope;
    while (current_scope != NULL) {
        for (int i = 0; i < current_scope->idents->count; ++i) {
            local_ident_t *local = current_scope->idents->take[i];
            if (strcmp(ident->literal, local->ident) == 0) {
                // 在本地变量中找到,则进行简单改写 (从而可以在符号表中有唯一名称,方便定位)
                expr->value = ast_new_ident(local->unique_ident);
                return;
            }
        }
        current_scope = current_scope->parent;
    }

    // 非本地作用域变量则查找父仅查找, 如果是自由变量则使用 env_n[free_var_index] 进行改写
    int8_t free_var_index = analysis_resolve_free(m->analysis_current, &ident->literal);
    if (free_var_index != -1) {
        // 如果使用的 ident 是逃逸的变量，则需要使用 access_env 代替
        // 假如 foo 是外部变量，则 foo 改写成 env[free_var_index] 从而达到闭包的效果
        expr->assert_type = AST_EXPR_ENV_ACCESS;
        ast_env_value *env_index = malloc(sizeof(ast_env_value));
        env_index->env = ast_new_ident(m->analysis_current->fn_name);
        env_index->index = free_var_index;
        env_index->unique_ident = ident->literal;
        expr->value = env_index;
        return;
    }

    // 如果是 xxx.xxx 这样的访问方式在 selector key 中已经进行了处理, 但是有部分 builtin 的全局符号 比如 print/set 等
    symbol_t *s = table_get(symbol_table, ident->literal);
    if (s != NULL) {
        return;
    }


    // 当前 module 中的全局符号是可以省略 module name 的, 所以需要在当前 module 的全局符号(当前 module 注册的符号都加上了前缀)中查找
    // 所以这里要拼接前缀
    char *global_ident = ident_with_module(m->ident, ident->literal);
    s = table_get(symbol_table, global_ident);
    if (s != NULL) {
        ident->literal = global_ident; // 完善访问名称
        return;
    }

    assertf(false, "ident not found line: %d, identifier '%s' undeclared \n", expr->line, ident->literal);
}


static void analysis_access(module_t *m, ast_access *access) {
    analysis_expr(m, &access->left);
    analysis_expr(m, &access->key);
}

/*
 * 如果是 import_path.test 则进行符号改写, 改写成 namespace + module_name
 * struct.key , instance? 则不做任何对处理。
 */
static void analysis_select(module_t *m, ast_expr *expr) {
    ast_select *select = expr->value;
    if (select->left.assert_type != AST_EXPR_IDENT) {
        analysis_expr(m, &select->left);
        return;
    }
    ast_ident *ident = select->left.value;
    ast_import *import = table_get(m->import_table, ident->literal);
    if (import) {
        // 这里直接将 module.select 改成了全局唯一名称，彻底消灭了select ！
        char *unique_ident = ident_with_module(import->module_ident, select->key);
        expr->assert_type = AST_EXPR_IDENT;
        expr->value = ast_new_ident(unique_ident);
        return;
    }

    analysis_expr(m, &select->left);
}

static void analysis_binary(module_t *m, ast_binary_expr *expr) {
    analysis_expr(m, &expr->left);
    analysis_expr(m, &expr->right);
}

static void analysis_unary(module_t *m, ast_unary_expr *expr) {
    analysis_expr(m, &expr->operand);
}

/**
 * person {
 *     foo: bar
 * }
 * @param expr
 */
static void analysis_struct_new(module_t *m, ast_struct_new_t *expr) {
    analysis_typeuse(m, &expr->type);

    for (int i = 0; i < expr->properties->length; ++i) {
        ast_struct_property *property = ct_list_value(expr->properties, i);
        analysis_expr(m, &property->value);
    }
}

static void analysis_map_new(module_t *m, ast_map_new *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_map_element *element = ct_list_value(expr->elements, i);
        analysis_expr(m, &element->key);
        analysis_expr(m, &element->value);
    }
}


static void analysis_set_new(module_t *m, ast_set_new *expr) {
    for (int i = 0; i < expr->keys->length; ++i) {
        ast_expr *key = ct_list_value(expr->keys, i);
        analysis_expr(m, key);
    }
}

static void analysis_tuple_new(module_t *m, ast_tuple_new *expr) {
    for (int i = 0; i < expr->elements->length; ++i) {
        ast_expr *element = ct_list_value(expr->elements, i);
        analysis_expr(m, element);
    }
}

/**
 * tuple_destr = tuple_new
 * (a.b, a, (c, c.d)) = (1, 2)
 * @param m
 * @param expr
 */
static void analysis_tuple_destr(module_t *m, ast_tuple_destr *tuple) {
    for (int i = 0; i < tuple->elements->length; ++i) {
        ast_expr *element = ct_list_value(tuple->elements, i);
        analysis_expr(m, element);
    }
}


static void analysis_list_new(module_t *m, ast_list_new *expr) {
    for (int i = 0; i < expr->values->length; ++i) {
        ast_expr *value = ct_list_value(expr->values, i);
        analysis_expr(m, value);
    }
}

static void analysis_catch(module_t *m, ast_catch *expr) {
    analysis_call(m, expr->call);
}

static void analysis_for_cond(module_t *m, ast_for_cond_stmt *stmt) {
    analysis_expr(m, &stmt->condition);

    analysis_begin_scope(m);
    analysis_block(m, stmt->body);
    analysis_end_scope(m);
}

static void analysis_for_tradition(module_t *m, ast_for_tradition_stmt *stmt) {
    analysis_begin_scope(m);
    analysis_stmt(m, stmt->init);
    analysis_expr(m, &stmt->cond);
    analysis_stmt(m, stmt->update);
    analysis_block(m, stmt->body);
    analysis_end_scope(m);
}

static void analysis_for_iterator(module_t *m, ast_for_iterator_stmt *stmt) {
    // iterate 是对变量的使用，所以其在 scope
    analysis_expr(m, &stmt->iterate);

    analysis_begin_scope(m); // iterator 中创建的 key 和 value 的所在作用域都应该在当前 for scope 里面

    analysis_var_decl(m, &stmt->key);
    analysis_var_decl(m, &stmt->value);
    analysis_block(m, stmt->body);

    analysis_end_scope(m);
}

static void analysis_return(module_t *m, ast_return_stmt *stmt) {
    if (stmt->expr != NULL) {
        analysis_expr(m, stmt->expr);
    }
}

// type foo = int
static void analysis_typedef(module_t *m, ast_typedef_stmt *stmt) {
    analysis_redeclare_check(m, stmt->ident);
    analysis_typeuse(m, &stmt->type);

    local_ident_t *local = local_ident_new(m, SYMBOL_TYPEDEF, stmt, stmt->ident);
    stmt->ident = local->unique_ident;
}


static void analysis_expr(module_t *m, ast_expr *expr) {
    switch (expr->assert_type) {
        case AST_EXPR_BINARY: {
            return analysis_binary(m, expr->value);
        }
        case AST_EXPR_UNARY: {
            return analysis_unary(m, expr->value);
        }
        case AST_EXPR_CATCH: {
            return analysis_catch(m, expr->value);
        }
        case AST_EXPR_STRUCT_NEW: {
            return analysis_struct_new(m, expr->value);
        }
        case AST_EXPR_MAP_NEW: {
            return analysis_map_new(m, expr->value);
        }
        case AST_EXPR_SET_NEW: {
            return analysis_set_new(m, expr->value);
        }
        case AST_EXPR_TUPLE_NEW: {
            return analysis_tuple_new(m, expr->value);
        }
        case AST_EXPR_TUPLE_DESTR: {
            return analysis_tuple_destr(m, expr->value);
        }
        case AST_EXPR_LIST_NEW: {
            return analysis_list_new(m, expr->value);
        }
        case AST_EXPR_ACCESS: {
            return analysis_access(m, expr->value);
        }
        case AST_EXPR_SELECT: {
            // analysis 仅进行了变量重命名
            // 此时作用域不明确，无法进行任何的表达式改写。
            return analysis_select(m, expr);
        }
        case AST_EXPR_IDENT: {
            // ident unique 改写并注册到符号表中
            return analysis_ident(m, expr);
        }
        case AST_CALL: {
            return analysis_call(m, expr->value);
        }
        case AST_FNDEF: {
            analysis_fndef_name(m, expr->value);

            // 函数体延迟到最后进行处理
            delay_fndef_t d = {
                    .fndef = expr->value,
                    .is_stmt = false,
                    .scope = m->analysis_current->current_scope
            };
            ct_list_push(m->analysis_current->delay_fndefs, &d);

            break;
        }
        default:
            return;
    }
}


static void analysis_stmt(module_t *m, ast_stmt *stmt) {
    switch (stmt->assert_type) {
        case AST_VAR_DECL: {
            return analysis_var_decl(m, stmt->value);
        }
        case AST_STMT_VAR_DEF: {
            return analysis_vardef(m, stmt->value);
        }
        case AST_STMT_VAR_TUPLE_DESTR: {
            return analysis_var_tuple_destr_stmt(m, stmt->value);
        }
        case AST_STMT_ASSIGN: {
            return analysis_assign(m, stmt->value);
        }
        case AST_FNDEF: {
            // 主要是 fn_name unique 处理
            analysis_fndef_name(m, stmt->value);

            delay_fndef_t d = {
                    .fndef = stmt->value,
                    .is_stmt = true,
                    .scope = m->analysis_current->current_scope
            };
            ct_list_push(m->analysis_current->delay_fndefs, &d);

            break;
        }
        case AST_CALL: {
            return analysis_call(m, stmt->value);
        }
        case AST_STMT_THROW: {
            return analysis_throw(m, stmt->value);
        }
        case AST_STMT_IF: {
            return analysis_if(m, stmt->value);
        }
        case AST_STMT_FOR_COND: {
            return analysis_for_cond(m, stmt->value);
        }
        case AST_STMT_FOR_TRADITION: {
            return analysis_for_tradition(m, stmt->value);
        }
        case AST_STMT_FOR_ITERATOR: {
            return analysis_for_iterator(m, stmt->value);
        }
        case AST_STMT_RETURN: {
            return analysis_return(m, stmt->value);
        }
        case AST_STMT_TYPEDEF: {
            return analysis_typedef(m, stmt->value);
        }
        default: {
            return;
        }
    }
}

/**
 * 多个模块的解析
 * @param m
 * @param stmt_list
 */
static void analysis_module(module_t *m, slice_t *stmt_list) {
    // init
    m->analysis_line = 0;
    int import_end_index = 0;
    for (int i = 0; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->assert_type != AST_STMT_IMPORT) {
            import_end_index = i;
            break;
        }

        ast_import *import = stmt->value;
        complete_import(m->source_dir, import);

        // 简单处理
        slice_push(m->imports, import);
        table_set(m->import_table, import->as, import);
    }

    // var_decl blocks
    slice_t *var_assign_list = slice_new(); // 存放 stmt
    slice_t *fn_list = slice_new();

    // 跳过 import 语句开始计算
    for (int i = import_end_index; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->assert_type == AST_VAR_DECL) {
            ast_var_decl *var_decl = stmt->value;
            char *ident = ident_with_module(m->ident, var_decl->ident);

            symbol_t *s = symbol_table_set(ident, SYMBOL_VAR, var_decl, false);
            slice_push(m->global_symbols, s);
            continue;
        }

        if (stmt->assert_type == AST_STMT_VAR_DEF) {
            ast_vardef_stmt *vardef = stmt->value;
            ast_var_decl *var_decl = &vardef->var_decl;
            char *ident = ident_with_module(m->ident, var_decl->ident);

            symbol_t *s = symbol_table_set(ident, SYMBOL_VAR, var_decl, false);
            slice_push(m->global_symbols, s);

            // 转换成 assign stmt，然后导入到 init 中
            ast_stmt *temp_stmt = NEW(ast_stmt);
            ast_assign_stmt *assign = NEW(ast_assign_stmt);
            assign->left = (ast_expr) {
                    .assert_type = AST_EXPR_IDENT,
                    .value = ast_new_ident(var_decl->ident),
//                    .code = var_decl->code,
            };
            assign->right = vardef->right;
            temp_stmt->assert_type = AST_STMT_ASSIGN;
            temp_stmt->value = assign;
            slice_push(var_assign_list, temp_stmt);
            continue;
        }

        if (stmt->assert_type == AST_STMT_TYPEDEF) {
            ast_typedef_stmt *type_decl = stmt->value;
            char *ident = ident_with_module(m->ident, type_decl->ident);
            symbol_t *s = symbol_table_set(ident, SYMBOL_TYPEDEF, type_decl, false);
            slice_push(m->global_symbols, s);
            continue;
        }

        if (stmt->assert_type == AST_FNDEF) {
            ast_fndef_t *fndef = stmt->value;
            // 这里就可以看到 fn name 没有做唯一性处理，只是加了 ident 限定
            fndef->name = ident_with_module(m->ident, fndef->name); // 全局函数改名

            symbol_t *s = symbol_table_set(fndef->name, SYMBOL_FN, fndef, false);
            slice_push(m->global_symbols, s);
            slice_push(fn_list, fndef);
            continue;
        }

        assert(false && "[analysis_module] stmt.code not allow, must var_decl/new_fn/type_decl");
    }

    // 添加 init fn
    ast_fndef_t *fn_init = NEW(ast_fndef_t);
    fn_init->name = ident_with_module(m->ident, FN_INIT_NAME);
    fn_init->return_type = type_base_new(TYPE_VOID);
    fn_init->formals = ct_list_new(sizeof(ast_var_decl));
    fn_init->body = var_assign_list;

    // 加入到全局符号表，等着调用就好了
    symbol_t *s = symbol_table_set(fn_init->name, SYMBOL_FN, fn_init, false);
    slice_push(m->global_symbols, s);
    slice_push(fn_list, fn_init);

    // 添加调用指令(后续 root module 会将这条指令添加到 main body 中)
    ast_stmt *temp_stmt = NEW(ast_stmt);
    ast_call *call = NEW(ast_call);
    call->left = (ast_expr) {
            .assert_type = AST_EXPR_IDENT,
            .value = ast_new_ident(s->ident), // module.init
    };
    call->actual_params = ct_list_new(sizeof(ast_expr));
    temp_stmt->assert_type = AST_CALL;
    temp_stmt->value = call;
    m->call_init_stmt = temp_stmt;

    // 此时所有对符号都已经主要到了全局变量表中，vardef 到右值则注册到 init fn 中，有下面到 fndef 进行改写
    // 遍历所有 fn list
    for (int i = 0; i < fn_list->count; ++i) {
        ast_fndef_t *fndef = fn_list->take[i];
        analysis_fndef(m, fndef, NULL);

        slice_push(m->ast_fndefs, fndef);
    }
}

/**
 * main.c 中定义的所有代码都会被丢到 main closure_t 中
 * @param t
 * @param stmt_list
 */
static void analysis_main(module_t *m, slice_t *stmt_list) {
    // 过滤处 import 部分, 其余部分再包到 main closure_t 中
    int import_end_index = 0;
    for (int i = 0; i < stmt_list->count; ++i) {
        ast_stmt *stmt = stmt_list->take[i];
        if (stmt->assert_type != AST_STMT_IMPORT) {
            import_end_index = i;
            break;
        }

        ast_import *import = stmt->value;
        complete_import(m->source_dir, import);

        // 简单处理
        slice_push(m->imports, import);
        table_set(m->import_table, import->as, import);
    }

    // init
    m->analysis_line = 0;

    // block 封装进 function,再封装到 closure_t 中
    ast_fndef_t *fndef = malloc(sizeof(ast_fndef_t));
    fndef->name = FN_MAIN_NAME;
    fndef->body = slice_new();
    fndef->return_type = type_base_new(TYPE_VOID);
    fndef->formals = ct_list_new(sizeof(ast_var_decl));
    for (int i = import_end_index; i < stmt_list->count; ++i) {
        slice_push(fndef->body, stmt_list->take[i]);
    }

    // 符号表注册
    symbol_t *s = symbol_table_set(FN_MAIN_NAME, SYMBOL_FN, fndef, true);
    slice_push(m->global_symbols, s);

    analysis_fndef(m, fndef, NULL);
    slice_push(m->ast_fndefs, fndef);
}

void analysis(module_t *m, slice_t *stmt_list) {
    if (m->type == MODULE_TYPE_MAIN) {
        analysis_main(m, stmt_list);
    } else {
        analysis_module(m, stmt_list);
    }
}
