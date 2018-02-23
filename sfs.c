#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/jbd2.h>
#include <linux/parser.h>
#include <linux/blkdev.h>
#include <linux/mpage.h>
#include <linux/aio.h>
#include <linux/pagevec.h>

#include "super.h"
#include "sfs_debug.h"



/* A super block lock for critical section opertion on the sb */
static DEFINE_MUTEX(sfs_sb_lock);
static DEFINE_MUTEX(sfs_inodes_mgmt_lock);

/* FIXME: This should be moved to in-memory structure of sfs_inode */
static DEFINE_MUTEX(sfs_dir_children_update_lock);

static struct kmem_cache *sfs_inode_cachep;

void sfs_sb_sync(struct super_block *vsb)
{
	struct buffer_head *bh;
	struct sfs_super_block *sb = SFS_SB(vsb);

	bh = sb_bread(vsb, SFS_SUPERBLOCK_BLOCK_NUMBER);
	BUG_ON(!bh);

	bh->b_data = (char *)sb;
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);
}

struct sfs_inode *sfs_inode_search(struct super_block *sb,
			struct sfs_inode *start,
			struct sfs_inode *search)
{
	uint64_t count = 0;

	while (start->inode_no != search->inode_no
			&& count < SFS_SB(sb)->inodes_count) {
		count++;
		start++;
	}

	if (start->inode_no == search->inode_no) {
		return start;
	}

	return NULL;
}


