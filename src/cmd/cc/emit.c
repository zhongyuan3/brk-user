#include "cc.h"

struct buf code;
struct fixup *fixups;
struct reloc *relocs;
int *label_pos;
int nlabels;
struct unit *units;
struct unit *unit_tail;

void emit32(unsigned int x)
{
	buf_u32(&code, x);
}

unsigned int enc_r(int f7, int rs2, int rs1, int f3, int rd, int op)
{
	return ((unsigned int)f7 << 25) | ((unsigned int)rs2 << 20) |
	       ((unsigned int)rs1 << 15) | ((unsigned int)f3 << 12) |
	       ((unsigned int)rd << 7) | (unsigned int)op;
}

unsigned int enc_i(int op, int rd, int f3, int rs1, int imm)
{
	return ((unsigned int)(imm & 0xfff) << 20) | ((unsigned int)rs1 << 15) |
	       ((unsigned int)f3 << 12) | ((unsigned int)rd << 7) |
	       (unsigned int)op;
}

unsigned int enc_s(int op, int f3, int rs1, int rs2, int imm)
{
	unsigned int u = (unsigned int)(imm & 0xfff);

	return ((u >> 5) << 25) | ((unsigned int)rs2 << 20) |
	       ((unsigned int)rs1 << 15) | ((unsigned int)f3 << 12) |
	       ((u & 0x1f) << 7) | (unsigned int)op;
}

unsigned int enc_b(int op, int f3, int rs1, int rs2, int imm)
{
	unsigned int u = (unsigned int)imm;

	return (((u >> 12) & 1) << 31) | (((u >> 5) & 0x3f) << 25) |
	       ((unsigned int)rs2 << 20) | ((unsigned int)rs1 << 15) |
	       ((unsigned int)f3 << 12) | (((u >> 1) & 0xf) << 8) |
	       (((u >> 11) & 1) << 7) | (unsigned int)op;
}

unsigned int enc_u(int op, int rd, int imm20)
{
	return ((unsigned int)(imm20 & 0xfffff) << 12) |
	       ((unsigned int)rd << 7) | (unsigned int)op;
}

unsigned int enc_j(int op, int rd, int imm)
{
	unsigned int u = (unsigned int)imm;

	return (((u >> 20) & 1) << 31) | (((u >> 1) & 0x3ff) << 21) |
	       (((u >> 11) & 1) << 20) | (((u >> 12) & 0xff) << 12) |
	       ((unsigned int)rd << 7) | (unsigned int)op;
}

void i_addi(int rd, int rs, int imm)
{
	if (imm >= -2048 && imm <= 2047) {
		emit32(enc_i(0x13, rd, 0, rs, imm));
	} else {
		/* li T6, imm; add rd, rs, T6 */
		emit32(enc_i(0x13, T6, 0, X0, 0));
		{
			unsigned long u = (unsigned long)imm;
			int i;

			for (i = 7; i >= 0; i--) {
				emit32(enc_i(0x13, T6, 1, T6, 8)); /* slli */
				emit32(enc_i(0x13, T6, 6, T6,
					     (int)((u >> (i * 8)) & 0xff)));
			}
		}
		emit32(enc_r(0, T6, rs, 0, rd, 0x33)); /* add */
	}
}

void i_addiw(int rd, int rs, int imm)
{
	emit32(enc_i(0x1b, rd, 0, rs, imm));
}

void i_lui(int rd, int imm20)
{
	emit32(enc_u(0x37, rd, imm20));
}

void i_ld(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 3, rs, imm));
}

void i_sd(int rs2, int rs1, int imm)
{
	emit32(enc_s(0x23, 3, rs1, rs2, imm));
}

void i_lw(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 2, rs, imm));
}

void i_lwu(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 6, rs, imm));
}

void i_sw(int rs2, int rs1, int imm)
{
	emit32(enc_s(0x23, 2, rs1, rs2, imm));
}

void i_lh(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 1, rs, imm));
}

void i_lhu(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 5, rs, imm));
}

void i_sh(int rs2, int rs1, int imm)
{
	emit32(enc_s(0x23, 1, rs1, rs2, imm));
}

void i_lb(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 0, rs, imm));
}

void i_lbu(int rd, int rs, int imm)
{
	emit32(enc_i(0x03, rd, 4, rs, imm));
}

void i_sb(int rs2, int rs1, int imm)
{
	emit32(enc_s(0x23, 0, rs1, rs2, imm));
}

void i_r(int f7, int f3, int rd, int rs1, int rs2)
{
	emit32(enc_r(f7, rs2, rs1, f3, rd, 0x33));
}

