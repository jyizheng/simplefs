#include "sfs.h"

static inline struct sfs_super_block *SFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

static inline struct sfs_inode *SFS_INODE(struct inode *inode)
{
	return inode->i_private;
}
