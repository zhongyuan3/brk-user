#ifndef BRKFS_H
#define BRKFS_H

#include <stdint.h>

#define BRKFS_N_DIRECT 6
#define BRKFS_N_INDIRECT 4
#define BRKFS_N_BLOCKS (BRKFS_N_DIRECT + BRKFS_N_INDIRECT)
#define BRKFS_ROOT_INO 1
#define BRKFS_MAGIC 0x6b7262

struct brkfs_super_block {
	uint32_t block_size;
	uint32_t inode_blocks_count;
	uint32_t data_blocks_count;
	uint32_t inode_bitmap_start;
	uint32_t data_bitmap_start;
	uint32_t inode_start;
	uint32_t data_start;
	uint32_t magic;
};

struct brkfs_inode {
	uint32_t ino;
	uint32_t mode;
	uint32_t rdev;
	uint32_t flags;
	uint32_t nlink;
	uint32_t size;
	uint32_t blocks[BRKFS_N_BLOCKS];
};

struct brkfs_direntry {
	uint32_t ino;
	uint16_t reclen;
	uint8_t name_len;
	uint8_t type;
	char name[];
};

#define BRKFS_DIRENTRY_MIN_LEN 12

#endif