void i_mv(int rd, int rs)
{
	i_addi(rd, rs, 0);
}

void i_seqz(int rd, int rs)
{
	emit32(enc_i(0x13, rd, 3, rs, 1)); /* sltiu rd, rs, 1 */
}

void i_snez(int rd, int rs)
{
	emit32(enc_r(0, rs, X0, 3, rd, 0x33)); /* sltu rd, x0, rs */
}

void i_ret(void)
{
	emit32(enc_i(0x67, X0, 0, RA, 0)); /* jalr x0, 0(ra) */
}

void i_ecall(void)
{
	emit32(0x00000073);
}

void i_li(int rd, long val)
{
	unsigned long u = (unsigned long)val;

	if (val >= -2048 && val <= 2047) {
		emit32(enc_i(0x13, rd, 0, X0, (int)val));
		return;
	}
	if (val >= -2147483648L && val <= 2147483647L) {
		long hi = ((long)val + 0x800) >> 12;
		long lo = (long)val - (hi << 12);

		i_lui(rd, (int)hi);
		i_addiw(rd, rd, (int)lo);
		return;
	}
	emit32(enc_i(0x13, rd, 0, X0, 0));
	for (int i = 7; i >= 0; i--) {
		emit32(enc_i(0x13, rd, 1, rd, 8)); /* slli */
		emit32(enc_i(0x13, rd, 6, rd,
			     (int)((u >> (i * 8)) & 0xff))); /* ori */
	}
}

void i_bf(int f3, int rs1, int rs2, int imm)
{
	emit32(enc_b(0x63, f3, rs1, rs2, imm));
}

int new_label(void)
{
	int id = nlabels++;

	label_pos = xrealloc(label_pos, (size_t)nlabels * sizeof(*label_pos));
	label_pos[id] = -1;
	return id;
}

void place_label(int id)
{
	label_pos[id] = code.len;
}

void add_fixup(int label)
{
	struct fixup *f = xcalloc(1, sizeof(*f));

	f->pos = code.len;
	f->label = label;
	f->next = fixups;
	fixups = f;
	emit32(enc_j(0x6f, X0, 0));
}

void jump_if_zero(int reg, int label)
{
	i_bf(1, reg, X0, 8); /* bne reg, x0, +8 */
	add_fixup(label);
}

void jump_if_nonzero(int reg, int label)
{
	i_bf(0, reg, X0, 8); /* beq reg, x0, +8 */
	add_fixup(label);
}

void jump_to(int label)
{
	add_fixup(label);
}

void patch_fixups(void)
{
	struct fixup *f;

	for (f = fixups; f; f = f->next) {
		int off;

		if (label_pos[f->label] < 0)
			fatal("internal error: unresolved label");
		off = label_pos[f->label] - f->pos;
		if (off < -(1 << 20) || off >= (1 << 20))
			fatal("function too large for jump");
		code.data[f->pos + 0] =
			(unsigned char)(enc_j(0x6f, X0, off) & 0xff);
		code.data[f->pos + 1] =
			(unsigned char)((enc_j(0x6f, X0, off) >> 8) & 0xff);
		code.data[f->pos + 2] =
			(unsigned char)((enc_j(0x6f, X0, off) >> 16) & 0xff);
		code.data[f->pos + 3] =
			(unsigned char)((enc_j(0x6f, X0, off) >> 24) & 0xff);
	}
	fixups = NULL;
}

void add_reloc(int kind, struct sym *sym, long addend)
{
	struct reloc *r = xcalloc(1, sizeof(*r));

	r->pos = code.len;
	r->kind = kind;
	r->sym = sym;
	r->addend = addend;
	r->next = relocs;
	relocs = r;
}

void i_la(int rd, struct sym *sym, long addend)
{
	add_reloc(R_HI20, sym, addend);
	emit32(enc_u(0x37, rd, 0));
	add_reloc(R_LO12I, sym, addend);
	emit32(enc_i(0x13, rd, 0, rd, 0));
}

void i_call_sym(struct sym *sym)
{
	add_reloc(R_JAL, sym, 0);
	emit32(enc_j(0x6f, RA, 0));
}

unsigned int rd_of(unsigned int ins)
{
	return (ins >> 7) & 0x1f;
}

unsigned int rs1_of(unsigned int ins)
{
	return (ins >> 15) & 0x1f;
}

void reset_emit_state(void)
{
	code.len = 0;
	syms = NULL;
	sym_tail = NULL;
	relocs = NULL;
	fixups = NULL;
	nlabels = 0;
	label_pos = NULL;
	globals = NULL;
	scope = NULL;
	locals_size = 0;
}
