#ifndef NATURE_SRC_VALUE_H_
#define NATURE_SRC_VALUE_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define FIXED_ARRAY_COUNT 1000
#define string char *
#define STRING_EOF '\0'

#define v_addr_t uint64_t
#define addr_t uint64_t

static inline void *mallocz(size_t size) {
//    return calloc(1, size);
    void *ptr;
    ptr = malloc(size);
    if (size) {
        memset(ptr, 0, size);
    }
    return ptr;
}

#define GROW_CAPACITY(capacity) \
  ((capacity) < 8 ? 8 : (capacity)*2)

#define NEW(type) mallocz(sizeof(type))

#define FLAG(value) (1 << value)

#endif //NATURE_SRC_VALUE_H_
