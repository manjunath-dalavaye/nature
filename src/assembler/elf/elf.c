#include "elf.h"
#include "utils/helper.h"
#include "utils/error.h"
#include <string.h>
#include <stdio.h>

static void elf_var_operand_rewrite(asm_operand_t *operand) {
    operand->type = ASM_OPERAND_TYPE_RIP_RELATIVE;
    operand->size = QWORD;
    asm_operand_rip_relative_t *r = NEW(asm_operand_rip_relative_t);
    r->disp = 0;
    operand->value = r;
}


static bool elf_is_jmp(asm_inst_t asm_inst) {
    return asm_inst.name[0] == 'j';
}

static bool elf_is_call(asm_inst_t asm_inst) {
    return str_equal(asm_inst.name, "call");
}

static uint8_t elf_jmp_reduce_count(asm_inst_t asm_inst) {
    if (asm_inst.name[0] != 'j') {
        return 0;
    }

    if (str_equal(asm_inst.name, "jmp")) {
        //   b:	eb f3                	jmp    0 <test>
        //   d:	e9 14 00 00 00       	jmpq   26 <test+0x26>
        return 5 - 2;
    }
    //   3:	74 fb                	je     0 <test>
    //   5:	0f 84 08 00 00 00    	je     13 <test+0x13>
    return 6 - 2;
}

static uint8_t elf_jmp_inst_count(asm_inst_t inst, uint8_t size) {
    if (inst.name[0] != 'j') {
        error_exit("[elf_jmp_inst_count] inst: %s not jmp or jcc:", inst.name);
    }
    if (size == BYTE) {
        return 2;
    }
    if (str_equal(inst.name, "jmp")) {
        return 5;
    }
    return 6;
}

static void elf_operand_rewrite_rel(asm_inst_t inst, asm_operand_t *operand, int rel_diff) {
    // 已经确定了指令长度，不能再随意修正了
    if (operand->type != ASM_OPERAND_TYPE_SYMBOL) {
        if (rel_diff == 0) {
            return;
        }
        if (operand->type == ASM_OPERAND_TYPE_UINT32) {
            uint8_t inst_count = 5;
            if (!elf_is_call(inst)) {
                inst_count = elf_jmp_inst_count(inst, DWORD);
            }
            asm_operand_uint32_t *v = NEW(asm_operand_uint32_t);
            v->value = (uint32_t) (rel_diff - inst_count);
            operand->value = v;
        } else {
            asm_operand_uint8_t *v = NEW(asm_operand_uint8_t);
            v->value = (uint8_t) (rel_diff - 2); // -2 表示去掉当前指令的差值
            operand->value = v;
        }
        return;
    }

    // symbol to rel32
    // call 指令不能改写成 rel8
    if (rel_diff == 0 || abs(rel_diff) > 128 || elf_is_call(inst)) {
        uint8_t inst_count = 5;
        if (!elf_is_call(inst)) {
            inst_count = elf_jmp_inst_count(inst, DWORD);
        }

        operand->type = ASM_OPERAND_TYPE_UINT32;
        operand->size = DWORD;
        asm_operand_uint32_t *v = NEW(asm_operand_uint32_t);
        v->value = 0;
        if (rel_diff != 0) {
            v->value = (uint32_t) (rel_diff - inst_count); // -5 表示去掉当前指令的差值
        }
        operand->value = v;
        return;
    }

    // jmp 指令
    operand->type = ASM_OPERAND_TYPE_UINT8;
    operand->size = BYTE;
    asm_operand_uint8_t *v = NEW(asm_operand_uint8_t);
    v->value = (uint8_t) (rel_diff - 2); // -2 表示去掉当前指令的差值
    operand->value = v;
}


/**
 * TODO symbol 必须和 起始 inst 或者虚拟 label 共用 offset
 * @param asm_inst
 */
