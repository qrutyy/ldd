// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/btree.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include "include/btreeutils.h"
#include "include/skiplist.h"

MODULE_DESCRIPTION("Block Device Redirect Module");
MODULE_AUTHOR("Mike Gavrilenko - @qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

#define MAX_BD_NAME_LENGTH 15
#define MAX_MINORS_AM 20
#define MAX_DS_NAME 2
#define MAIN_BLKDEV_NAME "bdr"
#define POOL_SIZE 50
#define SECTOR_OFFSET 32

/* Redefine sector as 32b ulong, bc provided kernel btree stores ulong keys */
typedef unsigned long bdrm_sector;

static int bdrm_current_redirect_pair_index;
static int bdrm_major;
static char sel_ds[MAX_DS_NAME];
static void* current_data_struct;
struct bio_set *bdrm_pool;
struct list_head bd_list;
static const char *available_ds[] = { "bt", "sl", };

struct btree {
	struct btree_head *head;
};

struct redir_sector_info {
	bdrm_sector *redirected_sector;
	unsigned int block_size;
};

enum data_type {
	BTREE_TYPE,
    SKIPLIST_TYPE
};

struct data_struct {
	enum data_type type;
	union {
        struct btree *map_tree;
        struct skiplist *map_list;
    } structure;
};

struct bdrm_manager {
	char *bd_name;
	struct gendisk *middle_disk;
	struct bdev_handle *bdev_handler;
	struct data_struct *sel_data_struct;
	struct list_head list;
};

static void* get_curr_ds_head(struct bdrm_manager *current_bdev_manager)
{
	if (current_bdev_manager->sel_data_struct->type == BTREE_TYPE) 
		return current_bdev_manager->sel_data_struct->structure.map_tree->head;
	if (current_bdev_manager->sel_data_struct->type == SKIPLIST_TYPE)
		return current_bdev_manager->sel_data_struct->structure.map_list->head;
	return NULL;
}

static int vector_add_bd(struct bdrm_manager *current_bdev_manager)
{
	list_add(&current_bdev_manager->list, &bd_list);

	return 0;
}

static int check_bdrm_manager_by_name(char *bd_name)
{
	struct bdrm_manager *entry;

	list_for_each_entry(entry, &bd_list, list) {
		if (entry->middle_disk->disk_name == bd_name &&
			entry->bdev_handler != NULL)
			return 0;
	}

	return -1;
}

static struct bdrm_manager *get_list_element_by_index(int index)
{
	struct bdrm_manager *entry;
	int i = 0;

	list_for_each_entry(entry, &bd_list, list) {
		if (i == index)
			return entry;
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
		pr_err(
			"No such bdrm_manager with middle disk %s and not empty handler\n",
			bio->bi_bdev->bd_disk->disk_name);
		return -EINVAL;
	}

	return 0;
}

static struct bdev_handle *open_bd_on_rw(char *bd_path)
{
	return bdev_open_by_path(bd_path, BLK_OPEN_WRITE | BLK_OPEN_READ, NULL,
							 NULL);
}

static void bdrm_bio_end_io(struct bio *bio)
{
	bio_endio(bio->bi_private);
	bio_put(bio);
}

static int ds_lookup(struct data_struct curr_ds, bdrm_sector key)
{
	if (curr_ds.type == BTREE_TYPE) {
		return btree_lookup(curr_ds.structure.map_tree->head, &btree_geo64, key);
	}
	if (curr_ds.type == SKIPLIST_TYPE) {
		return skiplist_find_node(key, curr_ds.structure.map_list)->data;
	}
	return 0;
}

static int ds_remove(struct data_struct curr_ds, bdrm_sector key)
{
	int status;
	if (curr_ds.type == BTREE_TYPE) {
		status = btree_remove(curr_ds.structure.map_tree->head, &btree_geo64, key);
	}
	if (curr_ds.type == SKIPLIST_TYPE) {
		status = skiplist_remove(key, curr_ds.structure.map_list);
	}
	if (!status)
		pr_info("ERROR: Failed to remove %d from _\n", sector); // TODO: add ds name
	return status;
}

static int ds_insert(struct data_struct curr_ds, bdrm_sector key, struct *redir_sector_info value)
{
	if (curr_ds.type == BTREE_TYPE) {
		return btree_insert(curr_ds.structure.map_tree->head, &btree_geo64, key, value, GFP_KERNEL);
	}
	if (curr_ds.type == SKIPLIST_TYPE) {
		return skiplist_add(key, value, curr_ds.structure.map_list)->data;
	}
	return 0;
}

/**
 * Configures write operations in clone segments for the specified BIO.
 * Allocates memory for original and redirected sector data, retrieves the current
 * redirection info from the B+Tree, and updates the mapping if necessary.
 * The redirected sector is then set in the clone BIO for processing.
 *
 * @main_bio - The original BIO representing the main device I/O operation.
 * @clone_bio - The clone BIO representing the redirected I/O operation.
 * @bptree_head - Pointer to the B+Tree header used for sector redirection mapping.
 *
 * It returns 0 on success, -ENOMEM if memory allocation fails.
 */
static int setup_write_in_clone_segments(struct bio *main_bio, struct bio *clone_bio, struct redirect_manager current_redirect_manager)
{
	int status;
	bdrm_sector original_sector_val = 0;
	bdrm_sector *original_sector = original_sector_val;
	bdrm_sector redirected_sector_val = 0;
	bdrm_sector *redirected_sector = &redirected_sector;
	struct redir_sector_info *old_mapped_rs_info; // Old redirected_sector from B+Tree
	struct redir_sector_info *curr_rs_info;

	curr_rs_info = kmalloc(sizeof(struct redir_sector_info), GFP_KERNEL);
	if (curr_rs_info == NULL)
		goto mem_err;

	if (main_bio->bi_iter.bi_sector == 0)
		*original_sector = SECTOR_OFFSET;
	else
		*original_sector = SECTOR_OFFSET + main_bio->bi_iter.bi_sector;
	*redirected_sector = *original_sector;

	pr_info("Original sector: bi_sector = %llu, block_size %u\n",
			main_bio->bi_iter.bi_sector, clone_bio->bi_iter.bi_size);

	curr_rs_info->block_size = main_bio->bi_iter.bi_size;
	curr_rs_info->redirected_sector = redirected_sector;

	old_mapped_rs_info = ds_lookup(current_redirect_manager->sel_data_struct, original_sector);
	if (!old_mapped_rs_info)
		goto lookup_err;

	pr_info("WRITE: head : %lu, key: %lu, val: %p\n",
			(unsigned long)bptree_head, *original_sector, redirected_sector);

	if (old_mapped_rs_info &&
		old_mapped_rs_info->redirected_sector != redirected_sector) {
		ds_remove(current_redirect_manager->sel_data_struct, original_sector);
		pr_info("DEBUG: removed old mapping (mapped_redirect_address = %lu redirected_sector = %lu)\n", *old_mapped_rs_info->redirected_sector, *redirected_sector);
	}

	status = ds_insert(current_redirect_manager->sel_data_struct, sector, curr_rs_info);	
	if (status)
		goto insert_err;

	clone_bio->bi_iter.bi_sector = *redirected_sector;

	return 0;

lookup_err:
	pr_err("Lookup in data structure _ failed\n"); // TODO: add ds name
	
insert_err:
	pr_err("Failed inserting key: %d value: %p in _\n", sector, curr_rs_info);
	return status;

mem_err:
	pr_err("Memory allocation failed\n");
	kfree(original_sector);
	kfree(redirected_sector);
	kfree(curr_rs_info);
	return -ENOMEM;
}

/**
 * Prepares a BIO split for partial handling of a clone BIO. Splits the clone BIO
 * into two parts, so the first half (split_bio) can be processed independently.
 * This function submits the split_bio to be read separately from the remaining
 * data in clone_bio.
 *
 * @clone_bio - The clone BIO to be split.
 * @main_bio - The main BIO containing the primary I/O request data.
 * @nearest_bs - The block size in bytes closest to the current data segment.
 *
 * It returns nearest_bs on successful split, -1 if memory allocation fails.
 */
static int setup_bio_split(struct bio *clone_bio, struct bio *main_bio, int nearest_bs)
{
	struct bio *split_bio; // first half of splitted bio

	split_bio = bio_split(clone_bio, nearest_bs / SECTOR_SIZE, GFP_KERNEL, bdrm_pool);
	if (!split_bio)
		return -1;

	pr_info("RECURSIVE READ p1: bs = %u, main to read = %u, st sec = %llu\n",
		split_bio->bi_iter.bi_size, main_bio->bi_iter.bi_size, split_bio->bi_iter.bi_sector);
	pr_info("RECURSIVE READ p2: bs = %u, main to read = %u,  st sec = %llu\n",
		clone_bio->bi_iter.bi_size, main_bio->bi_iter.bi_size, clone_bio->bi_iter.bi_sector);

	submit_bio(split_bio);
	pr_info("Submitted bio\n\n");

	return nearest_bs;
}


/**
 * Configures read operations for clone segments based on redirection info from
 * the B+Tree. This function retrieves the mapped or previous sector information,
 * determines the appropriate sector to read, and optionally splits the clone BIO
 * if more data is required. Handles cases where redirected and original sector
 * start points differ.
 *
 * @main_bio - The primary BIO representing the main device I/O operation.
 * @clone_bio - The clone BIO representing the redirected I/O operation.
 * @bptree_head - Pointer to the B+Tree header used for sector redirection mapping.
 * @redirect_manager - Manages redirection data for mapped sectors.
 *
 * It returns 0 on success, -ENOMEM if memory allocation fails, or -1 on split error.
 */
static int setup_read_from_clone_segments(struct bio *main_bio, struct bio *clone_bio, struct btree_head *bptree_head, struct bdrm_manager *redirect_manager)
{
	struct redir_sector_info *curr_rs_info;
	struct redir_sector_info *prev_rs_info;
	struct redir_sector_info *last_rs = NULL;
	bdrm_sector orig_sector_val = 0;
	bdrm_sector *original_sector = &orig_sector_val;
	bdrm_sector *redirected_sector;
	int32_t to_read_in_clone;
	int16_t status;

	if (main_bio->bi_iter.bi_size == 0)
		return 0;

	curr_rs_info = kmalloc(sizeof(struct redir_sector_info), GFP_KERNEL);

	if (curr_rs_info == NULL)
		goto mem_err;

	prev_rs_info = kmalloc(sizeof(struct redir_sector_info), GFP_KERNEL);

	if (prev_rs_info == NULL)
		goto mem_err;

	*original_sector = SECTOR_OFFSET + main_bio->bi_iter.bi_sector;
	curr_rs_info = ds_lookup(redirect_manager->sel_data_struct, original_sector);

	pr_info("READ: head: %lu, key: %lu\n", (unsigned long)bptree_head, *original_sector);

	if (!curr_rs_info) { // Read & Write sector starts aren't equal.
		pr_info("Sector: %lu isnt mapped\n", *original_sector);

		if (bptree_head->height == 0) { // BTREE is empty and we're getting system BIO's
			redirected_sector = kmalloc(sizeof(unsigned long), GFP_KERNEL);
			if (redirected_sector == NULL)
				goto mem_err;
			*redirected_sector = *original_sector;
			return 0;
		}

		last_rs = btree_last_no_rep(bptree_head, &btree_geo64, original_sector); // TODO
		pr_info("last_rs = %lu\n", *last_rs->redirected_sector);

		if (*original_sector > *last_rs->redirected_sector) {
			/**  We got a system check in the middle of respond to
			 * bio)... It means that we are processing bio, whose orig_sector
			 * isn't mapped and is bigger then every mapped sector.
			 */
			clone_bio->bi_iter.bi_sector = *original_sector;
			pr_info("Recognised system bio\n");
			return 0;
		}

		prev_rs_info = btree_get_prev_no_rep(bptree_head, &btree_geo64, original_sector); // TODO
		clone_bio->bi_iter.bi_sector = *original_sector;
		to_read_in_clone = (*original_sector * 512 + main_bio->bi_iter.bi_size) - (*prev_rs_info->redirected_sector * 512 + prev_rs_info->block_size);
		/* Address of main block end (reading fr:om original sector -> bi_size) -  First address of written blocks after original_sector */

		pr_info("To read = %d, main size = %u, prev_rs bs = %u, prev_rs sector = %lu\n", to_read_in_clone, main_bio->bi_iter.bi_size, prev_rs_info->block_size, *prev_rs_info->redirected_sector);
		pr_info("Clone bio: sector = %llu, size = %u, sec num = %u\n", clone_bio->bi_iter.bi_sector, clone_bio->bi_iter.bi_size, clone_bio->bi_iter.bi_size / SECTOR_SIZE);

		if (clone_bio->bi_iter.bi_size <= prev_rs_info->block_size + clone_bio->bi_iter.bi_size - to_read_in_clone && clone_bio->bi_iter.bi_sector + clone_bio->bi_iter.bi_size / SECTOR_SIZE > *prev_rs_info->redirected_sector + prev_rs_info->block_size / SECTOR_SIZE) {
			status = setup_bio_split(clone_bio, main_bio, clone_bio->bi_iter.bi_size - to_read_in_clone);
			if (status < 0)
				goto split_err;

			pr_info("2 To read = %d, bs = %u, clone bs = %u\n", to_read_in_clone, prev_rs_info->block_size, clone_bio->bi_iter.bi_size);
		} else if (clone_bio->bi_iter.bi_size > prev_rs_info->block_size + clone_bio->bi_iter.bi_size - to_read_in_clone) {
			while (to_read_in_clone > 0) {
				status = setup_bio_split(clone_bio, main_bio, prev_rs_info->block_size);
				if (status < 0)
					goto split_err;

				to_read_in_clone -= status;
				// prev_rs_info = btree_get_next(bptree_head, &btree_geo64, original_sector);

				pr_info("1 To read = %d, bs = %u, clone bs = %u\n", to_read_in_clone, prev_rs_info->block_size, clone_bio->bi_iter.bi_size);
			}
			clone_bio->bi_iter.bi_size = prev_rs_info->block_size;
		}
	} else if (curr_rs_info->redirected_sector) { // Read & Write start sectors are equal.
		pr_info("Found redirected sector: %lu, rs_bs = %u, main_bs = %u\n",
			*curr_rs_info->redirected_sector, curr_rs_info->block_size, main_bio->bi_iter.bi_size);

		to_read_in_clone = main_bio->bi_iter.bi_size - curr_rs_info->block_size; // size of block to read ahead
		clone_bio->bi_iter.bi_sector = *curr_rs_info->redirected_sector;

		while (to_read_in_clone > 0) {
			to_read_in_clone -= setup_bio_split(clone_bio, main_bio, curr_rs_info->block_size);
			if (status < 0)
				goto split_err;
			/** curr_rs_info = btree_get_next(bptree_head, &btree_geo64, original_sector);
			 * pr_info("next rs %lu\n", *curr_rs_info->redirected_sector);
			 */
		}
		clone_bio->bi_iter.bi_size = (to_read_in_clone < 0) ? curr_rs_info->block_size + to_read_in_clone : curr_rs_info->block_size;
		pr_info("End of read, Clone: size: %u, sector %llu, to_read = %d\n", clone_bio->bi_iter.bi_size, clone_bio->bi_iter.bi_sector, to_read_in_clone);
	}
	return 0;

split_err:
	pr_err("Bio split went wrong\n");
	bio_io_error(main_bio);
	return -1;

mem_err:
	pr_err("Memory allocation failed.\n");
	kfree(original_sector);
	kfree(redirected_sector);
	return -ENOMEM;
}

/**
 * bdr_submit_bio() - Takes the provided bio, allocates a clone (child)
 * for a redirect_bd. Although, it changes the way both bio's will end (+ maps
 * bio address with free one from aim BD in b+tree) and submits them.
 *
 * @bio - Expected bio request
 */
static void bdr_submit_bio(struct bio *bio)
{
	struct bio *clone;
	struct bdrm_manager *current_redirect_manager;
	void* current_ds_head;
	int16_t status;

	status = check_bio_link(bio);
	if (status) 
		goto link_err;

	current_redirect_manager = get_list_element_by_index(bdrm_current_redirect_pair_index);

	clone = bio_alloc_clone(current_redirect_manager->bdev_handler->bdev, bio,
							GFP_KERNEL, bdrm_pool);

	if (!clone)
		goto clone_err;

	clone->bi_private = bio;
	clone->bi_end_io = bdrm_bio_end_io;

if (bio_op(bio) == REQ_OP_READ) {
		status = setup_read_from_clone_segments(bio, clone, current_redirect_manager);
	} else if (bio_op(bio) == REQ_OP_WRITE) {
		status = setup_write_in_clone_segments(bio, clone, current_redirect_manager);
	} else {
		pr_warn("Unknown Operation in bio\n");
	}

	if (status)
		goto setup_err;


	submit_bio(clone);
	pr_info("Submitted bio\n\n");

link_err:
	pr_err("Failed to check link\n");
	bdrm_bio_end_io(bio);
	return;

ds_err:
	pr_err("Failed to get data structure's head\n");
	bdrm_bio_end_io(bio);
	return;

clone_err:
	pr_err("Bio allocation failed\n");
	bio_io_error(bio);
	return;

setup_err:
	pr_err("Setup failed with code %d\n", status);
	bio_io_error(bio);
	return;
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
 * AND DOESN'T ADD disk if there was one already TO FIX?
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
	set_capacity(new_disk,
				 get_capacity(linked_manager->bdev_handler->bdev->bd_disk));

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
	struct bdrm_manager *current_bdev_manager =
		kmalloc(sizeof(struct bdrm_manager), GFP_KERNEL);
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
	char *disk_name =
		kmalloc(strlen(MAIN_BLKDEV_NAME) + snprintf(NULL, 0, "%d", index) + 1,
				GFP_KERNEL);

	if (disk_name != NULL)
		sprintf(disk_name, "%s%d", MAIN_BLKDEV_NAME, index);

	return disk_name;
}

/**
 * Sets the name for a new BD, that will be used as 'device in the middle'.
 * Adds disk to the last bdrm_manager, that was modified by adding bdev_handler
 * through check_and_open_bd()
 *
 * @name_index - an index for disk name (make sense only in name displaying,
 * DOESN'T SYNC WITH VECTOR INDICES)
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

	list_last_entry(&bd_list, struct bdrm_manager, list)->middle_disk =
		new_disk;
	strcpy(list_last_entry(&bd_list, struct bdrm_manager, list)
			   ->middle_disk->disk_name,
		   disk_name);

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
			length = sprintf(
				buf + offset, "%d. %s -> %s\n", i,
				current_manager->middle_disk->disk_name,
				current_manager->bdev_handler->bdev->bd_disk->disk_name);

			if (length < 0) {
				pr_err("Error in formatting string\n");
				return -EFAULT;
			}

			offset += length;
			total_length += length;
		}
		if (list_empty(&bd_list))
			pr_warn("Vector should be empty, but is not\n");
	}

	return total_length;
}

/**
 * bdr_delete_bd() - Deletes bdev according to index from printed list (check
 * bdr_get_bd_names)
 */
static int bdr_delete_bd(const char *arg, const struct kernel_param *kp)
{
	int index = convert_to_int(arg) - 1;

	delete_bd(index);

	return 0;
}

static int check_available_ds(char* current_ds)
{
	int len = sizeof(available_ds)/sizeof(available_ds[0])
	for(i = 0; i < len; ++i)
	{
		if(!strcmp(x[i], s))
			return 0;
	}
	return -1;
}

static int bdr_get_data_structs(char *buf, const struct kernel_param *kp)
{
	int i, offset, length, total_length;

	for (i = 0; i < sizeof(available_ds) / sizeof(available_ds[0]); i++) { 
		length = sprintf(buf + offset, "%d. %s\n", i, available_ds[i]);

		if (length < 0) {
			pr_err("Error in formatting string\n");
			return -EFAULT;
		}

		offset += length;
		total_length += length;
	}
    return total_length;
}

/**
 * Function sets data structure that will be used for mapping of sectors
 * @arg - "type"
 */
static int bdr_set_data_struct(const char *arg, const struct kernel_param *kp)
{
	int status;
	if (sscanf(arg, "%s", sel_ds) != 1) {
		pr_err("Wrong input, 1 value required\n");
		return -EINVAL;
	}

	if (check_available_ds(sel_ds)) {
		pr_err("%s is not supported. Check available data structure by get_data_structs\n");
		return -1;
	}
	return 0;
}

static int ds_init(struct data_struct curr_ds)
{
	if (strcmp(sel_ds, "bt")) {
		struct btree *ds = kmalloc(sizeof(struct btree), GFP_KERNEL);
		if (!ds)
			goto mem_err;

		struct btree_head *root = kmalloc(sizeof(struct btree_head), GFP_KERNEL);
		if (!root)
			goto mem_err;
		
		btree_init(root);
		curr_ds.type = BTREE_TYPE;
		curr_ds.structure = ds;
		curr_ds.structure->head = root;
	}
	if (strcmp(sel_ds, "sl")) {
		struct skiplist *ds = skiplist_init();
		curr_ds.type = SKIPLIST_TYPE;
		curr_ds.structure = ds;
	}

	return 0;
	
mem_err:
	pr_err("Memory allocation faile\n");
	kfree(ds);
	kfree(root);
	return -ENOMEM;
}

/**
 * Function links 'middle' BD and the aim one, for vector purposes. (creates,
 * opens and links)
 * @arg - "from_disk_postfix path"
 */
static int bdr_set_redirect_bd(const char *arg, const struct kernel_param *kp)
{
	int status;
	int index;
	char path[MAX_BD_NAME_LENGTH];
	struct redirect_manager current_redirect_manager;

	if (sscanf(arg, "%d %s", &index, path) != 2) {
		pr_err("Wrong input, 2 values are required\n");
		return -EINVAL;
	}

	status = check_and_open_bd(path);

	if (status)
		return PTR_ERR(&status);

	list_last_entry(&bd_list, struct bdrm_manager, list);
	status = ds_init(list_last_entry(&bd_list, struct bdrm_manager, list)->sel_data_struct);

	if (status)
		return PTR_ERR(&status);

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
		while (get_list_element_by_index(i) != NULL)
			delete_bd(i + 1);
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

static const struct kernel_param_ops bdr_set_ds_ops = {
	.set = bdr_set_data_struct,
	.get = bdr_get_data_structs,
}

MODULE_PARM_DESC(delete_bd, "Delete BD");
module_param_cb(delete_bd, &bdr_delete_ops, NULL, 0200);

MODULE_PARM_DESC(get_bd_names, "Get list of disks and their redirect bd's");
module_param_cb(get_bd_names, &bdr_get_bd_ops, NULL, 0644);

MODULE_PARM_DESC(set_redirect_bd, "Link local disk with redirect aim bd");
module_param_cb(set_redirect_bd, &bdr_redirect_ops, NULL, 0200);

MODULE_PARM_DESC(set_data_structure, "Set data structure to be used in mapping");
module_param_cb(set_data_structure, &bdr_set_ds_ops, NULL, 0666);

module_init(bdr_init);
module_exit(bdr_exit);
