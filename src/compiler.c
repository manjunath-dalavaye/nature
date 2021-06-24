#include "compiler.h"
#include "src/ast/symbol.h"

list_op *compiler_block(closure *c, ast_block_stmt *block) {
  list_op *operates = list_op_new();
  for (int i = 0; i < block->count; ++i) {
    ast_stmt stmt = block->list[i];
    list_op *await_append;

    switch (stmt.type) {
      case AST_CLOSURE_DECL: {
        closure *child = lir_new_closure();
        child->parent = c;
        child->operates = compiler(child, (ast_closure_decl *) stmt.stmt);
      }
      case AST_VAR_DECL: {
        await_append = compiler_var_decl(c, (ast_var_decl *) stmt.stmt);
        break;
      }
      case AST_STMT_VAR_DECL_ASSIGN: {
        await_append = compiler_var_decl_assign(c, (ast_var_decl_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_ASSIGN: {
        await_append = compiler_assign(c, (ast_assign_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_IF: {
        await_append = compiler_if(c, (ast_if_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_FOR_IN: {
        await_append = compiler_for_in(c, (ast_for_in_stmt *) stmt.stmt);
        break;
      }
      case AST_STMT_WHILE: {
        await_append = compiler_while(c, (ast_while_stmt *) stmt.stmt);
        break;
      }
      case AST_CALL: {
        lir_operand *temp_target = lir_new_temp_var_operand();
        await_append = compiler_call(c, (ast_call *) stmt.stmt, temp_target);
        break;
      }
    }

    list_op_append(operates, await_append);
  }

  return operates;
}

/**
 * a = b + 1 + 3
 * @param stmt
 * @return
 */
list_op *compiler_var_decl_assign(closure *c, ast_var_decl_assign_stmt *stmt) {
  lir_operand *target = lir_new_var_operand(stmt->ident);
  return compiler_expr(c, stmt->expr, target);
}
list_op *compiler_assign(closure *c, ast_assign_stmt *stmt) {
  // left 可能是一个数组调用，可能是一个对象访问，也有可能是一个标量赋值
  lir_operand *left_target = lir_new_temp_var_operand();
  lir_operand *right_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, stmt->left, left_target);
  list_op_append(list, compiler_expr(c, stmt->right, right_target));

  lir_op *move_op = lir_new_op(LIR_OP_TYPE_MOVE);
  move_op->result = *left_target;
  move_op->first = *right_target;
  list_op_push(list, move_op);

  return list;
}

list_op *compiler_var_decl(closure *c, ast_var_decl *var_decl) {
  return NULL;
}

list_op *compiler_expr(closure *c, ast_expr expr, lir_operand *target) {
  switch (expr.type) {
    case AST_EXPR_TYPE_BINARY: {
      return compiler_binary(c, (ast_binary_expr *) expr.expr, target);
    }
    case AST_EXPR_TYPE_LITERAL: {
      ast_literal *literal = (ast_literal *) expr.expr;

      // 自引用，避免冗余的
      // a = 5
      // t1 = a
      if (literal->type == AST_BASE_TYPE_INT || literal->type == AST_BASE_TYPE_FLOAT) {
        lir_operand_immediate *immediate = malloc(sizeof(lir_operand_immediate));
        immediate->type = literal->type;
        immediate->value = literal->value;
        target->type = LIR_OPERAND_TYPE_IMMEDIATE;
        target->value = immediate;
      }

      // TODO 其他类型处理

      break;
    }
    case AST_EXPR_TYPE_IDENT: {
      target->type = LIR_OPERAND_TYPE_VAR;
      target->value = lir_new_var_operand((ast_ident) expr.expr);
      break;
    }
    case AST_CALL: {
      // 返回值存储在 target 中
      return compiler_call(c, (ast_call *) expr.expr, target);
    }
    case AST_EXPR_TYPE_ACCESS_LIST: {
      return compiler_access_list(c, (ast_access_list *) expr.expr, target);
    }
    case AST_EXPR_TYPE_NEW_LIST: {
      return compiler_new_list(c, (ast_new_list *) expr.expr, target);
    }
    case AST_EXPR_TYPE_ACCESS_MAP: {
      return compiler_access_map(c, (ast_access_map *) expr.expr, target);
    }
    case AST_EXPR_TYPE_NEW_MAP: {
      return compiler_new_map(c, (ast_new_map *) expr.expr, target);
    }
  }
  return NULL;
}

list_op *compiler_binary(closure *c, ast_binary_expr *expr, lir_operand *result_target) {
  uint8_t type;
  switch (expr->operator) {
    case AST_EXPR_OPERATOR_ADD: {
      type = LIR_OP_TYPE_ADD;
      break;
    }
  }

  lir_operand *left_target = lir_new_temp_var_operand();
  lir_operand *right_target = lir_new_temp_var_operand();
  list_op *operates = compiler_expr(c, expr->left, left_target);
  list_op_append(operates, compiler_expr(c, expr->right, right_target));
  lir_op *binary_op = lir_new_op(type);
  binary_op->result = *result_target;
  binary_op->first = *left_target;
  binary_op->second = *right_target;
  list_op_push(operates, binary_op);

  return operates;
}

list_op *compiler_if(closure *c, ast_if_stmt *if_stmt) {
  // 编译 condition
  lir_operand *condition_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, if_stmt->condition, condition_target);
  // 判断结果是否为 false, false 对应 else
  lir_op *cmp_goto = lir_new_op(LIR_OP_TYPE_CMP_GOTO);
  cmp_goto->first = *lir_new_immediate_bool_operand(false);
  cmp_goto->second = *condition_target;

  lir_op *end_label = lir_op_label("end_if");
  lir_op *alternate_label = lir_op_label("alternate_if");
  if (if_stmt->alternate.count == 0) {
    cmp_goto->result = end_label->result;
  } else {
    cmp_goto->result = alternate_label->result;
  }
  list_op_push(list, cmp_goto);

  // 编译 consequent block
  list_op *consequent_list = compiler_block(c, &if_stmt->consequent);
  list_op_push(consequent_list, lir_op_goto(&end_label->result));
  list_op_append(list, consequent_list);

  // 编译 alternate block
  if (if_stmt->alternate.count != 0) {
    list_op_push(list, alternate_label);
    list_op *alternate_list = compiler_block(c, &if_stmt->alternate);
    list_op_append(list, alternate_list);
  }
  // 追加 end_if 标签
  list_op_push(list, end_label);

  return list;
}

/**
 * 1.0 函数参数使用 param var 存储,按约定从左到右(op.result 为 param, op.first 为实参)
 * 1.0.1 op.operand 模仿 phi body 弄成列表的形式！
 * 1.1 参数1 存储 env 环境
 * 2. 目前编译依旧使用 var，所以不需要考虑寄存器溢出
 * 3. 函数返回结果存储在 target 中
 * @param c
 * @param expr
 * @return
 */
list_op *compiler_call(closure *c, ast_call *call, lir_operand *target) {
  // push 指令所有的物理寄存器入栈
  list_op *list = list_op_new();
  lir_op *call_op = lir_new_op(LIR_OP_TYPE_CALL);
  call_op->first = lir_op_label(call->name)->first; // 函数名称

  lir_operand_actual_param *params_operand = malloc(sizeof(lir_operand_actual_param));
  params_operand->count = 0;

  for (int i = 0; i < call->actual_param_count; ++i) {
    ast_expr ast_param_expr = call->actual_params[i];

    lir_operand *param_target = lir_new_temp_var_operand();

    list_op *param_list_op = compiler_expr(c, ast_param_expr, param_target);
    list_op_append(list, param_list_op);

    // 写入到 call 指令中
    params_operand->list[params_operand->count++] = param_target;
  }

  lir_operand call_params_operand = {.type= LIR_OPERAND_TYPE_ACTUAL_PARAM, .value = params_operand};
  call_op->second = call_params_operand; // 函数参数

  // return target
  call_op->result = *target; // 返回结果

  list_op_push(list, call_op);

  return list;
}

/**
 * 如何区分 a[1] = 2  和 c = a[1]
 *
 * a()[0]
 * a.b[0]
 * a[0]
 * list[int] l = make(list[int]);
 * 简单类型与复杂类型
 *
 * list 的边界是可以使用 push 动态扩容的，因此需要在某个地方存储 list 的最大 index,从而判断是否越界
 * for item by database
 *    list.push(item)
 *
 * 通过上面的示例可以确定在编译截断无法判断数组是否越界，需要延后到运行阶段，也就是 access_list 这里
 */
list_op *compiler_access_list(closure *c, ast_access_list *ast, lir_operand *target) {
  // new tmp 是无类型的。
  // left_target.type is list[int]
  // left_target.var = runtime.make_list(size)
  // left_target.var to symbol
  // 假设是内存机器，则有 left_target.val = sp[n]
  // 但是无论如何，此时 left_target 的type 是 var, val 对应 lir_operand_var.
  // 且 lir_operand_var.ident 可以在 symbol 查出相关类型
  // var 实际上会在真实物理计算机中的内存或者其他空间拥有一份空间，并写入了一个值。
  // 即当成 var 是有值的即可！！具体的值是多少咱也不知道
  lir_operand *base_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, ast->left, base_target);

  lir_operand *index_target = lir_new_temp_var_operand();
  list_op_append(list, compiler_expr(c, ast->index, index_target));

  lir_op *call_op = lir_runtime_two_param_call(
      RUNTIME_CALL_LIST_VALUE,
      *target,
      base_target,
      index_target
  );

  list_op_push(list, call_op);

  return list;
}

/**
 * origin [1, foo, bar(), car.done]
 * call runtime.make_list => t1
 * move 1 => t1[0]
 * move foo => t1[1]
 * move bar() => t1[2]
 * move car.done => t1[3]
 * move t1 => target
 * @param c
 * @param new_list
 * @param target
 * @return
 */
list_op *compiler_new_list(closure *c, ast_new_list *ast, lir_operand *base_target) {
  list_op *list = list_op_new();

  // 类型，容量 runtime.make_list(capacity, size)
  lir_operand *capacity_operand = lir_new_immediate_int_operand((int) ast->capacity);
  lir_operand *item_size_operand = lir_new_immediate_int_operand((int) type_sizeof(ast->type));
  lir_op *call_op = lir_runtime_two_param_call(
      RUNTIME_CALL_MAKE_LIST,
      *base_target,
      capacity_operand,
      item_size_operand
  );

  list_op_push(list, call_op);

  // compiler_expr to access_list
  for (int i = 0; i < ast->count; ++i) {
    ast_expr expr = ast->values[i];
    lir_operand *value_target = lir_new_temp_var_operand();
    list_op_append(list, compiler_expr(c, expr, value_target));

    lir_operand *refer_target = lir_new_temp_var_operand();
    lir_operand *index_target = lir_new_immediate_int_operand(i);
    call_op = lir_runtime_two_param_call(
        RUNTIME_CALL_LIST_VALUE,
        *refer_target,
        base_target,
        index_target
    );
    list_op_push(list, call_op);
    list_op_push(list, lir_op_move(refer_target, value_target));
  }

  return list;
}

/**
 * foo.bar
 * foo[0].bar
 * foo.bar.car
 *
 * @param c
 * @param ast
 * @param target
 * @return
 */
list_op *compiler_access_map(closure *c, ast_access_map *ast, lir_operand *target) {
  // compiler base address left_target
  lir_operand *base_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, ast->left, base_target);

  // compiler key to temp var
  lir_operand *key_target = lir_new_temp_var_operand();
  list_op_append(list, compiler_expr(c, ast->key, key_target));

  // runtime get offset by temp var runtime.map_offset(base, "key")
  lir_op *call_op = lir_runtime_two_param_call(
      RUNTIME_CALL_MAP_VALUE,
      *target,
      base_target,
      key_target
  );
  list_op_push(list, call_op);

  return NULL;
}

/**
 * call runtime.make_map => t1 // 基础地址
 * @param c
 * @param ast
 * @param base_target
 * @return
 */
list_op *compiler_new_map(closure *c, ast_new_map *ast, lir_operand *base_target) {
  list_op *list = list_op_new();
  lir_operand *capacity_operand = lir_new_immediate_int_operand((int) ast->capacity);
  lir_operand *item_size_operand = lir_new_immediate_int_operand(
      (int) type_sizeof(ast->key_type) + (int) type_sizeof(ast->value_type));

  lir_op *call_op = lir_runtime_two_param_call(
      RUNTIME_CALL_MAKE_MAP,
      *base_target,
      capacity_operand,
      item_size_operand
  );
  list_op_push(list, call_op);

  // 默认值初始化
  for (int i = 0; i < ast->count; ++i) {
    ast_expr key_expr = ast->values[i].key;
    lir_operand *key_target = lir_new_temp_var_operand();
    ast_expr value_expr = ast->values[i].value;
    lir_operand *value_target = lir_new_temp_var_operand();

    list_op_append(list, compiler_expr(c, key_expr, key_target));
    list_op_append(list, compiler_expr(c, value_expr, value_target));

    lir_operand *refer_target = lir_new_temp_var_operand();
    call_op = lir_runtime_two_param_call(
        RUNTIME_CALL_MAP_VALUE,
        *refer_target,
        base_target,
        key_target
    );
    list_op_push(list, call_op);
    list_op_push(list, lir_op_move(refer_target, value_target));

  }

  return list;
}

/**
 * call get count => count
 * for:
 *  cmp_goto count == 0 to end for
 *  call get key => key
 *  call get value => value
 *  ....
 *  sub count, 1 => count
 *  goto for:
 * end_for:
 * @param c
 * @param for_in_stmt
 * @return
 */
list_op *compiler_for_in(closure *c, ast_for_in_stmt *ast) {
  lir_operand *base_target = lir_new_temp_var_operand();
  list_op *list = compiler_expr(c, ast->iterate, base_target);

  lir_operand *count_target = lir_new_temp_var_operand(); // ?? 这个值特么存在哪里，我现在不可知呀？
  // 换句话说，我又怎么知道需要 for 循环几次？？
  // 根据 base_target get length, bug length how to trans to int
  list_op_push(list, lir_runtime_one_param_call(
      RUNTIME_CALL_ITERATE_COUNT,
      *count_target,
      base_target));
  // make label
  lir_op *for_label = lir_op_label("for");
  lir_op *end_for_label = lir_op_label("end_for");
  list_op_push(list, for_label);
  lir_op *cmp_goto = lir_new_op(LIR_OP_TYPE_CMP_GOTO);
  cmp_goto->first = *lir_new_immediate_int_operand(0);
  cmp_goto->second = *count_target;
  cmp_goto->result = end_for_label->result;

  // gen key
  // gen value
  lir_operand *key_target = lir_new_var_operand(ast->gen_key->ident);
  lir_operand *value_target = lir_new_var_operand(ast->gen_value->ident);
  list_op_push(list, lir_runtime_one_param_call(
      RUNTIME_CALL_ITERATE_GEN_KEY,
      *key_target,
      base_target));
  list_op_push(list, lir_runtime_one_param_call(
      RUNTIME_CALL_ITERATE_GEN_VALUE,
      *value_target,
      base_target));

  // block
  list_op_append(list, compiler_block(c, &ast->body));

  // sub count, 1 => count
  lir_op *sub_op = lir_new_op(LIR_OP_TYPE_SUB);
  sub_op->first = *count_target;
  sub_op->second = *lir_new_immediate_int_operand(1);
  sub_op->result = *count_target;
  list_op_push(list, sub_op);

  // goto for
  list_op_push(list, lir_op_goto(&for_label->result));

  list_op_push(list, lir_op_label("end_for"));

  return list;
}

list_op *compiler_while(closure *c, ast_while_stmt *ast) {
  list_op *list = list_op_new();
  lir_op *while_label = lir_op_label("while");
  lir_op *end_while_label = lir_op_label("end_while");
  list_op_push(list, while_label);

  lir_operand *condition_target = lir_new_temp_var_operand();
  list_op_append(list, compiler_expr(c, ast->condition, condition_target));
  lir_op *cmp_goto = lir_new_op(LIR_OP_TYPE_CMP_GOTO);
  cmp_goto->first = *lir_new_immediate_bool_operand(false);
  cmp_goto->second = *condition_target;
  cmp_goto->result = end_while_label->result;

  list_op_append(list, compiler_block(c, &ast->body));

  list_op_push(list, end_while_label);

  return list;
}

/**
 * 从何处读取实参为形参赋值？
 * 如何编译 env? env 的本质是运行时对栈的引用
 * call make_env
 * call set_env => a target
 * call get_env => t1 target
 * @param c
 * @param ast
 * @return
 */
list_op *compiler(closure *c, ast_closure_decl *ast) {
  // 捕获逃逸变量，并放在形参1中,对应的实参需要填写啥吗？
  list_op *list = list_op_new();
  // 形参处理,怎么处理？实参怎么合形参对应？
  //  ast->env
  // block 处理
  return list;
}



