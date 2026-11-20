#ifndef CC_H
#define CC_H

#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* basic utilities                                                     */
/* ------------------------------------------------------------------ */

struct buf {
	unsigned char *data;
	int len;
	int cap;
};

void fatal(const char *fmt, ...)
	__attribute__((noreturn, format(printf, 1, 2)));
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrndup(const char *s, size_t n);
char *xstrdup(const char *s);
int align_to(int n, int a);

void buf_init(struct buf *b);
void buf_grow(struct buf *b, int need);
void buf_byte(struct buf *b, int c);
void buf_u32(struct buf *b, unsigned int v);
void buf_u64(struct buf *b, unsigned long v);
void buf_pad(struct buf *b, int n);
int buf_align(struct buf *b, int a);

/* ------------------------------------------------------------------ */
/* lexer                                                               */
/* ------------------------------------------------------------------ */

enum {
	TK_EOF = 0,
	TK_IDENT,
	TK_NUM,
	TK_STR,
	TK_PUNCT,
};

struct token {
	int kind;
	char *text;
	long val;
	unsigned char *str;
	int str_len;
	int line;
	int col;
};

extern struct token *tokens;
extern int ntokens;

void lex_all(const char *src, int len);
bool starts_ident(int c);
bool is_ident_char(int c);
void reset_lexer_state(void);

/* ------------------------------------------------------------------ */
/* preprocessor                                                        */
/* ------------------------------------------------------------------ */

void pp_add_inc_path(const char *path);
void record_cli_macro(bool undef, const char *arg);
void apply_cli_macros(void);
int try_read_file(const char *path, char **out, int *out_len);
char *preprocess(const char *path, const char *src, int len, int *out_len);
void reset_pp_state(void);

/* ------------------------------------------------------------------ */
/* types                                                               */
/* ------------------------------------------------------------------ */

enum {
	TY_VOID = 0,
	TY_CHAR,
	TY_SHORT,
	TY_INT,
	TY_LONG,
	TY_PTR,
	TY_ARRAY,
	TY_FUNC,
	TY_STRUCT,
	TY_UNION,
	TY_PLACEHOLDER,
};

struct obj;

struct param {
	char *name;
	struct type *ty;
	struct obj *obj; /* set only while generating a definition */
};

struct member {
	char *name;
	struct type *ty;
	int offset;
};

struct type {
	int kind;
	int size;
	int align;
	bool is_unsigned;
	bool is_complete;
	struct type *base;
	int array_len;
	struct param *params;
	int nparams;
	bool variadic;
	struct member *members;
	int nmembers;
	char *tag;
};

extern struct type *ty_void;
extern struct type *ty_char;
extern struct type *ty_schar;
extern struct type *ty_short;
extern struct type *ty_ushort;
extern struct type *ty_int;
extern struct type *ty_uint;
extern struct type *ty_long;
extern struct type *ty_ulong;

struct type *new_type(int kind, int size, int align, bool uns);
struct type *ty_ptr(struct type *base);
struct type *ty_array(struct type *base, int len);
struct type *ty_func(struct type *ret, struct param *params, int n,
		     bool variadic);
struct type *new_aggregate_type(int kind, const char *tag);
void init_types(void);
bool is_integer(struct type *t);
bool is_ptr_like(struct type *t);
bool is_aggregate(struct type *t);
bool is_scalar(struct type *t);
struct type *decay(struct type *t);

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

enum {
	ND_NULL = 0,
	ND_MEMZERO,
	ND_NUM,
	ND_STR,
	ND_VAR,
	ND_ADD,
	ND_SUB,
	ND_MUL,
	ND_DIV,
	ND_MOD,
	ND_BITAND,
	ND_BITOR,
	ND_BITXOR,
	ND_SHL,
	ND_SHR,
	ND_EQ,
	ND_NE,
	ND_LT,
	ND_LE,
	ND_GT,
	ND_GE,
	ND_LOGAND,
	ND_LOGOR,
	ND_ASSIGN,
	ND_OPASSIGN,
	ND_PREINC,
	ND_POSTINC,
	ND_NEG,
	ND_NOT,
	ND_BITNOT,
	ND_ADDR,
	ND_DEREF,
	ND_MEMBER,
	ND_CALL,
	ND_CAST,
	ND_COND,
	ND_COMMA,
	ND_RETURN,
	ND_IF,
	ND_WHILE,
	ND_FOR,
	ND_DO,
	ND_BLOCK,
	ND_EXPR_STMT,
	ND_BREAK,
	ND_CONTINUE,
	ND_SWITCH,
	ND_CASE,
	ND_DEFAULT,
	ND_GOTO,
	ND_LABEL,
};

struct sym;
struct case_entry;

struct node {
	int kind;
	struct type *ty;
	struct node *lhs;
	struct node *rhs;
	struct node *cond;
	struct node *then;
	struct node *els;
	struct node *init;
	struct node *inc;
	struct node *body;
	struct node *next;
	struct node **args;
	int nargs;
	struct obj *var;
	struct node *callee;
	struct sym *sym;
	struct case_entry *cases;
	char *label_name;
	long val;
	int member_offset;
	int case_label;
};

struct case_entry {
	struct node *node;
	long val;
	bool is_default;
	struct case_entry *next;
};

struct node *new_node(int kind);
struct node *new_num(long val);
struct node *new_var_node(struct obj *var);

