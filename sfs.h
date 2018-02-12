
#define SFS_MAGIC 0x20180130
#define SFS_JOURNAL_MAGIC 0x01302018

#define SFS_DEFAULT_BLOCK_SIZE 4096
#define SFS_DEFAULT_BLOCK_BITS 12

#define SFS_DEFAULT_MAX_BYTES (512*1024*1024*1024L)
#define SFS_FILENAME_MAXLEN 255
#define SFS_START_INO 10


/**
 * Static reserve inode
 */ 

#define SFS_RESERVED_INODES 3

#ifdef SFS_DEBUG
#define sfs_trace(fmt, ...) {			\
	printk(KERN_ERR "[sfs] %s +%d: " fmt,	\
	       __FILE__, __LINE__, ##__VA_ARGS__);

#define sfs_debug(level, fmt, ...) {			\
	printk(level, "[sfs]:" fmt, ##__VA__ARGS__);	\


#else
#define sfs_trace(fmt, ...) no_printk(fmt, ##__VA_ARGS__)
#define sfs_debug(level, fmt, ...) no_printk(fmt, ##__VA_ARGS__)
#endif

/* Hard-coded inode number for the root dir */
const uint64_t SFS_ROOTDIR_INODE_NUMBER = 1;

/* The disk block for the super block */
const uint64_t SFS_SUPERBLOCK_BLOCK_NUMBER = 0;

/* The disk block for the inode table */
const uint64_t SFS_INODETABLE_BLOCK_NUMBER = 1;

/** Journal Setting */
const uint64_t SFS_JOURNAL_INODE_NUMBER = 2;
const uint64_t SFS_JOURNAL_BLOCK_NUMBER = 2;
const uint64_t SFS_JOURNAL_BLOCK_COUNT= 64 * 256 * 1024;

/* The disk block where the dir entry of root directory */
#define SFS_ROOTDIR_DATABLOCK_NUMBER (SFS_JOURNAL_BLOCK_COUNT + 2)

/* The Start Point of Unreserved Blocks */
#define SFS_LAST_RESERVED_BLOCK SFS_ROOTDIR_DATABLOCK_NUMBER

/* The Start Point of Unreserved Inodes */
#define SFS_LAST_RESERVED_INODE SFS_JOURNAL_INODE_NUMBER

/* The dir entry struct */
struct sfs_dir_entry {
	char filename[SFS_FILENAME_MAXLEN];
	uint64_t inode_no;
};

struct sfs_inode {
	mode_t mode;
	uint64_t inode_no;
	uint64_t start_block_number;
	uint64_t block_count;

	union {
		uint64_t file_size;
		uint64_t dir_children_count;
	};
};

/* FIXME: Maybe not needed */
const uint64_t SFS_MAX_FS_OBJ_SUPPORTED = 64;


struct journal_s;

/* super block definiation */
struct sfs_super_block {
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;

	uint64_t inodes_count;
	uint64_t free_blocks;
	struct journal_s *journal;

	char padding[4048];
};

#ifdef __KERNEL__

#include <linux/aio.h>
#include <linux/pagevec.h>
ssize_t sfs_in_kernel_write(struct kiocb *iocb, const struct iovec *iov,
			    unsigned long nr_segs, loff_t pos);

int sfs_generic_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata);


#endif
