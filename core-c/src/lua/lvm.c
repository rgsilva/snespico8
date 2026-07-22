/*
** $Id: lvm.c,v 2.155 2013/03/16 21:10:18 roberto Exp $
** Lua virtual machine
** See Copyright Notice in lua.h
*/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define lvm_c
#define LUA_CORE

#include "lua.h"

#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"



/* limit for table tag-method chains (to avoid loops) */
#define MAXTAGLOOP	100


const TValue *luaV_tonumber (const TValue *obj, TValue *n) {
  lua_Number num;
  if (ttisnumber(obj)) return obj;
  if (ttisstring(obj) && luaO_str2d(svalue(obj), tsvalue(obj)->len, &num)) {
    setnvalue(n, num);
    return n;
  }
  else
    return NULL;
}


int luaV_tostring (lua_State *L, StkId obj) {
  if (!ttisnumber(obj))
    return 0;
  else {
    char s[LUAI_MAXNUMBER2STR];
    lua_Number n = nvalue(obj);
    int l = lua_number2str(s, n);
    setsvalue2s(L, obj, luaS_newlstr(L, s, l));
    return 1;
  }
}


#ifndef P8_NO_HOOKS
static void traceexec (lua_State *L) {
  CallInfo *ci = L->ci;
  lu_byte mask = L->hookmask;
  int counthook = ((mask & LUA_MASKCOUNT) && L->hookcount == 0);
  if (counthook)
    resethookcount(L);  /* reset count */
  if (ci->callstatus & CIST_HOOKYIELD) {  /* called hook last time? */
    ci->callstatus &= ~CIST_HOOKYIELD;  /* erase mark */
    return;  /* do not call hook again (VM yielded, so it did not move) */
  }
  if (counthook)
    luaD_hook(L, LUA_HOOKCOUNT, -1);  /* call count hook */
  if (mask & LUA_MASKLINE) {
    Proto *p = ci_func(ci)->p;
    int npc = pcRel(ci->u.l.savedpc, p);
    int newline = getfuncline(p, npc);
    if (npc == 0 ||  /* call linehook when enter a new function, */
        ci->u.l.savedpc <= L->oldpc ||  /* when jump back (loop), or when */
        newline != getfuncline(p, pcRel(L->oldpc, p)))  /* enter a new line */
      luaD_hook(L, LUA_HOOKLINE, newline);  /* call line hook */
  }
  L->oldpc = ci->u.l.savedpc;
  if (L->status == LUA_YIELD) {  /* did hook yield? */
    if (counthook)
      L->hookcount = 1;  /* undo decrement to zero */
    ci->u.l.savedpc--;  /* undo increment (resume will increment it again) */
    ci->callstatus |= CIST_HOOKYIELD;  /* mark that it yielded */
    ci->func = L->top - 1;  /* protect stack below results */
    luaD_throw(L, LUA_YIELD);
  }
}
#endif /* !P8_NO_HOOKS */


static void callTM (lua_State *L, const TValue *f, const TValue *p1,
                    const TValue *p2, TValue *p3, int hasres) {
  ptrdiff_t result = savestack(L, p3);
  setobj2s(L, L->top++, f);  /* push function */
  setobj2s(L, L->top++, p1);  /* 1st argument */
  setobj2s(L, L->top++, p2);  /* 2nd argument */
  if (!hasres)  /* no result? 'p3' is third argument */
    setobj2s(L, L->top++, p3);  /* 3rd argument */
  /* metamethod may yield only when called from Lua code */
  luaD_call(L, L->top - (4 - hasres), hasres, isLua(L->ci));
  if (hasres) {  /* if has result, move it to its place */
    p3 = restorestack(L, result);
    setobjs2s(L, p3, --L->top);
  }
}


void luaV_gettable (lua_State *L, const TValue *t, TValue *key, StkId val) {
  // PICO-8 0.2.5 changelog: sub(str,pos,pos) can be written as str[pos]
  if (ttisstring(t) && ttisnumber(key)) {
    /* M9 T7 fix: this used the RAW fix32 bits as the index (s[2] looked
    ** up index 131072 -> always nil) and read garbage bits when the key
    ** was not a number. Real-PICO-8-pinned semantics (realvec): floor
    ** the index, count from the end for pos <= 0 -- but out of range
    ** is NIL, not "": the console's str[pos] is NOT quite
    ** sub(str,pos,pos) at the edges (sub gives "" there; the realvec
    ** stridx4 loop records the console printing its own DIVERGES lines
    ** for exactly the out-of-range ks). Non-number keys fall through
    ** to the metatable path and error like the console. */
    const char *s = svalue(t);
    size_t ls = tsvalue(t)->len;
    int k = luai_numasint(nvalue(key));  /* floor, like sub() */
    size_t idx = k > 0 ? (size_t)(k - 1) : ls + (size_t)k;
    if (idx >= ls)
      setnilvalue(val);
    else
      setsvalue2s(L, val, luaS_newlstr(L, s + idx, 1));
    return;
  }
  int loop;
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;
    if (ttistable(t)) {  /* `t' is a table? */
      Table *h = hvalue(t);
      const TValue *res = luaH_get(h, key); /* do a primitive get */
      if (!ttisnil(res) ||  /* result is not nil? */
          (tm = fasttm(L, h->metatable, TM_INDEX)) == NULL) { /* or no TM? */
        setobj2s(L, val, res);
        return;
      }
      /* else will try the tag method */
    }
    else if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_INDEX)))
      luaG_typeerror(L, t, "index");
    if (ttisfunction(tm)) {
      callTM(L, tm, t, key, val, 1);
      return;
    }
    t = tm;  /* else repeat with 'tm' */
  }
  luaG_runerror(L, "loop in gettable");
}


