#include "brkcc.h"

/*
 * Structured assembler: instruction records live in emit.c, next to the
 * encoded text buffer.  asm_finalize() rebuilds the text bytes from the
 * records after fixups have been applied; asm_print_unit() renders the
 * same records as a human-readable RISC-V listing for -S.
 */

extern unsigned int *asm_records;
extern int asm_nrecords;

void asm_finalize(void)
{
	int need = asm_nrecords * 4 - code.len;
	int i;

	if (need > 0)
		buf_grow(&code, need);
	for (i = 0; i < asm_nrecords; i++) {
		unsigned int ins = asm_records[i];

		code.data[i * 4 + 0] = (unsigned char)(ins & 0xff);
		code.data[i * 4 + 1] = (unsigned char)((ins >> 8) & 0xff);
		code.data[i * 4 + 2] = (unsigned char)((ins >> 16) & 0xff);
		code.data[i * 4 + 3] = (unsigned char)((ins >> 24) & 0xff);
	}
	code.len = asm_nrecords * 4;
}

static const char *const xreg[32] = {
	"zero", "ra", "sp", "gp", "tp", "t0", "t1", "t2",
	"s0",	"s1", "a0", "a1", "a2", "a3", "a4", "a5",
	"a6",	"a7", "s2", "s3", "s4", "s5", "s6", "s7",
	"s8",	"s9", "s10", "s11", "t3", "t4", "t5", "t6",
};

static const char *const freg[32] = {
	"ft0", "ft1", "ft2", "ft3", "ft4", "ft5", "ft6", "ft7",
	"fs0", "fs1", "fa0", "fa1", "fa2", "fa3", "fa4", "fa5",
	"fa6", "fa7", "fs2", "fs3", "fs4", "fs5", "fs6", "fs7",
	"fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11",
};

static int imm_i(unsigned int ins)
{
	int v = (int)(ins >> 20);

	if (v & 0x800)
		v -= 0x1000;
	return v;
}

static int imm_s(unsigned int ins)
{
	int v = (int)(((ins >> 25) << 5) | ((ins >> 7) & 0x1f));

	if (v & 0x800)
		v -= 0x1000;
	return v;
}

static int imm_b(unsigned int ins)
{
	int v = (((ins >> 31) & 1) << 12) | (((ins >> 7) & 1) << 11) |
		(((ins >> 25) & 0x3f) << 5) | (((ins >> 8) & 0xf) << 1);

	if (v & 0x1000)
		v -= 0x2000;
	return v;
}

static int imm_j(unsigned int ins)
{
	int v = (((ins >> 31) & 1) << 20) | (((ins >> 12) & 0xff) << 12) |
		(((ins >> 20) & 1) << 11) | (((ins >> 21) & 0x3ff) << 1);

	if (v & 0x100000)
		v -= 0x200000;
	return v;
}

static const char *r_name(unsigned int r)
{
	return xreg[r & 31];
}

static const char *f_name(unsigned int r)
{
	return freg[r & 31];
}

static void print_rr(FILE *fp, const char *op, unsigned int rd,
		     unsigned int rs1, unsigned int rs2)
{
	fprintf(fp, "\t%s %s, %s, %s\n", op, r_name(rd), r_name(rs1),
		r_name(rs2));
}

