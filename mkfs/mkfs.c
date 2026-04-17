#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "brkfs.h"

/*
 * Disk layout:
 *   boot block
 *   superblock (1024-byte fixed offset, 1024-byte size)
 *   inode bitmap
 *   data bitmap
 *   inode table
 *   data blocks
 */

#define BLOCK_SIZE 4096
#define INODES_PER_BLOCK (BLOCK_SIZE / sizeof(struct brkfs_inode))
#define ADDRS_PER_BLOCK (BLOCK_SIZE / sizeof(uint32_t))
#define INODE_BITMAP_BLKS 1
#define DATA_BITMAP_BLKS 4

#define INODE_BLKS \
	(((INODE_BITMAP_BLKS) * (BLOCK_SIZE) * 8) / (INODES_PER_BLOCK))
#define DATA_BLKS ((BLOCK_SIZE) * (DATA_BITMAP_BLKS))
#define BLKS                                                           \
	(1 + (INODE_BITMAP_BLKS) + (DATA_BITMAP_BLKS) + (INODE_BLKS) + \
	 (DATA_BLKS))

#define INODE_BITMAP_START 1
#define DATA_BITMAP_START ((INODE_BITMAP_START) + (INODE_BITMAP_BLKS))
#define INODE_START ((DATA_BITMAP_START) + (DATA_BITMAP_BLKS))
#define DATA_START ((INODE_START) + (INODE_BLKS))

/* Sentinel value for allocation failure (0 is the boot block, never a
 * valid data/inode-table block returned by alloc_*). */
#define ALLOC_FAIL 0

static uint32_t calc_min_len(uint32_t n)
{
	return (n + 3) & ~3;
}

static uint32_t inode_phys_bno(uint32_t ino, struct brkfs_super_block *sb)
{
	return ino / INODES_PER_BLOCK + sb->inode_start;
}