void luaV_settable (lua_State *L, const TValue *t, TValue *key, StkId val) {
  int loop;
  for (loop = 0; loop < MAXTAGLOOP; loop++) {
    const TValue *tm;
    if (ttistable(t)) {  /* `t' is a table? */
      Table *h = hvalue(t);
      TValue *oldval = cast(TValue *, luaH_get(h, key));
      /* if previous value is not nil, there must be a previous entry
         in the table; moreover, a metamethod has no relevance */
      if (!ttisnil(oldval) ||
         /* previous value is nil; must check the metamethod */
         ((tm = fasttm(L, h->metatable, TM_NEWINDEX)) == NULL &&
         /* no metamethod; is there a previous entry in the table? */
         (oldval != luaO_nilobject ||
         /* no previous entry; must create one. (The next test is
            always true; we only need the assignment.) */
         (oldval = luaH_newkey(L, h, key), 1)))) {
        /* no metamethod and (now) there is an entry with given key */
        setobj2t(L, oldval, val);  /* assign new value to that entry */
        invalidateTMcache(h);
        luaC_barrierback(L, obj2gco(h), val);
        return;
      }
      /* else will try the metamethod */
    }
    else  /* not a table; check metamethod */
      if (ttisnil(tm = luaT_gettmbyobj(L, t, TM_NEWINDEX)))
        luaG_typeerror(L, t, "index");
    /* there is a metamethod */
    if (ttisfunction(tm)) {
      callTM(L, tm, t, key, val, 0);
      return;
    }
    t = tm;  /* else repeat with 'tm' */
  }
  luaG_runerror(L, "loop in settable");
}


static int call_binTM (lua_State *L, const TValue *p1, const TValue *p2,
                       StkId res, TMS event) {
  const TValue *tm = luaT_gettmbyobj(L, p1, event);  /* try first operand */
  if (ttisnil(tm))
    tm = luaT_gettmbyobj(L, p2, event);  /* try second operand */
  if (ttisnil(tm)) return 0;
  callTM(L, tm, p1, p2, res, 1);
  return 1;
}


static const TValue *get_equalTM (lua_State *L, Table *mt1, Table *mt2,
                                  TMS event) {
  const TValue *tm1 = fasttm(L, mt1, event);
  const TValue *tm2;
  if (tm1 == NULL) return NULL;  /* no metamethod */
  if (mt1 == mt2) return tm1;  /* same metatables => same metamethods */
  tm2 = fasttm(L, mt2, event);
  if (tm2 == NULL) return NULL;  /* no metamethod */
  if (luaV_rawequalobj(tm1, tm2))  /* same metamethods? */
    return tm1;
  return NULL;
}


static int call_orderTM (lua_State *L, const TValue *p1, const TValue *p2,
                         TMS event) {
  if (!call_binTM(L, p1, p2, L->top, event))
    return -1;  /* no metamethod */
  else
    return !l_isfalse(L->top);
}


/* snespico8: C rewrite; full 64KB including 0.2.4 extended RAM (z8lua
** masked to 0x7fff, but standalone z8lua has no RAM attached at all) */
#define PEEK(ram, address) ((ram) ? (ram)[(address) & 0xffffu] : 0)

/* snespico8: dispatched-VM-instruction counter. SNES builds pass
** P8_VMOPS_ADDR (the fixed MB_VMOPS mailbox slot, calypsi/mailbox.h) so
** the counter lives at a link-layout-proof address the Mesen harness can
** sample live mid-frame; host builds keep a plain variable. */
#ifdef P8_VMOPS_ADDR
#define p8_vmops (*(volatile __far unsigned long *)(P8_VMOPS_ADDR))
#else
unsigned long p8_vmops;
#endif
#ifdef P8_OPSTATS
unsigned long p8_opstats[64];  /* per-opcode counters (runp8 -S) */
#endif

lua_Number luaV_peek(struct lua_State *L, lua_Number a, int count)
{
  unsigned char const *p = G(L)->pico8memory;
  unsigned int address = (unsigned int)p8fix_toint((long)a) & 0xffffu;
  unsigned long ret = 0;
  switch (count) {
    case 4:
      ret |= (unsigned long)PEEK(p, address + 1) << 8;
      ret |= (unsigned long)PEEK(p, address);
      address += 2;
      /* fall through */
    case 2:
      ret |= (unsigned long)PEEK(p, address + 1) << 24;
      /* fall through */
    case 1:
      ret |= (unsigned long)PEEK(p, address) << 16;
      break;
  }
  return (lua_Number)(long)(LUA_INT32)ret;
}


static int l_strcmp (const TString *ls, const TString *rs) {
  const char *l = getstr(ls);
  size_t ll = ls->tsv.len;
  const char *r = getstr(rs);
  size_t lr = rs->tsv.len;
  for (;;) {
    int temp = strcoll(l, r);
    if (temp != 0) return temp;
    else {  /* strings are equal up to a `\0' */
      size_t len = strlen(l);  /* index of first `\0' in both strings */
      if (len == lr)  /* r is finished? */
        return (len == ll) ? 0 : 1;
      else if (len == ll)  /* l is finished? */
        return -1;  /* l is smaller than r (because r is not finished) */
      /* both strings longer than `len'; go on comparing (after the `\0') */
      len++;
      l += len; ll -= len; r += len; lr -= len;
    }
  }
}


int luaV_lessthan (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttisnumber(l) && ttisnumber(r))
    return luai_numlt(L, nvalue(l), nvalue(r));
  else if (ttisstring(l) && ttisstring(r))
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) < 0;
  else if ((res = call_orderTM(L, l, r, TM_LT)) < 0)
    luaG_ordererror(L, l, r);
  return res;
}


int luaV_lessequal (lua_State *L, const TValue *l, const TValue *r) {
  int res;
  if (ttisnumber(l) && ttisnumber(r))
    return luai_numle(L, nvalue(l), nvalue(r));
  else if (ttisstring(l) && ttisstring(r))
    return l_strcmp(rawtsvalue(l), rawtsvalue(r)) <= 0;
  else if ((res = call_orderTM(L, l, r, TM_LE)) >= 0)  /* first try `le' */
    return res;
  else if ((res = call_orderTM(L, r, l, TM_LT)) < 0)  /* else try `lt' */
    luaG_ordererror(L, l, r);
  return !res;
}