static void print_fp(FILE *fp, unsigned int ins)
{
	unsigned int rd = (ins >> 7) & 31;
	unsigned int f3 = (ins >> 12) & 7;
	unsigned int rs1 = (ins >> 15) & 31;
	unsigned int rs2 = (ins >> 20) & 31;
	unsigned int f7 = ins >> 25;

	switch (f7) {
	case 0x00:
	case 0x01:
		fprintf(fp, "\t%s %s, %s, %s\n",
			f7 == 0 ? "fadd.s" : "fadd.d", f_name(rd),
			f_name(rs1), f_name(rs2));
		return;
	case 0x04:
	case 0x05:
		fprintf(fp, "\t%s %s, %s, %s\n",
			f7 == 0x04 ? "fsub.s" : "fsub.d", f_name(rd),
			f_name(rs1), f_name(rs2));
		return;
	case 0x08:
	case 0x09:
		fprintf(fp, "\t%s %s, %s, %s\n",
			f7 == 0x08 ? "fmul.s" : "fmul.d", f_name(rd),
			f_name(rs1), f_name(rs2));
		return;
	case 0x0c:
	case 0x0d:
		fprintf(fp, "\t%s %s, %s, %s\n",
			f7 == 0x0c ? "fdiv.s" : "fdiv.d", f_name(rd),
			f_name(rs1), f_name(rs2));
		return;
	case 0x10:
	case 0x11: {
		const char *op;

		if (f3 == 0)
			op = f7 == 0x10 ? "fsgnj.s" : "fsgnj.d";
		else if (f3 == 1)
			op = f7 == 0x10 ? "fsgnjn.s" : "fsgnjn.d";
		else
			op = f7 == 0x10 ? "fsgnjx.s" : "fsgnjx.d";
		fprintf(fp, "\t%s %s, %s, %s\n", op, f_name(rd),
			f_name(rs1), f_name(rs2));
		return;
	}
	case 0x20:
		fprintf(fp, "\tfcvt.s.d %s, %s\n", f_name(rd),
			f_name(rs1));
		return;
	case 0x21:
		fprintf(fp, "\tfcvt.d.s %s, %s\n", f_name(rd),
			f_name(rs1));
		return;
	case 0x50:
	case 0x51: {
		const char *op;

		if (f3 == 0)
			op = f7 == 0x50 ? "fle.s" : "fle.d";
		else if (f3 == 1)
			op = f7 == 0x50 ? "flt.s" : "flt.d";
		else
			op = f7 == 0x50 ? "feq.s" : "feq.d";
		fprintf(fp, "\t%s %s, %s, %s\n", op, r_name(rd),
			f_name(rs1), f_name(rs2));
		return;
	}
	case 0x60:
	case 0x61: {
		static const char *const w[] = {"w", "wu", "l", "lu"};

		fprintf(fp, "\tfcvt.%s.%s %s, %s\n", w[rs2 & 3],
			f7 == 0x60 ? "s" : "d", r_name(rd), f_name(rs1));
		return;
	}
	case 0x68:
	case 0x69: {
		static const char *const w[] = {"w", "wu", "l", "lu"};

		fprintf(fp, "\tfcvt.%s.%s %s, %s\n",
			f7 == 0x68 ? "s" : "d", w[rs2 & 3], f_name(rd),
			r_name(rs1));
		return;
	}
	case 0x70:
		fprintf(fp, "\tfmv.x.w %s, %s\n", r_name(rd), f_name(rs1));
		return;
	case 0x71:
		fprintf(fp, "\tfmv.x.d %s, %s\n", r_name(rd), f_name(rs1));
		return;
	case 0x78:
		fprintf(fp, "\tfmv.w.x %s, %s\n", f_name(rd), r_name(rs1));
		return;
	case 0x79:
		fprintf(fp, "\tfmv.d.x %s, %s\n", f_name(rd), r_name(rs1));
		return;
	default:
		fprintf(fp, "\t.word 0x%08x\n", ins);
		return;
	}
}