void elf_text_inst_build(asm_inst_t asm_inst, uint64_t *offset) {
    elf_text_inst_t *inst = ELF_TEXT_INST_NEW(asm_inst);

    // 外部标签引用处理
    asm_operand_t *rel_operand = asm_symbol_operand(asm_inst);
    // TODO call 和 jmp 和 jcc 指令，则 rel_operand 必定是 label, 否则是 symbol
    if (rel_operand != NULL) {
        // 1. 数据符号引用(直接改写成 0x0(rip))
        // 2. 标签符号引用(在符号表中,表明为内部符号,否则使用 rel32 占位)
        asm_operand_symbol_t *symbol_operand = rel_operand->value;
        if (elf_is_call(asm_inst) || elf_is_jmp(asm_inst)) {
            // 引用了内部符号,根据实际距离判断使用 rel32 还是 rel8
            if (table_exist(elf_symbol_table, symbol_operand->name)) {
                elf_symbol_t *symbol = table_get(elf_symbol_table, symbol_operand->name);
                // 计算 offset 并填充
                int rel_diff = *symbol->offset - global_text_offset;
                // call symbol 只有 rel32
                elf_operand_rewrite_rel(asm_inst, rel_operand, rel_diff);
            } else {
                // 引用了 label 符号，但是符号确不在符号表中
                // 此时使用 rel32 占位，如果是 jmp 指令后续可能需要替换
                elf_operand_rewrite_rel(asm_inst, rel_operand, 0);
                inst->rel_operand = rel_operand; // 二次遍历时改写
                inst->rel_symbol = symbol_operand->name; // 二次遍历是需要
                // 如果是 jmp 或者 jcc 指令就需要改写
                uint8_t reduce_count = elf_jmp_reduce_count(asm_inst);
                if (reduce_count > 0) {
                    inst->may_need_reduce = true;
                    inst->reduce_count = reduce_count;
                }
            }
        } else {
            // 数据符号重写(symbol to 0(%rip))
            elf_var_operand_rewrite(rel_operand);

            // 添加到重定位表
            elf_rel_t *rel = NEW(elf_rel_t);
            rel->name = symbol_operand->name;
            rel->offset = offset; // 引用的具体位置
            rel->addend = -4;
            rel->section = ELF_SECTION_TEXT;
            rel->type = ELF_SYMBOL_TYPE_VAR;
            list_push(elf_rel_list, rel);

            // 如果符号表中不存在就直接添加到符号,请勿重复添加
            elf_symbol_t *symbol = table_get(elf_symbol_table, symbol_operand->name);
            if (symbol == NULL) {
                symbol = NEW(elf_symbol_t);
                symbol->name = symbol_operand->name;
                symbol->type = 0; // 外部符号引用直接 no type
                symbol->section = 0; // 外部符号，所以这些信息都没有
                symbol->offset = 0;
                symbol->size = 0;
                symbol->is_rel = true; // 是否为外部引用符号(避免重复添加)
                symbol->is_local = false; // 局部 label 在生成符号表的时候可以忽略
                elf_symbol_insert(symbol);
            }

        }
    }

    inst->data = opcode_encoding(asm_inst, &inst->count);
    global_text_offset += inst->count; // advance global offset

    inst->offset = offset;
    inst->asm_inst = asm_inst;

    // 注册 elf_text_inst_t 到 elf_text_inst_list 和 elf_text_table 中
    list_push(elf_text_inst_list, inst);
}

/**
 * 指令重写， jmp/jcc rel32 重写为 rel8
 * 倒推符号表，如果找到符号占用引用则记录位置
 */