/*
** equality of Lua values. L == NULL means raw equality (no metamethods)
*/
int luaV_equalobj_ (lua_State *L, const TValue *t1, const TValue *t2) {
  const TValue *tm;
  lua_assert(ttisequal(t1, t2));
  switch (ttype(t1)) {
    case LUA_TNIL: return 1;
    case LUA_TNUMBER: return luai_numeq(nvalue(t1), nvalue(t2));
    case LUA_TBOOLEAN: return bvalue(t1) == bvalue(t2);  /* true must be 1 !! */
    case LUA_TLIGHTUSERDATA: return pvalue(t1) == pvalue(t2);
    case LUA_TLCF: return fvalue(t1) == fvalue(t2);
    case LUA_TSHRSTR: return eqshrstr(rawtsvalue(t1), rawtsvalue(t2));
    case LUA_TLNGSTR: return luaS_eqlngstr(rawtsvalue(t1), rawtsvalue(t2));
    case LUA_TUSERDATA: {
      if (uvalue(t1) == uvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = get_equalTM(L, uvalue(t1)->metatable, uvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    case LUA_TTABLE: {
      if (hvalue(t1) == hvalue(t2)) return 1;
      else if (L == NULL) return 0;
      tm = get_equalTM(L, hvalue(t1)->metatable, hvalue(t2)->metatable, TM_EQ);
      break;  /* will try TM */
    }
    default:
      lua_assert(iscollectable(t1));
      return gcvalue(t1) == gcvalue(t2);
  }
  if (tm == NULL) return 0;  /* no TM? */
  callTM(L, tm, t1, t2, L->top, 1);  /* call TM */
  return !l_isfalse(L->top);
}


void luaV_concat (lua_State *L, int total) {
  lua_assert(total >= 2);
  do {
    StkId top = L->top;
    int n = 2;  /* number of elements handled in this pass (at least 2) */
    if (!(ttisstring(top-2) || ttisnumber(top-2)) || !tostring(L, top-1)) {
      if (!call_binTM(L, top-2, top-1, top-2, TM_CONCAT))
        luaG_concaterror(L, top-2, top-1);
    }
    else if (tsvalue(top-1)->len == 0)  /* second operand is empty? */
      (void)tostring(L, top - 2);  /* result is first operand */
    else if (ttisstring(top-2) && tsvalue(top-2)->len == 0) {
      setobjs2s(L, top - 2, top - 1);  /* result is second op. */
    }
    else {
      /* at least two non-empty string values; get as many as possible */
      size_t tl = tsvalue(top-1)->len;
      char *buffer;
      int i;
      /* collect total length */
      for (i = 1; i < total && tostring(L, top-i-1); i++) {
        size_t l = tsvalue(top-i-1)->len;
        if (l >= (MAX_SIZET/sizeof(char)) - tl)
          luaG_runerror(L, "string length overflow");
        tl += l;
      }
      buffer = luaZ_openspace(L, &G(L)->buff, tl);
      tl = 0;
      n = i;
      do {  /* concat all strings */
        size_t l = tsvalue(top-i)->len;
        memcpy(buffer+tl, svalue(top-i), l * sizeof(char));
        tl += l;
      } while (--i > 0);
      setsvalue2s(L, top-n, luaS_newlstr(L, buffer, tl));
    }
    total -= n-1;  /* got 'n' strings to create 1 new */
    L->top -= n-1;  /* popped 'n' strings and pushed one */
  } while (total > 1);  /* repeat until only 1 result left */
}


void luaV_objlen (lua_State *L, StkId ra, const TValue *rb) {
  const TValue *tm;
  switch (ttypenv(rb)) {
    case LUA_TTABLE: {
      Table *h = hvalue(rb);
      tm = fasttm(L, h->metatable, TM_LEN);
      if (tm) break;  /* metamethod? break switch to call it */
      setnvalue(ra, cast_num(luaH_getn(h)));  /* else primitive len */
      return;
    }
    case LUA_TSTRING: {
      setnvalue(ra, cast_num(tsvalue(rb)->len));
      return;
    }
    default: {  /* try metamethod */
      tm = luaT_gettmbyobj(L, rb, TM_LEN);
      if (ttisnil(tm))  /* no metamethod? */
        luaG_typeerror(L, rb, "get length of");
      break;
    }
  }
  callTM(L, tm, rb, rb, ra, 1);
}


void luaV_arith (lua_State *L, StkId ra, const TValue *rb,
                 const TValue *rc, TMS op) {
  TValue tempb, tempc;
  const TValue *b, *c;
  if ((b = luaV_tonumber(rb, &tempb)) != NULL &&
      (c = luaV_tonumber(rc, &tempc)) != NULL) {
    lua_Number res = luaO_arith(L, op - TM_ADD + LUA_OPADD, nvalue(b), nvalue(c));
    setnvalue(ra, res);
  }
  else if (!call_binTM(L, rb, rc, ra, op))
    luaG_aritherror(L, rb, rc);
}


/*
** check whether cached closure in prototype 'p' may be reused, that is,
** whether there is a cached closure with the same upvalues needed by
** new closure to be created.
*/
static Closure *getcached (Proto *p, UpVal **encup, StkId base) {
  Closure *c = p->cache;
  if (c != NULL) {  /* is there a cached closure? */
    int nup = p->sizeupvalues;
    Upvaldesc *uv = p->upvalues;
    int i;
    for (i = 0; i < nup; i++) {  /* check whether it has right upvalues */
      TValue *v = uv[i].instack ? base + uv[i].idx : encup[uv[i].idx]->v;
      if (c->l.upvals[i]->v != v)
        return NULL;  /* wrong upvalue; cannot reuse closure */
    }
  }
  return c;  /* return cached closure (or NULL if no cached closure) */
}


/*
** create a new Lua closure, push it in the stack, and initialize
** its upvalues. Note that the call to 'luaC_barrierproto' must come
** before the assignment to 'p->cache', as the function needs the
** original value of that field.
*/
static void pushclosure (lua_State *L, Proto *p, UpVal **encup, StkId base,
                         StkId ra) {
  int nup = p->sizeupvalues;
  Upvaldesc *uv = p->upvalues;
  int i;
  Closure *ncl = luaF_newLclosure(L, nup);
  ncl->l.p = p;
  setclLvalue(L, ra, ncl);  /* anchor new closure in stack */
  for (i = 0; i < nup; i++) {  /* fill in its upvalues */
    if (uv[i].instack)  /* upvalue refers to local variable? */
      ncl->l.upvals[i] = luaF_findupval(L, base + uv[i].idx);
    else  /* get upvalue from enclosing function */
      ncl->l.upvals[i] = encup[uv[i].idx];
  }
  luaC_barrierproto(L, p, ncl);
  p->cache = ncl;  /* save it on cache for reuse */
}


/*
** finish execution of an opcode interrupted by an yield
*/
void luaV_finishOp (lua_State *L) {
  CallInfo *ci = L->ci;
  StkId base = ci->u.l.base;
  Instruction inst = *(ci->u.l.savedpc - 1);  /* interrupted instruction */
  OpCode op = GET_OPCODE(inst);
  switch (op) {  /* finish its execution */
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_MOD: case OP_POW: case OP_IDIV: case OP_BAND:
    case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR:
    case OP_LSHR: case OP_ROTL: case OP_ROTR:
    case OP_UNM: case OP_BNOT: case OP_LEN:
    case OP_GETTABUP: case OP_GETTABLE: case OP_SELF: {
      setobjs2s(L, base + GETARG_A(inst), --L->top);
      break;
    }
    case OP_LE: case OP_LT: case OP_EQ: {
      int res = !l_isfalse(L->top - 1);
      L->top--;
      /* metamethod should not be called when operand is K */
      lua_assert(!ISK(GETARG_B(inst)));
      if (op == OP_LE &&  /* "<=" using "<" instead? */
          ttisnil(luaT_gettmbyobj(L, base + GETARG_B(inst), TM_LE)))
        res = !res;  /* invert result */
      lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_JMP);
      if (res != GETARG_A(inst))  /* condition failed? */
        ci->u.l.savedpc++;  /* skip jump instruction */
      break;
    }
    case OP_CONCAT: {
      StkId top = L->top - 1;  /* top when 'call_binTM' was called */
      int b = GETARG_B(inst);      /* first element to concatenate */
      int total = cast_int(top - 1 - (base + b));  /* yet to concatenate */
      setobj2s(L, top - 2, top);  /* put TM result in proper position */
      if (total > 1) {  /* are there elements to concat? */
        L->top = top - 1;  /* top is one after last element (at top-2) */
        luaV_concat(L, total);  /* concat them (may yield again) */
      }
      /* move final result to final position */
      setobj2s(L, ci->u.l.base + GETARG_A(inst), L->top - 1);
      L->top = ci->top;  /* restore top */
      break;
    }
    case OP_TFORCALL: {
      lua_assert(GET_OPCODE(*ci->u.l.savedpc) == OP_TFORLOOP);
      L->top = ci->top;  /* correct top */
      break;
    }
    case OP_CALL: {
      if (GETARG_C(inst) - 1 >= 0)  /* nresults >= 0? */
        L->top = ci->top;  /* adjust results */
      break;
    }
    case OP_TAILCALL: case OP_SETTABUP: case OP_SETTABLE:
      break;
    default: lua_assert(0);
  }
}



/*
** some macros for common tasks in `luaV_execute'
*/

#if !defined luai_runtimecheck
#define luai_runtimecheck(L, c)		/* void */
#endif


#define RA(i)	(v.base+GETARG_A(i))
/* to be used after possible stack reallocation */
#define RB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, v.base+GETARG_B(i))
#define RC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgR, v.base+GETARG_C(i))
#define RKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? v.k+INDEXK(GETARG_B(i)) : v.base+GETARG_B(i))
#define RKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? v.k+INDEXK(GETARG_C(i)) : v.base+GETARG_C(i))


/* execute a jump instruction */
#define dojump(i,e) \
  { int a = GETARG_A(i); \
    if (a > 0) luaF_close(L, v.ci->u.l.base + a - 1); \
    v.ci->u.l.savedpc += GETARG_sBx(i) + e; }

/* for test instructions, execute the jump instruction that follows it */
#define donextjump()	{ i = *v.ci->u.l.savedpc; dojump(i, 1) }


#define Protect(x)	{ {x;}; v.base = v.ci->u.l.base; }

#define checkGC(L,c)  \
  Protect( luaC_condGC(L,{L->top = (c);  /* limit of live values */ \
                          luaC_step(L); \
                          L->top = v.ci->top;})  /* restore top */ \
           luai_threadyield(L); )


#define arith_op(op,tm) { \
        TValue *rb = RKB(i); \
        TValue *rc = RKC(i); \
        if (ttisnumber(rb) && ttisnumber(rc)) { \
          lua_Number nb = nvalue(rb), nc = nvalue(rc); \
          setnvalue(ra, op(L, nb, nc)); \
        } \
        else { Protect(luaV_arith(L, ra, rb, rc, tm)); } }

#define unary_op(op,tm) {\
        TValue *rb = RB(i); \
        if (ttisnumber(rb)) { \
          lua_Number nb = nvalue(rb); \
          setnvalue(ra, op(L, nb)); \
        } \
        else { Protect(luaV_arith(L, ra, rb, rb, tm)); } }

