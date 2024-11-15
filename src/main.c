// SPDX-License-Identifier: GPL-2.0-only

#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/list.h>
#include <linux/moduleparam.h>
#include "include/ds-control.h"
#include "main.h"

MODULE_DESCRIPTION("Log-Structured virtual Block Device Driver module");
MODULE_AUTHOR("Mike Gavrilenko - @qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

static int bdd_current_redirect_pair_index;
static int bdd_major;
char sel_ds[MAX_DS_NAME_LEN + 1];
struct bio_set *bdd_pool;
struct list_head bd_list;
static const char *available_ds[] = { "bt", "sl", "hm", "rb"};


static int vector_add_bd(struct bdd_manager *current_bdev_manager)
{
	list_add(&current_bdev_manager->list, &bd_list);

	return 0;
}

static int check_bdd_manager_by_name(char *bd_name)
{
	struct bdd_manager *entry;

	list_for_each_entry(entry, &bd_list, list) {
		if (entry->middle_disk->disk_name == bd_name &&
			entry->bdev_handler != NULL)
			return 0;
	}

	return -1;
}

static struct bdd_manager *get_list_element_by_index(int index)
{
	struct bdd_manager *entry;
	uint8_t i = 0;

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


static struct bdev_handle *open_bd_on_rw(char *bd_path)
{
	return bdev_open_by_path(bd_path, BLK_OPEN_WRITE | BLK_OPEN_READ, NULL,
							 NULL);
}

static void bdd_bio_end_io(struct bio *bio)
{
	bio_endio(bio->bi_private);
	bio_put(bio);
}

/**
 * Configures write operations in clone segments for the specified BIO.
 * Allocates memory for original and redirected sector data, retrieves the current
 * redirection info from the chosen data structure, and updates the mapping if necessary.
 * The redirected sector is then set in the clone BIO for processing.
 *
 * @main_bio - The original BIO representing the main device I/O operation.
 * @clone_bio - The clone BIO representing the redirected I/O operation.
 * @bdd_manager - Manager that stores information about used ds and bdd in whole.
 *
 * It returns 0 on success, -ENOMEM if memory allocation fails.
 */
static int setup_write_in_clone_segments(struct bio *main_bio, struct bio *clone_bio, struct bdd_manager *current_redirect_manager)
{
	int8_t status;
	sector_t *original_sector = NULL;
	sector_t *redirected_sector = NULL;
	struct redir_sector_info *old_mapped_rs_info = NULL; 
	struct redir_sector_info *curr_rs_info = NULL;
	
	original_sector = kmalloc(sizeof(uint64_t), GFP_KERNEL);
	curr_rs_info = kzalloc(sizeof(struct redir_sector_info), GFP_KERNEL);
	redirected_sector = kmalloc(sizeof(uint64_t), GFP_KERNEL);

	if (!(redirected_sector && curr_rs_info && original_sector))
		goto mem_err; 

	redirected_sector = kmalloc(sizeof(uint64_t), GFP_KERNEL);
	if (redirected_sector == NULL) 
		goto mem_err;

	if (main_bio->bi_iter.bi_sector == 0)
		*original_sector = SECTOR_OFFSET;
	else
		*original_sector = SECTOR_OFFSET + main_bio->bi_iter.bi_sector;
	
	*redirected_sector = *original_sector;

	pr_debug("Original sector: bi_sector = %llu, block_size %u\n",
			main_bio->bi_iter.bi_sector, clone_bio->bi_iter.bi_size);

	curr_rs_info->block_size = main_bio->bi_iter.bi_size;
	curr_rs_info->redirected_sector = redirected_sector;

	old_mapped_rs_info = ds_lookup(current_redirect_manager->sel_data_struct, original_sector);
	if (!old_mapped_rs_info)
		pr_err("Lookup in data structure _ failed\n");

	pr_debug("Old rs %p", old_mapped_rs_info);
	pr_info("WRITE: key: %llu, sec: %llu\n", *original_sector, *curr_rs_info->redirected_sector);

	if (old_mapped_rs_info && old_mapped_rs_info->redirected_sector != redirected_sector) {
		ds_remove(current_redirect_manager->sel_data_struct, original_sector);
	}

	status = ds_insert(current_redirect_manager->sel_data_struct, original_sector, curr_rs_info);	
	if (status)
		goto insert_err;

	clone_bio->bi_iter.bi_sector = *redirected_sector;

	return 0;

insert_err:
	pr_err("Failed inserting key: %llu vallue: %p in _\n", *original_sector, curr_rs_info);
	kfree(original_sector);
	kfree(redirected_sector);
	kfree(curr_rs_info);
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
	struct bio *split_bio = NULL; // first half of splitted bio

	split_bio = bio_split(clone_bio, nearest_bs / SECTOR_SIZE, GFP_KERNEL, bdd_pool);
	if (!split_bio)
		return -1;

	pr_debug("RECURSIVE READ p1: bs = %u, main to read = %u, st sec = %llu\n",
		split_bio->bi_iter.bi_size, main_bio->bi_iter.bi_size, split_bio->bi_iter.bi_sector);
	pr_debug("RECURSIVE READ p2: bs = %u, main to read = %u,  st sec = %llu\n",
		clone_bio->bi_iter.bi_size, main_bio->bi_iter.bi_size, clone_bio->bi_iter.bi_sector);

	submit_bio(split_bio);
	pr_debug("Submitted bio first part of splitted main_bio\n\n");

	return nearest_bs;
}


/**
 * Configures read operations for clone segments based on redirection info from
 * the chosen data structure. This function retrieves the mapped or previous sector information,
 * determines the appropriate sector to read, and optionally splits the clone BIO
 * if more data is required. Handles cases where redirected and original sector
 * start points differ.
 *
 * @main_bio - The primary BIO representing the main device I/O operation.
 * @clone_bio - The clone BIO representing the redirected I/O operation.
 * @redirect_manager - Manages redirection data for mapped sectors.
 *
 * It returns 0 on success, -ENOMEM if memory allocation fails, or -1 on split error.
 */
static int setup_read_from_clone_segments(struct bio *main_bio, struct bio *clone_bio, struct bdd_manager *redirect_manager)
{
	struct redir_sector_info *curr_rs_info = NULL;
	struct redir_sector_info *prev_rs_info = NULL;
	struct redir_sector_info *last_rs = NULL;
	sector_t *original_sector = NULL;
	sector_t *redirected_sector = NULL;
	int32_t to_read_in_clone;
	int16_t status;

	if (main_bio->bi_iter.bi_size == 0)
		return 0;

	curr_rs_info = kzalloc(sizeof(struct redir_sector_info), GFP_KERNEL);

	if (curr_rs_info == NULL)
		goto mem_err;

	prev_rs_info = kzalloc(sizeof(struct redir_sector_info), GFP_KERNEL);

	if (prev_rs_info == NULL)
		goto mem_err;

	original_sector = kmalloc(sizeof(sector_t), GFP_KERNEL);
	if (original_sector == NULL) 
		goto mem_err;

	*original_sector = SECTOR_OFFSET + main_bio->bi_iter.bi_sector;
	curr_rs_info = ds_lookup(redirect_manager->sel_data_struct, original_sector);
	pr_info("curr_rs_info = %p\n", curr_rs_info);

	pr_info("READ: key: %llu\n", *original_sector);

	if (!curr_rs_info) { // Read & Write sector starts aren't equal.
		pr_debug("Sector: %llu isnt mapped\n", *original_sector);
	
		if (ds_empty_check(redirect_manager->sel_data_struct)) { // ds is empty -> we're getting system BIO's
			redirected_sector = kmalloc(sizeof(unsigned long), GFP_KERNEL);
			if (redirected_sector == NULL)
				goto mem_err;

			*redirected_sector = *original_sector;
			return 0;
		}

		last_rs = ds_last(redirect_manager->sel_data_struct, original_sector);
		pr_debug("last_rs = %llu\n", *last_rs->redirected_sector);

		if (*original_sector > *last_rs->redirected_sector) {
			/**  We got a system check in the middle of respond to
			 * bio)... It means that we are processing bio, whose orig_sector
			 * isn't mapped and is bigger then every mapped sector.
			 */
			clone_bio->bi_iter.bi_sector = *original_sector;
			pr_debug("Recognised system bio\n");
			return 0;
		}
		
		prev_rs_info = ds_prev(redirect_manager->sel_data_struct, original_sector);
		clone_bio->bi_iter.bi_sector = *original_sector;
		to_read_in_clone = (*original_sector * 512 + main_bio->bi_iter.bi_size) - (*prev_rs_info->redirected_sector * 512 + prev_rs_info->block_size);
		/* Address of main block end (reading fr:om original sector -> bi_size) -  First address of written blocks after original_sector */

		pr_debug("To read = %d, main size = %u, prev_rs bs = %u, prev_rs sector = %llu\n", to_read_in_clone, main_bio->bi_iter.bi_size, prev_rs_info->block_size, *prev_rs_info->redirected_sector);
		pr_debug("Clone bio: sector = %llu, size = %u, sec num = %u\n", clone_bio->bi_iter.bi_sector, clone_bio->bi_iter.bi_size, clone_bio->bi_iter.bi_size / SECTOR_SIZE);

		if (clone_bio->bi_iter.bi_size <= prev_rs_info->block_size + clone_bio->bi_iter.bi_size - to_read_in_clone && clone_bio->bi_iter.bi_sector + clone_bio->bi_iter.bi_size / SECTOR_SIZE > *prev_rs_info->redirected_sector + prev_rs_info->block_size / SECTOR_SIZE) {
			status = setup_bio_split(clone_bio, main_bio, clone_bio->bi_iter.bi_size - to_read_in_clone);
			if (status < 0)
				goto split_err;

		} else if (clone_bio->bi_iter.bi_size > prev_rs_info->block_size + clone_bio->bi_iter.bi_size - to_read_in_clone) {
			while (to_read_in_clone > 0) {
				status = setup_bio_split(clone_bio, main_bio, prev_rs_info->block_size);
				if (status < 0)
					goto split_err;

				to_read_in_clone -= status;
			}
			clone_bio->bi_iter.bi_size = prev_rs_info->block_size;
		}
	} else if (curr_rs_info->redirected_sector) { // Read & Write start sectors are equal.
		pr_debug("Found redirected sector: %llu, rs_bs = %u, main_bs = %u\n",
			*(curr_rs_info->redirected_sector), curr_rs_info->block_size, main_bio->bi_iter.bi_size);

		to_read_in_clone = main_bio->bi_iter.bi_size - curr_rs_info->block_size; 
		clone_bio->bi_iter.bi_sector = *curr_rs_info->redirected_sector;

		while (to_read_in_clone > 0) {
			to_read_in_clone -= setup_bio_split(clone_bio, main_bio, curr_rs_info->block_size);
			if (status < 0)
				goto split_err;
		}
		clone_bio->bi_iter.bi_size = (to_read_in_clone < 0) ? curr_rs_info->block_size + to_read_in_clone : curr_rs_info->block_size;
		pr_debug("End of read, Clone: size: %u, sector %llu, to_read = %d\n", clone_bio->bi_iter.bi_size, clone_bio->bi_iter.bi_sector, to_read_in_clone);
	}
	return 0;

split_err:
	pr_err("Bio split went wrong\n");
	kfree(curr_rs_info);
	kfree(prev_rs_info);
	kfree(original_sector);
	bio_io_error(main_bio);
	return -1;

mem_err:
	pr_err("Memory allocation failed.\n");
	kfree(prev_rs_info);
	kfree(curr_rs_info);
	kfree(original_sector);
	return -ENOMEM;
}

/**
 * lsbdd_submit_bio() - Takes the provided bio, allocates a clone (child)
 * for a redirect_bd. Although, it changes the way both bio's will end (+ maps
 * bio address with free one from aim BD in chosen data structure) and submits them.
 *
 * @bio - Expected bio request
 */
static void lsbdd_submit_bio(struct bio *bio)
{
	struct bio *clone = NULL;
	struct bdd_manager *current_redirect_manager = NULL;
	int16_t status;

	pr_info("Entered submit bio\n");

	status = check_bdd_manager_by_name(bio->bi_bdev->bd_disk->disk_name);
	if (status) 
		goto check_err;

	current_redirect_manager = get_list_element_by_index(bdd_current_redirect_pair_index);

	clone = bio_alloc_clone(current_redirect_manager->bdev_handler->bdev, bio,
							GFP_KERNEL, bdd_pool);
	if (!clone)
		goto clone_err;

	clone->bi_private = bio;
	clone->bi_end_io = bdd_bio_end_io;

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
	return;

check_err:
	pr_err("No such bdd_manager with middle disk %s and not empty handler\n",
		bio->bi_bdev->bd_disk->disk_name);
	bdd_bio_end_io(bio);
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

static const struct block_device_operations lsbdd_bio_ops = {
	.owner = THIS_MODULE,
	.submit_bio = lsbdd_submit_bio,
};

/**
 * init_disk_bd() - Initialises gendisk structure, for 'middle' disk
 * @bd_name: name of creating BD
 *
 * DOESN'T SET UP the disks capacity, check lsbdd_submit_bio()
 * AND DOESN'T ADD disk if there was one already TO FIX?
 */
static struct gendisk *init_disk_bd(char *bd_name)
{
	struct gendisk *new_disk = NULL;
	struct bdd_manager *linked_manager = NULL;

	new_disk = blk_alloc_disk(NUMA_NO_NODE);

	new_disk->major = bdd_major;
	new_disk->first_minor = 1;
	new_disk->minors = MAX_MINORS_AM;
	new_disk->fops = &lsbdd_bio_ops;

	if (bd_name) {
		strcpy(new_disk->disk_name, bd_name);
	} else {
		pr_warn("bd_name is NULL, nothing to copy\n");
		return NULL;
	}

	if (list_empty(&bd_list)) {
		pr_warn("Couldn't init disk, bc list is empty\n");
		return NULL;
	}

	linked_manager = list_last_entry(&bd_list, struct bdd_manager, list);
	set_capacity(new_disk,
				 get_capacity(linked_manager->bdev_handler->bdev->bd_disk));

	return new_disk;
}

/**
 * check_and_open_bd() - Checks if name is occupied, if so - opens the BD, if
 * not - return -EINVAL. Additionally adds the BD to the vector.
 * and initialises data_struct.
 */
static int check_and_open_bd(char *bd_path)
{
	struct bdd_manager *current_bdev_manager = kzalloc(sizeof(struct bdd_manager), GFP_KERNEL);
	struct bdev_handle *current_bdev_handle = NULL;
	struct data_struct *curr_ds = kzalloc(sizeof(struct data_struct), GFP_KERNEL);
	
	if (!curr_ds + !current_bdev_manager > 0)
		goto mem_err;
	
	current_bdev_handle = open_bd_on_rw(bd_path);

	if (IS_ERR(current_bdev_handle))
		goto free_bdev;

	current_bdev_manager->bdev_handler = current_bdev_handle;
	current_bdev_manager->bd_name = bd_path;
	current_bdev_manager->sel_data_struct = curr_ds;

	vector_add_bd(current_bdev_manager);

	pr_info("Succesfully added %s to vector\n", bd_path);

	return 0;

free_bdev:
	pr_err("Couldnt open bd by path: %s\n", bd_path);
	kfree(curr_ds);
	kfree(current_bdev_manager);
	return PTR_ERR(current_bdev_handle);

mem_err:
	kfree(current_bdev_manager);
	kfree(curr_ds);
	return -ENOMEM;
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
 * Adds disk to the last bdd_manager, that was modified by adding bdev_handler
 * through check_and_open_bd()
 *
 * @name_index - an index for disk name
 * */
static int create_bd(int name_index)
{
	char *disk_name = NULL;
	int8_t status;
	struct gendisk *new_disk = NULL;

	disk_name = create_disk_name_by_index(name_index);

	if (!disk_name)
		goto mem_err;

	new_disk = init_disk_bd(disk_name);

	if (!new_disk)
		goto disk_init_err;

	if (list_empty(&bd_list)) {
		pr_err("Couldn't init disk, bc list is empty\n");
		goto disk_init_err;
	}

	list_last_entry(&bd_list, struct bdd_manager, list)->middle_disk =
		new_disk;
	strcpy(list_last_entry(&bd_list, struct bdd_manager, list)
			   ->middle_disk->disk_name,
		   disk_name);

	status = add_disk(new_disk);

	pr_debug("Status after add_disk with name %s: %d\n", disk_name, status);

	if (status) {
		put_disk(new_disk);
		goto disk_init_err;
	}

	return 0;

mem_err:
	pr_err("Memory allocation failed\n");
	kfree(disk_name);
	return -ENOMEM;

disk_init_err:
	pr_err("Disk initialization failed\n");
	kfree(new_disk);
	kfree(disk_name);
	return -ENOMEM;
}

static int delete_bd(int index)
{
	if (get_list_element_by_index(index)->bdev_handler) {
		bdev_release(get_list_element_by_index(index)->bdev_handler);
		get_list_element_by_index(index)->bdev_handler = NULL;
	} else {
		pr_info("BD with num %d is empty\n", index + 1);
	}

	if (get_list_element_by_index(index)->middle_disk) {
		del_gendisk(get_list_element_by_index(index)->middle_disk);
		put_disk(get_list_element_by_index(index)->middle_disk);
		get_list_element_by_index(index)->middle_disk = NULL;
	}
	if (get_list_element_by_index(index)->sel_data_struct) {
		ds_free(get_list_element_by_index(index)->sel_data_struct);
		get_list_element_by_index(index)->sel_data_struct = NULL;
	}

	list_del(&(get_list_element_by_index(index)->list));

	pr_info("Removed bdev with index %d (from list)\n", index + 1);
	return 0;
}

/**
 * lsbdd_get_bd_names() - Function that prints the list of block devices, that
 * are stored in vector.
 *
 * Vector stores only BD's that we've touched from this module.
 */
static int lsbdd_get_bd_names(char *buf, const struct kernel_param *kp)
{
	struct bdd_manager *current_manager = NULL;
	uint8_t total_length = 0;
	uint8_t offset = 0;
	uint8_t i = 0;
	uint8_t length = 0;

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
	}

	return total_length;
}

/**
 * lsbdd_delete_bd() - Deletes bdev according to index from printed list (check
 * lsbdd_get_bd_names)
 */
static int lsbdd_delete_bd(const char *arg, const struct kernel_param *kp)
{
	uint8_t index = convert_to_int(arg) - 1;

	delete_bd(index);

	return 0;
}

static int check_available_ds(char* current_ds)
{
	uint8_t i = 0;
	uint8_t len = 0;

	len = sizeof(available_ds)/sizeof(available_ds[0]);
	
	for(i = 0; i < len; ++i) {
		if(!strcmp(available_ds[i], current_ds))
			return 0;
	}
	return -1;
}

static int lsbdd_get_data_structs(char *buf, const struct kernel_param *kp)
{
	uint8_t i = 0;
	uint8_t offset = 0;
	uint8_t length = 0;
	uint8_t total_length = 0;

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
static int lsbdd_set_data_struct(const char *arg, const struct kernel_param *kp)
{

	if (sscanf(arg, "%s", sel_ds) != 1) {
		pr_err("Wrong input, 1 vallue required\n");
		return -EINVAL;
	}

	if (check_available_ds(sel_ds)) {
		pr_err("%s is not supported. Check available data structure by set_data_structs\n", sel_ds);
		return -1;
	}
	return 0;
}

/**
 * Function links 'middle' BD and the aim one, for vector purposes. (creates,
 * opens and links)
 * @arg - "from_disk_postfix path"
 */
static int lsbdd_set_redirect_bd(const char *arg, const struct kernel_param *kp)
{
	int8_t status;
	int index;
	char path[MAX_BD_NAME_LENGTH];

	if (sscanf(arg, "%d %s", &index, path) != 2) {
		pr_err("Wrong input, 2 values are required\n");
		return -EINVAL;
	}

	status = check_and_open_bd(path);

	if (status)
		return PTR_ERR(&status);
	
	status = ds_init(list_last_entry(&bd_list, struct bdd_manager, list)->sel_data_struct, sel_ds);

	if (status)
		return status;

	status = create_bd(index);

	if (status)
		return status;

	return 0;
}

static int __init lsbdd_init(void)
{
	int8_t status;

	pr_info("LSBDD module initialised\n");
	bdd_major = register_blkdev(0, MAIN_BLKDEV_NAME);

	if (bdd_major < 0) {
		pr_err("Unable to register mybdev block device\n");
		BUG();
	}

	bdd_pool = kzalloc(sizeof(struct bio_set), GFP_KERNEL);

	if (!bdd_pool)
		goto mem_err;

	status = bioset_init(bdd_pool, BIO_POOL_SIZE, 0, 0);

	if (status) {
		pr_err("Couldn't allocate bio set");
		goto mem_err;
	}

	INIT_LIST_HEAD(&bd_list);

	return 0;

mem_err:
	kfree(bdd_pool);
	pr_err("Memory allocation failed\n");
	return -ENOMEM;
}

static void __exit lsbdd_exit(void)
{
	uint8_t i = 0;
	struct bdd_manager *entry, *tmp;

	if (!list_empty(&bd_list)) {
		while (get_list_element_by_index(i) != NULL)
			delete_bd(i + 1);
	}

	list_for_each_entry_safe(entry, tmp, &bd_list, list) {
		list_del(&entry->list);
		kfree(entry);
	}

	bioset_exit(bdd_pool);
	kfree(bdd_pool);
	unregister_blkdev(bdd_major, MAIN_BLKDEV_NAME);

	pr_info("BDR module exited\n");
}

static const struct kernel_param_ops lsbdd_delete_ops = {
	.set = lsbdd_delete_bd,
	.get = NULL,
};

static const struct kernel_param_ops lsbdd_get_bd_ops = {
	.set = NULL,
	.get = lsbdd_get_bd_names,
};

static const struct kernel_param_ops lsbdd_redirect_ops = {
	.set = lsbdd_set_redirect_bd,
	.get = NULL,
};

static const struct kernel_param_ops lsbdd_ds_ops = {
	.set = lsbdd_set_data_struct,
	.get = lsbdd_get_data_structs,
};

MODULE_PARM_DESC(delete_bd, "Delete BD");
module_param_cb(delete_bd, &lsbdd_delete_ops, NULL, 0200);

MODULE_PARM_DESC(get_bd_names, "Get list of disks and their redirect bd's");
module_param_cb(get_bd_names, &lsbdd_get_bd_ops, NULL, 0644);

MODULE_PARM_DESC(set_redirect_bd, "Link local disk with redirect aim bd");
module_param_cb(set_redirect_bd, &lsbdd_redirect_ops, NULL, 0200);

MODULE_PARM_DESC(set_data_structure, "Set data structure to be used in mapping");
module_param_cb(set_data_structure, &lsbdd_ds_ops, NULL, S_IRUGO | S_IWUSR);

module_init(lsbdd_init);
module_exit(lsbdd_exit);