void elf_confirm_text_rel(string name) {
    if (list_empty(elf_text_inst_list)) {
        return;
    }

    list_node *current = list_last(elf_text_inst_list); // rear 为 empty 占位
    uint8_t reduce_count = 0;
    // 从尾部像前找, 找到超过 128 即可
    while (true) {
        elf_text_inst_t *inst = current->value;
        if ((global_text_offset - reduce_count - *inst->offset) > 128) {
            break;
        }

        if (inst->may_need_reduce && str_equal(inst->rel_symbol, name)) {
            reduce_count += inst->reduce_count;
        }

        // current 保存当前值
        if (current->prev == NULL) {
            break;
        }
        current = current->prev;
    }

    if (reduce_count == 0) {
        return;
    }

    // 这一行非常关键
    uint64_t *offset = NEW(uint64_t);
    *offset = *(((elf_text_inst_t *) current->value)->offset);
    // 从 current 开始，从左往右做 rewrite
    while (current->value != NULL) {
        elf_text_inst_t *inst = current->value;
        if (inst->may_need_reduce && str_equal(inst->rel_symbol, name)) {
            // 重写 inst 指令 rel32 为 rel8
            // jmp 的具体位置可以不计算，等到二次遍历再计算
            // 届时符号表已经全部收集完毕
            elf_rewrite_text_rel(inst);
        }
        *inst->offset = *offset;
        *offset += inst->count; // 重新计算当前 offset
        current = current->next;
    }

    // 最新的 offset
    global_text_offset = *offset;
}

/**
 * inst rel32 to rel8
 * @param t
 */
void elf_rewrite_text_rel(elf_text_inst_t *t) {
    asm_operand_uint8_t *operand = NEW(asm_operand_uint8_t);
    operand->value = 0; // 仅占位即可
    t->asm_inst.count = 1;
    t->asm_inst.operands[0]->type = ASM_OPERAND_TYPE_UINT8;
    t->asm_inst.operands[0]->size = BYTE;
    t->asm_inst.operands[0]->value = operand;
    t->data = opcode_encoding(t->asm_inst, &t->count);
    t->may_need_reduce = false;
}

/**
 * 遍历  elf_text_inst_list 如果其存在 rel_symbol,即符号引用
 * 则判断其是否在符号表中，如果其在符号表中，则填充指令 value 部分(此时可以选择重新编译)
 * 如果其依旧不在符号表中，则表示其引用了外部符号，此时直接添加一条 rel 记录即可
 * @param elf_text_inst_list
 */
void elf_text_inst_list_second_build() {
    if (list_empty(elf_text_inst_list)) {
        return;
    }

    list_node *current = elf_text_inst_list->front;
    while (current->value != NULL) {
        elf_text_inst_t *inst = current->value;
        if (inst->rel_symbol == NULL) {
            current = current->next;
            continue;
        }

        // 计算 rel
        elf_symbol_t *symbol = table_get(elf_symbol_table, inst->rel_symbol);
        if (symbol != NULL && !symbol->is_rel) {
            int rel_diff = *symbol->offset - *inst->offset;

            elf_operand_rewrite_rel(inst->asm_inst, inst->rel_operand, rel_diff);

            // 重新 encoding 指令
            inst->data = opcode_encoding(inst->asm_inst, &inst->count);
        } else {
            // 二次扫描都没有在符号表中找到
            // 说明引用了外部 label 符号(是否需要区分引用的外部符号的类型不同？section 填写的又是什么段？)
            // jmp or call rel32
            elf_rel_t *rel = NEW(elf_rel_t);
            rel->name = inst->rel_symbol;
            *inst->offset += 1; // 定位到 rel 而不是 inst offset
            rel->offset = inst->offset; // 实际引用位置
            rel->addend = -4; // 符号表如果都使用 rip,则占 4 个偏移
            rel->section = ELF_SECTION_TEXT;
            rel->type = ELF_SYMBOL_TYPE_FN;
            list_push(elf_rel_list, rel);

            // 添加到符号表表
            if (symbol == NULL) {
                symbol = NEW(elf_symbol_t);
                symbol->name = inst->rel_symbol;
                symbol->type = 0; // 外部符号引用直接 no type ?
                symbol->section = 0;
                symbol->offset = 0;
                symbol->size = 0;
                symbol->is_rel = true; // 是否为外部引用符号(避免重复添加)
                symbol->is_local = false; // 局部 label 在生成符号表的时候可以忽略
                elf_symbol_insert(symbol);
            }
        }
        current = current->next;
    }
}