#define vmdispatch(o)	switch(o)
#define vmcase(l,b)	case l: {b}  break;
#define vmcasenb(l,b)	case l: {b}		/* nb = no break */

/* M9 T6: the interpreter's shared frame state lives in one struct on
** luaV_execute's stack (bank 0 on the SNES -> direct-page access), and
** hot opcode bodies move into small per-op functions taking a VMCtx* --
** Calypsi's register allocator generates near--O0 code inside the
** original 21 KB switch body (measured: -O2 changed it by ~2%), but
** handles small functions well. The scoped #defines below keep the
** dispatch shell and the cold case bodies textually identical to
** upstream; ProtectV keeps v->base authoritative across anything that
** can move the stack. */
typedef struct VMCtx {
  lua_State *L;
  CallInfo *ci;
  LClosure *cl;
  TValue *k;
  StkId base;
} VMCtx;

/* ---- M9 T6: per-opcode bodies --------------------------------------
** Verbatim ports of the vmcase bodies (which now just call them): small
** functions Calypsi's register allocator can face. ProtectV keeps
** v->base authoritative across anything that can move the stack; ra is
** captured before ProtectV exactly where the original captured it
** before Protect. */

#define ProtectV(x)	{ {x;}; v->base = v->ci->u.l.base; }

#define vRA(i)	(v->base+GETARG_A(i))
#define vRB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgR, v->base+GETARG_B(i))
#define vRKB(i)	check_exp(getBMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_B(i)) ? v->k+INDEXK(GETARG_B(i)) : v->base+GETARG_B(i))
#define vRKC(i)	check_exp(getCMode(GET_OPCODE(i)) == OpArgK, \
	ISK(GETARG_C(i)) ? v->k+INDEXK(GETARG_C(i)) : v->base+GETARG_C(i))

