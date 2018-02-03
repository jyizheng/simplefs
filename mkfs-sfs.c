#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>

#include "sfs.h"

#define WELCOMEFILE_DATABLOCK_NUMBER (SFS_LAST_RESERVED_BLOCK + 1)
#define WELCOMEFILE_INODE_NUMBER (SFS_LAST_RESERVED_INODE + 1)
#define WELCOMEFILE_BLOCK_COUNT 50

static int write_superblock(int fd)
{
	struct sfs_super_block sb = {
		.version = 1,
		.magic = SFS_MAGIC,
		.block_size = SFS_DEFAULT_BLOCK_SIZE,
		.inodes_count = WELCOMEFILE_INODE_NUMBER,
		/* FIXME: Free block management is not implemented yet */
		.free_blocks = (~0) & ~(1 << SFS_LAST_RESERVED_BLOCK),

	};

	ssize_t ret;

	ret = write(fd, &sb, sizeof(sb));
	if (ret != SFS_DEFAULT_BLOCK_SIZE) {
		printf("bytes written [%ld] are not equal"
		       " to the default block size\n", ret);

	}

	printf("Super block written successfully\n");

	return 0;
}

static int write_root_inode(int fd)
{
	ssize_t ret;
	struct sfs_inode root_inode;

	root_inode.mode = S_IFDIR;
	root_inode.inode_no = SFS_ROOTDIR_INODE_NUMBER;
	root_inode.start_block_number = SFS_ROOTDIR_DATABLOCK_NUMBER;
	root_inode.dir_children_count = 1;
	root_inode.block_count = 1;

	ret = write(fd, &root_inode, sizeof(root_inode));

	if (ret != sizeof(root_inode)) {
		printf("The root inode was not written properly\n");
		return -1;
	}

	printf("Root dir inode written successfully\n");
	return 0;
}

static int write_journal_inode(int fd)
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

	printf("Journal inode written successfully\n");
	return 0;
}

static int write_welcome_inode(int fd, const struct sfs_inode *i)
{
	off_t nbytes;
	ssize_t ret;

	ret = write(fd, i, sizeof(*i));
	if (ret != sizeof(*i)) {
		printf("The welcomefile inode was not written properly\n");
		return -1;
	}
	
	printf("Welcomefile inode written successfully\n");

	nbytes = SFS_DEFAULT_BLOCK_SIZE - (sizeof(*i) * 3);
	ret = lseek(fd, nbytes, SEEK_CUR);

	if (ret == (off_t) -1) {
		printf("The padding bytes are not written properly\n");
		return -1;
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

int write_dirent(int fd, const struct sfs_dir_entry *entry)
{
	ssize_t nbytes = sizeof(*entry);
	ssize_t ret;

	off_t offset = lseek(fd, 0, SEEK_CUR ) ;

	if ((offset>>12) != 64*256*1024L + 2) {
		printf("offset=%ld\n", offset);
	}

	ret = write(fd, entry, nbytes);
	if (ret != nbytes) {
		printf("Writing the root dir entry has failed\n");
		return -1;
	}

	printf("Root dir entry written successfully\n");

	nbytes = SFS_DEFAULT_BLOCK_SIZE - sizeof(*entry);
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
	ssize_t ret;
	off_t offset = lseek( fd, 0, SEEK_CUR ) ;

	printf("offset blk=%ld\n", offset>>12);

	if ((offset>>12) != 64*256*1024L + 3) {
		printf("offset=%ld\n", offset);
	}

	ret = write(fd, block, len);
	if (ret != len) {
		printf("Writing file body has failed\n");
		return -1;
	}
	printf("Block has been written successfully\n");


	offset = lseek(fd, -len, SEEK_CUR) ;
	memset(block, '\0', len);
	ret = read(fd, block, len);

	printf("read block: %s\n", block); 	

	return 0;
}

int main(int argc, char *argv[])
{
	int fd;
	ssize_t ret;

	char welcomefile_body[] = "1:1 From the beginning, God created "
                                  "the heavens and the earth!\n";
	struct sfs_inode welcome = {
		.mode = S_IFREG,
		.inode_no = WELCOMEFILE_INODE_NUMBER,
		.start_block_number = WELCOMEFILE_DATABLOCK_NUMBER,
		.block_count = WELCOMEFILE_BLOCK_COUNT,
		.file_size = sizeof(welcomefile_body),
	};

	struct sfs_dir_entry entry = {
		.filename = "genesis",
		.inode_no = WELCOMEFILE_INODE_NUMBER,
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
		if (write_superblock(fd))
			break;
		if (write_root_inode(fd))
			break;
		if (write_journal_inode(fd))
			break;
		if (write_welcome_inode(fd, &welcome))
			break;
		
		if (write_journal(fd))
			break;

		if (write_dirent(fd, &entry))
			break;
		if (write_block(fd, welcomefile_body, welcome.file_size))
			break;
	
		ret = 0;
	} while (0);

	fsync(fd);
	close(fd);
	return ret;
}