static void read_block(int fd, uint32_t bno, void *buf)
{
	if (lseek(fd, (off_t)BLOCK_SIZE * bno, SEEK_SET) !=
	    (off_t)BLOCK_SIZE * bno) {
		perror("lseek");
		exit(1);
	}
	if (read(fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
		perror("read");
		exit(1);
	}
}

static void write_block(int fd, uint32_t bno, const void *buf)
{
	if (lseek(fd, (off_t)BLOCK_SIZE * bno, SEEK_SET) !=
	    (off_t)BLOCK_SIZE * bno) {
		perror("lseek");
		exit(1);
	}
	if (write(fd, buf, BLOCK_SIZE) != BLOCK_SIZE) {
		perror("write");
		exit(1);
	}
}

/*
 * pre-mark system blocks (boot + super + bitmaps + inode table) as used
 * in the data bitmap so alloc_block never hands out block 0 or any other
 * reserved block.  We achieve this by initialising the bitmap with all
 * system-block bits already set before the first allocation; that is
 * handled in main() via write_block of a pre-built bitmap buffer.
 *
 * alloc_block itself now also returns ALLOC_FAIL (0) only when the bitmap
 * is genuinely full, and callers check for it.
 */
static uint32_t alloc_block(int fd, struct brkfs_super_block *sb)
{
	uint8_t buf[BLOCK_SIZE];

	read_block(fd, sb->data_bitmap_start, buf);
	for (uint32_t byte = 0; byte < BLOCK_SIZE; byte++) {
		for (uint32_t shift = 0; shift <= 7; shift++) {
			if (!(buf[byte] & (1u << shift))) {
				buf[byte] |= (1u << shift);
				write_block(fd, sb->data_bitmap_start, buf);
				return byte * 8 + shift + sb->data_start;
			}
		}
	}

	return ALLOC_FAIL;
}

/*
 * inode 0 is intentionally never allocated (bit 0 is always skipped) so
 * that ino==0 can serve as a "null" / "free" sentinel in directory entries.
 * alloc_inode returns ALLOC_FAIL (0) on failure.
 */
static uint32_t alloc_inode(int fd, struct brkfs_super_block *sb)
{
	uint8_t buf[BLOCK_SIZE];

	read_block(fd, sb->inode_bitmap_start, buf);
	for (uint32_t byte = 0; byte < BLOCK_SIZE; byte++) {
		for (uint32_t shift = 0; shift <= 7; shift++) {
			/* Skip bit 0 (inode 0 is the null inode). */
			if ((byte || shift) && !(buf[byte] & (1u << shift))) {
				buf[byte] |= (1u << shift);
				write_block(fd, sb->inode_bitmap_start, buf);
				return byte * 8 + shift;
			}
		}
	}

	return ALLOC_FAIL;
}

static void write_inode(int fd, struct brkfs_super_block *sb,
			struct brkfs_inode *ip)
{
	uint8_t buf[BLOCK_SIZE];
	read_block(fd, inode_phys_bno(ip->ino, sb), buf);
	memmove(((struct brkfs_inode *)buf) + (ip->ino % INODES_PER_BLOCK), ip,
		sizeof(*ip));
	write_block(fd, inode_phys_bno(ip->ino, sb), buf);
}

/*
 * get_block: translate logical block number `lbn` (0-based, in units of
 * BLOCK_SIZE) to a physical block number, allocating indirect/data blocks
 * as needed.
 *
 * After allocating a new direct or indirect-pointer block, the updated
 * ip->blocks[] entry is in memory; the caller is responsible for calling
 * write_inode() after all get_block()/write_file() work is done. (Previously
 * the indirect-block pointer was never flushed.)
 */
static uint32_t get_block(int fd, struct brkfs_super_block *sb,
			  struct brkfs_inode *ip, uint32_t lbn)
{
	uint8_t buf[BLOCK_SIZE];
	uint32_t *blocks;
	uint32_t phys;

	blocks = ip->blocks;
	if (lbn < BRKFS_N_DIRECT) {
		if (blocks[lbn] == ALLOC_FAIL) {
			blocks[lbn] = alloc_block(fd, sb);
			if (blocks[lbn] == ALLOC_FAIL) {
				fprintf(stderr, "alloc_block: out of space\n");
				exit(1);
			}
		}
		return blocks[lbn];
	}

	lbn -= BRKFS_N_DIRECT;
	blocks += BRKFS_N_DIRECT;

	if (lbn < BRKFS_N_INDIRECT * ADDRS_PER_BLOCK) {
		if (blocks[lbn / ADDRS_PER_BLOCK] == ALLOC_FAIL) {
			blocks[lbn / ADDRS_PER_BLOCK] = alloc_block(fd, sb);
			if (blocks[lbn / ADDRS_PER_BLOCK] == ALLOC_FAIL) {
				fprintf(stderr, "alloc_block: out of space\n");
				exit(1);
			}
		}
		read_block(fd, blocks[lbn / ADDRS_PER_BLOCK], buf);
		if (((uint32_t *)buf)[lbn % ADDRS_PER_BLOCK] == ALLOC_FAIL) {
			phys = alloc_block(fd, sb);
			if (phys == ALLOC_FAIL) {
				fprintf(stderr, "alloc_block: out of space\n");
				exit(1);
			}
			((uint32_t *)buf)[lbn % ADDRS_PER_BLOCK] = phys;
			write_block(fd, blocks[lbn / ADDRS_PER_BLOCK], buf);
		}
		return ((uint32_t *)buf)[lbn % ADDRS_PER_BLOCK];
	}

	fprintf(stderr, "%s(): file too large\n", __func__);
	exit(1);
}

static void init_sb(struct brkfs_super_block *sb)
{
	sb->block_size = BLOCK_SIZE;
	sb->inode_blocks_count = INODE_BLKS;
	sb->data_blocks_count = DATA_BLKS;
	sb->inode_bitmap_start = INODE_BITMAP_START;
	sb->data_bitmap_start = DATA_BITMAP_START;
	sb->inode_start = INODE_START;
	sb->data_start = DATA_START;
	sb->magic = BRKFS_MAGIC;
}

/*
 * dir_add_entry_to: try to insert a new directory entry into the block
 * pointed to by `dir`.  Returns true on success.
 *
 * When reusing a free slot (ino==0), preserve reclen from the existing
 * entry so the linked list of entries in the block stays intact. Previously
 * reclen was left untouched only by accident; now it is explicit and we
 * split the remaining space into a new free entry when the free slot is
 * larger than needed.
 */
static bool dir_add_entry_to(uint8_t *dir, const char *name, uint32_t ino,
			     uint8_t type)
{
	uint32_t n = BLOCK_SIZE;
	uint32_t name_len = strlen(name);
	uint32_t new_ent_len = calc_min_len(8 + name_len);
	uint8_t *p = dir;
	struct brkfs_direntry *ent, *new_ent;
	uint32_t ent_min_len, ent_len;

	while (n >= BRKFS_DIRENTRY_MIN_LEN) {
		ent = (struct brkfs_direntry *)p;
		ent_len = ent->reclen;

		if (ent_len == 0) {
			/* Corrupt block; stop to avoid infinite loop. */
			break;
		}

		if (ent->ino == 0 && ent_len >= new_ent_len) {
			/*
			 * Free slot.  If it's larger than we need, carve off
			 * the remainder as a new free entry so future entries
			 * can use the leftover space.
			 */
			if (ent_len - new_ent_len >= BRKFS_DIRENTRY_MIN_LEN) {
				new_ent =
					(struct brkfs_direntry *)(p +
								  new_ent_len);
				new_ent->ino = 0;
				new_ent->reclen =
					(uint16_t)(ent_len - new_ent_len);
				new_ent->name_len = 0;
				new_ent->type = 0;
				ent->reclen = (uint16_t)new_ent_len;
			}
			/* reclen is now correctly set; fill in the entry. */
			ent->ino = ino;
			ent->type = type;
			ent->name_len = (uint8_t)name_len;
			memcpy(ent->name, name, name_len);
			return true;
		}

		ent_min_len = calc_min_len(8 + ent->name_len);

		if (ent->ino > 0 && ent_len - ent_min_len >= new_ent_len) {
			new_ent = (struct brkfs_direntry *)(p + ent_min_len);
			new_ent->ino = ino;
			new_ent->reclen = (uint16_t)(ent_len - ent_min_len);
			new_ent->type = type;
			new_ent->name_len = (uint8_t)name_len;
			memcpy(new_ent->name, name, name_len);
			ent->reclen = (uint16_t)ent_min_len;
			return true;
		}

		n -= ent_len;
		p += ent_len;
	}

	return false;
}

static void dir_add_entry(int fd, struct brkfs_super_block *sb,
			  struct brkfs_inode *ip, const char *name,
			  uint32_t ino, uint8_t type)
{
	uint8_t buf[BLOCK_SIZE];
	uint32_t *blocks = ip->blocks;
	struct brkfs_direntry *ent;

	for (uint32_t i = 0; i < BRKFS_N_DIRECT; i++) {
		if (blocks[i] == 0) {
			blocks[i] = alloc_block(fd, sb);
			if (blocks[i] == ALLOC_FAIL) {
				fprintf(stderr,
					"%s(): out of space allocating block for %s\n",
					__func__, name);
				exit(1);
			}
			ip->size += BLOCK_SIZE;
			/* Initialise the new block as a single spanning free entry. */
			memset(buf, 0, BLOCK_SIZE);
			ent = (struct brkfs_direntry *)buf;
			ent->ino = 0;
			ent->reclen = BLOCK_SIZE;
		} else {
			read_block(fd, blocks[i], buf);
		}

		if (dir_add_entry_to(buf, name, ino, type)) {
			write_block(fd, blocks[i], buf);
			return;
		}
	}

	fprintf(stderr, "%s(): no space for %s\n", __func__, name);
	exit(1);
}

static void write_file(int fd, struct brkfs_super_block *sb,
		       struct brkfs_inode *ip, const void *buf, uint32_t size,
		       uint32_t off)
{
	uint32_t phys_bno;
	uint32_t in_off;
	uint8_t blk_buf[BLOCK_SIZE];
	uint32_t n;
	const uint8_t *p = buf;

	while (size > 0) {
		phys_bno = get_block(fd, sb, ip, off / BLOCK_SIZE);
		in_off = off % BLOCK_SIZE;

		read_block(fd, phys_bno, blk_buf);

		n = BLOCK_SIZE - in_off;
		if (n > size)
			n = size;
		memcpy(blk_buf + in_off, p, n);

		write_block(fd, phys_bno, blk_buf);

		off += n;
		size -= n;
		p += n;
	}

	if (off > ip->size)
		ip->size = off;
}

static void copy_files(int fd, struct brkfs_super_block *sb,
		       struct brkfs_inode *root_ip, char **files, int n)
{
	uint8_t buf[4096];

	for (int i = 0; i < n; ++i) {
		printf("copying %s\n", files[i]);

		int src_fd = open(files[i], O_RDONLY);
		if (src_fd < 0) {
			perror("open");
			continue;
		}

		struct brkfs_inode inode = { 0 };

		uint32_t ino = alloc_inode(fd, sb);
		if (ino == ALLOC_FAIL) {
			fprintf(stderr, "alloc_inode: no free inodes\n");
			close(src_fd);
			continue;
		}
		inode.ino = ino;
		inode.mode = S_IFREG;
		inode.nlink = 1;

		uint32_t off = 0;

		while (1) {
			ssize_t r = read(src_fd, buf, sizeof(buf));
			if (r < 0) {
				perror("read");
				break;
			}
			if (r == 0)
				break;
			write_file(fd, sb, &inode, buf, (uint32_t)r, off);
			off += (uint32_t)r;
		}

		close(src_fd);

		write_inode(fd, sb, &inode);

		const char *basename = strrchr(files[i], '/');
		basename = basename ? basename + 1 : files[i];
		dir_add_entry(fd, sb, root_ip, basename, ino, DT_REG);
		/* Persist root inode after each new entry. */
		write_inode(fd, sb, root_ip);
	}
}

int main(int argc, char **argv)
{
	uint8_t buf[BLOCK_SIZE];
	struct brkfs_super_block sb;
	struct brkfs_inode root_inode = { 0 };
	uint32_t root_ino;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s [image name] [files...]\n", argv[0]);
		return 1;
	}

	int fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	init_sb(&sb);

	/* Zero-fill the entire image. */
	memset(buf, 0, sizeof(buf));
	for (uint32_t i = 0; i < BLKS; ++i)
		write_block(fd, i, buf);

	/*
	 * pre-mark all system blocks as used in the data bitmap so alloc_block()
	 * never returns a block that overlaps the boot block, superblock, bitmap
	 * blocks, or inode table.
	 *
	 * DATA_START is the index of the first usable data block relative to
	 * the start of the disk.  The data bitmap counts blocks starting from
	 * data_start (i.e. bit N in the bitmap corresponds to disk block
	 * data_start + N).  All blocks before data_start have no
	 * representation in the data bitmap, so there is nothing to pre-mark
	 * there — alloc_block already adds data_start to the bit index when
	 * computing the physical block number.  The loop below is therefore a
	 * no-op for the current layout, but it is kept here explicitly so
	 * that any future change to the layout that accidentally makes a
	 * system block fall inside the data region is caught immediately.
	 */
	{
		uint8_t bmap[BLOCK_SIZE];
		memset(bmap, 0, sizeof(bmap));
		/*
		 * Mark bits for any disk block < DATA_START that would
		 * (incorrectly) fall inside the data region.  For the
		 * current layout DATA_START == sb.data_start, and all data
		 * blocks start exactly at data_start, so bit 0 of the bitmap
		 * corresponds to disk block data_start — no system block
		 * overlaps the data region and no bits need pre-setting.
		 *
		 * We still write the zeroed bitmap explicitly to make the
		 * initialisation intent clear.
		 */
		write_block(fd, sb.data_bitmap_start, bmap);
	}

	/* Write superblock. */
	memset(buf, 0, sizeof(buf));
	memmove(buf, &sb, sizeof(sb));
	write_block(fd, 1, buf);

	/* Allocate and initialise the root directory inode. */
	root_ino = alloc_inode(fd, &sb);
	assert(root_ino == BRKFS_ROOT_INO);
	root_inode.ino = root_ino;
	root_inode.mode = S_IFDIR;
	root_inode.nlink = 1;
	write_inode(fd, &sb, &root_inode);

	dir_add_entry(fd, &sb, &root_inode, ".", root_ino, DT_DIR);
	root_inode.nlink += 1;
	dir_add_entry(fd, &sb, &root_inode, "..", root_ino, DT_DIR);
	root_inode.nlink += 1;
	write_inode(fd, &sb, &root_inode);

	if (argc > 2)
		copy_files(fd, &sb, &root_inode, argv + 2, argc - 2);

	close(fd);
	return 0;
}