#define DEF_ARITH(name, op, tm) \
static void name (VMCtx *v, Instruction i) { \
  lua_State *L = v->L; \
  TValue *rb = vRKB(i); \
  TValue *rc = vRKC(i); \
  if (ttisnumber(rb) && ttisnumber(rc)) { \
    lua_Number nb = nvalue(rb), nc = nvalue(rc); \
    setnvalue(vRA(i), op(L, nb, nc)); \
  } \
  else { StkId ra = vRA(i); ProtectV(luaV_arith(L, ra, rb, rc, tm)); } \
}

#define DEF_UNARY(name, op, tm) \
static void name (VMCtx *v, Instruction i) { \
  lua_State *L = v->L; \
  TValue *rb = vRB(i); \
  if (ttisnumber(rb)) { \
    lua_Number nb = nvalue(rb); \
    setnvalue(vRA(i), op(L, nb)); \
  } \
  else { StkId ra = vRA(i); ProtectV(luaV_arith(L, ra, rb, rb, tm)); } \
}

static void vmop_move (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  (void)L;  /* only the debug-build setobj checks use it */
  setobjs2s(L, vRA(i), vRB(i));
}
static void vmop_loadk (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  (void)L;  /* only the debug-build setobj checks use it */
  TValue *rb = v->k + GETARG_Bx(i);
  setobj2s(L, vRA(i), rb);
}
static void vmop_loadbool (VMCtx *v, Instruction i) {
  StkId ra = vRA(i);
  setbvalue(ra, GETARG_B(i));
  if (GETARG_C(i)) v->ci->u.l.savedpc++;  /* skip next instruction (if C) */
}
static void vmop_loadnil (VMCtx *v, Instruction i) {
  StkId ra = vRA(i);
  int b = GETARG_B(i);
  do {
    setnilvalue(ra++);
  } while (b--);
}
static void vmop_getupval (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  (void)L;  /* only the debug-build setobj checks use it */
  int b = GETARG_B(i);
  setobj2s(L, vRA(i), v->cl->upvals[b]->v);
}
static void vmop_gettabup (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  int b = GETARG_B(i);
  StkId ra = vRA(i);
  ProtectV(luaV_gettable(L, v->cl->upvals[b]->v, vRKC(i), ra));
}
static void vmop_gettable (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  StkId ra = vRA(i);
  ProtectV(luaV_gettable(L, vRB(i), vRKC(i), ra));
}
static void vmop_settabup (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  int a = GETARG_A(i);
  ProtectV(luaV_settable(L, v->cl->upvals[a]->v, vRKB(i), vRKC(i)));
}
static void vmop_setupval (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  StkId ra = vRA(i);
  UpVal *uv = v->cl->upvals[GETARG_B(i)];
  setobj(L, uv->v, ra);
  luaC_barrier(L, uv, ra);
}
static void vmop_settable (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  StkId ra = vRA(i);
  ProtectV(luaV_settable(L, ra, vRKB(i), vRKC(i)));
}

DEF_ARITH(vmop_add,  luai_numadd,  TM_ADD)
DEF_ARITH(vmop_sub,  luai_numsub,  TM_SUB)
DEF_ARITH(vmop_mul,  luai_nummul,  TM_MUL)
DEF_ARITH(vmop_div,  luai_numdiv,  TM_DIV)
DEF_ARITH(vmop_mod,  luai_nummod,  TM_MOD)
DEF_ARITH(vmop_pow,  luai_numpow,  TM_POW)
DEF_ARITH(vmop_idiv, luai_numidiv, TM_IDIV)
DEF_ARITH(vmop_band, luai_numband, TM_BAND)
DEF_ARITH(vmop_bor,  luai_numbor,  TM_BOR)
DEF_ARITH(vmop_bxor, luai_numbxor, TM_BXOR)
DEF_ARITH(vmop_shl,  luai_numshl,  TM_SHL)
DEF_ARITH(vmop_shr,  luai_numshr,  TM_SHR)
DEF_ARITH(vmop_lshr, luai_numlshr, TM_LSHR)
DEF_ARITH(vmop_rotl, luai_numrotl, TM_ROTL)
DEF_ARITH(vmop_rotr, luai_numrotr, TM_ROTR)
DEF_UNARY(vmop_unm,   luai_numunm,   TM_UNM)
DEF_UNARY(vmop_bnot,  luai_numbnot,  TM_BNOT)
DEF_UNARY(vmop_peek,  luai_numpeek,  TM_PEEK)
DEF_UNARY(vmop_peek2, luai_numpeek2, TM_PEEK2)
DEF_UNARY(vmop_peek4, luai_numpeek4, TM_PEEK4)

