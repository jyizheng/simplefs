#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <stdbool.h>

#include "sfs.h"
#include "alloc_table.h"

#define ROOT_OBJ_COUNT 3
#define DB_OBJ_COUNT 11
#define DEV_OBJ_COUNT 1

static int write_superblock(int fd, int total_inodes, bool update)
{
	struct sfs_super_block sb = {
		.version = 1,
		.magic = SFS_MAGIC,
		.block_size = SFS_DEFAULT_BLOCK_SIZE,
		.inodes_count = total_inodes,
		/* FIXME: Free block management is not implemented yet */
		.free_blocks = (~0) & ~(1 << SFS_LAST_RESERVED_BLOCK),

	};

	ssize_t ret;

	if (update == true) {
		lseek(fd, 0, SEEK_SET);
	}

	ret = write(fd, &sb, sizeof(sb));
	if (ret != SFS_DEFAULT_BLOCK_SIZE) {
		printf("bytes written [%ld] are not equal"
		       " to the default block size\n", ret);
	}
	printf("Super block written successfully\n");
	return 0;
}

static int write_root_inode(int fd, int *total_inodes)
{
	ssize_t ret;
	struct sfs_inode root_inode;

	root_inode.mode = S_IFDIR;
	root_inode.inode_no = SFS_ROOTDIR_INODE_NUMBER;
	root_inode.start_block_number = SFS_ROOTDIR_DATABLOCK_NUMBER;
	root_inode.dir_children_count = ROOT_OBJ_COUNT;
	root_inode.block_count = 1;

	ret = write(fd, &root_inode, sizeof(root_inode));

	if (ret != sizeof(root_inode)) {
		printf("The root inode was not written properly\n");
		return -1;
	}

	*total_inodes += 1;
	printf("Root dir inode written successfully\n");
	printf("total_inodes=%d\n", *total_inodes);
	return 0;
}

static int write_journal_inode(int fd, int *total_inodes)
{
	ssize_t ret;

	struct sfs_inode journal;

	journal.inode_no = SFS_JOURNAL_INODE_NUMBER;
	journal.start_block_number = SFS_JOURNAL_BLOCK_NUMBER;
	journal.block_count = SFS_JOURNAL_BLOCK_COUNT;
	
	ret = write(fd, &journal, sizeof(journal));

	if (ret != sizeof(journal)) {
		printf("Error while writing journal inode\n");
	}

	*total_inodes += 1;
	printf("Journal inode written successfully\n");
	printf("total_inodes=%d\n", *total_inodes);
	return 0;
}

static int write_db_dir_inode(int fd, int *total_inodes)
{
	ssize_t ret;
	struct sfs_inode db_inode;

	db_inode.mode = S_IFDIR;
	db_inode.inode_no = FT_DB_DIR_INODE_NUMBER;
	db_inode.start_block_number = FT_DB_DIR_DATABLOCK_NUMBER;
	db_inode.dir_children_count = DB_OBJ_COUNT;
	db_inode.block_count = 1;

	ret = write(fd, &db_inode, sizeof(db_inode));

	if (ret != sizeof(db_inode)) {
		printf("The root inode was not written properly\n");
		return -1;
	}

	*total_inodes += 1;
	printf("DB dir inode written successfully\n");
	printf("total_inodes=%d\n", *total_inodes);
	return 0;
}

static int write_dev_dir_inode(int fd, int *total_inodes)
{
	ssize_t ret;
	struct sfs_inode dev_inode;

	dev_inode.mode = S_IFDIR;
	dev_inode.inode_no = FT_DEV_DIR_INODE_NUMBER;
	dev_inode.start_block_number = FT_DEV_DIR_DATABLOCK_NUMBER;
	dev_inode.dir_children_count = DEV_OBJ_COUNT;
	dev_inode.block_count = 1;

	ret = write(fd, &dev_inode, sizeof(dev_inode));

	if (ret != sizeof(dev_inode)) {
		printf("The root inode was not written properly\n");
		return -1;
	}

	*total_inodes += 1;
	printf("Dev dir inode written successfully\n");
	printf("total_inodes=%d\n", *total_inodes);
	return 0;
}


static int write_temp_dir_inode(int fd, int *total_inodes)
{
	ssize_t ret;
	struct sfs_inode temp_inode;

	temp_inode.mode = S_IFDIR;
	temp_inode.inode_no = FT_TEMP_DIR_INODE_NUMBER;
	temp_inode.start_block_number = FT_TEMP_DIR_DATABLOCK_NUMBER;
	temp_inode.dir_children_count = 0;
	temp_inode.block_count = 1;

	ret = write(fd, &temp_inode, sizeof(temp_inode));

	if (ret != sizeof(temp_inode)) {
		printf("The root inode was not written properly\n");
		return -1;
	}

	*total_inodes += 1;
	printf("Temp dir inode written successfully\n");
	printf("total_inodes=%d\n", *total_inodes);
	return 0;
}