void sfs_inode_add(struct super_block *vsd, struct sfs_inode *inode)
{
	struct sfs_super_block *sb = SFS_SB(vsd);
	struct buffer_head *bh;
	struct sfs_inode *inode_iterator;
	
	if (mutex_lock_interruptible(&sfs_inodes_mgmt_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return;
	}

	bh = sb_bread(vsd, SFS_INODETABLE_BLOCK_NUMBER);
	BUG_ON(!bh);

	inode_iterator = (struct sfs_inode *)bh->b_data;

	if (mutex_lock_interruptible(&sfs_sb_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return;
	}

	/* Append the new inode in the end in the inde store */
	inode_iterator += sb->inodes_count;

	memcpy(inode_iterator, inode, sizeof(struct sfs_inode));
	sb->inodes_count++;

	mark_buffer_dirty(bh);
	sfs_sb_sync(vsd);
	brelse(bh);
	
	mutex_unlock(&sfs_sb_lock);
	mutex_unlock(&sfs_inodes_mgmt_lock);	
}

static int sfs_iterate(struct file *filp, struct dir_context *ctx)
{
	loff_t pos;
	struct inode *inode;
	struct super_block *sb;
	struct buffer_head *bh;
	struct sfs_inode *sfs_inode;
	struct sfs_dir_entry *entry;
	int i;

	pos = ctx->pos;
	inode = filp->f_dentry->d_inode;
	sb = inode->i_sb;

	if (pos) {
		/* FIXME: use a hack of reading pos to figure if we have filled
		 *  in all data.
		 */
		return 0;
	}

	sfs_inode = SFS_INODE(inode);
	if (unlikely(!S_ISDIR(sfs_inode->mode))) {
		printk(KERN_ERR
		       "inode [%llu][%lu] for fs object [%s] not a directory\n",
			sfs_inode->inode_no, inode->i_ino,
			filp->f_dentry->d_name.name);
		return -ENOTDIR;
	}
	 
	bh = sb_bread(sb, sfs_inode->start_block_number);
	BUG_ON(!bh);

	entry = (struct sfs_dir_entry *)bh->b_data;
	for (i = 0; i < sfs_inode->dir_children_count; i++) {
		dir_emit(ctx, entry->filename, SFS_FILENAME_MAXLEN,
			entry->inode_no, DT_UNKNOWN);
		ctx->pos += sizeof(struct sfs_dir_entry);
		pos += sizeof(struct sfs_dir_entry);
		entry++;
	}

	brelse(bh);
	return 0;	
}

/* This function returns a sfs_inode with the given inode_no from the inode
 * table. If it exists
 */
struct sfs_inode *sfs_get_inode(struct super_block *sb, uint64_t inode_no)
{
	struct sfs_super_block *sfs_sb = SFS_SB(sb);
	struct sfs_inode *sfs_inode = NULL;
	struct sfs_inode *inode_buffer = NULL;

	int i;
	struct buffer_head *bh;

	bh = sb_bread(sb, SFS_INODETABLE_BLOCK_NUMBER);
	BUG_ON(!bh);

	sfs_inode = (struct sfs_inode *) bh->b_data;

#if 0
	if (mutex_lock_interruptible(&sfs_inodes_mgmt_lock)) {
		printk(KERN_ERR "Failed to acquire mutex lock %s + %d\n",
			__FILE, __LINE__);
		return NULL;
	}
#endif
	for (i = 0; i < sfs_sb->inodes_count; i++) {
		if (sfs_inode->inode_no == inode_no) {
			inode_buffer = kmem_cache_alloc(sfs_inode_cachep,
						  	GFP_KERNEL);
			memcpy(inode_buffer, sfs_inode, sizeof(*inode_buffer));
			break;
		}
		sfs_inode++;
	}

#if 0
	mutex_unlock(&sfs_inodes_mgmt_lock);
#endif
	brelse(bh);
	return inode_buffer;
}

static void sfs_truncate_blocks(struct inode *inode, loff_t offset)
{
	BUG();
}

static void sfs_write_failed(struct address_space *mapping, loff_t to)
{
	struct inode *inode = mapping->host;

	if (to > inode->i_size) {
		truncate_pagecache(inode, to, inode->i_size);
		sfs_truncate_blocks(inode, inode->i_size);
	}
}

int sfs_get_block(struct inode *inode, sector_t iblock,
		  struct buffer_head *bh_result, int create)
{
	struct sfs_inode *sfs_inode;

	sfs_inode = SFS_INODE(inode);

	BUG_ON(iblock > sfs_inode->block_count);

	clear_buffer_new(bh_result);
	set_buffer_new(bh_result);
	map_bh(bh_result, inode->i_sb,
			sfs_inode->start_block_number + iblock);
	bh_result->b_size = 1 << inode->i_sb->s_blocksize_bits ; 

	return 0;
}

static int
sfs_write_begin(struct file *file, struct address_space *mapping,
		loff_t pos, unsigned len, unsigned flags,
		struct page **pagep, void **fsdata)
{
	int ret;
	
	ret = block_write_begin(mapping, pos, len, flags, pagep,
				sfs_get_block);

	if (ret < 0)
		sfs_write_failed(mapping, pos + len);	

	return ret;
}

static int sfs_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	int ret;

	ret = sfs_generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (ret < len) {
		BUG_ON(true);
		sfs_write_failed(mapping, pos + len);
	}

	return ret;
}

ssize_t sfs_sync_read(struct file *filp, char __user *buf,
			size_t len, loff_t *ppos)
{
	return do_sync_read(filp, buf, len, ppos);
}

ssize_t sfs_sync_write(struct file *filp, const char __user *buf,
			size_t len, loff_t *ppos)
{
	ssize_t ret;
	ret = do_sync_write(filp, buf, len, ppos);
	return ret;
}

static ssize_t
sfs_file_write(struct kiocb *iocb, const struct iovec *iov,
		unsigned long nr_segs, loff_t pos)
{
	ssize_t ret;


	ret = sfs_in_kernel_write(iocb, iov, nr_segs, pos);
	return ret;
}

static int
sfs_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	printk("sfs_fsync is called\n");
	return 0;
}

static int sfs_setattr(struct dentry *dentry, struct iattr *iattr)
{
	int ret;
	struct inode *inode = dentry->d_inode;
	loff_t size;

	ret = inode_change_ok(inode, iattr);
	if (ret)
		return ret;

	size = i_size_read(inode);
	if ((iattr->ia_valid & ATTR_SIZE) && iattr->ia_size < size) {
		i_size_write(inode, iattr->ia_size);
		mark_inode_dirty(inode);
	}

	return ret;
}

/* --------------- address_space_operations --------------- */

static int sfs_readpage(struct file *file, struct page *page)
{
	return mpage_readpage(page, sfs_get_block);
}

static int sfs_readpages(struct file *file, struct address_space *mapping,
			struct list_head *pages, unsigned nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, sfs_get_block);
}

static int
sfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int ret = block_write_full_page(page, sfs_get_block, wbc);
	return ret;
}

static int
sfs_writepages(struct address_space *mapping, struct writeback_control *wbc)
{
	int ret;

	ret = mpage_writepages(mapping, wbc, sfs_get_block);
	return ret;
}

static sector_t sfs_bmap(struct address_space *mapping, sector_t block)
{
	struct sfs_inode *sfs_inode;

	sfs_inode = SFS_INODE(mapping->host);
	BUG_ON(block > sfs_inode->block_count);

	return sfs_inode->start_block_number + block;
}

