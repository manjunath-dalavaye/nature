#ifndef NATURE_SRC_SCANNER_H_
#define NATURE_SRC_SCANNER_H_

#include "utils/linked.h"
#include "utils/helper.h"
#include "src/syntax/token.h"
#include "src/module.h"

linked_t *scanner(module_t *m);

void scanner_cursor_init(module_t *module);

bool scanner_skip_space(module_t *m);

bool scanner_is_alpha(module_t *m, char c);

bool scanner_is_string(module_t *module, char s); // '/"/`

bool scanner_is_number(module_t *m, char c);

bool scanner_is_hex_number(module_t *m, char c);

bool scanner_is_float(module_t *module, char *word);

char *scanner_ident_advance(module_t *module);

char *scanner_hex_number_advance(module_t *m);

char *scanner_number_advance(module_t *m);

char *scanner_string_advance(module_t *module, char close_char);

token_e scanner_ident(char *word, int length);

token_e scanner_rest(char *word,
                     int word_length,
                     int8_t rest_start,
                     int8_t rest_length,
                     char *rest,
                     int8_t type);

token_e scanner_special_char(module_t *m);

char scanner_guard_advance(module_t *module); // guard 前进一个字符
char *scanner_gen_word(module_t *module);

bool scanner_at_eof(module_t *m); // guard 是否遇见了 '\0'

bool scanner_match(module_t *module, char expected);

bool scanner_is_space(char c);

bool scanner_at_stmt_end(module_t *m);

#endif //NATURE_SRC_SCANNER_H_