static int write_db_files_inode(int fd, const struct sfs_inode *i,
				int count, int *total_inodes)
{
	off_t nbytes;
	ssize_t ret;

	ret = write(fd, i, sizeof(*i) * count);
	if (ret != sizeof(*i) * count) {
		printf("The DB files inode was not written properly\n");
		return -1;
	}
	
	*total_inodes += count;
	printf("DB files inode written successfully\n");
	printf("total_inodes=%d\n", *total_inodes);
	return 0;
}

static int write_dev_null_inode(int fd, int *total_inodes)
{
	ssize_t ret;
	struct sfs_inode dev_null_inode;

	dev_null_inode.mode = S_IFREG;
	dev_null_inode.inode_no = DEV_NULL_INODE_NUMBER;
	dev_null_inode.start_block_number = DEV_NULL_DATABLOCK_NUMBER;
	dev_null_inode.block_count = 1;
	dev_null_inode.file_size = 0;

	ret = write(fd, &dev_null_inode, sizeof(dev_null_inode));

	if (ret != sizeof(dev_null_inode)) {
		printf("The dev null inode was not written properly\n");
		return -1;
	}

	*total_inodes += 1;
	printf("Dev null inode written successfully\n");
	printf("total_inodes=%d\n", *total_inodes);
	return 0;
}


static int sfs_inode_table_lseek(int fd, int total_inode) {

	off_t nbytes = SFS_DEFAULT_BLOCK_SIZE -
				sizeof(struct sfs_inode) * total_inode;
	int ret = lseek(fd, nbytes, SEEK_CUR);

	printf("%s: nbytes=%ld, total_inode=%d\n", __func__, nbytes, total_inode);

	if (ret == (off_t) -1) {
		printf("The padding bytes are not written properly\n");
		return 1;
	}
	printf("Inode table padding bytes written sucessfully\n");
	return 0;
}

int write_journal(int fd)
{
	ssize_t ret;
	off_t offset;

	ret = lseek(fd, SFS_DEFAULT_BLOCK_SIZE * SFS_JOURNAL_BLOCK_COUNT, SEEK_CUR);
	if (ret == (off_t) -1) {
		printf("Cannot write journal\n");
		return -1;
	}
	
	//printf("ret=%"PRIu64"\n", ret);

	offset = lseek( fd, 0, SEEK_CUR ) ;

	if ((offset>>12) != 64*256*1024L + 2) {
		printf("offset=%ld\n", offset);
	}

	printf("Journal written successfully\n");
	return 0;
}

int write_dirent(int fd, const struct sfs_dir_entry *entry, int count)
{
	ssize_t nbytes = sizeof(*entry) * count;
	ssize_t ret;


	//printf("ret=%"PRIu64"\n", ret);

	off_t offset = lseek( fd, 0, SEEK_CUR ) ;

	printf("write_dirent: offset=%ld\n", offset >> 12);
	printf("DEV_DIR_BLK_NO=%ld\n", FT_DEV_DIR_DATABLOCK_NUMBER);
	printf("DB_DIR_BLK_NO=%ld\n", FT_DB_DIR_DATABLOCK_NUMBER);
	printf("TEMP_DIR_BLK_NO=%ld\n", FT_TEMP_DIR_DATABLOCK_NUMBER);

	if ((offset>>12) != 64*256*1024L + 2) {
		printf("wrong offset=%ld\n", offset);
	}

	ret = write(fd, entry, nbytes);
	if (ret != nbytes) {
		printf("Writing the root dir entry has failed\n");
		return -1;
	}

	printf("Dir entry written successfully\n");

	nbytes = SFS_DEFAULT_BLOCK_SIZE - sizeof(*entry) * count;
	ret = lseek(fd, nbytes, SEEK_CUR);

	if (ret == (off_t) -1) {
		printf("Writing the padding for root dir has failed\n");
		return -1;
	}

	printf("Padding after the root dir entry written successfully\n");
	return 0;
}

int write_block(int fd, char *block, size_t len)
{
	return 0;
}

