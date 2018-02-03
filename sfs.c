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

#include "super.h"

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

	printk("inodes_count=%d\n", SFS_SB(sb)->inodes_count);

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

int sfs_sb_get_one_block(struct super_block *vsb, uint64_t *out)
{
	struct sfs_super_block *sb = SFS_SB(vsb);
	int i;
	int ret = 0;

	if (mutex_lock_interruptible(&sfs_sb_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		ret = -EINTR;
		goto end;
	}


	for (i = 3; i < SFS_MAX_FS_OBJ_SUPPORTED; i++) {
		if (sb->free_blocks & (1 << i)) {
			break;
		}
	}

	if (unlikely(i == SFS_MAX_FS_OBJ_SUPPORTED)) {
		printk(KERN_ERR "No more free blocks available\n");
		ret = -ENOSPC;
		goto end;
	}

	*out = i;

	/* Remove the identified block from the free list */
	sb->free_blocks &= ~(1 <<i);

	sfs_sb_sync(vsb);

end:
	mutex_unlock(&sfs_sb_lock);
	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
static int sfs_iterate(struct file *filp, struct dir_context *ctx)
#else
static int sfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
#endif
{
	loff_t pos;
	struct inode *inode;
	struct super_block *sb;
	struct buffer_head *bh;
	struct sfs_inode *sfs_inode;
	struct sfs_dir_entry *entry;
	int i;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	pos = ctx->pos;

#else
	pos = filp->f_pos;
#endif

	inode = filp->f_dentry->d_inode;
	sb = inode->i_sb;

	printk("%s is called\n", __func__); 	
	printk("pos=%lld\n", pos); 	

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

	if (inode->i_ino != 1) {
		printk(KERN_INFO "i_ino does not match\n");

	}

	if (sfs_inode->start_block_number != 2 + 64*256*1024L) {
		printk(KERN_INFO "start block number not match\n");
	}
	 
	bh = sb_bread(sb, sfs_inode->start_block_number);

	printk(KERN_INFO "bh=%p\n", bh);

	BUG_ON(!bh);

	entry = (struct sfs_dir_entry *)bh->b_data;
	for (i = 0; i < sfs_inode->dir_children_count; i++) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
		dir_emit(ctx, entry->filename, SFS_FILENAME_MAXLEN,
			entry->inode_no, DT_UNKNOWN);
		ctx->pos += sizeof(struct sfs_dir_entry);
#else
		filldir(dirent, entry->filename, SFS_FILENAME_MAXLEN, pos,
			entry->inode_no, DT_UNKNOWN);
		filp->f_pos += sizeof(struct sfs_dir_entry);
#endif
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

ssize_t sfs_read(struct file *filp, char __user *buf, size_t len,
		 loff_t *ppos)
{
	struct sfs_inode *inode = SFS_INODE(filp->f_path.dentry->d_inode);
	struct buffer_head *bh;

	char *buffer;
	int nbytes;
	
	
	printk("======== sfs_read start =======\n");
	printk("inode->file_size=%llu\n", inode->file_size);
	printk("inode->start_block_number=%llu\n", inode->start_block_number);
	printk("buf len=%zu\n", len);
	printk("*ppos=%llu\n", *ppos);

	if (*ppos >= inode->file_size) {
		return 0;
	}
	
	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb,
				inode->start_block_number);
	if (!bh) {
		printk(KERN_ERR "Reading the block number [%llu] failed.\n",
			inode->start_block_number);
		return 0;
	}

	buffer = (char *)bh->b_data;
	printk("b_size=%d\n", bh->b_size);

	nbytes = min((size_t) inode->file_size, len);
	nbytes = min(nbytes, 4096);

	if (copy_to_user(buf, buffer, nbytes)) {
		brelse(bh);
		printk(KERN_ERR "Error copying data to userspace buffer\n");
		return -EFAULT;
	}

	brelse(bh);
	*ppos += nbytes;

	printk("======== sfs_read end =======\n");
	return nbytes;
}

int sfs_inode_save(struct super_block *sb, struct sfs_inode *sfs_inode)
{
	struct sfs_inode *inode_iterator;
	struct buffer_head *bh;

	bh = sb_bread(sb, SFS_INODETABLE_BLOCK_NUMBER);
	BUG_ON(!bh);

	if (mutex_lock_interruptible(&sfs_sb_lock)) {
		sfs_trace("Failed to acquire mutex lock\n");
		return -EINTR;
	}

	inode_iterator = sfs_inode_search(sb,
		(struct sfs_inode *)bh->b_data,
		sfs_inode);
	if (likely(inode_iterator)) {
		memcpy(inode_iterator, sfs_inode, sizeof(*inode_iterator));
		printk(KERN_INFO "The inode update\n");
		
		mark_buffer_dirty(bh);
		sync_dirty_buffer(bh);
	} else {
		mutex_unlock(&sfs_sb_lock);
		printk(KERN_ERR "The new filesize couldnot be stored\n");
		return -EIO;
	}
	brelse(bh);

	mutex_unlock(&sfs_sb_lock);

	return 0;
}

ssize_t sfs_write(struct file *filp, const char __user *buf, size_t len,
		  loff_t *ppos)
{
	struct inode *inode;
	struct sfs_inode *sfs_inode;
	struct buffer_head *bh;
	struct super_block *sb;
	struct sfs_super_block *sfs_sb;

#if 0
	handle_t *handle;
#endif
	char *buffer;
	int retval;
	size_t nbytes;	


	inode = filp->f_path.dentry->d_inode;
	sb = filp->f_path.dentry->d_inode->i_sb;
	sfs_sb = SFS_SB(sb);

#if 0	
	handle = jbd2_journal_start(sfs_sb->journal, 1);
	if (IS_ERR(handle)) {
		printk(KERN_ERR "jdb2_journal_start failed\n");
		return PTR_ERR(handle);
	}
#endif

	printk("======= I am in sfs_write =========\n");
	printk("inode->i_size=%llu\n", inode->i_size);
	printk("len=%zu\n", len);
	printk("*ppos=%llu\n", *ppos);

	retval = generic_write_checks(filp, ppos, &len, 0);
	if (retval)
		return retval;

	sfs_inode = SFS_INODE(inode);

	bh = sb_bread(filp->f_path.dentry->d_inode->i_sb,
			sfs_inode->start_block_number);

	if (!bh) {
		printk(KERN_ERR "Reading the block number [%llu] failed.",
			sfs_inode->start_block_number);
		return 0;
	}

	buffer = (char *)bh->b_data;
	printk("b_size=%zu\n", bh->b_size);

	//print_hex_dump(KERN_ALERT, "sfs_before_copy: ", DUMP_PREFIX_ADDRESS,
        //        16, 1, (char *)bh->b_data, 100, 1);

	buffer += *ppos;
#if 0
	retval = jbd2_journal_get_write_access(handle, bh);
	if (WARN_ON(retval)) {
		brelse(bh);
		sfs_trace("Cannot get write access for bh\n");
		return retval;
	}
#endif
	

	nbytes = min(len, 4096);
	if (copy_from_user(buffer, buf, nbytes)) {
		brelse(bh);
		printk(KERN_ERR "Error coping data from userspace buffer\n");
		return -EFAULT;
	}
	*ppos += nbytes;

#if 0
	retval = jbd2_journal_dirty_metadata(handle, bh);
	if (WARN_ON(retval)) {
		brelse(bh);
		return retval;
	}
	
	handle->h_sync = 1;
	retval = jbd2_journal_stop(handle);
	if (WARN_ON(retval)) {
		brelse(bh);
		return retval;
	}
#endif
	//print_hex_dump(KERN_ALERT, "sfs_aft_copy: ", DUMP_PREFIX_ADDRESS,
        //        16, 1, (char *)bh->b_data, 100, 1);

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);	

	sfs_inode->file_size = *ppos;
	retval = sfs_inode_save(sb, sfs_inode);
	if (retval) {
		len = retval;
	}

	inode->i_size = *ppos;
	printk("===== sfs_inode->file_size=%lu =====\n", sfs_inode->file_size);
	printk("===== inode->i_size=%zu =====\n", inode->i_size);

	mutex_unlock(&sfs_inodes_mgmt_lock);
	return len;
}

const struct file_operations sfs_file_operations = {
	.read = sfs_read,
	.write = sfs_write,
};

const struct file_operations sfs_dir_operations = {
	.owner = THIS_MODULE,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 11, 0)
	.iterate = sfs_iterate,
#else
	.readdir = sfs_readdir,
#endif
};

