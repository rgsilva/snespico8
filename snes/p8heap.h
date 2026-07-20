#ifndef P8HEAP_H
#define P8HEAP_H
#include <stddef.h>

void p8heap_add_region(void *base, unsigned long size);
unsigned long p8heap_free_bytes(void);
void *p8heap_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize);

#endif