const struct address_space_operations sfs_aops = {
	.readpage	= sfs_readpage,
	.readpages	= sfs_readpages,
	.writepage	= sfs_writepage,
	.writepages	= sfs_writepages,
	.write_begin	= sfs_write_begin,
	.write_end	= sfs_write_end,
	.bmap		= sfs_bmap,
};

struct dentry *sfs_lookup(struct inode *parent_inode,
                          struct dentry *child_dentry, unsigned int flags);

static int sfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			bool excl);

static int sfs_mkdir(struct inode *dir, struct dentry *dentry,
			umode_t mode);


static int sfs_mkdir(struct inode *dir, struct dentry *dentry,
		     umode_t mode)
{
	printk(KERN_ERR "Do not allow to dynamically create new file\n");
	return -EINVAL;
}

static int sfs_create(struct inode *dir, struct dentry *dentry,
		      umode_t mode, bool excl)
{
	printk(KERN_ERR "Do not allow to dynamically create new dir\n");
	return -EINVAL;
}

const struct file_operations sfs_file_ops = {
	.fsync = sfs_fsync,
	.read = do_sync_read,
	.write = sfs_sync_write,
	.aio_read = generic_file_aio_read,
	.aio_write = sfs_file_write,
};

const struct file_operations sfs_dir_ops = {
	.owner = THIS_MODULE,
	.iterate = sfs_iterate,
	.fsync = sfs_fsync,
};

static const struct inode_operations sfs_file_inode_ops = {
	.setattr = sfs_setattr
};

static struct inode_operations sfs_dir_inode_ops = {
	.create = sfs_create,
	.lookup = sfs_lookup,
	.mkdir = sfs_mkdir,
	.setattr = sfs_setattr,
};

static struct inode *sfs_iget(struct super_block *sb, int ino)
{
	struct inode *inode;
	struct sfs_inode *sfs_inode;

	sfs_inode = sfs_get_inode(sb, ino);
	
	if ((inode = iget_locked(sb, ino)) == NULL)
		return ERR_PTR(-ENOMEM);

	/* FIXME : This may be an assertion */
	inode->i_ino = ino;
	inode->i_sb = sb;

	if (S_ISDIR(sfs_inode->mode)) {
		inode->i_size = sfs_inode->dir_children_count *
				sizeof(sfs_inode);
		inode->i_fop = &sfs_dir_ops;
		inode->i_op = &sfs_dir_inode_ops;
	} else if (S_ISREG(sfs_inode->mode) || ino == SFS_JOURNAL_INODE_NUMBER) {
		inode->i_size = sfs_inode->file_size;
		inode->i_fop = &sfs_file_ops;
		inode->i_op = &sfs_file_inode_ops;
		inode->i_data.a_ops = &sfs_aops;
	} else {
		printk(KERN_ERR "Unknown inode type\n");
	}

	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_private = sfs_inode;

	unlock_new_inode(inode);
	return inode;
}

struct dentry *sfs_lookup(struct inode *parent_inode,
			  struct dentry *child_dentry,
			  unsigned int flags)
{
	struct sfs_inode *parent = SFS_INODE(parent_inode);
	struct super_block *sb = parent_inode->i_sb;
	struct buffer_head *bh;
	struct sfs_dir_entry *entry;
	int i;

	bh = sb_bread(sb, parent->start_block_number);
	BUG_ON(!bh);
	sfs_trace("Lookup in: ino=%llu, b=%llu\n",
				parent->inode_no, parent->start_block_number);

	entry = (struct sfs_dir_entry *)bh->b_data;
	for (i = 0; i < parent->dir_children_count; i++) {
		if (!strcmp(entry->filename, child_dentry->d_name.name)) {
			struct inode *inode = sfs_iget(sb, entry->inode_no);
			inode_init_owner(inode, parent_inode,
					 SFS_INODE(inode)->mode);
			d_add(child_dentry, inode);
			return NULL;
		}
		entry++;		
	}
	return NULL;
}

void sfs_destroy_inode(struct inode *inode)
{
	struct sfs_inode *sfs_inode = SFS_INODE(inode);
	kmem_cache_free(sfs_inode_cachep, sfs_inode);
}

static void sfs_put_super(struct super_block *sb)
{
	struct sfs_super_block *sfs_sb = SFS_SB(sb);
	if (sfs_sb->journal)
		WARN_ON(jbd2_journal_destroy(sfs_sb->journal) < 0);
	sfs_sb->journal = NULL;
}

int sfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{	
	return 0;
}

static int sfs_sync_fs(struct super_block *sb, int wait)
{
	return 0;
}

static const struct super_operations sfs_sops = {
	.destroy_inode = sfs_destroy_inode,
	.put_super = sfs_put_super,
	.write_inode = sfs_write_inode,
	.sync_fs = sfs_sync_fs,
};

int sfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *root_inode;
	struct buffer_head *bh;
	struct sfs_super_block *sb_disk;
	int ret = -EPERM;

	bh = sb_bread(sb, SFS_SUPERBLOCK_BLOCK_NUMBER);
	BUG_ON(!bh);
	
	sb_disk = (struct sfs_super_block *)bh->b_data;
	printk(KERN_INFO "The magic number obtained in disk is: [%llu]\n", sb_disk->magic);

	if (unlikely(sb_disk->magic != SFS_MAGIC)) {
		printk(KERN_ERR "The fs that you try to mount is not SFS "
				"Magicnumber mismatch\n");
		goto release;
	}

	if (unlikely(sb_disk->block_size != SFS_DEFAULT_BLOCK_SIZE)) {
		printk(KERN_ERR "SFS seem to be formatted using a "
				"nonstandard block size\n");
		goto release;
	}

	sb_disk->journal = NULL;
	
	printk(KERN_INFO "SFS of version [%llu] formatted with a "
			 "block size of [%llu] detected in the device.\n",
			  sb_disk->version, sb_disk->block_size);

	sb->s_magic = SFS_MAGIC;
	sb->s_fs_info = sb_disk;

	sb->s_maxbytes = SFS_DEFAULT_MAX_BYTES;
	sb->s_blocksize_bits = SFS_DEFAULT_BLOCK_BITS;
	sb->s_blocksize = sb_disk->block_size;
	sb->s_op = &sfs_sops;
	
	root_inode = new_inode(sb);
	root_inode->i_ino = SFS_ROOTDIR_INODE_NUMBER;
	inode_init_owner(root_inode, NULL, S_IFDIR);
	root_inode->i_sb = sb;
	root_inode->i_op = &sfs_dir_inode_ops;
	root_inode->i_fop = &sfs_dir_ops;
	root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime =
		CURRENT_TIME;
	root_inode->i_private = sfs_get_inode(sb, SFS_ROOTDIR_INODE_NUMBER);
	
	sb->s_root = d_make_root(root_inode);
	if (!sb->s_root) {
		ret = -ENOMEM;
		goto release;
	}
	
	ret = 0;

release:
	brelse(bh);
	return ret;
}

static struct dentry *sfs_mount(struct file_system_type *fs_type,
				int flags, const char *dev_name,
				void *data)

{
	struct dentry *ret;

	ret = mount_bdev(fs_type, flags, dev_name, data, sfs_fill_super);

	if (unlikely(IS_ERR(ret)))
		printk(KERN_ERR "ERROR mounting sfs\n");
	else
		printk(KERN_INFO "sfs is successfully mounted on [%s]\n",
					dev_name);
	return ret;	
}

static void sfs_kill_sb(struct super_block *sb)
{
	sync_filesystem(sb);
	printk(KERN_INFO "SFS superblock is destroyed. Unmount successful.\n");
	kill_block_super(sb);
	return;
}

struct file_system_type sfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "sfs",
	.mount = sfs_mount,
	.kill_sb = sfs_kill_sb,
	.fs_flags = FS_REQUIRES_DEV,
};

static int sfs_init(void)
{
	int ret;

	sfs_inode_cachep = kmem_cache_create("sfs_inode_cache",
				sizeof(struct sfs_inode),
				0,
				(SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD),
				NULL);

	if (!sfs_inode_cachep) {
		return -ENOMEM;
	}

	ret = register_filesystem(&sfs_fs_type);
	if (likely(ret == 0)) 
		printk(KERN_INFO "Successfully registered sfs\n");
	else
		printk(KERN_INFO "Failed to register sfs, error %d\n", ret);

	return ret;
}

static void sfs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&sfs_fs_type);
	kmem_cache_destroy(sfs_inode_cachep);

	if (likely(ret == 0)) 
		printk(KERN_INFO "Successfully unregistered sfs\n");
	else
		printk(KERN_INFO "Failed to unregister sfs, error %d\n", ret);

}

module_init(sfs_init);
module_exit(sfs_exit);
MODULE_LICENSE("GPL");
