#ifndef NATURE_SRC_ASSEMBLER_ELF_ELF_H_
#define NATURE_SRC_ASSEMBLER_ELF_ELF_H_

#include "src/assembler/amd64/asm.h"
#include "lib_elf.h"
#include "src/assembler/amd64/opcode.h"
#include "src/lib/list.h"
#include "src/lib/table.h"

#define TEXT_INDEX 1
#define RELA_TEXT_INDEX 2
#define DATA_INDEX 3
#define SYMTAB_INDEX 4
#define SHSTRTAB_INDEX 5
#define SYMTAB_LAST_LOCAL_INDEX 3 // 符号表最后一个 local 字符的索引
string filename;

uint64_t global_offset;

typedef enum {
  ELF_SYMBOL_TYPE_VAR = 1,
  ELF_SYMBOL_TYPE_FN = 2,
//  ELF_SYMBOL_TYPE_SECTION,
//  ELF_SYMBOL_TYPE_FILE,
} elf_symbol_type;

typedef enum {
  ELF_SECTION_TEXT = TEXT_INDEX,
  ELF_SECTION_DATA = DATA_INDEX,
  ELF_SECTION_RELA_TEXT = RELA_TEXT_INDEX,
} elf_section;

/**
 * 指令存储结构
 * @param asm_inst
 */
typedef struct {
  uint8_t *data; // 指令二进制
  uint8_t count; // 指令长度
  uint64_t *offset; // 指令起始 offset
  asm_inst_t asm_inst; // 原始指令, 指令改写与二次扫描时使用
  string rel_symbol; // 使用的符号
  asm_operand_t *rel_operand; // 引用自 asm_inst
} elf_text_inst_t;

/**
 * 全局符号表存储结构
 * @param asm_inst
 */
typedef struct {
  string name;
  uint8_t size; // 8/16/32/64
  elf_symbol_type type;  // fn/var
  bool is_rel; // 是否引用外部符号
  bool is_local; // 是否是本地符号
  elf_section section; // 所在段，估计只有 text 段了
  uint64_t *offset;  // 符号所在偏移, 只有符号定义需要这个偏移地址,现阶段只有 text 段内便宜，改地址需要被修正
  int symtab_index; // 在符号表的索引
} elf_symbol_t;

/**
 * 重定位表, 如果一个符号引用了外部符号多次，这里就需要记录多次
 * @param asm_inst
 */
typedef struct {
  string name;
  elf_symbol_type type; // 符号引用还是标签引用
  uint8_t section; // 使用符号
  uint64_t *offset;
  int8_t addend;
} elf_rel_t;

list *elf_text_inst_list;
table *elf_symbol_table; // key: symbol_name, value list_node
list *elf_symbol_list; // list_node link
list *elf_rel_list;

// 如果 asm_inst 的参数是 label 或者 inst.name = label 需要进行符号注册与处理
// 其中需要一个 link 结构来引用最近 128 个字节的指令，做 jmp rel 跳转，原则上不能影响原来的指令
// 符号表的收集工作，符号表收集需要记录偏移地址，所以如果存在修改，也需要涉及到这里的数据修改
void elf_text_inst_build(asm_inst_t asm_inst);
void elf_text_inst_list_build(list *asm_inst_list); // 一次构建基于 asm_inst 列表
void elf_text_inst_list_second_build(list *elf_text_inst_list); // 二次构建(基于 elf_text_inst_list)

void elf_symbol_insert(elf_symbol_t *symbol);

void elf_confirm_text_rel(string name);

/**
 * rel32 to rel8, count - 3
 * @param inst
 */
void elf_rewrite_text_rel(elf_text_inst_t *inst);

uint64_t *elf_new_current_offset();

typedef struct {
  Elf64_Ehdr ehdr;
  uint8_t *text;
  uint64_t text_count;
//  uint8_t *data;// 数据段省略
  char *shstrtab;
  Elf64_Shdr *shdr;
  uint8_t shdr_count;
  Elf64_Sym *symtab;
  uint64_t symtab_count;
  char *strtab;
  Elf64_Rela *rela_text;
  uint64_t real_text_count;
} elf_t;

/**
 * 文件头表
 * 代码段 (.text)
 * 数据段 (.data)
 * 段表字符串表 (.shstrtab)
 * 段表 (section header table)
 * 符号表 (.symtab)
 * 字符串表(.strtab)
 * 重定位表(.rel.text)
 * @return
 */
elf_t elf_new();

uint8_t *elf_encoding(elf_t elf, uint64_t *count);

void elf_to_file(uint8_t *binary, uint64_t count, string filename);

/**
 * 生成二进制结果
 * @param count
 * @return
 */
uint8_t *elf_text_build(uint64_t *count);

/**
 * 段表构建
 * @return
 */
string elf_shdr_build(uint64_t text_size,
                      uint64_t symtab_size,
                      uint64_t strtab_size,
                      uint64_t rela_text_size,
                      Elf64_Shdr *shdr);

/**
 * 重定位表构建
 * @param rel_text
 * @param count
 */
Elf64_Rela *elf_rela_text_build(uint64_t *count);

/**
 *
 * @param symbol 符号表和个数
 * @param count
 * @return 字符串表
 */
string elf_symtab_build(Elf64_Sym *symbol);

#endif //NATURE_SRC_ASSEMBLER_ELF_ELF_H_
