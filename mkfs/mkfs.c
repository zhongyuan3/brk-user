/*
 * Host tool: create a brkfs image from brkfs.h layout.
 *
 * On-disk layout (block numbers are 0-based, size s_blocksize):
 *   block 0: bytes [0,1024) reserved; [1024,2048) struct brkfs_super_block;
 *            rest of block 0 zero.
 *   block s_inode_bitmap_start: inode bitmap (1 bit per inode slot; inode N uses bit N).
 *   block s_data_bitmap_start: data block bitmap (1 bit per data slot; slot K is
 *            physical block s_data_start + K).
 *   blocks [s_inode_start, s_inode_start + s_inode_blocks): inode table; inode
 *            number ino (>=1) at byte offset (ino-1)*sizeof(inode) from inode_start.
 *   blocks [s_data_start, s_data_start + s_data_blocks): payload and index blocks;
 *            inode i_blocks[] holds absolute physical block numbers.
 *
 * i_block[]: [0..6] direct data; [7] singly-indirect index block; [8] doubly;
 *            [9] triply (see brkfs.h).
 */

#include "brkfs.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define BRKFS_INODE_BYTES ((uint32_t)sizeof(struct brkfs_inode))
#define BRKFS_PTRS_PER_BLOCK(bs) ((bs) / (uint32_t)sizeof(uint32_t))

struct host_file {
	char *path;
	char *name;
	uint8_t *data;
	uint32_t size;
};

struct mkfs_ctx {
	uint8_t *img;
	uint32_t bs;
	struct brkfs_super_block *sb;
	uint8_t *inode_bmp;
	uint8_t *data_bmp;
	uint32_t inodes_capacity;
	uint32_t data_slots;
};

enum { OUT_QUIET = 0, OUT_NORMAL = 1, OUT_VERBOSE = 2 };

static int out_level = OUT_NORMAL;

