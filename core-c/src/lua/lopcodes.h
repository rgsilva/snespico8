/*
** $Id: lopcodes.h,v 1.142.1.1 2013/04/12 18:48:47 roberto Exp roberto $
** Opcodes for Lua virtual machine
** See Copyright Notice in lua.h
*/

#ifndef lopcodes_h
#define lopcodes_h

#include "llimits.h"


/*===========================================================================
  We assume that instructions are unsigned numbers.
  All instructions have an opcode in the first 6 bits.
  Instructions can have the following fields:
	`A' : 8 bits
	`B' : 9 bits
	`C' : 9 bits
	'Ax' : 26 bits ('A', 'B', and 'C' together)
	`Bx' : 18 bits (`B' and `C' together)
	`sBx' : signed Bx

  A signed argument is represented in excess K; that is, the number
  value is the unsigned value minus K. K is exactly the maximum value
  for that argument (so that -max is represented by 0, and +max is
  represented by 2*max), which is half the maximum for the corresponding
  unsigned argument.
===========================================================================*/


enum OpMode {iABC, iABx, iAsBx, iAx};  /* basic instruction format */


/*
** size and position of opcode arguments.
*/
#define SIZE_C		9
#define SIZE_B		9
#define SIZE_Bx		(SIZE_C + SIZE_B)
#define SIZE_A		8
#define SIZE_Ax		(SIZE_C + SIZE_B + SIZE_A)

#define SIZE_OP		6

#define POS_OP		0
#define POS_A		(POS_OP + SIZE_OP)
#define POS_C		(POS_A + SIZE_A)
#define POS_B		(POS_C + SIZE_C)
#define POS_Bx		POS_C
#define POS_Ax		POS_A


/*
** limits for opcode arguments.
** we use (signed) int to manipulate most arguments,
** so they must fit in LUAI_BITSINT-1 bits (-1 for sign)
*/
#if SIZE_Bx < LUAI_BITSINT-1
#define MAXARG_Bx        ((1<<SIZE_Bx)-1)
#define MAXARG_sBx        (MAXARG_Bx>>1)         /* `sBx' is signed */
#else
#define MAXARG_Bx        MAX_INT
#define MAXARG_sBx        MAX_INT
#endif

#if SIZE_Ax < LUAI_BITSINT-1
#define MAXARG_Ax	((1<<SIZE_Ax)-1)
#else
#define MAXARG_Ax	MAX_INT
#endif


#define MAXARG_A        ((1<<SIZE_A)-1)
#define MAXARG_B        ((1<<SIZE_B)-1)
#define MAXARG_C        ((1<<SIZE_C)-1)

#if defined(P8_XDUMP)
/* M9 T7 step 5 (cross-dumper): emit code shaped for the 16-bit-int
** target -- its MAX_INT (INT_MAX-2 = 32765) caps Bx/sBx/Ax and biases
** sBx, so jump encodings and LOADKX thresholds match what the
** console's own lcode would emit. Without this, host 18-bit jump
** biases decode as garbage through the target's 16-bit truncation. */
#undef MAXARG_Bx
#undef MAXARG_sBx
#undef MAXARG_Ax
#define MAXARG_Bx	32765
#define MAXARG_sBx	32765
#define MAXARG_Ax	32765
#endif


/* creates a mask with `n' 1 bits at position `p' */
#define MASK1(n,p)	((~((~(Instruction)0)<<(n)))<<(p))

/* creates a mask with `n' 0 bits at position `p' */
#define MASK0(n,p)	(~MASK1(n,p))

/*
** the following macros help to manipulate instructions
*/

