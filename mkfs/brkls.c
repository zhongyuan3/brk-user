/*
 * Host tool: list files in a brkfs image (root directory) or extract one file.
 *
 * Layout must match mkfs.c / brkfs.h.
 */

#include "brkfs.h"

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define BRKFS_PTRS_PER_BLOCK(bs) ((bs) / (uint32_t)sizeof(uint32_t))

struct img {
	uint8_t *base;
	size_t len;
	struct brkfs_super_block *sb;
};

static void warn(const char *fmt, ...)
{
	va_list ap;
	fputs("brkls: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static noreturn void die(const char *msg)
{
	fputs("brkls: ", stderr);
	perror(msg);
	exit(1);
}

static noreturn void usage(void)
{
	fputs("usage: brkls image\n"
	      "       brkls image name   # dump file \"name\" from root to stdout\n",
	      stderr);
	exit(2);
}

static uint32_t div_round_up(uint32_t a, uint32_t b)
{
	return (a + b - 1u) / b;
}

static uint8_t *blk_ptr(struct img *im, uint32_t blk)
{
	return im->base + (uint64_t)blk * im->sb->s_blocksize;
}

/* s_blocks_count may be 0 on images built by an older mkfs; fall back to file size. */
static uint32_t img_total_blocks(struct img *im)
{
	uint32_t bs = im->sb->s_blocksize;
	uint32_t n = im->sb->s_blocks_count;
	if (n != 0u)
		return n;
	if (bs == 0 || im->len < bs)
		return 0;
	return (uint32_t)(im->len / bs);
}

static int blk_valid(struct img *im, uint32_t blk)
{
	uint32_t n = img_total_blocks(im);
	return n != 0u && blk < n;
}

static int read_inode(struct img *im, uint32_t ino, struct brkfs_inode *out)
{
	if (ino < 1 || ino > im->sb->s_inodes_count) {
		warn("inode %u out of range (max %u)", ino,
		     im->sb->s_inodes_count);
		return -1;
	}
	uint32_t bs = im->sb->s_blocksize;
	uint32_t isize = im->sb->s_inode_size;
	if (isize < sizeof(struct brkfs_inode)) {
		warn("inode size %u too small on disk", isize);
		return -1;
	}
	uint64_t off = (uint64_t)im->sb->s_inode_table * bs +
		       (uint64_t)(ino - 1u) * isize;
	if (off + sizeof(struct brkfs_inode) > im->len) {
		warn("inode table read out of bounds");
		return -1;
	}
	memcpy(out, im->base + off, sizeof(*out));
	return 0;
}

/*
 * Read file payload into out[0 .. in->i_size); out must be at least in->i_size.
 * Supports direct blocks and single indirect (same as mkfs).
 */
static int read_file_payload(struct img *im, const struct brkfs_inode *in,
			     uint8_t *out)
{
	uint32_t bs = im->sb->s_blocksize;
	uint32_t sz = in->i_size;
	if (sz == 0)
		return 0;

	uint32_t nblk = div_round_up(sz, bs);
	uint32_t ptrs = BRKFS_PTRS_PER_BLOCK(bs);
	uint32_t pos = 0;

	uint32_t ndir = nblk < BRKFS_DIRECT_BLOCKS ? nblk : BRKFS_DIRECT_BLOCKS;
	for (uint32_t i = 0; i < ndir; i++) {
		uint32_t b = in->i_block[i];
		if (!blk_valid(im, b)) {
			warn("invalid direct block %u", b);
			return -1;
		}
		uint32_t chunk = bs;
		if (sz - pos < chunk)
			chunk = sz - pos;
		memcpy(out + pos, blk_ptr(im, b), chunk);
		pos += chunk;
	}
	if (nblk <= BRKFS_DIRECT_BLOCKS)
		return pos == sz ? 0 : -1;

	uint32_t rem = nblk - BRKFS_DIRECT_BLOCKS;
	if (rem > ptrs) {
		warn("file uses double/triple indirect (not supported by brkls)");
		return -1;
	}

	uint32_t iblk = in->i_block[BRKFS_INDIRECT_BLOCK];
	if (!iblk || !blk_valid(im, iblk)) {
		warn("invalid indirect block %u", iblk);
		return -1;
	}
	uint32_t *idx = (uint32_t *)blk_ptr(im, iblk);
	for (uint32_t j = 0; j < rem; j++) {
		uint32_t b = idx[j];
		if (!blk_valid(im, b)) {
			warn("invalid indirect data block %u", b);
			return -1;
		}
		uint32_t chunk = bs;
		if (sz - pos < chunk)
			chunk = sz - pos;
		memcpy(out + pos, blk_ptr(im, b), chunk);
		pos += chunk;
	}
	return pos == sz ? 0 : -1;
}

static int load_image(const char *path, struct img *im)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		die(path);
	struct stat st;
	if (fstat(fd, &st) != 0)
		die("fstat");
	if (!S_ISREG(st.st_mode)) {
		warn("not a regular file: %s", path);
		close(fd);
		return -1;
	}
	if (st.st_size < (off_t)(BRKFS_SUPER_BLOCK_OFFSET +
				 sizeof(struct brkfs_super_block))) {
		warn("file too small to hold superblock");
		close(fd);
		return -1;
	}
	void *p = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (p == MAP_FAILED)
		die("mmap");
	im->base = p;
	im->len = (size_t)st.st_size;
	im->sb = (struct brkfs_super_block *)((uint8_t *)p +
					      BRKFS_SUPER_BLOCK_OFFSET);

	if (im->sb->s_magic != BRKFS_MAGIC) {
		warn("bad magic (0x%x, expected 0x%x)", im->sb->s_magic,
		     BRKFS_MAGIC);
		munmap(p, im->len);
		return -1;
	}
	uint32_t bs = im->sb->s_blocksize;
	if (bs < BRKFS_SUPER_BLOCK_SIZE || bs % BRKFS_SUPER_BLOCK_SIZE != 0) {
		warn("invalid block size %u", bs);
		munmap(p, im->len);
		return -1;
	}
	if (im->sb->s_blocks_count == 0u)
		warn("s_blocks_count is 0; using file size / block size for bounds");

	uint32_t total_blk = img_total_blocks(im);
	if (total_blk == 0u) {
		warn("cannot determine block count");
		munmap(p, im->len);
		return -1;
	}
	uint64_t expect = (uint64_t)total_blk * bs;
	if (expect > im->len) {
		warn("image truncated: need %llu bytes, have %zu",
		     (unsigned long long)expect, im->len);
		munmap(p, im->len);
		return -1;
	}
	return 0;
}

static const char *mode_str(uint32_t mode)
{
	switch (mode & S_IFMT) {
	case S_IFREG:
		return "-";
	case S_IFDIR:
		return "d";
	case S_IFCHR:
		return "c";
	case S_IFBLK:
		return "b";
	case S_IFIFO:
		return "p";
	case S_IFSOCK:
		return "s";
	case S_IFLNK:
		return "l";
	default:
		return "?";
	}
}

static int list_root(struct img *im)
{
	struct brkfs_inode root;
	if (read_inode(im, BRKFS_ROOT_INO, &root) != 0)
		return 1;
	if ((root.i_mode & S_IFMT) != S_IFDIR) {
		warn("root inode is not a directory");
		return 1;
	}
	uint8_t *buf = malloc(root.i_size ? root.i_size : 1);
	if (!buf) {
		warn("malloc");
		return 1;
	}
	if (read_file_payload(im, &root, buf) != 0) {
		free(buf);
		return 1;
	}

	int name_len_max = 0;
	for (uint32_t off = 0; off < root.i_size;) {
		struct brkfs_dir_entry *de =
			(struct brkfs_dir_entry *)(buf + off);
		uint32_t el = de->entry_len;
		if (el < BRKFS_DIR_ENTRY_MIN_LEN || el > root.i_size - off) {
			warn("bad dirent entry_len=%u at offset %u", el, off);
			break;
		}
		if (de->name_len > name_len_max)
			name_len_max = de->name_len;
		off += el;
	}

	printf("%-6s %-8s %-*s %s\n", "ino", "bytes", name_len_max, "name",
	       "type");
	for (uint32_t off = 0; off < root.i_size;) {
		if (off + sizeof(struct brkfs_dir_entry) > root.i_size) {
			warn("directory truncated at offset %u", off);
			break;
		}
		struct brkfs_dir_entry *de =
			(struct brkfs_dir_entry *)(buf + off);
		uint32_t el = de->entry_len;
		if (el < BRKFS_DIR_ENTRY_MIN_LEN || el > root.i_size - off) {
			warn("bad dirent entry_len=%u at offset %u", el, off);
			break;
		}
		if (de->name_len > el - 8u) {
			warn("bad dirent name_len at offset %u", off);
			break;
		}

		struct brkfs_inode in = { 0 };
		uint32_t sz = 0;
		const char *ms = "???";
		if (read_inode(im, de->inode, &in) == 0) {
			sz = in.i_size;
			ms = mode_str(in.i_mode);
		}
		printf("%-6u %-8u %-*.*s %s\n", de->inode, sz, name_len_max,
		       (int)de->name_len, de->name, ms);

		off += el;
	}
	free(buf);
	return 0;
}

static int cat_file(struct img *im, const char *name)
{
	struct brkfs_inode root;
	if (read_inode(im, BRKFS_ROOT_INO, &root) != 0)
		return 1;
	if ((root.i_mode & S_IFMT) != S_IFDIR) {
		warn("root inode is not a directory");
		return 1;
	}
	uint8_t *buf = malloc(root.i_size ? root.i_size : 1);
	if (!buf) {
		warn("malloc");
		return 1;
	}
	if (read_file_payload(im, &root, buf) != 0) {
		free(buf);
		return 1;
	}

	size_t name_len = strlen(name);
	if (name_len > BRKFS_NAME_LEN) {
		warn("name too long");
		free(buf);
		return 1;
	}
	uint32_t target_ino = 0;
	for (uint32_t off = 0; off < root.i_size;) {
		struct brkfs_dir_entry *de =
			(struct brkfs_dir_entry *)(buf + off);
		uint32_t el = de->entry_len;
		if (el < BRKFS_DIR_ENTRY_MIN_LEN || el > root.i_size - off) {
			warn("corrupt root directory");
			free(buf);
			return 1;
		}
		if (de->name_len == (uint8_t)name_len &&
		    memcmp(de->name, name, name_len) == 0) {
			target_ino = de->inode;
			break;
		}
		off += el;
	}
	free(buf);

	if (target_ino == 0) {
		warn("no file named \"%s\"", name);
		return 1;
	}

	struct brkfs_inode in;
	if (read_inode(im, target_ino, &in) != 0)
		return 1;
	uint32_t fmt = in.i_mode & S_IFMT;
	if (fmt != S_IFREG && fmt != S_IFCHR) {
		warn("\"%s\" is not a regular file", name);
		return 1;
	}
	if (fmt == S_IFCHR)
		warn("\"%s\" has chr mode (legacy image?); dumping raw data",
		     name);
	uint8_t *data = malloc(in.i_size ? in.i_size : 1);
	if (!data) {
		warn("malloc");
		return 1;
	}
	if (read_file_payload(im, &in, data) != 0) {
		free(data);
		return 1;
	}
	if (in.i_size) {
		if (fwrite(data, 1, in.i_size, stdout) != in.i_size) {
			warn("short write to stdout");
			free(data);
			return 1;
		}
	}
	free(data);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2 && argc != 3)
		usage();

	const char *imgpath = argv[1];
	const char *name = (argc == 3) ? argv[2] : NULL;

	struct img im;
	if (load_image(imgpath, &im) != 0)
		return 1;

	int rc;
	if (name)
		rc = cat_file(&im, name);
	else
		rc = list_root(&im);

	munmap(im.base, im.len);
	return rc;
}