void elf_text_inst_list_build(list *inst_list) {
    if (list_empty(inst_list)) {
        return;
    }
    list_node *current = inst_list->front;
    uint64_t *offset = elf_current_text_offset();
    while (current->value != NULL) {
        asm_inst_t *inst = current->value;
        if (str_equal(inst->name, "label")) {
            elf_text_label_build(*inst, offset);
            current = current->next;
            continue;
        }
        elf_text_inst_build(*inst, offset);
        offset = elf_current_text_offset();

        current = current->next;
    }
}

void elf_symbol_insert(elf_symbol_t *symbol) {
    table_set(elf_symbol_table, symbol->name, symbol);
    list_push(elf_symbol_list, symbol);
}

uint64_t *elf_current_text_offset() {
    uint64_t *offset = NEW(uint64_t);
    *offset = global_text_offset;
    return offset;
}

uint64_t *elf_current_data_offset() {
    uint64_t *offset = NEW(uint64_t);
    *offset = global_data_offset;
    return offset;
}

static char *elf_header_ident() {
    char *ident = malloc(sizeof(char) * EI_NIDENT);
    memset(ident, 0, EI_NIDENT);

    ident[0] = 0x7F; // del 符号的编码
    ident[1] = 'E';
    ident[2] = 'L';
    ident[3] = 'F';
    ident[4] = ELFCLASS64; // elf 文件类型: 64 位
    ident[5] = ELFDATA2LSB; // 字节序： 小端
    ident[6] = EV_CURRENT; // elf 版本号
    ident[7] = ELFOSABI_NONE; // os abi = unix v
    ident[8] = 0; // ABI version
    return ident;
}

elf_t elf_new() {
    // 数据段构建(依旧是遍历符号表)
    uint64_t data_size = 0;
    uint8_t *data = elf_data_build(&data_size);

    // 代码段构建 .text
    uint64_t text_size = 0;
    uint8_t *text = elf_text_build(&text_size);

    // 符号表构建(首先计算符号的数量)
    uint64_t symtab_count = 4;
    list_node *current = elf_symbol_list->front;
    while (current->value != NULL) {
        elf_symbol_t *s = current->value;
        if (!s->is_local) {
            symtab_count++;
        }
        current = current->next;
    }
    Elf64_Sym *symtab = malloc(sizeof(Elf64_Sym) * symtab_count);
    string strtab = elf_symtab_build(symtab);

    // 代码段重定位表构建
    uint64_t rel_text_count;
    Elf64_Rela *rel_text = elf_rela_text_build(&rel_text_count);

    // 段表构建
    Elf64_Shdr *shdr = malloc(sizeof(Elf64_Shdr) * SHDR_COUNT);
    string shstrtab = elf_shdr_build(text_size,
                                     data_size,
                                     symtab_count * sizeof(Elf64_Sym),
                                     strlen(strtab),
                                     rel_text_count * sizeof(Elf64_Rela),
                                     shdr);

    Elf64_Off shoff = sizeof(Elf64_Ehdr) + text_size + data_size + strlen(shstrtab);
    // 文件头构建
    Elf64_Ehdr ehdr = {
            .e_ident = {
                    0x7F, // del 符号编码
                    'E',
                    'L',
                    'F',
                    ELFCLASS64,  // elf 文件类型: 64 位
                    ELFDATA2LSB, // 字节序： 小端
                    EV_CURRENT,   // elf 版本号
                    ELFOSABI_NONE, // os abi = unix v
                    0, // ABI version
            },
            .e_type = ET_REL, // elf 文件类型 = 可重定位文件
            .e_machine = EM_X86_64,
            .e_version = EV_CURRENT,
            .e_entry = 0, // elf 文件程序入口的线性绝对地址，一般用于可执行文件，可重定位文件配置为 0 即可
            .e_phoff = 0, // 程序头表在文件中的偏移，对于可重定位文件来说，值同样为 0，
            .e_shoff = shoff, // 段表在文件中偏移地址
            .e_flags = 0, // elf 平台相关熟悉，设置为 0 即可
            .e_ehsize = sizeof(Elf64_Ehdr), // 文件头表的大小
            .e_phentsize = 0, // 程序头表项的大小, 可重定位表没有这个头
            .e_phnum = 0, // 程序头表项, 这个只能是 0
            .e_shentsize = sizeof(Elf64_Shdr), // 段表项的大小
            .e_shnum = SHDR_COUNT, // 段表项数
            .e_shstrndx = SHSTRTAB_INDEX, // 段表字符串表的索引
    };

    // 输出二进制
    return (elf_t) {
            .ehdr = ehdr,
            .text = text,
            .text_size = text_size,
            .data = data,
            .data_size = data_size,
            .shstrtab = shstrtab,
            .shdr = shdr,
            .shdr_count = SHDR_COUNT,
            .symtab = symtab,
            .symtab_count = symtab_count,
            .strtab = strtab,
            .rela_text = rel_text,
            .real_text_count = rel_text_count
    };
}

