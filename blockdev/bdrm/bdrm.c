// SPDX-License-Identifier: GPL-2.0-only

#include <linux/blkdev.h>
#include <linux/moduleparam.h>
#include <linux/btree.h>
#include <linux/bio.h>
#include <linux/list.h>

MODULE_DESCRIPTION("Block Device Redirect Module");
MODULE_AUTHOR("Mike Gavrilenko - @qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

#define MAX_BD_NAME_LENGTH 15
#define MAX_MINORS_AM 20
#define MAIN_BLKDEV_NAME "bdr"
#define POOL_SIZE 50

/* Redefine sector as 32b ulong, bc provided kernel btree stores ulong keys */
typedef unsigned long bdrm_sector;

static int bdrm_current_redirect_pair_index;
static int bdrm_major;
static bdrm_sector free_write_sector = 8;
struct bio_set *bdrm_pool;
struct list_head bd_list;

typedef struct bdrm_manager {
	char *bd_name;
	struct gendisk *middle_disk;
	struct bdev_handle *bdev_handler;
	struct btree_head *map_tree;
	struct list_head list;
} bdrm_manager;

static int vector_add_bd(struct bdrm_manager *current_bdev_manager)
{
	list_add(&current_bdev_manager->list, &bd_list);

	return 0;
}

static int check_bdrm_manager_by_name(char *bd_name)
{
	struct bdrm_manager *entry;

	list_for_each_entry(entry, &bd_list, list) {
		if (entry->middle_disk->disk_name == bd_name && entry->bdev_handler != NULL) 
		return 0;
	}

	return -1;
}

static struct bdrm_manager *get_list_element_by_index(int index) {
	struct bdrm_manager *entry;
	int i = 0;

	list_for_each_entry(entry, &bd_list, list) {
		if (i == index) {
			return entry;
		}
		i++;
	}

	return NULL;
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
	if (check_bdrm_manager_by_name(bio->bi_bdev->bd_disk->disk_name)) {
		pr_err("No such bdrm_manager with middle disk %s and not empty handler\n", bio->bi_bdev->bd_disk->disk_name);
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

	pr_info("Block size: %d\n", main_bio->bi_iter.bi_size);

	original_sector = kmalloc(sizeof(unsigned long), GFP_KERNEL);

	if (original_sector == NULL)
		goto mem_err;

	redirected_sector = kmalloc(sizeof(unsigned long), GFP_KERNEL);

	if (redirected_sector == NULL)
		goto mem_err;

	*original_sector = main_bio->bi_iter.bi_sector + clone_bio->bi_iter.bi_size / SECTOR_SIZE;
	*redirected_sector = *original_sector;

	/* Placebo rn. We support only 4kb blocks, so we could hardcode, but in future sizes would be more diverse  */
	// free_write_sector += (clone_bio->bi_iter.bi_size + SECTOR_SIZE - 1) / SECTOR_SIZE;
	mapped_redirect_address = btree_lookup(bptree_head, &btree_geo64, original_sector);

	pr_info("WRITE: head : %lu, key: %p, val: %p\n", (unsigned long)bptree_head, original_sector, redirected_sector);
	pr_info("Sector size: %d, next free sector: %d\n", SECTOR_SIZE, free_write_sector);

	if (mapped_redirect_address && mapped_redirect_address != redirected_sector) {
		btree_remove(bptree_head, &btree_geo64, original_sector);
		pr_info("DEBUG: removed old mapping (mapped_redirect_address = %p redirected_sector = %p)\n", mapped_redirect_address, redirected_sector);
	}

        status = btree_insert(bptree_head, &btree_geo64, original_sector, redirected_sector, GFP_KERNEL);

        if (status)
		return PTR_ERR(&status);

	clone_bio->bi_iter.bi_sector = *redirected_sector;

	return 0;

mem_err:
	pr_err("Memory allocation failed\n");
	kfree(original_sector);
	kfree(redirected_sector);
	return -ENOMEM;
}

static int setup_read_from_clone_segments(struct bio *main_bio, struct bio *clone_bio, struct btree_head *bptree_head)
{
	bdrm_sector *original_sector;
	bdrm_sector *redirected_sector;

	pr_info("Block size: %d\n", main_bio->bi_iter.bi_size);

	original_sector = kmalloc(sizeof(unsigned long), GFP_KERNEL);

	if (original_sector == NULL)
		goto mem_err;

	/* original_sector - Middle disk sector */
	*original_sector = main_bio->bi_iter.bi_sector + clone_bio->bi_iter.bi_size / SECTOR_SIZE;
	redirected_sector = btree_lookup(bptree_head, &btree_geo64, original_sector);

	if (redirected_sector != NULL) {
		pr_info("Found redirected sector: %lu\n", *redirected_sector);
	} else {
		pr_info("Redirected ssector has not been found\n");
	}

	pr_info("READ: head: %lu, key: %p, val: %p\n", (unsigned long)bptree_head, original_sector, redirected_sector);

	if (redirected_sector == NULL) {
		pr_info("Sector: %lu isnt mapped\n", *original_sector);
		redirected_sector = kmalloc(sizeof(unsigned long), GFP_KERNEL);

		*redirected_sector = *original_sector;
		// What should we do in this case?

		
		// *redirected_sector = free_read_sector;

		// /* Update next_free_sector to the next available sector */
		// free_read_sector += (clone_bio->bi_iter.bi_size + SECTOR_SIZE - 1) / SECTOR_SIZE;

		// pr_info("READ: head: %lu, key: %p, val: %p\n", (unsigned long)bptree_head, original_sector, redirected_sector);
                // status = btree_insert(bptree_head, &btree_geo64, original_sector, redirected_sector, GFP_KERNEL);

                // if (status < 0) {
		// 	pr_err("Failed to insert mapping for sector %p\n", (void *)original_sector);
		// 	return -EFAULT;
		// }
	}

	clone_bio->bi_iter.bi_sector = *redirected_sector;

	return 0;

mem_err:
	pr_err("Memory allocation failed\n");
	kfree(original_sector);
	kfree(redirected_sector);
	return -ENOMEM;
}

static void bdrm_bio_end_io(struct bio *bio)
{
	bio_endio(bio->bi_private);
	bio_put(bio);
}

/**
 * bdr_submit_bio() - Takes the provided bio, allocates a clone (child)
 * for a redirect_bd. Although, it changes the way both bio's will end (+ maps bio address with free one from aim BD in b+tree) and submits them.
 * @bio - Expected bio request
 */
static void bdr_submit_bio(struct bio *bio)
{
	int status;
	struct bio *clone;
	struct bdrm_manager *current_redirect_manager;
	struct btree_head *current_bptree_head;

	status = check_bio_link(bio);

	if (status) {
		bdrm_bio_end_io(bio);
		return;
	}

	pr_info("Redirect index in vector = %d\n", bdrm_current_redirect_pair_index);

	current_redirect_manager = get_list_element_by_index(bdrm_current_redirect_pair_index);
	current_bptree_head = get_list_element_by_index(bdrm_current_redirect_pair_index)->map_tree;

	clone = bio_alloc_clone(current_redirect_manager->bdev_handler->bdev, bio, GFP_KERNEL, bdrm_pool);

	if (!clone) {
		pr_err("Bio alalocation failed\n");
		bio_io_error(bio);
		return;
	}

	clone->bi_private = bio;
	clone->bi_end_io = bdrm_bio_end_io;

	if (bio_op(bio) == REQ_OP_READ) {
		status = setup_read_from_clone_segments(bio, clone, current_bptree_head);
	} else if (bio_op(bio) == REQ_OP_WRITE) {
		status = setup_write_in_clone_segments(bio, clone, current_bptree_head);
	} else {
		pr_warn("Unknown Operation in abio\n");
	}

	if (IS_ERR((void*) (intptr_t) status)) {
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
 * init_disk_bd() - Initialises gendisk structure, for 'middle' disk
 * @bd_name: name of creating BD
 *
 * DOESN'T SET UP the disks capacity, check bdr_submit_bio()
 * AND DOESN'T ADD disk if there was one already TO FIX
 */
static struct gendisk *init_disk_bd(char *bd_name)
{
	struct gendisk *new_disk = NULL;
	struct bdrm_manager *linked_manager = NULL;

	new_disk = blk_alloc_disk(NUMA_NO_NODE);

	new_disk->major = bdrm_major;
	new_disk->first_minor = 1;
	new_disk->minors = MAX_MINORS_AM;
	new_disk->fops = &bdr_bio_ops;

	if (bd_name) {
		strcpy(new_disk->disk_name, bd_name);
	} else {
		/* all in all - it can't happen, due to prev. checks in create_bd */
		WARN_ON("bd_name is NULL, nothing to copy\n");
		goto free_bd_meta;
	}

	if (list_empty(&bd_list)) {
		pr_info("Couldn't init disk, bc list is empty\n");
		return NULL;
	}

	linked_manager = list_last_entry(&bd_list, struct bdrm_manager, list);
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
 * not - return -EINVAL. Additionally adds the BD to the vector.
 */
static int check_and_open_bd(char *bd_path)
{
	int error;
	struct bdrm_manager *current_bdev_manager = kmalloc(sizeof(struct bdrm_manager), GFP_KERNEL);
	struct bdev_handle *current_bdev_handle = NULL;

	current_bdev_handle = open_bd_on_rw(bd_path);

	if (IS_ERR(current_bdev_handle)) {
		pr_err("Couldnt open bd by path: %s\n", bd_path);
		goto free_bdev;
	}

	current_bdev_manager->bdev_handler = current_bdev_handle;
	current_bdev_manager->bd_name = bd_path;
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
 * Adds disk to the last bdrm_manager, that was modified by adding bdev_handler through check_and_open_bd()
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

	if (list_empty(&bd_list)) {
		pr_info("Couldn't init disk, bc list is empty\n");
		goto disk_init_err;
	}

	list_last_entry(&bd_list, struct bdrm_manager, list)->middle_disk = new_disk;
	strcpy(list_last_entry(&bd_list, struct bdrm_manager, list)->middle_disk->disk_name, disk_name);

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
	if (get_list_element_by_index(index)->bdev_handler) {
		bdev_release(get_list_element_by_index(index)->bdev_handler);
		get_list_element_by_index(index)->bdev_handler = NULL;
	} else {
		pr_info("Bdev handler of %d is empty\n", index + 1);
	}

	if (get_list_element_by_index(index)->middle_disk) {
		del_gendisk(get_list_element_by_index(index)->middle_disk);
		put_disk(get_list_element_by_index(index)->middle_disk);
		get_list_element_by_index(index)->middle_disk = NULL;
	}
	if (get_list_element_by_index(index)->map_tree) {
		btree_destroy(get_list_element_by_index(index)->map_tree);
		get_list_element_by_index(index)->map_tree = NULL;
	}

	list_del(&(get_list_element_by_index(index)->list));

	pr_info("Removed bdev with index %d (from list)\n", index + 1);
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
	struct bdrm_manager *current_manager;
	int total_length = 0;
	int offset = 0;
	int i = 0;
	int length = 0;

	if (list_empty(&bd_list)) {
		pr_warn("Vector is empty\n");
		return 0;
	}

	list_for_each_entry(current_manager, &bd_list, list) {
		if (current_manager->bdev_handler != NULL) {
			i++;
			length = sprintf(buf + offset, "%d. %s -> %s\n", i, current_manager->middle_disk->disk_name, current_manager->bdev_handler->bdev->bd_disk->disk_name);

			if (length < 0) {
				pr_err("Error in formatting string\n");
				return -EFAULT;
			}

			offset += length;
			total_length += length;
		}
		if (list_empty(&bd_list)) {
			pr_warn("Vector should be empty, but is not\n");
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
 * Function links 'middle' BD and the aim one, for vector purposes. (creates, opens and links)
 * @arg - "from_disk_postfix path"
 */
static int bdr_set_redirect_bd(const char *arg, const struct kernel_param *kp)
{
	int status;
	int index;
	char path[MAX_BD_NAME_LENGTH];
	struct btree_head *root = kmalloc(sizeof(struct btree_head), GFP_KERNEL);

	if (sscanf(arg, "%d %s", &index, path) != 2) {
		pr_err("Wrong input, 2 values are required\n");
		return -EINVAL;
	}

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

	list_last_entry(&bd_list, struct bdrm_manager, list)->map_tree = root;

	status = create_bd(index);

	if (status)
		return PTR_ERR(&status);

	return 0;
}

static int __init bdr_init(void)
{
	int status;

	pr_info("BDR module init\n");
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

	INIT_LIST_HEAD(&bd_list);

	return 0;
mem_err:
	kfree(bdrm_pool);
	pr_err("Memory allocation failed\n");
	return -ENOMEM;
}

static void __exit bdr_exit(void)
{
	int i = 0;
	struct bdrm_manager *entry, *tmp;

	if (!list_empty(&bd_list)) {
		while (get_list_element_by_index(i) != NULL) {
			delete_bd(i + 1);
		}
	}

	list_for_each_entry_safe(entry, tmp, &bd_list, list) {
		list_del(&entry->list);
		kfree(entry);
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
module_param_cb(delete_bd, &bdr_delete_ops, NULL, 0200);

MODULE_PARM_DESC(get_bd_names, "Get list of disks and their redirect bd's");
module_param_cb(get_bd_names, &bdr_get_bd_ops, NULL, 0644);

MODULE_PARM_DESC(set_redirect_bd, "Link local disk with redirect aim bd");
module_param_cb(set_redirect_bd, &bdr_redirect_ops, NULL, 0200);

module_init(bdr_init);
module_exit(bdr_exit);
