#ifndef BRKFS_H
#define BRKFS_H

#include <stdint.h>

#define BRKFS_DIRECT_BLOCKS 7 /* Total number of direct block pointers */
#define BRKFS_INDIRECT_BLOCK \
	BRKFS_DIRECT_BLOCKS /* Single indirect block pointer index */
#define BRKFS_DOUBLE_INDIRECT_BLOCK \
	(BRKFS_INDIRECT_BLOCK + 1) /* Double indirect block pointer index */
#define BRKFS_TRIPLE_INDIRECT_BLOCK    \
	(BRKFS_DOUBLE_INDIRECT_BLOCK + \
	 1) /* Triple indirect block pointer index */
#define BRKFS_BLOCKS \
	(BRKFS_TRIPLE_INDIRECT_BLOCK + 1) /* Total number of block pointers */
#define BRKFS_ROOT_INO 1
#define BRKFS_MAGIC 0x6b7262

#define BRKFS_SUPER_BLOCK_OFFSET 1024
#define BRKFS_SUPER_BLOCK_SIZE 1024

#define BRKFS_DIR_ENTRY_MIN_LEN 12

#define BRKFS_NAME_LEN 255

struct brkfs_super_block {
	uint32_t s_blocksize;
	uint32_t s_inode_blocks;
	uint32_t s_data_blocks;
	uint32_t s_inode_bitmap_start;
	uint32_t s_data_bitmap_start;
	uint32_t s_inode_start;
	uint32_t s_data_start;
	uint32_t s_magic;
};

struct brkfs_inode {
	uint32_t i_ino;
	uint32_t i_mode;
	uint32_t i_rdev;
	uint32_t i_flags;
	uint32_t i_nlink;
	uint32_t i_size;
	uint32_t i_block[BRKFS_BLOCKS];
};

struct brkfs_dir_entry {
	uint32_t inode;
	uint16_t entry_len;
	uint8_t name_len;
	uint8_t file_type;
	char name[];
};

#endif