/**
 * 包含的项：
 * .text/.data/.rel.text/.shstrtab/.symtab/.strtab
 * @param text_size
 * @param symtab_size
 * @param strtab_size
 * @param rel_text_size
 * @return
 */
char *elf_shdr_build(uint64_t text_size,
                     uint64_t data_size,
                     uint64_t symtab_size,
                     uint64_t strtab_size,
                     uint64_t rela_text_size,
                     Elf64_Shdr *shdr) {

    // 段表字符串表
    char *shstrtab_data = " ";
    uint64_t rela_text_name = strlen(shstrtab_data);
    uint64_t text_name = 6;
    shstrtab_data = str_connect(shstrtab_data, ".rela.text ");
    uint64_t data_name = strlen(shstrtab_data);
    shstrtab_data = str_connect(shstrtab_data, ".data ");
    uint64_t shstrtab_name = strlen(shstrtab_data);
    shstrtab_data = str_connect(shstrtab_data, ".shstrtab ");
    uint64_t symtab_name = strlen(shstrtab_data);
    shstrtab_data = str_connect(shstrtab_data, ".symtab ");
    uint64_t strtab_name = strlen(shstrtab_data);
    shstrtab_data = str_connect(shstrtab_data, ".strtab ");

    uint64_t offset = sizeof(Elf64_Ehdr);
    // 符号表偏移
    uint64_t text_offset = offset;
    offset += text_size;
    // 数据段在文件中的偏移
    uint64_t data_offset = offset;
    offset += data_size;
    // 段表字符串表偏移
    uint64_t shstrtab_offset = offset;
    offset += strlen(shstrtab_data);
    // 段表偏移
    uint64_t shdr_offset = offset;
    offset += SHDR_COUNT * sizeof(Elf64_Shdr);
    // 符号表偏移
    uint64_t symtab_offset = offset;
    offset += symtab_size;
    // 字符串表偏移
    uint64_t strtab_offset = offset;
    offset += strtab_size;
    // 重定位表偏移
    uint64_t rela_text_offset = offset;
    offset += rela_text_size;



//  Elf64_Shdr **section_table = malloc(sizeof(Elf64_Shdr) * 5);

    // 空段
    shdr[0] = (Elf64_Shdr) {
            .sh_name = 0,
            .sh_type = 0, // 表示程序段
            .sh_flags = 0,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = 0,
            .sh_size = 0,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 0,
            .sh_entsize = 0
    };

    // 代码段
    shdr[1] = (Elf64_Shdr) {
            .sh_name = text_name,
            .sh_type = SHT_PROGBITS, // 表示程序段
            .sh_flags = SHF_ALLOC | SHF_EXECINSTR,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = text_offset,
            .sh_size = text_size,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0
    };

    // 代码段重定位表
    shdr[2] = (Elf64_Shdr) {
            .sh_name = rela_text_name,
            .sh_type = SHT_RELA, // 表示程序段
            .sh_flags = SHF_INFO_LINK,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = rela_text_offset,
            .sh_size = rela_text_size,
            .sh_link = 4,
            .sh_info = 1,
            .sh_addralign = 8,
            .sh_entsize = sizeof(Elf64_Rela)
    };

    // 数据段
    shdr[3] = (Elf64_Shdr) {
            .sh_name = data_name,
            .sh_type = SHT_PROGBITS, // 表示程序段
            .sh_flags =  SHF_ALLOC | SHF_WRITE,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = data_offset,
            .sh_size = data_size,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 4,
            .sh_entsize = 0
    };

    // 符号表段
    shdr[4] = (Elf64_Shdr) {
            .sh_name = symtab_name,
            .sh_type = SHT_SYMTAB, // 表示程序段
            .sh_flags =  0,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = symtab_offset,
            .sh_size = symtab_size,
            .sh_link = 5,
            .sh_info = SYMTAB_LAST_LOCAL_INDEX + 1, // 符号表最后一个 local 符号的索引
            .sh_addralign = 8,
            .sh_entsize = sizeof(Elf64_Sym)
    };

    // 字符串串表 5
    shdr[5] = (Elf64_Shdr) {
            .sh_name = strtab_name,
            .sh_type = SHT_STRTAB, // 表示程序段
            .sh_flags =  0,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = strtab_offset,
            .sh_size = strtab_size,
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0,
    };


    // 段表字符串表 6
    shdr[6] = (Elf64_Shdr) {
            .sh_name = shstrtab_name,
            .sh_type = SHT_STRTAB, // 表示程序段
            .sh_flags =  0,
            .sh_addr = 0, // 可执行文件才有该地址
            .sh_offset = shstrtab_offset,
            .sh_size = strlen(shstrtab_data),
            .sh_link = 0,
            .sh_info = 0,
            .sh_addralign = 1,
            .sh_entsize = 0,
    };

    return shstrtab_data;
}

