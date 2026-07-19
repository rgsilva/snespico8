// Calypsi 5.17/5.18 cc65816 -O2 (large code/data): storing through an
// array indexed by a post-incremented struct field held via a far
// pointer writes to the WRONG SLOT (off by one) — Lua's lexer
// save() `b->buffer[b->n++] = c` produced "ocal " when saving "local".
// Workaround: read the index into a local, store, then write back n+1.
#include <stddef.h>
struct mb { char *buffer; size_t n; size_t size; };
void save(struct mb *b, int c)
{
  b->buffer[b->n++] = (char)c; // chars land shifted by one position
}