#define dojumpV(i,e) \
  { int a = GETARG_A(i); \
    if (a > 0) luaF_close(L, v->ci->u.l.base + a - 1); \
    v->ci->u.l.savedpc += GETARG_sBx(i) + e; }
#define donextjumpV()	{ i = *v->ci->u.l.savedpc; dojumpV(i, 1) }

/* M9 T7: EQ/LT/LE carry an in-body number-number fast path -- ~20% of
** gameplay ops paid a far call into luaV_* just to discover both
** operands were numbers and compare 32 bits. Number comparisons cannot
** invoke metamethods or move the stack, so the fast path needs no
** ProtectV; the slow path is byte-identical to the pre-T7 body. */
static void vmop_eq (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  TValue *rb = vRKB(i);
  TValue *rc = vRKC(i);
  if (ttisnumber(rb) && ttisnumber(rc)) {
    if ((nvalue(rb) == nvalue(rc)) != GETARG_A(i))
      v->ci->u.l.savedpc++;
    else
      donextjumpV();
    return;
  }
  ProtectV(
    if (cast_int(equalobj(L, rb, rc)) != GETARG_A(i))
      v->ci->u.l.savedpc++;
    else
      donextjumpV();
  )
}
static void vmop_lt (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  TValue *rb = vRKB(i);
  TValue *rc = vRKC(i);
  if (ttisnumber(rb) && ttisnumber(rc)) {
    if ((nvalue(rb) < nvalue(rc)) != GETARG_A(i))
      v->ci->u.l.savedpc++;
    else
      donextjumpV();
    return;
  }
  ProtectV(
    if (luaV_lessthan(L, rb, rc) != GETARG_A(i))
      v->ci->u.l.savedpc++;
    else
      donextjumpV();
  )
}
static void vmop_le (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  TValue *rb = vRKB(i);
  TValue *rc = vRKC(i);
  if (ttisnumber(rb) && ttisnumber(rc)) {
    if ((nvalue(rb) <= nvalue(rc)) != GETARG_A(i))
      v->ci->u.l.savedpc++;
    else
      donextjumpV();
    return;
  }
  ProtectV(
    if (luaV_lessequal(L, rb, rc) != GETARG_A(i))
      v->ci->u.l.savedpc++;
    else
      donextjumpV();
  )
}
static void vmop_test (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  StkId ra = vRA(i);
  if (GETARG_C(i) ? l_isfalse(ra) : !l_isfalse(ra))
    v->ci->u.l.savedpc++;
  else
    donextjumpV();
}
static void vmop_testset (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  StkId ra = vRA(i);
  TValue *rb = vRB(i);
  if (GETARG_C(i) ? l_isfalse(rb) : !l_isfalse(rb))
    v->ci->u.l.savedpc++;
  else {
    setobjs2s(L, ra, rb);
    donextjumpV();
  }
}
static void vmop_forprep (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  StkId ra = vRA(i);
  const TValue *init = ra;
  const TValue *plimit = ra+1;
  const TValue *pstep = ra+2;
  if (!tonumber(init, ra))
    luaG_runerror(L, LUA_QL("for") " initial value must be a number");
  else if (!tonumber(plimit, ra+1))
    luaG_runerror(L, LUA_QL("for") " limit must be a number");
  else if (!tonumber(pstep, ra+2))
    luaG_runerror(L, LUA_QL("for") " step must be a number");
  setnvalue(ra, luai_numsub(L, nvalue(ra), nvalue(pstep)));
  v->ci->u.l.savedpc += GETARG_sBx(i);
}
static void vmop_tforcall (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  StkId ra = vRA(i);
  StkId cb = ra + 3;  /* call base */
  setobjs2s(L, cb+2, ra+2);
  setobjs2s(L, cb+1, ra+1);
  setobjs2s(L, cb, ra);
  L->top = cb + 3;  /* func. + 2 args (state and index) */
  ProtectV(luaD_call(L, cb, GETARG_C(i), 1));
  L->top = v->ci->top;
}
static void vmop_tforloop (VMCtx *v, Instruction i) {
  lua_State *L = v->L;
  (void)L;  /* only the debug-build setobj checks use it */
  StkId ra = vRA(i);
  if (!ttisnil(ra + 1)) {  /* continue loop? */
    setobjs2s(L, ra, ra + 1);  /* save control variable */
    v->ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
  }
}