static void warn(const char *fmt, ...)
{
	va_list ap;
	fputs("mkfs: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static noreturn void die(const char *msg)
{
	fputs("mkfs: ", stderr);
	perror(msg);
	exit(1);
}

static void fmt_bytes(char *buf, size_t buflen, uint64_t n)
{
	if (n >= 1048576u && n % 1048576u == 0)
		snprintf(buf, buflen, "%" PRIu64 " MiB", n / 1048576u);
	else if (n >= 1024u && n % 1024u == 0)
		snprintf(buf, buflen, "%" PRIu64 " KiB", n / 1024u);
	else
		snprintf(buf, buflen, "%" PRIu64 " B", n);
}

/* Verbose trace to stdout (no prefix; lines are indented for readability). */
static void vlog(const char *fmt, ...)
{
	if (out_level < OUT_VERBOSE)
		return;
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static void *xmalloc(size_t n)
{
	void *p = malloc(n);
	if (p)
		return p;
	die("malloc");
}

static uint32_t div_round_up(uint32_t a, uint32_t b)
{
	assert(b > 0);
	return (a + b - 1u) / b;
}

static uint32_t file_payload_blocks(uint32_t file_size, uint32_t bs)
{
	if (file_size == 0)
		return 0;
	return div_round_up(file_size, bs);
}

/* Payload blocks + index blocks needed to map them. */
static uint32_t data_blocks_for_file(uint32_t file_size, uint32_t bs)
{
	uint32_t payload = file_payload_blocks(file_size, bs);
	if (payload == 0)
		return 0;
	if (payload <= BRKFS_DIRECT_BLOCKS)
		return payload;

	uint32_t ptrs = BRKFS_PTRS_PER_BLOCK(bs);
	uint32_t rem1 = payload - BRKFS_DIRECT_BLOCKS;
	if (rem1 <= ptrs)
		return payload + 1;

	warn("file too large for this mkfs (needs double indirect)");
	exit(1);
}

static uint32_t dirent_reclen(const char *name, uint8_t name_len)
{
	uint32_t core = 8u + (uint32_t)name_len;
	uint32_t al = (core + 3u) & ~3u;
	if (al < BRKFS_DIR_ENTRY_MIN_LEN)
		al = BRKFS_DIR_ENTRY_MIN_LEN;
	return al;
}

static uint32_t root_dir_size(struct host_file *files, int nfiles, uint32_t bs)
{
	uint32_t total = 0;
	total += dirent_reclen(".", 1);
	total += dirent_reclen("..", 2);
	for (int i = 0; i < nfiles; i++) {
		size_t len = strlen(files[i].name);
		if (len == 0 || len > BRKFS_NAME_LEN) {
			warn("bad entry name (length) for %s", files[i].path);
			exit(1);
		}
		total += dirent_reclen(files[i].name, (uint8_t)len);
	}
	total = ((total + bs - 1) / bs) *
		bs; /* directory always takes full blocks */
	return total;
}

static void write_dirent(void *base, uint32_t *off, uint32_t ino, uint8_t type,
			 const char *name, uint8_t name_len)
{
	uint32_t reclen = dirent_reclen(name, name_len);
	struct brkfs_dir_entry *de =
		(struct brkfs_dir_entry *)((uint8_t *)base + *off);
	de->inode = ino;
	de->entry_len = (uint16_t)reclen;
	de->name_len = name_len;
	de->file_type = type;
	memcpy(de->name, name, name_len);
	memset(de->name + name_len, 0, reclen - 8u - (uint32_t)name_len);
	*off += reclen;
}

static void set_bit(uint8_t *bmp, uint32_t bit)
{
	bmp[bit / 8] |= (uint8_t)(1u << (bit % 8));
}

static int test_bit(const uint8_t *bmp, uint32_t bit)
{
	return (bmp[bit / 8] >> (bit % 8)) & 1;
}

static uint32_t count_bitmap_bits(const uint8_t *bmp, uint32_t nbits)
{
	uint32_t c = 0;
	for (uint32_t b = 0; b < nbits; b++) {
		if (test_bit(bmp, b))
			c++;
	}
	return c;
}

static uint32_t find_free_bit(uint8_t *bmp, uint32_t nbits, uint32_t start)
{
	for (uint32_t b = start; b < nbits; b++) {
		if (!test_bit(bmp, b))
			return b;
	}
	warn("data or inode bitmap full");
	exit(1);
}

static uint8_t *blk_ptr(struct mkfs_ctx *c, uint32_t blk)
{
	return c->img + (uint64_t)blk * c->bs;
}

/*
 * Map logical file block lbn (0-based payload block) to physical block number.
 * desc receives a short path label for verbose output.
 */
static int resolve_lbn_to_phys(struct mkfs_ctx *c, const struct brkfs_inode *in,
			       uint32_t lbn, uint32_t *phys_out, char *desc,
			       size_t desclen)
{
	const uint32_t D = BRKFS_DIRECT_BLOCKS;
	const uint32_t ptrs = BRKFS_PTRS_PER_BLOCK(c->bs);
	const uint64_t dspan = (uint64_t)ptrs * ptrs;
	const uint64_t tspan = dspan * ptrs;

	if (lbn < D) {
		snprintf(desc, desclen, "direct[%u]", lbn);
		*phys_out = in->i_block[lbn];
		return 1;
	}

	uint64_t off = (uint64_t)lbn - D;
	if (off < ptrs) {
		uint32_t ib = in->i_block[BRKFS_INDIRECT_BLOCK];
		if (!ib)
			return 0;
		const uint32_t *idx = (const uint32_t *)blk_ptr(c, ib);
		snprintf(desc, desclen, "s_ind[%u]", (uint32_t)off);
		*phys_out = idx[(uint32_t)off];
		return 1;
	}

	off -= ptrs;
	if (off < dspan) {
		uint32_t dib = in->i_block[BRKFS_DOUBLE_INDIRECT_BLOCK];
		if (!dib)
			return 0;
		uint32_t j = (uint32_t)(off / ptrs);
		uint32_t k = (uint32_t)(off % ptrs);
		const uint32_t *top = (const uint32_t *)blk_ptr(c, dib);
		uint32_t sib = top[j];
		if (!sib)
			return 0;
		const uint32_t *mid = (const uint32_t *)blk_ptr(c, sib);
		snprintf(desc, desclen, "d_ind[%u][%u]", j, k);
		*phys_out = mid[k];
		return 1;
	}

	off -= dspan;
	if (off < tspan) {
		uint32_t tib = in->i_block[BRKFS_TRIPLE_INDIRECT_BLOCK];
		if (!tib)
			return 0;
		uint32_t i = (uint32_t)(off / dspan);
		uint64_t rem = off % dspan;
		uint32_t j = (uint32_t)(rem / ptrs);
		uint32_t k = (uint32_t)(rem % ptrs);
		const uint32_t *t1 = (const uint32_t *)blk_ptr(c, tib);
		uint32_t b1 = t1[i];
		if (!b1)
			return 0;
		const uint32_t *t2 = (const uint32_t *)blk_ptr(c, b1);
		uint32_t b2 = t2[j];
		if (!b2)
			return 0;
		const uint32_t *t3 = (const uint32_t *)blk_ptr(c, b2);
		snprintf(desc, desclen, "t_ind[%u][%u][%u]", i, j, k);
		*phys_out = t3[k];
		return 1;
	}

	return 0;
}

static void print_one_lbn_line(struct mkfs_ctx *c, const struct brkfs_inode *in,
			       uint32_t lbn)
{
	char desc[48];
	uint32_t phys;

	if (!resolve_lbn_to_phys(c, in, lbn, &phys, desc, sizeof desc)) {
		vlog("      LBN %5u  (unmapped, missing index?)\n", lbn);
		return;
	}
	vlog("      LBN %5u -> phys %5u  %s\n", lbn, phys, desc);
}

static void print_inode_mapping_verbose(struct mkfs_ctx *c,
					const struct brkfs_inode *in)
{
	const uint32_t bs = c->bs;
	const uint32_t nblk = file_payload_blocks(in->i_size, bs);

	if (nblk == 0) {
		vlog("      (no payload blocks; i_size=%u)\n", in->i_size);
		return;
	}

	vlog("      i_size=%u  payload_blocks=%u  block_size=%u\n", in->i_size,
	     nblk, bs);

	vlog("      i_block[] (inode slots; s_idx/d_top/t_top are index blocks):\n");
	for (uint32_t s = 0; s < BRKFS_BLOCKS; s++) {
		if (in->i_block[s] == 0)
			continue;
		const char *role =
			(s < BRKFS_DIRECT_BLOCKS)	   ? "data" :
			(s == BRKFS_INDIRECT_BLOCK)	   ? "s_idx" :
			(s == BRKFS_DOUBLE_INDIRECT_BLOCK) ? "d_top" :
			(s == BRKFS_TRIPLE_INDIRECT_BLOCK) ? "t_top" :
							     "?";
		vlog("        [%u] phys %u - %s\n", s, in->i_block[s], role);
	}

	const uint32_t max_detail = 80;

	if (nblk <= max_detail) {
		for (uint32_t lbn = 0; lbn < nblk; lbn++)
			print_one_lbn_line(c, in, lbn);
	} else {
		const uint32_t half = max_detail / 2;
		uint32_t lbn;
		for (lbn = 0; lbn < half; lbn++)
			print_one_lbn_line(c, in, lbn);
		vlog("      ...  %u LBNs omitted (LBN %u .. %u)  ...\n",
		     nblk - max_detail, half, nblk - half - 1);
		for (lbn = nblk - half; lbn < nblk; lbn++)
			print_one_lbn_line(c, in, lbn);
	}
}

static void write_inode_at(struct mkfs_ctx *c, uint32_t ino,
			   const struct brkfs_inode *in)
{
	if (ino == 0)
		return;
	uint64_t off = (uint64_t)c->sb->s_inode_start * c->bs +
		       (uint64_t)(ino - 1u) * BRKFS_INODE_BYTES;
	memcpy(c->img + off, in, sizeof(*in));
}

/* Allocate a data block, return the physical block number. */
static uint32_t alloc_data_slot(struct mkfs_ctx *c)
{
	uint32_t bit = find_free_bit(c->data_bmp, c->data_slots, 0);
	set_bit(c->data_bmp, bit);
	return c->sb->s_data_start + bit;
}

/* Allocate payload blocks, copy data, then wire inode block pointers. */
static void store_file(struct mkfs_ctx *c, struct brkfs_inode *ino,
		       const uint8_t *data, uint32_t size)
{
	uint32_t bs = c->bs;
	uint32_t nblk = file_payload_blocks(size, bs);
	uint32_t ptrs = BRKFS_PTRS_PER_BLOCK(bs);

	memset(ino->i_block, 0, sizeof(ino->i_block));

	if (nblk == 0) {
		ino->i_size = size;
		return;
	}

	uint32_t *dblks = xmalloc((size_t)nblk * sizeof(uint32_t));
	uint32_t pos = 0;
	for (uint32_t i = 0; i < nblk; i++) {
		uint32_t db = alloc_data_slot(c);
		dblks[i] = db;
		uint32_t chunk = bs;
		if (size - pos < chunk)
			chunk = size - pos;
		memcpy(blk_ptr(c, db), data + pos, chunk);
		pos += chunk;
	}
	ino->i_size = size;

	if (nblk <= BRKFS_DIRECT_BLOCKS) {
		for (uint32_t i = 0; i < nblk; i++)
			ino->i_block[i] = dblks[i];
		free(dblks);
		return;
	}

	for (uint32_t i = 0; i < BRKFS_DIRECT_BLOCKS; i++)
		ino->i_block[i] = dblks[i];

	uint32_t rem = nblk - BRKFS_DIRECT_BLOCKS;
	uint32_t iblk = alloc_data_slot(c);
	ino->i_block[BRKFS_INDIRECT_BLOCK] = iblk;
	uint32_t *idx = (uint32_t *)blk_ptr(c, iblk);
	memset(idx, 0, bs);

	if (rem <= ptrs) {
		for (uint32_t j = 0; j < rem; j++)
			idx[j] = dblks[BRKFS_DIRECT_BLOCKS + j];
		free(dblks);
		return;
	}

	warn("file too large while writing (needs double indirect)");
	exit(1);
}

static noreturn void usage(void)
{
	fprintf(stderr,
		"usage: mkfs [-q|--quiet] [-v|--verbose] [-b blocksize] [-n inodes] [-d datablocks] image file ...\n");
	exit(2);
}

int main(int argc, char **argv)
{
	uint32_t bs = 4096;
	uint32_t min_inodes = 128;
	uint32_t min_datablocks = 0;

	out_level = OUT_NORMAL;

	int i = 1;
	while (i < argc && argv[i][0] == '-') {
		if (!strcmp(argv[i], "-b") && i + 1 < argc) {
			bs = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-n") && i + 1 < argc) {
			min_inodes = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-d") && i + 1 < argc) {
			min_datablocks = (uint32_t)strtoul(argv[++i], NULL, 0);
		} else if (!strcmp(argv[i], "-v") ||
			   !strcmp(argv[i], "--verbose")) {
			out_level = OUT_VERBOSE;
		} else if (!strcmp(argv[i], "-q") ||
			   !strcmp(argv[i], "--quiet")) {
			out_level = OUT_QUIET;
		} else {
			usage();
		}
		i++;
	}

	if (i >= argc)
		usage();
	const char *outpath = argv[i++];
	int nfiles = argc - i;
	if (nfiles == 0) {
		warn("need at least one file to pack");
		usage();
	}

	if (bs < 512 || bs % 512 != 0 ||
	    bs < BRKFS_SUPER_BLOCK_OFFSET + BRKFS_SUPER_BLOCK_SIZE) {
		warn("invalid block size %u (need >= %u, multiple of 512)", bs,
		     BRKFS_SUPER_BLOCK_OFFSET + BRKFS_SUPER_BLOCK_SIZE);
		return 1;
	}

	struct host_file *files =
		xmalloc((size_t)nfiles * sizeof(struct host_file));
	memset(files, 0, (size_t)nfiles * sizeof(struct host_file));

	for (int f = 0; f < nfiles; f++) {
		files[f].path = argv[i + f];
		files[f].name = strrchr(files[f].path, '/');
		files[f].name = files[f].name ? files[f].name + 1 :
						files[f].path;
		struct stat st;
		if (stat(files[f].path, &st) != 0)
			die(files[f].path);
		if (!S_ISREG(st.st_mode)) {
			warn("not a regular file: %s", files[f].path);
			return 1;
		}
		if (st.st_size > 0x7fffffff) {
			warn("file too large (>2GiB): %s", files[f].path);
			return 1;
		}
		files[f].size = (uint32_t)st.st_size;
		FILE *fp = fopen(files[f].path, "rb");
		if (!fp)
			die(files[f].path);
		files[f].data = xmalloc(files[f].size ? files[f].size : 1);
		if (files[f].size && fread(files[f].data, 1, files[f].size,
					   fp) != files[f].size) {
			warn("short read: %s", files[f].path);
			return 1;
		}
		fclose(fp);
	}

	uint32_t root_sz = root_dir_size(files, nfiles, bs);
	vlog("sizes:\n");
	vlog("  root directory (padded)     %7u bytes\n", root_sz);
	uint32_t data_used = data_blocks_for_file(root_sz, bs);
	vlog("  root on-disk blocks         %7u (payload + index)\n",
	     data_used);
	for (int f = 0; f < nfiles; f++) {
		uint32_t db = data_blocks_for_file(files[f].size, bs);
		vlog("  %-28s  %7u B  ->  %u blocks\n", files[f].name,
		     files[f].size, db);
		data_used += db;
	}
	vlog("  (sum payload+index blocks)  %7u\n", data_used);

	uint32_t n_inodes = 2u + (uint32_t)nfiles;
	if (n_inodes < min_inodes)
		n_inodes = min_inodes;

	uint32_t inode_blocks = div_round_up(n_inodes * BRKFS_INODE_BYTES, bs);
	uint32_t max_ino_bits = bs * 8u;
	uint32_t max_data_bits = bs * 8u;
	if (n_inodes > max_ino_bits) {
		warn("inode count %u exceeds one bitmap block (%u bits)",
		     n_inodes, max_ino_bits);
		return 1;
	}

	uint32_t data_blocks = data_used + 512;
	if (data_blocks < min_datablocks)
		data_blocks = min_datablocks;
	if (data_blocks > max_data_bits)
		data_blocks = max_data_bits;

	vlog("data bitmap capacity: %u blocks (%u free vs minimum %u)\n",
	     data_blocks, data_blocks > data_used ? data_blocks - data_used : 0,
	     data_used);

	if (data_used > data_blocks) {
		warn("need at least %u data blocks; raise -d", data_used);
		return 1;
	}

	uint32_t inode_bmp_blk = 1;
	uint32_t data_bmp_blk = 2;
	uint32_t inode_start = 3;
	uint32_t data_start = inode_start + inode_blocks;
	uint32_t total_blks = data_start + data_blocks;

	uint64_t imgsz = (uint64_t)total_blks * bs;
	uint8_t *img = xmalloc((size_t)imgsz);
	memset(img, 0, (size_t)imgsz);

	struct brkfs_super_block sb = {
		.s_blocksize = bs,
		.s_inode_blocks = inode_blocks,
		.s_data_blocks = data_blocks,
		.s_inode_bitmap_start = inode_bmp_blk,
		.s_data_bitmap_start = data_bmp_blk,
		.s_inode_start = inode_start,
		.s_data_start = data_start,
		.s_magic = BRKFS_MAGIC,
	};
	memcpy(img + BRKFS_SUPER_BLOCK_OFFSET, &sb, sizeof(sb));

	vlog("layout (block size %u):\n", bs);
	vlog("  superblock      byte offset %u  magic %#x\n",
	     (unsigned)BRKFS_SUPER_BLOCK_OFFSET, sb.s_magic);
	vlog("  inode bitmap    block %u\n", sb.s_inode_bitmap_start);
	vlog("  data bitmap     block %u\n", sb.s_data_bitmap_start);
	vlog("  inode table     blocks %u .. %u  (%u blocks)\n",
	     sb.s_inode_start, sb.s_inode_start + sb.s_inode_blocks - 1u,
	     sb.s_inode_blocks);
	vlog("  data area       blocks %u .. %u  (%u slots)\n", sb.s_data_start,
	     sb.s_data_start + sb.s_data_blocks - 1u, sb.s_data_blocks);
	vlog("  image           %u blocks (%" PRIu64 " bytes)\n", total_blks,
	     imgsz);

	struct mkfs_ctx ctx;
	ctx.img = img;
	ctx.bs = bs;
	ctx.sb = (struct brkfs_super_block *)(img + BRKFS_SUPER_BLOCK_OFFSET);
	ctx.inode_bmp = img + (uint64_t)inode_bmp_blk * bs;
	ctx.data_bmp = img + (uint64_t)data_bmp_blk * bs;
	ctx.inodes_capacity = max_ino_bits;
	ctx.data_slots = data_blocks;

	set_bit(ctx.inode_bmp, 1);

	struct brkfs_inode root = { 0 };
	root.i_ino = BRKFS_ROOT_INO;
	root.i_mode = S_IFDIR | 0755;
	root.i_nlink = 2;
	root.i_size = root_sz;
	uint32_t root_off = 0;
	uint8_t *dirbuf = xmalloc(root_sz);
	uint32_t old_root_off = root_off;
	write_dirent(dirbuf, &root_off, 1, DT_DIR, ".", 1);
	old_root_off = root_off;
	write_dirent(dirbuf, &root_off, 1, DT_DIR, "..", 2);
	for (int f = 0; f < nfiles; f++) {
		uint32_t ino = 2u + (uint32_t)f;
		uint8_t nl = (uint8_t)strlen(files[f].name);
		old_root_off = root_off;
		write_dirent(dirbuf, &root_off, ino, DT_REG, files[f].name, nl);
	}
	if (root_off > root_sz) {
		warn("root directory entry length mismatch");
		return 1;
	}
	struct brkfs_dir_entry *last_de =
		(struct brkfs_dir_entry *)(dirbuf + old_root_off);
	assert(last_de->name_len == strlen(files[nfiles - 1].name));
	assert(!memcmp(last_de->name, files[nfiles - 1].name, last_de->name_len));
	last_de->entry_len += root_sz - root_off; /* fix up last dirent */

	store_file(&ctx, &root, dirbuf, root_sz);
	write_inode_at(&ctx, 1, &root);
	free(dirbuf);

	for (int f = 0; f < nfiles; f++) {
		struct brkfs_inode in = { 0 };
		uint32_t ino = 2u + (uint32_t)f;
		in.i_ino = ino;
		in.i_mode = S_IFCHR | 0755;
		in.i_nlink = 1;
		set_bit(ctx.inode_bmp, ino);
		store_file(&ctx, &in, files[f].data, files[f].size);
		write_inode_at(&ctx, ino, &in);
		vlog("  inode %-3u  %-28s  %7u B\n", ino, files[f].name,
		     files[f].size);
		print_inode_mapping_verbose(&ctx, &in);
		free(files[f].data);
	}
	free(files);

	int fd = open(outpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		die(outpath);
	if (write(fd, img, (size_t)imgsz) != (ssize_t)imgsz) {
		warn("short write to %s", outpath);
		return 1;
	}
	close(fd);

	if (out_level >= OUT_NORMAL) {
		char human[48];
		fmt_bytes(human, sizeof human, imgsz);
		uint32_t d_used = count_bitmap_bits(ctx.data_bmp, data_blocks);
		uint32_t i_used =
			count_bitmap_bits(ctx.inode_bmp, ctx.inodes_capacity);
		printf("mkfs: wrote %s: %s, %u blocks * %u B, data %u/%u "
		       "blocks used, inodes %u used, %d files\n",
		       outpath, human, total_blks, bs, d_used, data_blocks,
		       i_used, nfiles);
	}

	free(img);
	return 0;
}