char *elf_symtab_build(Elf64_Sym *symtab) {
    // 内部初始化
//  symbol = malloc(sizeof(symbol) * size);
    int index = 0;

    // 字符串表
    char *strtab_data = " ";

    // 0: NULL
    symtab[index++] = (Elf64_Sym) {
            .st_name = 0, // 字符串表的偏移
            .st_value = 0, // 符号相对于所在段基址的偏移
            .st_size = 0, // 符号的大小，单位字节
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_NOTYPE),
            .st_other = 0,
            .st_shndx = 0, // 符号所在段，在段表内的索引
    };

    // 1: file
    symtab[index++] = (Elf64_Sym) {
            .st_name = strlen(strtab_data),
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_FILE),
            .st_other = 0,
            .st_shndx = SHN_ABS,
    };
    strtab_data = str_connect(strtab_data, filename);
    strtab_data = str_connect(strtab_data, " ");

    // 2: section: 1 = .text
    symtab[index++] = (Elf64_Sym) {
            .st_name = 0,
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
            .st_other = 0,
            .st_shndx = TEXT_INDEX,
    };

    // 3: section: 3 = .data
    symtab[index++] = (Elf64_Sym) {
            .st_name = 0,
            .st_value = 0,
            .st_size = 0,
            .st_info = ELF64_ST_INFO(STB_LOCAL, STT_SECTION),
            .st_other = 0,
            .st_shndx = DATA_INDEX,
    };

    // 4. 填充其余符号(list 遍历)
    list_node *current = elf_symbol_list->front;
    while (current->value != NULL) {
        elf_symbol_t *s = current->value;
        if (!s->is_local) {
            Elf64_Sym sym = {
                    .st_name = strlen(strtab_data),
                    .st_size = s->size,
                    .st_info = ELF64_ST_INFO(STB_GLOBAL, s->type),
                    .st_other = 0,
                    .st_shndx = s->section,
            };
            if (s->offset != NULL) {
                sym.st_value = *s->offset;
            }
            int temp = index++;
            symtab[temp] = sym;
            s->symtab_index = temp;
            strtab_data = str_connect(strtab_data, s->name);
            strtab_data = str_connect(strtab_data, " ");
        }
        current = current->next;
    }

    return strtab_data;
}

