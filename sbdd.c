#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/bio.h>
#include <linux/bvec.h>
#include <linux/init.h>
#include <linux/wait.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/numa.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/moduleparam.h>
#include <linux/spinlock_types.h>

#define SBDD_SECTOR_SHIFT       9
#define SBDD_SECTOR_SIZE        (1 << SBDD_SECTOR_SHIFT)
#define SBDD_MIB_SECTORS        (1 << (20 - SBDD_SECTOR_SHIFT))
#define SBDD_NAME               "sbdd"

struct sbdd {
	wait_queue_head_t       exitwait;
	spinlock_t              datalock;
	atomic_t                deleting;
	atomic_t                refs_cnt;
	sector_t                capacity;
	u8                      *data;
	struct gendisk          *gd;
};

static struct sbdd              __sbdd = { 0 };
static unsigned long            __sbdd_capacity_mib = 100;

static sector_t sbdd_xfer(struct bio_vec* bvec, sector_t pos, int dir)
{
	void *buff = kmap_atomic(bvec->bv_page) + bvec->bv_offset;
	sector_t len = bvec->bv_len >> SBDD_SECTOR_SHIFT;
	size_t offset;
	size_t nbytes;

	if (pos + len > __sbdd.capacity)
		len = __sbdd.capacity - pos;

	offset = pos << SBDD_SECTOR_SHIFT;
	nbytes = len << SBDD_SECTOR_SHIFT;

	spin_lock(&__sbdd.datalock);

	if (dir)
		memcpy(__sbdd.data + offset, buff, nbytes);
	else
		memcpy(buff, __sbdd.data + offset, nbytes);

	spin_unlock(&__sbdd.datalock);

	pr_debug("pos=%6llu len=%4llu %s\n", pos, len, dir ? "written" : "read");

	kunmap_atomic(buff);
	return len;
}

static void sbdd_submit_bio(struct bio *bio)
{
	struct bvec_iter iter;
	struct bio_vec bvec;
	int dir;
	sector_t pos;

	bio = bio_split_to_limits(bio);
	if (!bio)
		return;

	if (atomic_read(&__sbdd.deleting)) {
		bio_io_error(bio);
		return;
	}

	if (!atomic_inc_not_zero(&__sbdd.refs_cnt)) {
		bio_io_error(bio);
		return;
	}

	dir = bio_data_dir(bio);
	pos = bio->bi_iter.bi_sector;
	bio_for_each_segment(bvec, bio, iter)
		pos += sbdd_xfer(&bvec, pos, dir);

	bio_endio(bio);

	if (atomic_dec_and_test(&__sbdd.refs_cnt))
		wake_up(&__sbdd.exitwait);
}

/*
There are no read or write operations. These operations are performed by
the request() function associated with the request queue of the disk.
*/
static struct block_device_operations const __sbdd_bdev_ops = {
	.owner = THIS_MODULE,
	.submit_bio = sbdd_submit_bio,
};

static int sbdd_create(void)
{
	/* Configure queue */
	struct queue_limits limits = {
		.logical_block_size = SBDD_SECTOR_SIZE,
		.physical_block_size = SBDD_SECTOR_SIZE,
	};
	int ret = 0;

	pr_info("allocating data\n");
	__sbdd.capacity = (sector_t)__sbdd_capacity_mib * SBDD_MIB_SECTORS;
	__sbdd.data = vzalloc(__sbdd.capacity << SBDD_SECTOR_SHIFT);
	if (!__sbdd.data) {
		pr_err("unable to alloc data\n");
		return -ENOMEM;
	}

	spin_lock_init(&__sbdd.datalock);
	init_waitqueue_head(&__sbdd.exitwait);

	pr_info("allocating disk\n");
	__sbdd.gd = blk_alloc_disk(&limits, NUMA_NO_NODE);
	if (IS_ERR(__sbdd.gd)) {
		pr_err("blk_alloc_disk() failed\n");
		ret = PTR_ERR(__sbdd.gd);
		__sbdd.gd = NULL;
		return ret;
	}

	/* Configure gendisk */
	__sbdd.gd->fops = &__sbdd_bdev_ops;
	__sbdd.gd->private_data = &__sbdd;
	scnprintf(__sbdd.gd->disk_name, DISK_NAME_LEN, SBDD_NAME);
	set_capacity(__sbdd.gd, __sbdd.capacity);
	atomic_set(&__sbdd.refs_cnt, 1);

	/*
	Allocating gd does not make it available, add_disk() is required.
	After this call, gd methods can be called at any time. Should not be
	called before the driver is fully initialized and ready to process reqs.
	*/
	pr_info("adding disk\n");
	ret = add_disk(__sbdd.gd);
	if (ret)
		pr_err("add_disk() failed\n");

	return ret;
}

static void sbdd_delete(void)
{
	atomic_set(&__sbdd.deleting, 1);
	atomic_dec_if_positive(&__sbdd.refs_cnt);
	wait_event(__sbdd.exitwait, !atomic_read(&__sbdd.refs_cnt));

	/* gd will be removed only after the last reference put */
	if (__sbdd.gd) {
		pr_info("deleting disk\n");
		del_gendisk(__sbdd.gd);
		put_disk(__sbdd.gd);
	}

	if (__sbdd.data) {
		pr_info("freeing data\n");
		vfree(__sbdd.data);
	}
}

/*
Note __init is for the kernel to drop this function after
initialization complete making its memory available for other uses.
There is also __initdata note, same but used for variables.
*/
static int __init sbdd_init(void)
{
	int ret = 0;

	pr_info("starting initialization...\n");
	ret = sbdd_create();

	if (ret) {
		pr_err("initialization failed\n");
		sbdd_delete();
	} else {
		pr_info("initialization complete\n");
	}

	return ret;
}

/*
Note __exit is for the compiler to place this code in a special ELF section.
Sometimes such functions are simply discarded (e.g. when module is built
directly into the kernel). There is also __exitdata note.
*/
static void __exit sbdd_exit(void)
{
	pr_info("exiting...\n");
	sbdd_delete();
	pr_info("exiting complete\n");
}

/* Called on module loading. Is mandatory. */
module_init(sbdd_init);

/* Called on module unloading. Unloading module is not allowed without it. */
module_exit(sbdd_exit);

/* Set desired capacity with insmod */
module_param_named(capacity_mib, __sbdd_capacity_mib, ulong, S_IRUGO);

/* Note for the kernel: a free license module. A warning will be outputted without it. */
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple Block Device Driver");
