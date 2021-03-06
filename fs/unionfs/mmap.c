/*
 * Copyright (c) 2003-2007 Erez Zadok
 * Copyright (c) 2003-2006 Charles P. Wright
 * Copyright (c) 2005-2007 Josef 'Jeff' Sipek
 * Copyright (c) 2005-2006 Junjiro Okajima
 * Copyright (c) 2006      Shaya Potter
 * Copyright (c) 2005      Arun M. Krishnakumar
 * Copyright (c) 2004-2006 David P. Quigley
 * Copyright (c) 2003-2004 Mohammad Nayyer Zubair
 * Copyright (c) 2003      Puja Gupta
 * Copyright (c) 2003      Harikesavan Krishnan
 * Copyright (c) 2003-2007 Stony Brook University
 * Copyright (c) 2003-2007 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "union.h"

static int unionfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int err = -EIO;
	struct inode *inode;
	struct inode *lower_inode;
	struct page *lower_page;
	struct address_space *lower_mapping; /* lower inode mapping */
	gfp_t mask;

	BUG_ON(!PageUptodate(page));
	inode = page->mapping->host;
	/* if no lower inode, nothing to do */
	if (!inode || !UNIONFS_I(inode) || UNIONFS_I(inode)->lower_inodes) {
		err = 0;
		goto out;
	}
	lower_inode = unionfs_lower_inode(inode);
	lower_mapping = lower_inode->i_mapping;

	/*
	 * find lower page (returns a locked page)
	 *
	 * We turn off __GFP_FS while we look for or create a new lower
	 * page.  This prevents a recursion into the file system code, which
	 * under memory pressure conditions could lead to a deadlock.  This
	 * is similar to how the loop driver behaves (see loop_set_fd in
	 * drivers/block/loop.c).  If we can't find the lower page, we
	 * redirty our page and return "success" so that the VM will call us
	 * again in the (hopefully near) future.
	 */
	mask = mapping_gfp_mask(lower_mapping) & ~(__GFP_FS);
	lower_page = find_or_create_page(lower_mapping, page->index, mask);
	if (!lower_page) {
		err = 0;
		set_page_dirty(page);
		goto out;
	}

	/* copy page data from our upper page to the lower page */
	copy_highpage(lower_page, page);
	flush_dcache_page(lower_page);
	SetPageUptodate(lower_page);
	set_page_dirty(lower_page);

	/*
	 * Call lower writepage (expects locked page).  However, if we are
	 * called with wbc->for_reclaim, then the VFS/VM just wants to
	 * reclaim our page.  Therefore, we don't need to call the lower
	 * ->writepage: just copy our data to the lower page (already done
	 * above), then mark the lower page dirty and unlock it, and return
	 * success.
	 */
	if (wbc->for_reclaim) {
		unlock_page(lower_page);
		goto out_release;
	}

	BUG_ON(!lower_mapping->a_ops->writepage);
	wait_on_page_writeback(lower_page); /* prevent multiple writers */
	clear_page_dirty_for_io(lower_page); /* emulate VFS behavior */
	err = lower_mapping->a_ops->writepage(lower_page, wbc);
	if (err < 0)
		goto out_release;

	/*
	 * Lower file systems such as ramfs and tmpfs, may return
	 * AOP_WRITEPAGE_ACTIVATE so that the VM won't try to (pointlessly)
	 * write the page again for a while.  But those lower file systems
	 * also set the page dirty bit back again.  Since we successfully
	 * copied our page data to the lower page, then the VM will come
	 * back to the lower page (directly) and try to flush it.  So we can
	 * save the VM the hassle of coming back to our page and trying to
	 * flush too.  Therefore, we don't re-dirty our own page, and we
	 * never return AOP_WRITEPAGE_ACTIVATE back to the VM (we consider
	 * this a success).
	 *
	 * We also unlock the lower page if the lower ->writepage returned
	 * AOP_WRITEPAGE_ACTIVATE.  (This "anomalous" behaviour may be
	 * addressed in future shmem/VM code.)
	 */
	if (err == AOP_WRITEPAGE_ACTIVATE) {
		err = 0;
		unlock_page(lower_page);
	}

	/* all is well */

	/* lower mtimes have changed: update ours */
	unionfs_copy_attr_times(inode);

out_release:
	/* b/c find_or_create_page increased refcnt */
	page_cache_release(lower_page);
out:
	/*
	 * We unlock our page unconditionally, because we never return
	 * AOP_WRITEPAGE_ACTIVATE.
	 */
	unlock_page(page);
	return err;
}

static int unionfs_writepages(struct address_space *mapping,
			      struct writeback_control *wbc)
{
	int err = 0;
	struct inode *lower_inode;
	struct inode *inode;

	inode = mapping->host;
	if (ibstart(inode) < 0 && ibend(inode) < 0)
		goto out;
	lower_inode = unionfs_lower_inode(inode);
	if (!lower_inode)
		goto out;

	err = generic_writepages(mapping, wbc);
	if (!err)
		unionfs_copy_attr_times(inode);
out:
	return err;
}