Elf64_Rela *elf_rela_text_build(uint64_t *count) {
    Elf64_Rela *r = malloc(sizeof(Elf64_Rela) * elf_rel_list->count);
    *count = elf_rel_list->count;
    list_node *current = elf_rel_list->front;
    int i = 0;
    while (current->value != NULL) {
        elf_rel_t *rel = current->value;
        // 宿友的 elf 都必须在符号表中找到,因为 rela_text 中的 info 存储着在符号表中的索引
        elf_symbol_t *s = table_get(elf_symbol_table, rel->name);
        if (s == NULL) {
            error_exit("[elf_rela_text_build] not found symbol %s in table, all rel symbol must store to symbol table",
                       rel->name);
        }
        uint64_t index = s->symtab_index;
        // r_sym 表示重定位项在符号表内的索引(?)
        r[i] = (Elf64_Rela) {
                .r_offset = *rel->offset,
                .r_info = ELF64_R_INFO(index, R_X86_64_PC32),
                .r_addend = rel->addend,
        };
        if (rel->type == ELF_SYMBOL_TYPE_VAR) {
            r[i].r_offset += 3; // mov 0x0(%rip),%rsi = 48 8b 35 00 00 00 00，其中偏移是从第四个字符开始
        }

        i++;
        current = current->next;
    }

    return r;
}

uint8_t *elf_encoding(elf_t elf, uint64_t *count) {
    *count = sizeof(Elf64_Ehdr) +
             elf.data_size +
             elf.text_size +
             strlen(elf.shstrtab) +
             sizeof(Elf64_Shdr) * elf.shdr_count +
             sizeof(Elf64_Sym) * elf.symtab_count +
             strlen(elf.strtab) +
             sizeof(Elf64_Rela) * elf.real_text_count;
    uint8_t *binary = malloc(*count);

    // 文件头
    uint8_t *p = binary;
    memcpy(p, &elf.ehdr, sizeof(Elf64_Ehdr));
    p += sizeof(Elf64_Ehdr);

    // 代码段
    memcpy(p, elf.text, elf.text_size);
    p += elf.text_size;

    // 数据段
    memcpy(p, elf.data, elf.data_size);
    p += elf.data_size;

    // 段表字符串表
    size_t len = strlen(elf.shstrtab);
    str_replace(elf.shstrtab, 32, 0);
    memcpy(p, elf.shstrtab, len);
    p += len;

    // 段表
    memcpy(p, elf.shdr, sizeof(Elf64_Shdr) * elf.shdr_count);
    p += sizeof(Elf64_Shdr) * elf.shdr_count;

    // 符号表
    memcpy(p, elf.symtab, sizeof(Elf64_Sym) * elf.symtab_count);
    p += sizeof(Elf64_Sym) * elf.symtab_count;

    // 字符串表
    len = strlen(elf.strtab);
    str_replace(elf.strtab, 32, 0);
    memcpy(p, elf.strtab, len);
    p += len;

    // 重定位表
    memcpy(p, elf.rela_text, sizeof(Elf64_Rela) * elf.real_text_count);
    p += sizeof(Elf64_Rela) * elf.real_text_count;

    return binary;
}

void elf_to_file(uint8_t *binary, uint64_t count, char *file) {
    FILE *f = fopen(file, "w+b");
    fwrite(binary, 1, count, f);
    fclose(f);
}