#if 0
static int simplefs_get_blocks(struct inode *inode,
			   sector_t iblock, unsigned long maxblocks,
			   struct buffer_head *bh_result,
			   int create)
{
	return 0;
}

static int simplefs_get_block(struct inode *inode, sector_t iblock,
		   struct buffer_head *bh_result, int create)
{
}
#endif

static sector_t sfs_bmap(struct address_space *mapping, sector_t block)
{
	struct sfs_inode *sfs_inode;


	printk(KERN_ERR "simplefs_bmap is called\n");
	sfs_inode = SFS_INODE(mapping->host);

	BUG_ON(block > sfs_inode->block_count);

	printk("iblock=%lu, start_block_number=%llu\n",
		block, sfs_inode->start_block_number);

	return sfs_inode->start_block_number + block;
}

const struct address_space_operations sfs_aops = {
	.bmap = sfs_bmap,
};

struct dentry *sfs_lookup(struct inode *parent_inode,
                          struct dentry *child_dentry, unsigned int flags);


static int sfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			bool excl);

static int sfs_mkdir(struct inode *dir, struct dentry *dentry,
			umode_t mode);


static struct inode_operations sfs_inode_ops = {
	.create = sfs_create,
	.lookup = sfs_lookup,
	.mkdir = sfs_mkdir,

};