#if defined(P8_BYTEDECODE)
/* M9 T7: Calypsi lowers 32-bit constant shifts as counted lsr/ror
** loops (a >>14 field extract costs ~150 cycles), so the SNES build
** decodes instruction fields byte-wise. Little-endian bytes b0..b3:
** OP = b0[0:5], A = b0[6:7]+b1[0:5], C = b1[6:7]+b2[0:6],
** B = b2[7]+b3, Bx = C..B, Ax = A..B. The Bx/Ax forms keep only the
** low 16 result bits -- exactly what the portable cast(int,...) keeps
** on this 16-bit-int target (lcode caps emitted values at MAX_INT, so
** the dropped bits are always zero in real code). Get-forms only; the
** SET/CREATE forms below stay portable (parse-time cost). The host
** build keeps the portable shifts, and parity pins the two decoders
** against each other on every vector.
** p8i_b evaluates `i' more than once: the argument must be a simple
** lvalue with no side effects (no *pc++ -- hoist into a local). */
#define p8i_b(i,n)	((unsigned int)((const unsigned char *)&(i))[n])
#define GET_OPCODE(i)	(cast(OpCode, p8i_b(i,0) & 0x3Fu))
#define GETARG_A(i)	(cast(int, (p8i_b(i,0) >> 6) | ((p8i_b(i,1) & 0x3Fu) << 2)))
#define GETARG_C(i)	(cast(int, (p8i_b(i,1) >> 6) | ((p8i_b(i,2) & 0x7Fu) << 2)))
#define GETARG_B(i)	(cast(int, (p8i_b(i,2) >> 7) | (p8i_b(i,3) << 1)))
#if LUAI_BITSINT >= 32
/* host equivalence build: int holds the full 18/26-bit fields, and
** lcode emits them full-width (jumps biased by the 18-bit MAXARG_sBx),
** so decode full-width -- the masked forms below would break every
** jump on a 32-bit-int build. */
#define GETARG_Bx(i)	(cast(int, (p8i_b(i,1) >> 6) | (p8i_b(i,2) << 2) | \
	(p8i_b(i,3) << 10)))
#define GETARG_Ax(i)	(cast(int, (p8i_b(i,0) >> 6) | (p8i_b(i,1) << 2) | \
	(p8i_b(i,2) << 10) | (p8i_b(i,3) << 18)))
#else
/* 16-bit-int target: keep only the low 16 result bits, exactly what
** the portable cast(int,...) keeps here. GETARG_sBx then recovers
** jump offsets by 16-bit two's-complement wraparound of the
** Bx - MAXARG_sBx subtraction, same as the portable form does. */
#define GETARG_Bx(i)	(cast(int, (p8i_b(i,1) >> 6) | (p8i_b(i,2) << 2) | \
	((p8i_b(i,3) & 0x3Fu) << 10)))
#define GETARG_Ax(i)	(cast(int, (p8i_b(i,0) >> 6) | (p8i_b(i,1) << 2) | \
	((p8i_b(i,2) & 0x3Fu) << 10)))
#endif
#else
#define GET_OPCODE(i)	(cast(OpCode, ((i)>>POS_OP) & MASK1(SIZE_OP,0)))

#define getarg(i,pos,size)	(cast(int, ((i)>>pos) & MASK1(size,0)))

#define GETARG_A(i)	getarg(i, POS_A, SIZE_A)
#define GETARG_B(i)	getarg(i, POS_B, SIZE_B)
#define GETARG_C(i)	getarg(i, POS_C, SIZE_C)
#define GETARG_Bx(i)	getarg(i, POS_Bx, SIZE_Bx)
#define GETARG_Ax(i)	getarg(i, POS_Ax, SIZE_Ax)
#endif

#define SET_OPCODE(i,o)	((i) = (((i)&MASK0(SIZE_OP,POS_OP)) | \
		((cast(Instruction, o)<<POS_OP)&MASK1(SIZE_OP,POS_OP))))

#define setarg(i,v,pos,size)	((i) = (((i)&MASK0(size,pos)) | \
                ((cast(Instruction, v)<<pos)&MASK1(size,pos))))

#define SETARG_A(i,v)	setarg(i, v, POS_A, SIZE_A)
#define SETARG_B(i,v)	setarg(i, v, POS_B, SIZE_B)
#define SETARG_C(i,v)	setarg(i, v, POS_C, SIZE_C)
#define SETARG_Bx(i,v)	setarg(i, v, POS_Bx, SIZE_Bx)
#define SETARG_Ax(i,v)	setarg(i, v, POS_Ax, SIZE_Ax)

#define GETARG_sBx(i)	(GETARG_Bx(i)-MAXARG_sBx)
#define SETARG_sBx(i,b)	SETARG_Bx((i),cast(unsigned int, (b)+MAXARG_sBx))


#define CREATE_ABC(o,a,b,c)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, b)<<POS_B) \
			| (cast(Instruction, c)<<POS_C))