void luaV_execute (lua_State *L) {

  VMCtx v;
  v.L = L;
  v.ci = L->ci;
 newframe:  /* reentry point when frame changes (call/return) */
  lua_assert(v.ci == L->ci);
  v.cl = clLvalue(v.ci->func);
  v.k = v.cl->p->k;
  v.base = v.ci->u.l.base;
  /* main loop of interpreter */
  for (;;) {
    Instruction i = *(v.ci->u.l.savedpc++);
#ifndef P8_NO_VMOPS
    p8_vmops++;  /* snespico8: M1.5 instruction counter (M9: release
                    builds may pass -DP8_NO_VMOPS to drop it) */
#endif
#ifdef P8_OPSTATS
    p8_opstats[GET_OPCODE(i)]++;  /* host-only: T6 split-set evidence */
#endif
    StkId ra;
#ifndef P8_NO_HOOKS
    /* SNES builds compile the hook check out (P8_NO_HOOKS): PICO-8
    ** exposes no sethook path, and the test costs a far read + branch
    ** on every dispatched op. debug.sethook on such a build sets state
    ** the VM never reads (pinned in docs/COMPATIBILITY.md). */
    if ((L->hookmask & (LUA_MASKLINE | LUA_MASKCOUNT)) &&
        (--L->hookcount == 0 || L->hookmask & LUA_MASKLINE)) {
      Protect(traceexec(L));
    }
#endif
    /* M9 T7: `ra' is computed inside the cases that use it -- the hot
    ** per-op functions (T6) recompute vRA themselves, and the shell
    ** paying a looped >>6 plus a x6 for every op was pure waste.
    ** WARNING: several calls may realloc the stack and invalidate `ra' */
    lua_assert(v.base == v.ci->u.l.base);
    lua_assert(v.base <= L->top && L->top < L->stack + L->stacksize);
    vmdispatch (GET_OPCODE(i)) {
      vmcase(OP_MOVE,
        vmop_move(&v, i);
      )
      vmcase(OP_LOADK,
        vmop_loadk(&v, i);
      )
      vmcase(OP_LOADKX,
        TValue *rb;
        Instruction ix;  /* P8_BYTEDECODE: GETARG_* args must not side-effect */
        ra = RA(i);
        lua_assert(GET_OPCODE(*v.ci->u.l.savedpc) == OP_EXTRAARG);
        ix = *v.ci->u.l.savedpc++;
        rb = v.k + GETARG_Ax(ix);
        setobj2s(L, ra, rb);
      )
      vmcase(OP_LOADBOOL,
        vmop_loadbool(&v, i);
      )
      vmcase(OP_LOADNIL,
        vmop_loadnil(&v, i);
      )
      vmcase(OP_GETUPVAL,
        vmop_getupval(&v, i);
      )
      vmcase(OP_GETTABUP,
        vmop_gettabup(&v, i);
      )
      vmcase(OP_GETTABLE,
        vmop_gettable(&v, i);
      )
      vmcase(OP_SETTABUP,
        vmop_settabup(&v, i);
      )
      vmcase(OP_SETUPVAL,
        vmop_setupval(&v, i);
      )
      vmcase(OP_SETTABLE,
        vmop_settable(&v, i);
      )
      vmcase(OP_NEWTABLE,
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        Table *t = luaH_new(L);
        ra = RA(i);
        sethvalue(L, ra, t);
        if (b != 0 || c != 0)
          luaH_resize(L, t, luaO_fb2int(b), luaO_fb2int(c));
        checkGC(L, ra + 1);
      )
      vmcase(OP_SELF,
        StkId rb = RB(i);
        ra = RA(i);
        setobjs2s(L, ra+1, rb);
        Protect(luaV_gettable(L, rb, RKC(i), ra));
      )
      vmcase(OP_ADD,
        vmop_add(&v, i);
      )
      vmcase(OP_SUB,
        vmop_sub(&v, i);
      )
      vmcase(OP_MUL,
        vmop_mul(&v, i);
      )
      vmcase(OP_DIV,
        vmop_div(&v, i);
      )
      vmcase(OP_MOD,
        vmop_mod(&v, i);
      )
      vmcase(OP_POW,
        vmop_pow(&v, i);
      )
      vmcase(OP_IDIV,
        vmop_idiv(&v, i);
      )
      vmcase(OP_BAND,
        vmop_band(&v, i);
      )
      vmcase(OP_BOR,
        vmop_bor(&v, i);
      )
      vmcase(OP_BXOR,
        vmop_bxor(&v, i);
      )
      vmcase(OP_SHL,
        vmop_shl(&v, i);
      )
      vmcase(OP_SHR,
        vmop_shr(&v, i);
      )
      vmcase(OP_LSHR,
        vmop_lshr(&v, i);
      )
      vmcase(OP_ROTL,
        vmop_rotl(&v, i);
      )
      vmcase(OP_ROTR,
        vmop_rotr(&v, i);
      )
      vmcase(OP_UNM,
        vmop_unm(&v, i);
      )
      vmcase(OP_BNOT,
        vmop_bnot(&v, i);
      )
      vmcase(OP_PEEK,
        vmop_peek(&v, i);
      )
      vmcase(OP_PEEK2,
        vmop_peek2(&v, i);
      )
      vmcase(OP_PEEK4,
        vmop_peek4(&v, i);
      )
      vmcase(OP_NOT,
        TValue *rb = RB(i);
        int res = l_isfalse(rb);  /* next assignment may change this value */
        ra = RA(i);
        setbvalue(ra, res);
      )
      vmcase(OP_LEN,
        ra = RA(i);
        Protect(luaV_objlen(L, ra, RB(i)));
      )
      vmcase(OP_CONCAT,
        int b = GETARG_B(i);
        int c = GETARG_C(i);
        StkId rb;
        L->top = v.base + c + 1;  /* mark the end of concat operands */
        Protect(luaV_concat(L, c - b + 1));
        ra = RA(i);  /* 'luav_concat' may invoke TMs and move the stack */
        rb = b + v.base;
        setobjs2s(L, ra, rb);
        checkGC(L, (ra >= rb ? ra + 1 : rb));
        L->top = v.ci->top;  /* restore top */
      )
      vmcase(OP_JMP,
        dojump(i, 0);
      )
      vmcase(OP_EQ,
        vmop_eq(&v, i);
      )
      vmcase(OP_LT,
        vmop_lt(&v, i);
      )
      vmcase(OP_LE,
        vmop_le(&v, i);
      )
      vmcase(OP_TEST,
        vmop_test(&v, i);
      )
      vmcase(OP_TESTSET,
        vmop_testset(&v, i);
      )
      vmcase(OP_CALL,
        int b = GETARG_B(i);
        int nresults = GETARG_C(i) - 1;
        ra = RA(i);
        if (b != 0) L->top = ra+b;  /* else previous instruction set top */
        if (luaD_precall(L, ra, nresults)) {  /* C function? */
          if (nresults >= 0) L->top = v.ci->top;  /* adjust results */
          v.base = v.ci->u.l.base;
        }
        else {  /* Lua function */
          v.ci = L->ci;
          v.ci->callstatus |= CIST_REENTRY;
          goto newframe;  /* restart luaV_execute over new Lua function */
        }
      )
      vmcase(OP_TAILCALL,
        int b = GETARG_B(i);
        ra = RA(i);
        if (b != 0) L->top = ra+b;  /* else previous instruction set top */
        lua_assert(GETARG_C(i) - 1 == LUA_MULTRET);
        if (luaD_precall(L, ra, LUA_MULTRET))  /* C function? */
          v.base = v.ci->u.l.base;
        else {
          /* tail call: put called frame (n) in place of caller one (o) */
          CallInfo *nci = L->ci;  /* called frame */
          CallInfo *oci = nci->previous;  /* caller frame */
          StkId nfunc = nci->func;  /* called function */
          StkId ofunc = oci->func;  /* caller function */
          /* last stack slot filled by 'precall' */
          StkId lim = nci->u.l.base + getproto(nfunc)->numparams;
          int aux;
          /* close all upvalues from previous call */
          if (v.cl->p->sizep > 0) luaF_close(L, oci->u.l.base);
          /* move new frame into old one */
          for (aux = 0; nfunc + aux < lim; aux++)
            setobjs2s(L, ofunc + aux, nfunc + aux);
          oci->u.l.base = ofunc + (nci->u.l.base - nfunc);  /* correct v.base */
          oci->top = L->top = ofunc + (L->top - nfunc);  /* correct top */
          oci->u.l.savedpc = nci->u.l.savedpc;
          oci->callstatus |= CIST_TAIL;  /* function was tail called */
          v.ci = L->ci = oci;  /* remove new frame */
          lua_assert(L->top == oci->u.l.base + getproto(ofunc)->maxstacksize);
          goto newframe;  /* restart luaV_execute over new Lua function */
        }
      )
      vmcasenb(OP_RETURN,
        int b = GETARG_B(i);
        ra = RA(i);
        if (b != 0) L->top = ra+b-1;
        if (v.cl->p->sizep > 0) luaF_close(L, v.base);
        b = luaD_poscall(L, ra);
        if (!(v.ci->callstatus & CIST_REENTRY))  /* 'v.ci' still the called one */
          return;  /* external invocation: return */
        else {  /* invocation via reentry: continue execution */
          v.ci = L->ci;
          if (b) L->top = v.ci->top;
          lua_assert(isLua(v.ci));
          lua_assert(GET_OPCODE(*((v.ci)->u.l.savedpc - 1)) == OP_CALL);
          goto newframe;  /* restart luaV_execute over new Lua function */
        }
      )
      vmcase(OP_FORLOOP,
        lua_Number step;
        ra = RA(i);
        step = nvalue(ra+2);
        lua_Number idx = luai_numadd(L, (lua_Number)nvalue(ra), step); /* increment index */
        lua_Number limit = nvalue(ra+1);
        /* check for idx sign wrap-around */
        if (luai_numlt(L, 0, step) ? luai_numle(L, nvalue(ra), idx)
                                   : luai_numle(L, idx, nvalue(ra)))
        if (luai_numlt(L, 0, step) ? luai_numle(L, idx, limit)
                                   : luai_numle(L, limit, idx)) {
          v.ci->u.l.savedpc += GETARG_sBx(i);  /* jump back */
          setnvalue(ra, idx);  /* update internal index... */
          setnvalue(ra+3, idx);  /* ...and external index */
        }
      )
      vmcase(OP_FORPREP,
        vmop_forprep(&v, i);
      )
      vmcasenb(OP_TFORCALL,
        vmop_tforcall(&v, i);
        i = *(v.ci->u.l.savedpc++);  /* go to next instruction */
        ra = RA(i);
        lua_assert(GET_OPCODE(i) == OP_TFORLOOP);
        goto l_tforloop;
      )
      vmcase(OP_TFORLOOP,
        l_tforloop:
        vmop_tforloop(&v, i);
      )
      vmcase(OP_SETLIST,
        int n = GETARG_B(i);
        int c = GETARG_C(i);
        int last;
        Table *h;
        ra = RA(i);
        if (n == 0) n = cast_int(L->top - ra) - 1;
        if (c == 0) {
          Instruction ix;  /* P8_BYTEDECODE: no side effects in GETARG args */
          lua_assert(GET_OPCODE(*v.ci->u.l.savedpc) == OP_EXTRAARG);
          ix = *v.ci->u.l.savedpc++;
          c = GETARG_Ax(ix);
        }
        luai_runtimecheck(L, ttistable(ra));
        h = hvalue(ra);
        last = ((c-1)*LFIELDS_PER_FLUSH) + n;
        if (last > h->sizearray)  /* needs more space? */
          luaH_resizearray(L, h, last);  /* pre-allocate it at once */
        for (; n > 0; n--) {
          TValue *val = ra+n;
          luaH_setint(L, h, last--, val);
          luaC_barrierback(L, obj2gco(h), val);
        }
        L->top = v.ci->top;  /* correct top (in case of previous open call) */
      )
      vmcase(OP_CLOSURE,
        Proto *p = v.cl->p->p[GETARG_Bx(i)];
        Closure *ncl = getcached(p, v.cl->upvals, v.base);  /* cached closure */
        ra = RA(i);
        if (ncl == NULL)  /* no match? */
          pushclosure(L, p, v.cl->upvals, v.base, ra);  /* create a new one */
        else
          setclLvalue(L, ra, ncl);  /* push cashed closure */
        checkGC(L, ra + 1);
      )
      vmcase(OP_VARARG,
        int b = GETARG_B(i) - 1;
        int j;
        int n = cast_int(v.base - v.ci->func) - v.cl->p->numparams - 1;
        ra = RA(i);
        if (b < 0) {  /* B == 0? */
          b = n;  /* get all var. arguments */
          Protect(luaD_checkstack(L, n));
          ra = RA(i);  /* previous call may change the stack */
          L->top = ra + n;
        }
        for (j = 0; j < b; j++) {
          if (j < n) {
            setobjs2s(L, ra + j, v.base - n + j);
          }
          else {
            setnilvalue(ra + j);
          }
        }
      )
      vmcase(OP_EXTRAARG,
        lua_assert(0);
      )
    }
  }
}