static void print_insn(FILE *fp, unsigned int ins)
{
	unsigned int op = ins & 0x7f;
	unsigned int rd = (ins >> 7) & 31;
	unsigned int f3 = (ins >> 12) & 7;
	unsigned int rs1 = (ins >> 15) & 31;
	unsigned int rs2 = (ins >> 20) & 31;
	unsigned int f7 = ins >> 25;

	switch (op) {
	case 0x33: {
		static const char *const base[8] = {
			"add", "sll", "slt", "sltu", "xor", "srl", "or", "and",
		};
		static const char *const muldiv[8] = {
			"mul", "mulh", "mulhsu", "mulhu",
			"div", "divu", "rem", "remu",
		};

		if (f7 == 1)
			print_rr(fp, muldiv[f3], rd, rs1, rs2);
		else
			print_rr(fp, f7 == 0x20 && f3 == 0 ? "sub" :
					 f7 == 0x20 && f3 == 5 ? "sra" :
								base[f3],
				 rd, rs1, rs2);
		return;
	}
	case 0x13:
		switch (f3) {
		case 0:
			fprintf(fp, "\taddi %s, %s, %d\n", r_name(rd),
				r_name(rs1), imm_i(ins));
			return;
		case 1:
			fprintf(fp, "\tslli %s, %s, %d\n", r_name(rd),
				r_name(rs1), (int)(ins >> 20) & 63);
			return;
		case 2:
			fprintf(fp, "\tslti %s, %s, %d\n", r_name(rd),
				r_name(rs1), imm_i(ins));
			return;
		case 3:
			fprintf(fp, "\tsltiu %s, %s, %d\n", r_name(rd),
				r_name(rs1), imm_i(ins));
			return;
		case 4:
			fprintf(fp, "\txori %s, %s, %d\n", r_name(rd),
				r_name(rs1), imm_i(ins));
			return;
		case 5:
			fprintf(fp, "\t%s %s, %s, %d\n",
				f7 == 0x20 ? "srai" : "srli", r_name(rd),
				r_name(rs1), (int)(ins >> 20) & 63);
			return;
		case 6:
			fprintf(fp, "\tori %s, %s, %d\n", r_name(rd),
				r_name(rs1), imm_i(ins));
			return;
		default:
			fprintf(fp, "\tandi %s, %s, %d\n", r_name(rd),
				r_name(rs1), imm_i(ins));
			return;
		}
	case 0x1b:
		if (f3 == 0)
			fprintf(fp, "\taddiw %s, %s, %d\n", r_name(rd),
				r_name(rs1), imm_i(ins));
		else if (f3 == 1)
			fprintf(fp, "\tslliw %s, %s, %d\n", r_name(rd),
				r_name(rs1), (int)(ins >> 20) & 31);
		else if (f3 == 5)
			fprintf(fp, "\t%s %s, %s, %d\n",
				f7 == 0x20 ? "sraiw" : "srliw", r_name(rd),
				r_name(rs1), (int)(ins >> 20) & 31);
		else
			fprintf(fp, "\t.word 0x%08x\n", ins);
		return;
	case 0x37:
		fprintf(fp, "\tlui %s, 0x%x\n", r_name(rd),
			(ins >> 12) & 0xfffff);
		return;
	case 0x03:
		switch (f3) {
		case 0:
			fprintf(fp, "\tlb %s, %d(%s)\n", r_name(rd),
				imm_i(ins), r_name(rs1));
			return;
		case 1:
			fprintf(fp, "\tlh %s, %d(%s)\n", r_name(rd),
				imm_i(ins), r_name(rs1));
			return;
		case 2:
			fprintf(fp, "\tlw %s, %d(%s)\n", r_name(rd),
				imm_i(ins), r_name(rs1));
			return;
		case 3:
			fprintf(fp, "\tld %s, %d(%s)\n", r_name(rd),
				imm_i(ins), r_name(rs1));
			return;
		case 4:
			fprintf(fp, "\tlbu %s, %d(%s)\n", r_name(rd),
				imm_i(ins), r_name(rs1));
			return;
		case 5:
			fprintf(fp, "\tlhu %s, %d(%s)\n", r_name(rd),
				imm_i(ins), r_name(rs1));
			return;
		case 6:
			fprintf(fp, "\tlwu %s, %d(%s)\n", r_name(rd),
				imm_i(ins), r_name(rs1));
			return;
		default:
			fprintf(fp, "\t.word 0x%08x\n", ins);
			return;
		}
	case 0x23: {
		static const char *const st[8] = {
			"sb", "sh", "sw", "sd",
		};

		if (f3 < 4)
			fprintf(fp, "\t%s %s, %d(%s)\n", st[f3],
				r_name(rs2), imm_s(ins), r_name(rs1));
		else
			fprintf(fp, "\t.word 0x%08x\n", ins);
		return;
	}
	case 0x63: {
		static const char *const br[8] = {
			"beq", "bne", "blt", "bge",
			"bltu", "bgeu", "?6", "?7",
		};

		if (f3 < 6)
			fprintf(fp, "\t%s %s, %s, %d\n", br[f3],
				r_name(rs1), r_name(rs2), imm_b(ins));
		else
			fprintf(fp, "\t.word 0x%08x\n", ins);
		return;
	}
	case 0x6f:
		fprintf(fp, "\tjal %s, %d\n", r_name(rd), imm_j(ins));
		return;
	case 0x67:
		if (f3 == 0)
			fprintf(fp, "\tjalr %s, %d(%s)\n", r_name(rd),
				imm_i(ins), r_name(rs1));
		else
			fprintf(fp, "\t.word 0x%08x\n", ins);
		return;
	case 0x73:
		if (ins == 0x00000073)
			fprintf(fp, "\tecall\n");
		else if (ins == 0x00100073)
			fprintf(fp, "\tebreak\n");
		else
			fprintf(fp, "\t.word 0x%08x\n", ins);
		return;
	case 0x53:
		print_fp(fp, ins);
		return;
	default:
		fprintf(fp, "\t.word 0x%08x\n", ins);
		return;
	}
}

