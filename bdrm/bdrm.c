// SPDX-License-Identifier: GPL-2.0-only

#include <linux/blkdev.h>
#include <linux/moduleparam.h>
#include <linux/btree.h>
#include <linux/bio.h>

MODULE_DESCRIPTION("Block Device Redirect Module");
MODULE_AUTHOR("Mike Gavrilenko - @qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

#define INIT_VECTOR_CAP 4
#define MAX_BD_NAME_LENGTH 15
#define MAIN_BLKDEV_NAME "bdr"
#define POOL_SIZE 50

typedef unsigned long bdrm_sector; // redefine sector as 32b ulong, bc provided kernel btree stores ulong keys. 

static int bdrm_current_redirect_pair_index;
static int bdrm_major;
static bdrm_sector next_free_sector = 8; // global variable to track the next free sector
struct bio_set *bdrm_pool;

typedef struct vector {
	int size;
	int capacity;
	struct blkdev_manager *arr;
} vector;

typedef struct blkdev_manager {
	char *bd_name;
	struct gendisk *middle_disk;
	struct bdev_handle *bdev_handler;
	struct btree_head *map_tree;
} blkdev_manager;

static vector *bd_vector;

static int vector_init(void)
{
	bd_vector = kzalloc(sizeof(vector), GFP_KERNEL);

	if (!bd_vector)
		goto mem_err;

	bd_vector->capacity = INIT_VECTOR_CAP;
	bd_vector->size = 0;
	bd_vector->arr = kcalloc(bd_vector->capacity, sizeof(struct blkdev_manager *), GFP_KERNEL);

	if (!bd_vector->arr)
		goto mem_err;

	return 0;
mem_err:
	kfree(bd_vector);
	bd_vector = NULL;
	pr_err("memory allocation failed\n");
	return -ENOMEM;
}

static int vector_add_bd(struct blkdev_manager *current_bdev_manager)
{
	struct blkdev_manager *new_array;

	if (bd_vector->size < bd_vector->capacity) {
		pr_info("Vector wasn't resized\n");
	} else {
		pr_info("Vector was resized\n");

		bd_vector->capacity *= 2;
		new_array = krealloc(bd_vector->arr, bd_vector->capacity, GFP_KERNEL);

		if (!new_array) {
			pr_info("Vector's array allocation failed\n");
			return -ENOMEM;
		}
		bd_vector->arr = new_array;
	}

	bd_vector->arr[bd_vector->size] = *current_bdev_manager;
	bd_vector->size++;

	return 0;
}

static int vector_check_bd_manager_by_name(char *bd_name)
{
	int i;

	for (i = 0; i < bd_vector->size; i++) {
		if (bd_vector->arr[i].middle_disk->disk_name == bd_name && bd_vector->arr[i].bdev_handler != NULL)
			return 0;
	}
	return -1;
}

static int convert_to_int(const char *arg)
{
	long number;
	int res = kstrtol(arg, 10, &number);

	if (res != 0)
		return res;

	return (int)number;
}

static int check_bio_link(struct bio *bio)
{
	if (vector_check_bd_manager_by_name(bio->bi_bdev->bd_disk->disk_name)) {
		pr_err("No such bd_manager with middle disk %s and not empty handler\n", bio->bi_bdev->bd_disk->disk_name);
		return -EINVAL;
	}

	return 0;
}

static struct bdev_handle *open_bd_on_rw(char *bd_path)
{
	return bdev_open_by_path(bd_path, BLK_OPEN_WRITE | BLK_OPEN_READ, NULL, NULL);
}

static int setup_write_in_clone_segments(struct bio *main_bio, struct bio *clone_bio, struct btree_head *bptree_head)
{
	int status;
	bdrm_sector *original_sector;
	bdrm_sector *redirected_sector;
	bdrm_sector *mapped_redirect_address;

	original_sector = (unsigned long *)kmalloc(sizeof(unsigned long), GFP_KERNEL);
	redirected_sector = (unsigned long *)kmalloc(sizeof(unsigned long), GFP_KERNEL);

	// pr_info("to write: %d\n", clone_bio->bi_iter.bi_size);

	*original_sector = main_bio->bi_iter.bi_sector + clone_bio->bi_iter.bi_size / SECTOR_SIZE;
	*redirected_sector = next_free_sector; // add to doc: (even if the mapping exists - it readds it to support the sequential access)
	next_free_sector += (clone_bio->bi_iter.bi_size + SECTOR_SIZE - 1) / SECTOR_SIZE; // isn;'t it a placebo? we support only 4kb blocks
		pr_info("orig sector: %p bi_sector: %llu\n", (void*)original_sector, main_bio->bi_iter.bi_sector);

	pr_info("WRITE: head : %lu, key: %p, val: %p\n", (unsigned long)bptree_head, original_sector, redirected_sector);
	mapped_redirect_address = btree_lookup(bptree_head, &btree_geo64, original_sector);

	if (mapped_redirect_address && mapped_redirect_address != redirected_sector)
		btree_remove(bptree_head, &btree_geo64, original_sector);

	status = btree_insert(bptree_head, &btree_geo64, original_sector, redirected_sector, GFP_KERNEL);

	if (status)
		return PTR_ERR(&status);

	clone_bio->bi_iter.bi_sector = *redirected_sector;

	return 0;
}

static int setup_read_from_clone_segments(struct bio *main_bio, struct bio *clone_bio, struct btree_head *bptree_head)
{
	bdrm_sector *original_sector;
	bdrm_sector *redirected_sector;
	int status;

	if (clone_bio->bi_iter.bi_size != 4096) {
		pr_err("Undefined block size: %d\n", clone_bio->bi_iter.bi_size);
		return -EFBIG;
	}

	original_sector = (unsigned long *)kmalloc(sizeof(unsigned long), GFP_KERNEL);

	/* original_sector - middle disk segment */
	*original_sector = main_bio->bi_iter.bi_sector + clone_bio->bi_iter.bi_size / SECTOR_SIZE; 
	pr_info("orig sector: %p bi_sector: %llu\n", (void*)original_sector, main_bio->bi_iter.bi_sector);

	redirected_sector = btree_lookup(bptree_head, &btree_geo64, original_sector);
	// TODO: make lookup more efficient by seq node access (btree header doesn't provide such opportunity)
	if (redirected_sector != NULL) {
		pr_info("READ: head: %lu, key: %p, val: %p\n", (unsigned long)bptree_head, original_sector, redirected_sector);
		pr_info("val int: %lu\n", *redirected_sector);

	}
	pr_info("READ: head: %lu, key: %p\n", (unsigned long)bptree_head, original_sector);

	if (redirected_sector == NULL) {
		pr_warn("Sector: %lu isn't mapped\n", original_sector);
		redirected_sector = (unsigned long *)kmalloc(sizeof(unsigned long), GFP_KERNEL);

		*redirected_sector = next_free_sector;

		// pr_info("Next free sector before mapping: %lu\n", next_free_sector);

		// update next_free_sector to the next available sector
		next_free_sector += (clone_bio->bi_iter.bi_size + SECTOR_SIZE - 1) / SECTOR_SIZE; // ensure we round up to the nearest sector
		
		pr_info("READ: head: %lu, key: %p, val: %p\n", (unsigned long)bptree_head, original_sector, redirected_sector);

		status = btree_insert(bptree_head, &btree_geo64, original_sector, redirected_sector, GFP_KERNEL);

		if (status < 0) {
			pr_err("Failed to insert mapping for sector %p\n", (void*)original_sector);
			return -EFAULT;
		}
	}

	clone_bio->bi_iter.bi_sector = *redirected_sector;

	return 0;
}

static void bdrm_bio_end_io(struct bio *bio)
{
	bio_endio(bio->bi_private);
	bio_put(bio);
}

/**
 * bdr_submit_bio() - takes the provided bio, allocates a clone (child)
 * for a redirect_bd. Although, it changes the way both bio's will end and submits them.
 * @bio - Expected bio request
 */
static void bdr_submit_bio(struct bio *bio)
{
	int status;
	struct bio *clone;
	struct blkdev_manager *current_redirect_manager;
	struct btree_head *current_bptree_head;
	pr_info("Entered submit bio\n");

	status = check_bio_link(bio);

	if (status) {
		bdrm_bio_end_io(bio);
		return;
	}

	pr_info("Redirect index in vector = %d\n", bdrm_current_redirect_pair_index);
	
	current_redirect_manager = &bd_vector->arr[bdrm_current_redirect_pair_index];
	current_bptree_head = bd_vector->arr[bdrm_current_redirect_pair_index].map_tree;
	clone = bio_alloc_clone(current_redirect_manager->bdev_handler->bdev, bio, GFP_KERNEL, bdrm_pool);

	if (!clone) {
		pr_err("Bio alalocation failed\n");
		bio_io_error(bio);
		return;
	}
	// pr_info("current bptree head: %lu\n", (unsigned long)&current_bptree_head);

	clone->bi_private = bio;
	clone->bi_end_io = bdrm_bio_end_io;

	if (bio_op(bio) == REQ_OP_READ) {
		pr_info("read\n");
		status = setup_read_from_clone_segments(bio, clone, current_bptree_head);
	} else if (bio_op(bio) == REQ_OP_WRITE) {
		pr_info("write\n");
		status = setup_write_in_clone_segments(bio, clone, current_bptree_head);
	} else {
		pr_warn("Unknown Operation in abio\n");
	}
	// pr_info("ended up operation\n");

	if (IS_ERR(status)) {
		pr_err("Segment setup went wrong\n");
		bio_io_error(bio);
		return;
	}

	submit_bio(clone);

	pr_info("Submitted bio\n\n");
}

static const struct block_device_operations bdr_bio_ops = {
		.owner = THIS_MODULE,
		.submit_bio = bdr_submit_bio,
};

/**
 * init_disk_bd() - Initialises gendisk structure, for 'local' disk
 * @bd_name: name of creating BD
 *
 * DOESN'T SET UP the disks capacity, check bdr_submit_bio()
 * AND DOESN'T ADD disk if there was one already TO FIX
 */
static struct gendisk *init_disk_bd(char *bd_name)
{
	struct gendisk *new_disk = NULL;
	struct blkdev_manager *linked_manager = NULL;

	new_disk = blk_alloc_disk(NUMA_NO_NODE);

	new_disk->major = bdrm_major;
	new_disk->first_minor = 1;
	new_disk->minors = bd_vector->size;
	new_disk->fops = &bdr_bio_ops;

	if (bd_name) {
		strcpy(new_disk->disk_name, bd_name);
	} else {
		/* all in all - it can't happen, due to prev. checks in create_bd */
		WARN_ON("bd_name is NULL, nothing to copy\n");
		goto free_bd_meta;
	}

	linked_manager = &bd_vector->arr[bd_vector->size - 1];
	set_capacity(new_disk, get_capacity(linked_manager->bdev_handler->bdev->bd_disk));

	return new_disk;

free_bd_meta:
	kfree(linked_manager);
	put_disk(new_disk);
	kfree(new_disk);
	return NULL;
}

/**
 * check_and_open_bd() - Checks if name is occupied, if so - opens the BD, if
 * not - return -EINVAL.
 */
static int check_and_open_bd(char *bd_path)
{
	int error;
	struct blkdev_manager *current_bdev_manager = kmalloc(sizeof(struct blkdev_manager), GFP_KERNEL);
	struct bdev_handle *current_bdev_handle = NULL;

	current_bdev_handle = open_bd_on_rw(bd_path);

	if (IS_ERR(current_bdev_handle)) {
		pr_err("Couldnt open bd by path: %s\n", bd_path);
		goto free_bdev;
	}

	current_bdev_manager->bdev_handler = current_bdev_handle;
	current_bdev_manager->bd_name = bd_path;
	pr_info("current aim path: %s\n", bd_path);
	vector_add_bd(current_bdev_manager);

	if (error) {
		pr_err("Vector add failed: no disk with such name\n");
		goto free_bdev;
	}

	pr_info("Vector succesfully supplemented\n");

	return 0;

free_bdev:
	kfree(current_bdev_manager);
	return PTR_ERR(current_bdev_handle);
}

static char *create_disk_name_by_index(int index)
{
	char *disk_name = kmalloc(strlen(MAIN_BLKDEV_NAME) + snprintf(NULL, 0, "%d", index) + 1, GFP_KERNEL);

	if (disk_name != NULL)
		sprintf(disk_name, "%s%d", MAIN_BLKDEV_NAME, index);

	return disk_name;
}

/**
 * Sets the name for a new BD, that will be used as 'device in the middle'.
 * Adds disk to the last bd_manager, that was modified by adding bdev_handler through check_and_open_bd()
 *
 * @name_index - an index for disk name (make sense only in name displaying, DOESN'T SYNC WITH VECTOR INDICES)
 */
static int create_bd(int name_index)
{
	char *disk_name = NULL;
	int status;
	struct gendisk *new_disk = NULL;

	disk_name = create_disk_name_by_index(name_index);

	if (!disk_name)
		goto mem_err;

	new_disk = init_disk_bd(disk_name);

	if (!new_disk) {
		kfree(disk_name);
		goto disk_init_err;
	}

	bd_vector->arr[bd_vector->size - 1].middle_disk = new_disk;
	strcpy(bd_vector->arr[bd_vector->size - 1].middle_disk->disk_name, disk_name);

	status = add_disk(new_disk);

	pr_info("Status after add_disk with name %s: %d\n", disk_name, status);

	if (status) {
		put_disk(new_disk);
		goto disk_init_err;
	}

	return 0;
mem_err:
	kfree(disk_name);
	pr_err("Memory allocation failed\n");
	return -ENOMEM;
disk_init_err:
	kfree(new_disk);
	pr_err("Disk initialization failed\n");
	return -ENOMEM;
}

static int delete_bd(int index)
{
	bdev_release(bd_vector->arr[index].bdev_handler);
	bd_vector->arr[index].bdev_handler = NULL;

	del_gendisk(bd_vector->arr[index].middle_disk);
	put_disk(bd_vector->arr[index].middle_disk);
	bd_vector->arr[index].middle_disk = NULL;

	btree_destroy(bd_vector->arr[index].map_tree);
	kfree(bd_vector->arr[index].map_tree);
	bd_vector->arr[index].map_tree = NULL;

	pr_info("Removed bdev with index %d (from list)", index + 1);

	return 0;
}

/**
 * bdr_get_bd_names() - Function that prints the list of block devices, that
 * are stored in vector.
 *
 * Vector stores only BD's that we've touched from this module.
 */
static int bdr_get_bd_names(char *buf, const struct kernel_param *kp)
{
	struct blkdev_manager current_manager;
	int total_length = 0;
	int offset = 0;
	int i_increased;
	int length = 0;

	if (bd_vector->size == 0) {
		pr_warn("Vector is empty\n");
		return 0;
	}

	for (int i = 0; i < bd_vector->size; i++) {
		current_manager = bd_vector->arr[i];
		if (current_manager.bdev_handler != NULL) {
			i_increased = i + 1;
			length = sprintf(buf + offset, "%d. %s -> %s\n", i_increased, current_manager.middle_disk->disk_name, current_manager.bdev_handler->bdev->bd_disk->disk_name);

			if (length < 0) {
				pr_err("Error in formatting string\n");
				return -EFAULT;
			}

			offset += length;
			total_length += length;
		}
	}

	return total_length;

}

/**
 * bdr_delete_bd() - Deletes bdev according to index from printed list (check bdr_get_bd_names)
 */
static int bdr_delete_bd(const char *arg, const struct kernel_param *kp)
{
	int index = convert_to_int(arg) - 1;

	delete_bd(index);

	return 0;
}

/**
 * This function takes disk prefix, creates it + the name of the BD and makes it the aim of the redirect operation.
 * @arg - "from_disk_postfix path"
 */
static int bdr_set_redirect_bd(const char *arg, const struct kernel_param *kp)
{
	int status;
	int index;
	char path[MAX_BD_NAME_LENGTH];
	struct btree_head *root = kmalloc(sizeof(struct btree_head), GFP_KERNEL);

	// struct mempool_s *mempool = mempool_create_kmalloc_pool(POOL_SIZE, sizeof(sector));

	if (sscanf(arg, "%d %s", &index, path) != 2) {
		pr_err("Wrong input, 2 values are required\n");
		return -EINVAL;
	}

	bdrm_current_redirect_pair_index = bd_vector->size;

	if (status) {
		pr_info("%d\n", status);
		return PTR_ERR(&status);
	}

	status = check_and_open_bd(path);

	if (status)
		return PTR_ERR(&status);

	status = btree_init(root);
	
	if (status)
		return PTR_ERR(&status);

	bd_vector->arr[bdrm_current_redirect_pair_index].map_tree = root;

	status = create_bd(index);

	if (status)
		return PTR_ERR(&status);

	return 0;
}

static int __init bdr_init(void)
{
	int status;

	pr_info("BDR module init\n");
	vector_init();
	bdrm_major = register_blkdev(0, MAIN_BLKDEV_NAME);

	if (bdrm_major < 0) {
		pr_err("Unable to register mybdev block device\n");
		return -EIO;
	}

	bdrm_pool = kzalloc(sizeof(struct bio_set), GFP_KERNEL);

	if (!bdrm_pool)
		goto mem_err;

	status = bioset_init(bdrm_pool, BIO_POOL_SIZE, 0, 0);

	if (status) {
		pr_err("Couldn't allocate bio set");
		goto mem_err;
	}

	return 0;
mem_err:
	kfree(bdrm_pool);
	pr_err("Memory allocation failed\n");
	return -ENOMEM;
}

static void __exit bdr_exit(void)
{
	int i;

	if (!bd_vector->arr) {
		for (i = 0; i < bd_vector->size; i++)
			delete_bd(i + 1);
		kfree(bd_vector);
	}
	bioset_exit(bdrm_pool);
	kfree(bdrm_pool);
	unregister_blkdev(bdrm_major, MAIN_BLKDEV_NAME);

	pr_info("BDR module exit\n");
}

static const struct kernel_param_ops bdr_delete_ops = {
		.set = bdr_delete_bd,
		.get = NULL,
};

static const struct kernel_param_ops bdr_get_bd_ops = {
		.set = NULL,
		.get = bdr_get_bd_names,
};

static const struct kernel_param_ops bdr_redirect_ops = {
		.set = bdr_set_redirect_bd,
		.get = NULL,
};

MODULE_PARM_DESC(delete_bd, "Delete BD");
module_param_cb(delete_bd, &bdr_delete_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(get_bd_names, "Get list of disks and their redirect bd's");
module_param_cb(get_bd_names, &bdr_get_bd_ops, NULL, S_IRUGO | S_IWUSR);

MODULE_PARM_DESC(set_redirect_bd, "Link local disk with redirect aim bd");
module_param_cb(set_redirect_bd, &bdr_redirect_ops, NULL, S_IWUSR);

module_init(bdr_init);
module_exit(bdr_exit);


// prev moves to the left
// last goes to the most left one -> the smallest one
// add check for kmalloc