uint8_t *elf_text_build(uint64_t *size) {
    *size = 0;
    list_node *current = elf_text_inst_list->front;
    while (current->value != NULL) {
        elf_text_inst_t *inst = current->value;
        *size += inst->count;
        current = current->next;
    }

    uint8_t *text = malloc(sizeof(uint8_t) * *size);
    if (*size == 0) {
        return text;
    }

    uint8_t *p = text;

    current = elf_text_inst_list->front;
    while (current->value != NULL) {
        elf_text_inst_t *inst = current->value;
        memcpy(p, inst->data, inst->count);
        p += inst->count;
        current = current->next;
    }

    return text;
}

/**
 * 写入 custom 符号表即可
 * @param decl
 */
void elf_var_decl_build(asm_var_decl decl) {
    elf_symbol_t *symbol = NEW(elf_symbol_t);
    symbol->name = decl.name;
    symbol->type = ELF_SYMBOL_TYPE_VAR;
    symbol->section = ELF_SECTION_DATA;
    symbol->offset = elf_current_data_offset();
    global_data_offset += decl.size;
    symbol->size = decl.size;
    symbol->value = decl.value;
    symbol->is_rel = false;
    symbol->is_local = false; // data 段的都是全局符号，可以被其他文件引用
    elf_symbol_insert(symbol);
//    elf_confirm_text_rel(symbol->as); // TODO 符号表需要指定重排吗？
}

void elf_var_decl_list_build(list *decl_list) {
    if (list_empty(decl_list)) {
        return;
    }
    list_node *current = decl_list->front;
    while (current->value != NULL) {
        asm_var_decl *decl = current->value;
        elf_var_decl_build(*decl);
        current = current->next;
    }
}

uint8_t *elf_data_build(uint64_t *size) {
    // 遍历符号表计算数量并申请内存
    list_node *current = elf_symbol_list->front;
    while (current->value != NULL) {
        elf_symbol_t *t = current->value;
        if (t->type != ELF_SYMBOL_TYPE_VAR) {
            current = current->next;
            continue;
        }
        *size += t->size;
        current = current->next;
    }
    // 按 4 字节对齐
    if (*size > 0) {
        *size = (*size - *size % 4) + 4;
    }

    uint8_t *data = malloc(*size);
    uint8_t *p = data;

    current = elf_symbol_list->front;
    while (current->value != NULL) {
        elf_symbol_t *symbol = current->value;
        if (symbol->type != ELF_SYMBOL_TYPE_VAR) {
            current = current->next;
            continue;
        }

        if (symbol->value != NULL) {
            memcpy(p, symbol->value, symbol->size);
        }
        p += symbol->size;
        current = current->next;
    }

    return data;
}

void elf_init(char *_filename) {
    filename = _filename;
    global_data_offset = 0;
    global_text_offset = 0;

    elf_text_inst_list = list_new();
    elf_symbol_table = table_new();
    elf_symbol_list = list_new();
    elf_rel_list = list_new();
}

void elf_text_label_build(asm_inst_t asm_inst, uint64_t *offset) {
    // text 中唯一需要注册到符号表的数据, 且不需要编译进 elf_text_item
    asm_operand_symbol_t *s = asm_inst.operands[0]->value;
    elf_symbol_t *symbol = NEW(elf_symbol_t);
    symbol->name = s->name;
    symbol->type = ELF_SYMBOL_TYPE_FN;
    symbol->section = ELF_SECTION_TEXT;
    symbol->size = 0;
    symbol->is_rel = false;
    symbol->is_local = s->is_local; // 局部 label 在生成符号表的时候可以忽略

    elf_confirm_text_rel(symbol->name);

    // confirm 后可能会产生 offset 修正,所以需要在 confirm 之后再确定当前 offset
    *offset = global_text_offset;
    symbol->offset = offset; // symbol 和 inst 共用 offset
    elf_symbol_insert(symbol);
}