static struct reloc *reloc_at(int pos)
{
	struct reloc *r;

	for (r = relocs; r; r = r->next) {
		if (r->pos == pos)
			return r;
	}
	return NULL;
}

static char *reloc_target(struct reloc *r)
{
	static char buf[256];
	long a = r->addend;

	if (a)
		snprintf(buf, sizeof(buf), "%s%+ld", r->sym->name, a);
	else
		snprintf(buf, sizeof(buf), "%s", r->sym->name);
	return buf;
}

static void print_reloc(FILE *fp, unsigned int ins, struct reloc *r)
{
	char *name = reloc_target(r);
	unsigned int rd = (ins >> 7) & 31;
	unsigned int rs1 = (ins >> 15) & 31;

	switch (r->kind) {
	case R_HI20:
		fprintf(fp, "\tlui %s, %%hi(%s)\n", r_name(rd), name);
		break;
	case R_LO12I:
		fprintf(fp, "\taddi %s, %s, %%lo(%s)\n", r_name(rd),
			r_name(rs1), name);
		break;
	case R_JAL:
		fprintf(fp, "\tjal ra, %s\n", name);
		break;
	default:
		fprintf(fp, "\t.word 0x%08x\n", ins);
		break;
	}
}

static void print_labels(FILE *fp, int pos)
{
	struct sym *s;

	for (s = syms; s; s = s->next) {
		if (!s->defined || s->sec != SEC_TEXT || s->offset != pos)
			continue;
		fprintf(fp, "%s:\n", s->name);
	}
}

void asm_print_unit(const char *path)
{
	FILE *fp = stdout;
	int i;

	if (path && strcmp(path, "-")) {
		fp = fopen(path, "w");
		if (!fp)
			fatal("cannot open '%s'", path);
	}
	fprintf(fp, "\t.text\n");
	for (i = 0; i < asm_nrecords; i++) {
		int pos = i * 4;
		struct reloc *r;

		print_labels(fp, pos);
		r = reloc_at(pos);
		if (r)
			print_reloc(fp, asm_records[i], r);
		else
			print_insn(fp, asm_records[i]);
	}
	if (fp != stdout)
		fclose(fp);
}
