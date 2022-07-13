#include "register.h"
#include "src/lib/error.h"
#include "src/lib/helper.h"

void amd64_register_init() {
    rax = REG_OPERAND("rax", 0, QWORD);
    rcx = REG_OPERAND("rcx", 1, QWORD);
    rdx = REG_OPERAND("rdx", 2, QWORD);
    rbx = REG_OPERAND("rbx", 3, QWORD);
    rsp = REG_OPERAND("rsp", 4, QWORD);
    rbp = REG_OPERAND("rbp", 5, QWORD);
    rsi = REG_OPERAND("rsi", 6, QWORD);
    rdi = REG_OPERAND("rdi", 7, QWORD);
    r8 = REG_OPERAND("r8", 8, QWORD);
    r9 = REG_OPERAND("r9", 9, QWORD);
    r10 = REG_OPERAND("r10", 10, QWORD);
    r11 = REG_OPERAND("r11", 11, QWORD);
    r12 = REG_OPERAND("r12", 12, QWORD);
    r13 = REG_OPERAND("r13", 13, QWORD);
    r14 = REG_OPERAND("r14", 14, QWORD);
    r15 = REG_OPERAND("r15", 15, QWORD);

    eax = REG_OPERAND("eax", 0, DWORD);
    ecx = REG_OPERAND("ecx", 1, DWORD);
    edx = REG_OPERAND("edx", 2, DWORD);
    ebx = REG_OPERAND("ebx", 3, DWORD);
    esp = REG_OPERAND("esp", 4, DWORD);
    ebp = REG_OPERAND("ebp", 5, DWORD);
    esi = REG_OPERAND("esi", 6, DWORD);
    edi = REG_OPERAND("edi", 7, DWORD);
    r8d = REG_OPERAND("r8d", 8, DWORD);
    r9d = REG_OPERAND("r9d", 9, DWORD);
    r10d = REG_OPERAND("r10d", 10, DWORD);
    r11d = REG_OPERAND("r11d", 11, DWORD);
    r12d = REG_OPERAND("r12d", 12, DWORD);
    r13d = REG_OPERAND("r13d", 13, DWORD);
    r14d = REG_OPERAND("r14d", 14, DWORD);
    r15d = REG_OPERAND("r15d", 15, DWORD);

    ax = REG_OPERAND("ax", 0, WORD);
    cx = REG_OPERAND("cx", 1, WORD);
    dx = REG_OPERAND("dx", 2, WORD);
    bx = REG_OPERAND("bx", 3, WORD);
    sp = REG_OPERAND("sp", 4, WORD);
    bp = REG_OPERAND("bp", 5, WORD);
    si = REG_OPERAND("si", 6, WORD);
    di = REG_OPERAND("di", 7, WORD);
    r8w = REG_OPERAND("r8w", 8, WORD);
    r9w = REG_OPERAND("r9w", 9, WORD);
    r10w = REG_OPERAND("r10w", 10, WORD);
    r11w = REG_OPERAND("r11w", 11, WORD);
    r12w = REG_OPERAND("r12w", 12, WORD);
    r13w = REG_OPERAND("r13w", 13, WORD);
    r14w = REG_OPERAND("r14w", 14, WORD);
    r15w = REG_OPERAND("r15w", 15, WORD);

    al = REG_OPERAND("al", 0, BYTE);
    cl = REG_OPERAND("cl", 1, BYTE);
    dl = REG_OPERAND("dl", 2, BYTE);
    bl = REG_OPERAND("bl", 3, BYTE);
    spl = REG_OPERAND("spl", 4, BYTE);
    bpl = REG_OPERAND("bpl", 5, BYTE);
    sil = REG_OPERAND("sil", 6, BYTE);
    dil = REG_OPERAND("dil", 7, BYTE);
    r8b = REG_OPERAND("r8b", 8, BYTE);
    r9b = REG_OPERAND("r9b", 9, BYTE);
    r10b = REG_OPERAND("r10b", 10, BYTE);
    r11b = REG_OPERAND("r11b", 11, BYTE);
    r12b = REG_OPERAND("r12b", 12, BYTE);
    r13b = REG_OPERAND("r13b", 13, BYTE);
    r14b = REG_OPERAND("r14b", 14, BYTE);
    r15b = REG_OPERAND("r15b", 15, BYTE);

    ah = REG_OPERAND("ah", 4, BYTE);
    ch = REG_OPERAND("ch", 5, BYTE);
    dh = REG_OPERAND("dh", 6, BYTE);
    bh = REG_OPERAND("bh", 7, BYTE);

    xmm0 = REG_OPERAND("xmm0", 0, OWORD);
    xmm1 = REG_OPERAND("xmm1", 1, OWORD);
    xmm2 = REG_OPERAND("xmm2", 2, OWORD);
    xmm3 = REG_OPERAND("xmm3", 3, OWORD);
    xmm4 = REG_OPERAND("xmm4", 4, OWORD);
    xmm5 = REG_OPERAND("xmm5", 5, OWORD);
    xmm6 = REG_OPERAND("xmm6", 6, OWORD);
    xmm7 = REG_OPERAND("xmm7", 7, OWORD);
    xmm8 = REG_OPERAND("xmm8", 8, OWORD);
    xmm9 = REG_OPERAND("xmm9", 9, OWORD);
    xmm10 = REG_OPERAND("xmm10", 10, OWORD);
    xmm11 = REG_OPERAND("xmm11", 11, OWORD);
    xmm12 = REG_OPERAND("xmm12", 12, OWORD);
    xmm13 = REG_OPERAND("xmm13", 13, OWORD);
    xmm14 = REG_OPERAND("xmm14", 14, OWORD);
    xmm15 = REG_OPERAND("xmm15", 15, OWORD);

    ymm0 = REG_OPERAND("ymm0", 0, YWORD);
    ymm1 = REG_OPERAND("ymm1", 1, YWORD);
    ymm2 = REG_OPERAND("ymm2", 2, YWORD);
    ymm3 = REG_OPERAND("ymm3", 3, YWORD);
    ymm4 = REG_OPERAND("ymm4", 4, YWORD);
    ymm5 = REG_OPERAND("ymm5", 5, YWORD);
    ymm6 = REG_OPERAND("ymm6", 6, YWORD);
    ymm7 = REG_OPERAND("ymm7", 7, YWORD);
    ymm8 = REG_OPERAND("ymm8", 8, YWORD);
    ymm9 = REG_OPERAND("ymm9", 9, YWORD);
    ymm10 = REG_OPERAND("ymm10", 10, YWORD);
    ymm11 = REG_OPERAND("ymm11", 11, YWORD);
    ymm12 = REG_OPERAND("ymm12", 12, YWORD);
    ymm13 = REG_OPERAND("ymm13", 13, YWORD);
    ymm14 = REG_OPERAND("ymm14", 14, YWORD);
    ymm15 = REG_OPERAND("ymm15", 15, YWORD);

    zmm0 = REG_OPERAND("zmm0", 0, ZWORD);
    zmm1 = REG_OPERAND("zmm1", 1, ZWORD);
    zmm2 = REG_OPERAND("zmm2", 2, ZWORD);
    zmm3 = REG_OPERAND("zmm3", 3, ZWORD);
    zmm4 = REG_OPERAND("zmm4", 4, ZWORD);
    zmm5 = REG_OPERAND("zmm5", 5, ZWORD);
    zmm6 = REG_OPERAND("zmm6", 6, ZWORD);
    zmm7 = REG_OPERAND("zmm7", 7, ZWORD);
    zmm8 = REG_OPERAND("zmm8", 8, ZWORD);
    zmm9 = REG_OPERAND("zmm9", 9, ZWORD);
    zmm10 = REG_OPERAND("zmm10", 10, ZWORD);
    zmm11 = REG_OPERAND("zmm11", 11, ZWORD);
    zmm12 = REG_OPERAND("zmm12", 12, ZWORD);
    zmm13 = REG_OPERAND("zmm13", 13, ZWORD);
    zmm14 = REG_OPERAND("zmm14", 14, ZWORD);
    zmm15 = REG_OPERAND("zmm15", 15, ZWORD);

}

static string amd64_register_table_key(uint8_t index, uint8_t size) {
    uint16_t int_key = ((uint16_t) index << 8) | size;
    return itoa(int_key);
}

asm_operand_register_t *amd64_register_find(uint8_t index, uint8_t size) {
    return table_get(amd64_regs_table, amd64_register_table_key(index, size));
}

asm_operand_register_t *reg_operand_new(char *name, uint8_t index, uint8_t size) {
    asm_operand_register_t *reg = NEW(asm_operand_register_t);
    reg->name = name;
    reg->index = index;
    reg->size = size;

    table_set(amd64_regs_table, amd64_register_table_key(index, size), reg);
    return reg;
}