static int sfs_mkdir(struct inode *dir, struct dentry *dentry,
		     umode_t mode)
{
	printk(KERN_ERR "Do not allow to dynamically create new file\n");
	return -EINVAL;
	//return sfs_create_fs_object(dir, dentry, S_IFDIR | mode);
}

static int sfs_create(struct inode *dir, struct dentry *dentry,
		      umode_t mode, bool excl)
{
	printk(KERN_ERR "Do not allow to dynamically create new dir\n");
	return -EINVAL;
	//return sfs_create_fs_object(dir, dentry, mode);
}

static struct inode *sfs_iget(struct super_block *sb, int ino)
{
	struct inode *inode;
	struct sfs_inode *sfs_inode;

	sfs_inode = sfs_get_inode(sb, ino);
	
	inode = new_inode(sb);
	inode->i_ino = ino;
	inode->i_sb = sb;
	inode->i_op = &sfs_inode_ops;
	inode->i_mapping->a_ops = &sfs_aops;

	if (S_ISDIR(sfs_inode->mode)) {
		inode->i_size = sfs_inode->dir_children_count *
				sizeof(sfs_inode);
		inode->i_fop = &sfs_dir_operations;
	} else if (S_ISREG(sfs_inode->mode) || ino == SFS_JOURNAL_INODE_NUMBER) {
		inode->i_size = sfs_inode->file_size;
		inode->i_fop = &sfs_file_operations;
	} else {
		printk(KERN_ERR "Unknown inode type\n");
	}

	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_private = sfs_inode;

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
		sfs_trace("Have file: %s (ino=%llu)\n",
					entry->filename, entry->inode_no);

		if (!strcmp(entry->filename, child_dentry->d_name.name)) {
			struct inode *inode = sfs_iget(sb, entry->inode_no);
			inode_init_owner(inode, parent_inode,
					 SFS_INODE(inode)->mode);

			d_add(child_dentry, inode);
			return NULL;
		}
		entry++;		
	}

	printk(KERN_ERR "No inode found for the file name [%s]\n",
		child_dentry->d_name.name);
	return NULL;
}

void sfs_destroy_inode(struct inode *inode)
{
	struct sfs_inode *sfs_inode = SFS_INODE(inode);
	
	printk(KERN_INFO "Freeing private data of inode %p (%lu)\n",
		sfs_inode, inode->i_ino);

	kmem_cache_free(sfs_inode_cachep, sfs_inode);
}

static void sfs_put_super(struct super_block *sb)
{
	struct sfs_super_block *sfs_sb = SFS_SB(sb);
	if (sfs_sb->journal)
		WARN_ON(jbd2_journal_destroy(sfs_sb->journal) < 0);
	
	sfs_sb->journal = NULL;
}

static const struct super_operations sfs_sops = {
	.destroy_inode = sfs_destroy_inode,
	.put_super = sfs_put_super,
};

static int sfs_sb_load_journal(struct super_block *sb, struct inode *inode)
{
	struct journal_s *journal;
	struct sfs_super_block *sfs_sb = SFS_SB(sb);
	
	journal = jbd2_journal_init_inode(inode);
	if (!journal) {
		printk(KERN_ERR "Cannot load journal\n");
		return 1;
	}

	journal->j_private = sb;
	sfs_sb->journal = journal;

	return 0;
}

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
	root_inode->i_op = &sfs_inode_ops;
	root_inode->i_fop = &sfs_dir_operations;
	root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime =
		CURRENT_TIME;
	root_inode->i_private = sfs_get_inode(sb, SFS_ROOTDIR_INODE_NUMBER);
	
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0)
	sb->s_root = d_make_root(root_inode);
#else
	sb->s_root = d_alloc_root(root_inode);
	if (!sb->s_root)
		iput(root_inode);
#endif
	if (!sb->s_root) {
		ret = -ENOMEM;
		goto release;
	}

	
	if (!(sb_disk->journal)) {
		struct inode *journal_inode;
		journal_inode = sfs_iget(sb, SFS_JOURNAL_INODE_NUMBER);
		ret = sfs_sb_load_journal(sb, journal_inode);
	}

	#if 0
	/* FIXME: disable this first */
	if (sb_disk->journal) {
		ret = jbd2_journal_load(sb_disk->journal);
	}

	printk("j_maxlen=%u\n", sb_disk->journal->j_maxlen);
	printk("j_max_transaction_buffers=%u\n",
				sb_disk->journal->j_max_transaction_buffers);
	#endif
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

