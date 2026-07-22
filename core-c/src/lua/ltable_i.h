/*
** ltable_i.h -- private bits shared by ltable.c (the alloc/mutation
** half, pinned -O0 by Calypsi miscompile #4) and ltable_get.c (the
** read-only lookup half, split out in M9 T7 so GETTABLE's luaH_get
** chain can build -O2 --speed).
**
** The hash helpers are static in BOTH TUs on purpose: an extern
** function here would put a far call back on the exact path the split
** exists to speed up. The dummy node must be ONE object across the
** TUs (isdummy compares pointers), so ltable.c exports it.
*/

#ifndef ltable_i_h
#define ltable_i_h

#include "lobject.h"
#include "lstate.h"
#include "ltable.h"

#define hashpow2(t,n)		(gnode(t, lmod((n), sizenode(t))))

#define hashstr(t,str)		hashpow2(t, (str)->tsv.hash)
#define hashboolean(t,p)	hashpow2(t, p)


/*
** for some types, it is better to avoid modulus by power of 2, as
** they tend to have many 2 factors.
*/
#define hashmod(t,n)	(gnode(t, ((n) % ((sizenode(t)-1)|1))))


#define hashpointer(t,p)	hashmod(t, IntPoint(p))


LUAI_DDEC const Node luaH_dummynode_;

#define dummynode		(&luaH_dummynode_)

#define isdummy(n)		((n) == dummynode)


/*
** hash for lua_Numbers
*/
static Node *hashnum (const Table *t, lua_Number n) {
  int i;
  luai_hashnum(i, n);
  if (i < 0) {
    if (cast(unsigned int, i) == 0u - i)  /* use unsigned to avoid overflows */
      i = 0;  /* handle INT_MIN */
    i = -i;  /* must be a positive value */
  }
  return hashmod(t, i);
}


/*
** returns the `main' position of an element in a table (that is, the index
** of its hash value)
*/
static Node *mainposition (const Table *t, const TValue *key) {
  switch (ttype(key)) {
    case LUA_TNUMBER:
      return hashnum(t, nvalue(key));
    case LUA_TLNGSTR: {
      TString *s = rawtsvalue(key);
      if (s->tsv.extra == 0) {  /* no hash? */
        s->tsv.hash = luaS_hash(getstr(s), s->tsv.len, s->tsv.hash);
        s->tsv.extra = 1;  /* now it has its hash */
      }
      return hashstr(t, rawtsvalue(key));
    }
    case LUA_TSHRSTR:
      return hashstr(t, rawtsvalue(key));
    case LUA_TBOOLEAN:
      return hashboolean(t, bvalue(key));
    case LUA_TLIGHTUSERDATA:
      return hashpointer(t, pvalue(key));
    case LUA_TLCF:
      return hashpointer(t, fvalue(key));
    default:
      return hashpointer(t, gcvalue(key));
  }
}

#endif