int main(int argc, char *argv[])
{
	int fd;
	ssize_t ret;
	int total_inodes = 0;

	struct sfs_inode db_inodes[DB_OBJ_COUNT] = {
		{ .mode = S_IFREG,
		  .inode_no = FT_DATA_INODE_NUMBER,
		  .start_block_number = FT_DATA_DATABLOCK_NUMBER,
		  .block_count = FT_DATA_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = FT_META_INODE_NUMBER,
		  .start_block_number = FT_META_DATABLOCK_NUMBER,
		  .block_count = FT_META_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = FT_LOG_INODE_NUMBER,
		  .start_block_number = FT_LOG_DATABLOCK_NUMBER,
		  .block_count = FT_LOG_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = TOKU_DIR_INODE_NUMBER,
		  .start_block_number = TOKU_DIR_DATABLOCK_NUMBER,
		  .block_count = TOKU_DIR_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = TOKU_ENV_INODE_NUMBER,
		  .start_block_number = TOKU_ENV_DATABLOCK_NUMBER,
		  .block_count = TOKU_ENV_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = TOKU_LOCK_DATA_INODE_NUMBER,
		  .start_block_number = TOKU_LOCK_DATA_DATABLOCK_NUMBER,
		  .block_count = TOKU_LOCK_DATA_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = TOKU_LOCK_ENV_INODE_NUMBER,
		  .start_block_number = TOKU_LOCK_ENV_DATABLOCK_NUMBER,
		  .block_count = TOKU_LOCK_ENV_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = TOKU_LOCK_LOGS_INODE_NUMBER,
		  .start_block_number = TOKU_LOCK_LOGS_DATABLOCK_NUMBER,
		  .block_count = TOKU_LOCK_LOGS_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = TOKU_LOCK_RECV_INODE_NUMBER,
		  .start_block_number = TOKU_LOCK_RECV_DATABLOCK_NUMBER,
		  .block_count = TOKU_LOCK_RECV_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = TOKU_LOCK_TEMP_INODE_NUMBER,
		  .start_block_number = TOKU_LOCK_TEMP_DATABLOCK_NUMBER,
		  .block_count = TOKU_LOCK_TEMP_BLOCK_COUNT,
		  .file_size = 0,
		},
		{ .mode = S_IFREG,
		  .inode_no = TOKU_ROLLBACK_INODE_NUMBER,
		  .start_block_number = TOKU_ROLLBACK_DATABLOCK_NUMBER,
		  .block_count = TOKU_ROLLBACK_BLOCK_COUNT,
		  .file_size = 0,
		}
	};

	struct sfs_dir_entry root_entries[ROOT_OBJ_COUNT] = {
		{.filename = "db",	.inode_no = FT_DB_DIR_INODE_NUMBER},
		{.filename = "dev",	.inode_no = FT_DEV_DIR_INODE_NUMBER},
		{.filename = "tmp",	.inode_no = FT_TEMP_DIR_INODE_NUMBER}
	};
	
	struct sfs_dir_entry db_entries[DB_OBJ_COUNT] = {
		{.filename = "ftfs_data_2_1_19.tokudb", 	.inode_no = FT_DATA_INODE_NUMBER},
		{.filename = "ftfs_meta_2_3_19.tokudb", 	.inode_no = FT_META_INODE_NUMBER},
		{.filename = "log000000000000.tokulog25", 	.inode_no = FT_LOG_INODE_NUMBER},
		{.filename = "tokudb.directory", 		.inode_no = TOKU_DIR_INODE_NUMBER},
		{.filename = "tokudb.environment", 		.inode_no = TOKU_ENV_INODE_NUMBER},
		{.filename = "tokudb_lock_dont_delete_me_data",	.inode_no = TOKU_LOCK_DATA_INODE_NUMBER},
		{.filename = "tokudb_lock_dont_delete_me_environment",	.inode_no = TOKU_LOCK_ENV_INODE_NUMBER},
		{.filename = "tokudb_lock_dont_delete_me_logs", 	.inode_no = TOKU_LOCK_LOGS_INODE_NUMBER},
		{.filename = "tokudb_lock_dont_delete_me_recovery",	.inode_no = TOKU_LOCK_RECV_INODE_NUMBER},
		{.filename = "tokudb_lock_dont_delete_me_temp",		.inode_no = TOKU_LOCK_TEMP_INODE_NUMBER},
		{.filename = "tokudb.rollback", 			.inode_no = TOKU_ROLLBACK_INODE_NUMBER}
	};

	struct sfs_dir_entry dev_entry = {
		.filename = "null",
		.inode_no = DEV_NULL_INODE_NUMBER
	};


	if (argc != 2) {
		printf("Usage: mkfs-sfs <device> \n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}

	ret = -1;

	do {
		if (write_superblock(fd, 0, false))
			break;
		if (write_root_inode(fd, &total_inodes))
			break;
		if (write_journal_inode(fd, &total_inodes))
			break;
		if (write_db_dir_inode(fd, &total_inodes))
			break;
		if (write_dev_dir_inode(fd, &total_inodes))
			break;
		if (write_temp_dir_inode(fd, &total_inodes))
			break;
		if (write_db_files_inode(fd, db_inodes, DB_OBJ_COUNT, &total_inodes))
			break;
		if (write_dev_null_inode(fd, &total_inodes))
			break;
		/* Skip the remaining part of inode table */
		if (sfs_inode_table_lseek(fd, total_inodes))
			break;
		if (write_journal(fd))
			break;

		/* Root dir */
		if (write_dirent(fd, root_entries, ROOT_OBJ_COUNT))
			break;
		/* DB dir */
		if (write_dirent(fd, db_entries, DB_OBJ_COUNT))
			break;
		/* DEV dir */
		if (write_dirent(fd, &dev_entry, DEV_OBJ_COUNT))
			break;

		/* Indeed this does nothing */
		if (write_block(fd, NULL, 0))
			break;

		/* Update superblock */
		if (write_superblock(fd, total_inodes, true))
			break;

		ret = 0;
	} while (0);

	fsync(fd);
	close(fd);
	return ret;
}
