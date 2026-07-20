/* Region heap for the Lua state on the SNES.
**
** Regions are discontiguous windows (HiROM cart SRAM comes in 8 KB
** windows at $30-$3x:6000-7FFF, plus a leftover chunk of WRAM $7E), so
** allocations never cross a region boundary. Classic address-ordered
** first-fit free list with split and coalesce; each live block carries
** a 4-byte size header. Single allocations are already capped at 32 KB
** by MAX_SIZET; regions smaller than a request simply don't satisfy it.
**
** size_t is 16-bit on Calypsi — internal sizes use unsigned long. */
#include <stddef.h>
#include <string.h>
#include "p8heap.h"

typedef struct FreeBlk {
  struct FreeBlk *next;
  unsigned long size; /* total bytes incl. this header */
} FreeBlk;

#define ALIGN(n) (((n) + 3ul) & ~3ul)
#define HDR 4ul             /* live-block size prefix */
#define MIN_FRAG 16ul       /* don't split off crumbs */

static FreeBlk *freelist;
static unsigned long total_free;

void p8heap_add_region(void *base, unsigned long size)
{
  FreeBlk *b = (FreeBlk *)base;
  FreeBlk **pp;
  b->size = size & ~3ul;
  /* address-ordered insert */
  for (pp = &freelist; *pp && (char *)*pp < (char *)b; pp = &(*pp)->next)
    ;
  b->next = *pp;
  *pp = b;
  total_free += b->size;
}

unsigned long p8heap_free_bytes(void)
{
  return total_free;
}

static void *heap_alloc(unsigned long n)
{
  FreeBlk **pp;
  n = ALIGN(n) + HDR;
  if (n < sizeof(FreeBlk))
    n = sizeof(FreeBlk);
  for (pp = &freelist; *pp; pp = &(*pp)->next) {
    FreeBlk *b = *pp;
    if (b->size >= n) {
      unsigned long rest = b->size - n;
      char *p;
      if (rest >= MIN_FRAG) {
        FreeBlk *tail = (FreeBlk *)((char *)b + n);
        tail->size = rest;
        tail->next = b->next;
        *pp = tail;
      } else {
        n = b->size; /* absorb the crumb */
        *pp = b->next;
      }
      total_free -= n;
      p = (char *)b;
      *(unsigned long *)p = n;
      return p + HDR;
    }
  }
  return NULL;
}

static void heap_free(void *ptr)
{
  char *p = (char *)ptr - HDR;
  FreeBlk *b = (FreeBlk *)p;
  FreeBlk **pp;
  unsigned long size = *(unsigned long *)p;
  b->size = size;
  total_free += size;
  for (pp = &freelist; *pp && (char *)*pp < p; pp = &(*pp)->next)
    ;
  b->next = *pp;
  *pp = b;
  /* coalesce forward */
  if (b->next && (char *)b + b->size == (char *)b->next) {
    b->size += b->next->size;
    b->next = b->next->next;
  }
  /* coalesce backward: find predecessor */
  if (pp != &freelist) {
    FreeBlk *prev = (FreeBlk *)((char *)pp - offsetof(FreeBlk, next));
    if ((char *)prev + prev->size == (char *)b) {
      prev->size += b->size;
      prev->next = b->next;
    }
  }
}

void *p8heap_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
  (void)ud;
  (void)osize;
  if (nsize == 0) {
    if (ptr)
      heap_free(ptr);
    return NULL;
  }
  if (ptr == NULL)
    return heap_alloc((unsigned long)nsize);
  {
    unsigned long have = *(unsigned long *)((char *)ptr - HDR) - HDR;
    if ((unsigned long)nsize <= have)
      return ptr; /* shrink in place (no split; Lua shrinks are rare) */
    {
      void *np = heap_alloc((unsigned long)nsize);
      if (!np)
        return NULL;
      memcpy(np, ptr, have);
      heap_free(ptr);
      return np;
    }
  }
}