/* ------------------------------------------------------------------ */
/* symbols and scopes                                                  */
/* ------------------------------------------------------------------ */

enum {
	SEC_TEXT = 0,
	SEC_DATA,
	SEC_BSS,
};

struct data_reloc {
	int offset;
	struct sym *sym;
	long addend;
	struct data_reloc *next;
};

struct sym {
	char *name;
	int sec;
	int offset;
	int size;
	int align;
	bool defined;
	bool is_static;
	unsigned long addr;
	unsigned char *init;
	struct data_reloc *drel;
	struct sym *next;
};

struct obj {
	char *name;
	struct type *ty;
	bool is_local;
	bool is_function;
	bool defined;
	int offset;
	bool is_param;
	int param_index;
	struct sym *sym;
	struct node *body;
	struct obj *va_area;
	int frame_size;
	struct obj *next;
};

extern struct obj *globals;
extern struct sym *syms;
extern struct sym *sym_tail;
extern int unit_id;

struct scope {
	struct obj *vars;
	struct scope *parent;
};

extern struct scope *scope;
extern int locals_size;

struct sym *new_sym(const char *name, int sec, int size, int align);
void make_local_sym(struct sym *s);
struct obj *find_global(const char *name);
struct obj *new_global(const char *name, struct type *ty);
void enter_scope(void);
void leave_scope(void);
struct obj *find_local(const char *name);
struct obj *new_local(const char *name, struct type *ty, bool is_param,
		      int param_index);

/* ------------------------------------------------------------------ */
/* instruction encoding                                                */
/* ------------------------------------------------------------------ */

enum {
	X0 = 0,
	RA = 1,
	SP = 2,
	T0 = 5,
	T1 = 6,
	T2 = 7,
	S0 = 8,
	A0 = 10,
	A1 = 11,
	A2 = 12,
	T6 = 31,
};

extern struct buf code;

void emit32(unsigned int x);
unsigned int enc_r(int f7, int rs2, int rs1, int f3, int rd, int op);
unsigned int enc_i(int op, int rd, int f3, int rs1, int imm);
unsigned int enc_s(int op, int f3, int rs1, int rs2, int imm);
unsigned int enc_b(int op, int f3, int rs1, int rs2, int imm);
unsigned int enc_u(int op, int rd, int imm20);
unsigned int enc_j(int op, int rd, int imm);
void i_addi(int rd, int rs, int imm);
void i_addiw(int rd, int rs, int imm);
void i_lui(int rd, int imm20);
void i_ld(int rd, int rs, int imm);
void i_sd(int rs2, int rs1, int imm);
void i_lw(int rd, int rs, int imm);
void i_lwu(int rd, int rs, int imm);
void i_sw(int rs2, int rs1, int imm);
void i_lh(int rd, int rs, int imm);
void i_lhu(int rd, int rs, int imm);
void i_sh(int rs2, int rs1, int imm);
void i_lb(int rd, int rs, int imm);
void i_lbu(int rd, int rs, int imm);
void i_sb(int rs2, int rs1, int imm);
void i_r(int f7, int f3, int rd, int rs1, int rs2);
void i_mv(int rd, int rs);
void i_seqz(int rd, int rs);
void i_snez(int rd, int rs);
void i_ret(void);
void i_ecall(void);
void i_li(int rd, long val);
void i_bf(int f3, int rs1, int rs2, int imm);

/* ------------------------------------------------------------------ */
/* labels, relocations and units                                       */
/* ------------------------------------------------------------------ */

struct fixup {
	int pos;
	int label;
	struct fixup *next;
};

struct reloc {
	int pos;
	int kind;
	struct sym *sym;
	long addend;
	struct reloc *next;
};

enum {
	R_HI20 = 0,
	R_LO12I,
	R_JAL,
};

struct unit {
	unsigned char *code;
	int code_len;
	struct sym *syms;
	struct reloc *relocs;
	unsigned char *data;
	int data_len;
	int bss_len;
	unsigned long text_base;
	unsigned long data_base;
	unsigned long bss_base;
	struct unit *next;
};

extern struct fixup *fixups;
extern struct reloc *relocs;
extern int *label_pos;
extern int nlabels;
extern struct unit *units;
extern struct unit *unit_tail;

int new_label(void);
void place_label(int id);
void jump_if_zero(int reg, int label);
void jump_if_nonzero(int reg, int label);
void jump_to(int label);
void add_fixup(int label);
void patch_fixups(void);
void add_reloc(int kind, struct sym *sym, long addend);
void i_la(int rd, struct sym *sym, long addend);
void i_call_sym(struct sym *sym);
unsigned int rd_of(unsigned int ins);
unsigned int rs1_of(unsigned int ins);
void reset_emit_state(void);

/* ------------------------------------------------------------------ */
/* parser                                                              */
/* ------------------------------------------------------------------ */

void parse_translation_unit(void);
void reset_parser_state(void);

/* ------------------------------------------------------------------ */
/* code generation                                                     */
/* ------------------------------------------------------------------ */

void gen_function(struct obj *fn);
void gen_runtime(void);
void gen_start(struct sym *main_sym);
void reset_codegen_state(void);

/* ------------------------------------------------------------------ */
/* ELF writer and linker                                               */
/* ------------------------------------------------------------------ */

void finalize_unit(void);
void make_runtime_refs(void);
void write_object(const char *path, struct unit *u);
struct unit *read_object(const char *path);
void link_and_write(const char *path);

#endif