#define CREATE_ABx(o,a,bc)	((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_A) \
			| (cast(Instruction, bc)<<POS_Bx))

#define CREATE_Ax(o,a)		((cast(Instruction, o)<<POS_OP) \
			| (cast(Instruction, a)<<POS_Ax))


/*
** Macros to operate RK indices
*/

/* this bit 1 means constant (0 means register) */
#define BITRK		(1 << (SIZE_B - 1))

/* test whether value is a constant */
#define ISK(x)		((x) & BITRK)

/* gets the index of the constant */
#define INDEXK(r)	((int)(r) & ~BITRK)

#define MAXINDEXRK	(BITRK - 1)

/* code a constant index as a RK value */
#define RKASK(x)	((x) | BITRK)


/*
** invalid register that fits in 8 bits
*/
#define NO_REG		MAXARG_A


/*
** R(x) - register
** Kst(x) - constant (in constant table)
** RK(x) == if ISK(x) then Kst(INDEXK(x)) else R(x)
*/


/*
** grep "ORDER OP" if you change these enums
*/

typedef enum {
/*----------------------------------------------------------------------
name		args	description
------------------------------------------------------------------------*/
OP_MOVE,/*	A B	R(A) := R(B)					*/
OP_LOADK,/*	A Bx	R(A) := Kst(Bx)					*/
OP_LOADKX,/*	A 	R(A) := Kst(extra arg)				*/
OP_LOADBOOL,/*	A B C	R(A) := (Bool)B; if (C) pc++			*/
OP_LOADNIL,/*	A B	R(A), R(A+1), ..., R(A+B) := nil		*/
OP_GETUPVAL,/*	A B	R(A) := UpValue[B]				*/

OP_GETTABUP,/*	A B C	R(A) := UpValue[B][RK(C)]			*/
OP_GETTABLE,/*	A B C	R(A) := R(B)[RK(C)]				*/

OP_SETTABUP,/*	A B C	UpValue[A][RK(B)] := RK(C)			*/
OP_SETUPVAL,/*	A B	UpValue[B] := R(A)				*/
OP_SETTABLE,/*	A B C	R(A)[RK(B)] := RK(C)				*/

OP_NEWTABLE,/*	A B C	R(A) := {} (size = B,C)				*/

OP_SELF,/*	A B C	R(A+1) := R(B); R(A) := R(B)[RK(C)]		*/

OP_ADD,/*	A B C	R(A) := RK(B) + RK(C)				*/
OP_SUB,/*	A B C	R(A) := RK(B) - RK(C)				*/
OP_MUL,/*	A B C	R(A) := RK(B) * RK(C)				*/
OP_DIV,/*	A B C	R(A) := RK(B) / RK(C)				*/
OP_MOD,/*	A B C	R(A) := RK(B) % RK(C)				*/
OP_POW,/*	A B C	R(A) := RK(B) ^ RK(C)				*/
OP_IDIV,/*	A B C	R(A) := RK(B) \ RK(C)				*/
OP_BAND,/*	A B C	R(A) := RK(B) & RK(C)				*/
OP_BOR,/*	A B C	R(A) := RK(B) | RK(C)				*/
OP_BXOR,/*	A B C	R(A) := RK(B) ^^ RK(C)				*/
OP_SHL,/*	A B C	R(A) := RK(B) << RK(C)				*/
OP_SHR,/*	A B C	R(A) := RK(B) >> RK(C)				*/
OP_LSHR,/*	A B C	R(A) := RK(B) >>> RK(C)				*/
OP_ROTL,/*	A B C	R(A) := RK(B) <<> RK(C)				*/
OP_ROTR,/*	A B C	R(A) := RK(B) >>< RK(C)				*/

OP_UNM,/*	A B	R(A) := -R(B)					*/
OP_BNOT,/*	A B	R(A) := ~R(B)					*/
OP_NOT,/*	A B	R(A) := not R(B)				*/
OP_PEEK,/*	A B	R(A) := @ R(B)					*/
OP_PEEK2,/*	A B	R(A) := % R(B)					*/
OP_PEEK4,/*	A B	R(A) := $ R(B)					*/
OP_LEN,/*	A B	R(A) := length of R(B)				*/

OP_CONCAT,/*	A B C	R(A) := R(B).. ... ..R(C)			*/

OP_JMP,/*	A sBx	pc+=sBx; if (A) close all upvalues >= R(A - 1)	*/
OP_EQ,/*	A B C	if ((RK(B) == RK(C)) ~= A) then pc++		*/
OP_LT,/*	A B C	if ((RK(B) <  RK(C)) ~= A) then pc++		*/
OP_LE,/*	A B C	if ((RK(B) <= RK(C)) ~= A) then pc++		*/

OP_TEST,/*	A C	if not (R(A) <=> C) then pc++			*/
OP_TESTSET,/*	A B C	if (R(B) <=> C) then R(A) := R(B) else pc++	*/

OP_CALL,/*	A B C	R(A), ... ,R(A+C-2) := R(A)(R(A+1), ... ,R(A+B-1)) */
OP_TAILCALL,/*	A B C	return R(A)(R(A+1), ... ,R(A+B-1))		*/
OP_RETURN,/*	A B	return R(A), ... ,R(A+B-2)	(see note)	*/

OP_FORLOOP,/*	A sBx	R(A)+=R(A+2);
			if R(A) <?= R(A+1) then { pc+=sBx; R(A+3)=R(A) }*/
OP_FORPREP,/*	A sBx	R(A)-=R(A+2); pc+=sBx				*/

OP_TFORCALL,/*	A C	R(A+3), ... ,R(A+2+C) := R(A)(R(A+1), R(A+2));	*/
OP_TFORLOOP,/*	A sBx	if R(A+1) ~= nil then { R(A)=R(A+1); pc += sBx }*/

OP_SETLIST,/*	A B C	R(A)[(C-1)*FPF+i] := R(A+i), 1 <= i <= B	*/

OP_CLOSURE,/*	A Bx	R(A) := closure(KPROTO[Bx])			*/

OP_VARARG,/*	A B	R(A), R(A+1), ..., R(A+B-2) = vararg		*/

OP_EXTRAARG/*	Ax	extra (larger) argument for previous opcode	*/
} OpCode;


