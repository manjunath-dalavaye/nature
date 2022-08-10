#ifndef NATURE_SRC_LIR_LOWER_AMD64_H_
#define NATURE_SRC_LIR_LOWER_AMD64_H_

#include "src/assembler/amd64/asm.h"
#include "src/lir/lir.h"
#include "src/register/register.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef list *(*amd64_lower_fn)(closure *c, lir_op *op);

list *amd64_lower_fn_formal_params(closure *c);

list *amd64_lower_fn_begin(closure *c, lir_op *op);

list *amd64_lower_fn_end(closure *c, lir_op *op);

list *amd64_lower_closure(closure *c);

list *amd64_lower_block(closure *c, lir_basic_block *block);

/**
 * 分发入口, 基于 type->type 做选择(包含 label type)
 * @param c
 * @param op
 * @return
 */
list *amd64_lower_op(closure *c, lir_op *op);

list *amd64_lower_label(closure *c, lir_op *op);

list *amd64_lower_call(closure *c, lir_op *op);

list *amd64_lower_return(closure *c, lir_op *op);

list *amd64_lower_bal(closure *c, lir_op *op);

list *amd64_lower_cmp_goto(closure *c, lir_op *op);

list *amd64_lower_add(closure *c, lir_op *op);

list *amd64_lower_sgt(closure *c, lir_op *op);

list *amd64_lower_mov(closure *c, lir_op *op);

list *amd64_lower_lea(closure *c, lir_op *op);


// 返回下一个可用的寄存器(属于一条指令的 fixed_reg)
reg_t *amd64_lower_next_reg(uint8_t used[2], uint8_t size);

/**
 * 返回下一个可用参数，可能是寄存器参数也可能是内存偏移
 * @param count
 * @param asm_operand
 * @return
 */
reg_t *amd64_lower_fn_next_reg_target(uint8_t used[2], type_base_t base);

// 只要返回了指令就有一个使用的寄存器的列表，已经使用的固定寄存器就不能重复使用
list *amd64_lower_operand_transform(lir_operand *operand,
                                    amd64_asm_operand_t *asm_operand,
                                    uint8_t used[2]);

uint8_t amd64_formal_min_stack(uint8_t size);


#endif //NATURE_SRC_LIR_LOWER_AMD64_H_