/* Readpage expects a locked page, and must unlock it */
static int unionfs_readpage(struct file *file, struct page *page)
{
	int err;
	struct file *lower_file;
	struct inode *inode;
	mm_segment_t old_fs;
	char *page_data = NULL;
	mode_t orig_mode;

	unionfs_read_lock(file->f_path.dentry->d_sb, UNIONFS_SMUTEX_PARENT);
	err = unionfs_file_revalidate(file, false);
	if (unlikely(err))
		goto out;
	unionfs_check_file(file);

	if (!UNIONFS_F(file)) {
		err = -ENOENT;
		goto out;
	}

	lower_file = unionfs_lower_file(file);
	/* FIXME: is this assertion right here? */
	BUG_ON(lower_file == NULL);

	inode = file->f_path.dentry->d_inode;

	page_data = (char *) kmap(page);
	/*
	 * Use vfs_read because some lower file systems don't have a
	 * readpage method, and some file systems (esp. distributed ones)
	 * don't like their pages to be accessed directly.  Using vfs_read
	 * may be a little slower, but a lot safer, as the VFS does a lot of
	 * the necessary magic for us.
	 */
	lower_file->f_pos = page_offset(page);
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	/*
	 * generic_file_splice_write may call us on a file not opened for
	 * reading, so temporarily allow reading.
	 */
	orig_mode = lower_file->f_mode;
	lower_file->f_mode |= FMODE_READ;
	err = vfs_read(lower_file, page_data, PAGE_CACHE_SIZE,
		       &lower_file->f_pos);
	lower_file->f_mode = orig_mode;
	set_fs(old_fs);
	if (err >= 0 && err < PAGE_CACHE_SIZE)
		memset(page_data + err, 0, PAGE_CACHE_SIZE - err);
	kunmap(page);

	if (err < 0)
		goto out;
	err = 0;

	/* if vfs_read succeeded above, sync up our times */
	unionfs_copy_attr_times(inode);

	flush_dcache_page(page);

	/*
	 * we have to unlock our page, b/c we _might_ have gotten a locked
	 * page.  but we no longer have to wakeup on our page here, b/c
	 * UnlockPage does it
	 */
out:
	if (err == 0)
		SetPageUptodate(page);
	else
		ClearPageUptodate(page);

	unlock_page(page);
	unionfs_check_file(file);
	unionfs_read_unlock(file->f_path.dentry->d_sb);

	return err;
}

static int unionfs_prepare_write(struct file *file, struct page *page,
				 unsigned from, unsigned to)
{
	int err;

	unionfs_read_lock(file->f_path.dentry->d_sb, UNIONFS_SMUTEX_PARENT);
	err = unionfs_file_revalidate(file, true);
	if (!err) {
		unionfs_copy_attr_times(file->f_path.dentry->d_inode);
		unionfs_check_file(file);
	}
	unionfs_read_unlock(file->f_path.dentry->d_sb);

	return err;
}

static int unionfs_commit_write(struct file *file, struct page *page,
				unsigned from, unsigned to)
{
	int err = -ENOMEM;
	struct inode *inode, *lower_inode;
	struct file *lower_file = NULL;
	unsigned bytes = to - from;
	char *page_data = NULL;
	mm_segment_t old_fs;

	BUG_ON(file == NULL);

	unionfs_read_lock(file->f_path.dentry->d_sb, UNIONFS_SMUTEX_PARENT);
	err = unionfs_file_revalidate(file, true);
	if (unlikely(err))
		goto out;
	unionfs_check_file(file);

	inode = page->mapping->host;

	if (UNIONFS_F(file) != NULL)
		lower_file = unionfs_lower_file(file);

	/* FIXME: is this assertion right here? */
	BUG_ON(lower_file == NULL);

	page_data = (char *)kmap(page);
	lower_file->f_pos = page_offset(page) + from;

	/*
	 * We use vfs_write instead of copying page data and the
	 * prepare_write/commit_write combo because file system's like
	 * GFS/OCFS2 don't like things touching those directly,
	 * calling the underlying write op, while a little bit slower, will
	 * call all the FS specific code as well
	 */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	err = vfs_write(lower_file, page_data + from, bytes,
			&lower_file->f_pos);
	set_fs(old_fs);

	kunmap(page);

	if (err < 0)
		goto out;

	/* if vfs_write succeeded above, sync up our times/sizes */
	lower_inode = lower_file->f_path.dentry->d_inode;
	if (!lower_inode)
		lower_inode = unionfs_lower_inode(inode);
	BUG_ON(!lower_inode);
	fsstack_copy_inode_size(inode, lower_inode);
	unionfs_copy_attr_times(inode);
	mark_inode_dirty_sync(inode);

out:
	if (err < 0)
		ClearPageUptodate(page);

	unionfs_check_file(file);
	unionfs_read_unlock(file->f_path.dentry->d_sb);
	return err;		/* assume all is ok */
}

/*
 * Although unionfs isn't a block-based file system, it may stack on one.
 * ->bmap is needed, for example, to swapon(2) files.
 */
sector_t unionfs_bmap(struct address_space *mapping, sector_t block)
{
	int err = -EINVAL;
	struct inode *inode, *lower_inode;
	sector_t (*bmap)(struct address_space *, sector_t);

	inode = (struct inode *)mapping->host;
	lower_inode = unionfs_lower_inode(inode);
	if (!lower_inode)
		goto out;
	bmap = lower_inode->i_mapping->a_ops->bmap;
	if (bmap)
		err = bmap(lower_inode->i_mapping, block);
out:
	return err;
}


struct address_space_operations unionfs_aops = {
	.writepage	= unionfs_writepage,
	.writepages	= unionfs_writepages,
	.readpage	= unionfs_readpage,
	.prepare_write	= unionfs_prepare_write,
	.commit_write	= unionfs_commit_write,
	.bmap		= unionfs_bmap,
};