#define NUM_OPCODES	(cast(int, OP_EXTRAARG) + 1)



/*===========================================================================
  Notes:
  (*) In OP_CALL, if (B == 0) then B = top. If (C == 0), then `top' is
  set to last_result+1, so next open instruction (OP_CALL, OP_RETURN,
  OP_SETLIST) may use `top'.

  (*) In OP_VARARG, if (B == 0) then use actual number of varargs and
  set top (like in OP_CALL with C == 0).

  (*) In OP_RETURN, if (B == 0) then return up to `top'.

  (*) In OP_SETLIST, if (B == 0) then B = `top'; if (C == 0) then next
  'instruction' is EXTRAARG(real C).

  (*) In OP_LOADKX, the next 'instruction' is always EXTRAARG.

  (*) For comparisons, A specifies what condition the test should accept
  (true or false).

  (*) All `skips' (pc++) assume that next instruction is a jump.

===========================================================================*/


/*
** masks for instruction properties. The format is:
** bits 0-1: op mode
** bits 2-3: C arg mode
** bits 4-5: B arg mode
** bit 6: instruction set register A
** bit 7: operator is a test (next instruction must be a jump)
*/

enum OpArgMask {
  OpArgN,  /* argument is not used */
  OpArgU,  /* argument is used */
  OpArgR,  /* argument is a register or a jump offset */
  OpArgK   /* argument is a constant or register/constant */
};

LUAI_DDEC const lu_byte luaP_opmodes[NUM_OPCODES];

#define getOpMode(m)	(cast(enum OpMode, luaP_opmodes[m] & 3))
#define getBMode(m)	(cast(enum OpArgMask, (luaP_opmodes[m] >> 4) & 3))
#define getCMode(m)	(cast(enum OpArgMask, (luaP_opmodes[m] >> 2) & 3))
#define testAMode(m)	(luaP_opmodes[m] & (1 << 6))
#define testTMode(m)	(luaP_opmodes[m] & (1 << 7))


LUAI_DDEC const char *const luaP_opnames[NUM_OPCODES+1];  /* opcode names */


/* number of list items to accumulate before a SETLIST instruction */
#define LFIELDS_PER_FLUSH	50


#endif
