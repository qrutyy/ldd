#include <linux/blkdev.h>
#include <linux/moduleparam.h>

MODULE_DESCRIPTION("Block Device Redirect Module");
MODULE_AUTHOR("qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

#define INIT_VECTOR_CAP 4
#define MAX_BD_NAME_LENGTH 15
#define MAIN_BLKDEV_NAME "bdr"

static int current_redirect_pair_index;
static int major;
struct bio_set *pool;

typedef struct vector
{
	int size;
	int capacity;
	struct blkdev_manager *arr;
} vector;

typedef struct blkdev_manager
{ 
	char *bd_path;
	char *bd_name; // TODO init or remove  if init - add free
	struct gendisk *middle_disk; 
	struct bdev_handle *bdev_handler;
} blkdev_manager;

static vector *bd_vector;

static int vector_init(void) 
{
	bd_vector = kzalloc(sizeof(vector), GFP_KERNEL);

	if (!bd_vector)
		goto mem_err;

	bd_vector->capacity = INIT_VECTOR_CAP;
	bd_vector->size = 0;
	bd_vector->arr = kzalloc(sizeof(struct blkdev_manager *) * bd_vector->capacity, GFP_KERNEL);
	
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
	if (bd_vector->size < bd_vector->capacity) {
		pr_info("Vector wasn't resized\n");
	} else {
		pr_info("Vector was resized\n");

		bd_vector->capacity *= 2;
		bd_vector->arr = krealloc(bd_vector->arr, bd_vector->capacity, GFP_KERNEL);
		
		if (!bd_vector->arr) {
			pr_info("Vector's array allocation failed\n");
			return -ENOMEM;
		}
	}

	bd_vector->arr[bd_vector->size] = *current_bdev_manager;
	bd_vector->size++;
	
	return 0;
}

static int vector_check_bd_manager_by_name(char *bd_name)
{
	int i;
	
	pr_info("Vector check input name: %s, real name: %s\n", bd_name, bd_vector->arr[0].bd_name);
	
	for (i = 0; i < bd_vector->size; i ++) {
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

static int __init bdr_init(void)
{

	pr_info("BD checker module init\n");

	vector_init();

	major = register_blkdev(0, MAIN_BLKDEV_NAME);

	if (major < 0) {
		pr_err("Unable to register mybdev block device\n");
		return -EIO;
	}

	pool = kzalloc(sizeof(struct bio_set), GFP_KERNEL);

	if (!pool) {
		pr_info("hahahah\n");
		goto mem_err;
	}

	return 0;
mem_err:
	kfree(pool);
	pr_err("Memory allocation failed\n");
	return -ENOMEM;
}

static void __exit bdr_exit(void) {
	
	int i = 0;

	if (!bd_vector->arr) {
		for (i = 0; i < bd_vector->size; i ++) {
			bdev_release(bd_vector->arr[i].bdev_handler);

			kfree(bd_vector->arr[i].bdev_handler);
			bd_vector->arr[i].bdev_handler = NULL;
			kfree(bd_vector->arr[i].middle_disk);
			kfree(&bd_vector->arr[i]);
		}
		kfree(bd_vector);
	}

	unregister_blkdev(major, MAIN_BLKDEV_NAME);
	
	pr_info("BD checker module exit\n");
}

static int check_bio_link(struct bio *bio) {

	if (vector_check_bd_manager_by_name(bio->bi_bdev->bd_disk->disk_name)) {
		pr_err("No such bd_manager with middle disk %s and not empty handler\n", bio->bi_bdev->bd_disk->disk_name);
		return -EINVAL;
	}

	return 0;
}

/**
 * bdr_submit_bio() - takes the provided bio, allocates a clone (child)
 * for a redirect_bd. Although, it changes the way both bio's will end and submits them.
 * @bio - Expected bio request
 */
static void bdr_submit_bio(struct bio *bio) {
	struct bio *clone;
	int status;
	struct blkdev_manager current_redirect_manager;

	pr_info("Entered submit bio\n");

	if (check_bio_link(bio)) {
		pr_err("Check bio link failed\n");
		return;
	}

	status = bioset_init(pool, BIO_POOL_SIZE, 0, 0);

	if (status) {
		pr_err("Couldn't allocate bio set");
		kfree(pool);
		return;
	}

	current_redirect_manager = bd_vector->arr[current_redirect_pair_index];
	clone = bio_alloc_clone(current_redirect_manager.bdev_handler->bdev, bio, GFP_KERNEL, pool);

	if (!clone) {
		pr_err("Bio allocation failed\n");
		bio_io_error(bio);
		return;
	}

	bio_chain(clone, bio);
	submit_bio(clone);
	bio_endio(bio);
}

static const struct block_device_operations bdr_bio_ops = {
		.owner = THIS_MODULE,
		.submit_bio = bdr_submit_bio,
};

static struct bdev_handle *open_bd_on_rw(char *bd_path) {
	return bdev_open_by_path(bd_path, BLK_OPEN_WRITE | BLK_OPEN_READ, NULL, NULL);
}

/**
 * init_bd() - Initialises gendisk structure, for 'local' disk
 * @bd_name: name of creating BD
 *
 * DOESN'T SET UP the disks capacity, check bdr_submit_bio()
 * AND DOESN'T ADD disk if there was one already TO FIX
 */
static struct gendisk *init_disk_bd(char *bd_name) {

	struct gendisk *new_disk = NULL;
	struct blkdev_manager *linked_manager = NULL;

	new_disk = blk_alloc_disk(NUMA_NO_NODE);
	new_disk->major = major;
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
	linked_manager->bd_name = bd_name;
	
	pr_info("Set capacity: %lld \n", get_capacity(linked_manager->bdev_handler->bdev->bd_disk));
	set_capacity(new_disk, get_capacity(linked_manager->bdev_handler->bdev->bd_disk));

	kfree(linked_manager);

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
static int check_and_open_bd(char *bd_path) {

	int error;
	struct blkdev_manager *current_bdev_manager = kmalloc(sizeof(struct blkdev_manager), GFP_KERNEL);
	struct bdev_handle *current_bdev_handle = NULL;
	
	current_bdev_handle = open_bd_on_rw(bd_path);

	if (IS_ERR(current_bdev_handle)) { 
		pr_err("%s path", bd_path);
		goto free_bdev;
	}

	current_bdev_manager->bdev_handler = current_bdev_handle;
	
	vector_add_bd(current_bdev_manager);
	
	if (error) {
		pr_err("vector add failed: no disk with such name\n");
		goto free_bdev;
	}

	pr_info("vector succesfully supplemented\n");

	return 0;

free_bdev:
	kfree(current_bdev_manager);
	return PTR_ERR(current_bdev_handle);
}

static char* create_disk_name_by_index(int index) {
	char* disk_name = kmalloc(strlen(MAIN_BLKDEV_NAME) + snprintf(NULL, 0, "%d", index) + 1, GFP_KERNEL); 

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
static int create_bd(int name_index) {

	char *disk_name = NULL;
	int status;
	struct gendisk *new_disk = NULL;
	
	disk_name = create_disk_name_by_index(name_index);

	if (!disk_name)
		goto mem_err;
	
	new_disk = init_disk_bd(disk_name);
	
	if (!new_disk) 
		goto disk_init_err;

	bd_vector->arr[bd_vector->size - 1].middle_disk = new_disk;
	bd_vector->arr[bd_vector->size - 1].bd_name = disk_name;

	pr_info("status after add_disk with name %s: %d\n", disk_name, status);

	status = add_disk(new_disk);

	if (status) {
		put_disk(new_disk);
		goto disk_init_err;
	}
	
	kfree(disk_name);

	return 0;
mem_err:
	kfree(disk_name);
	pr_err("Memory allocation failed\n");
	return -ENOMEM;
disk_init_err:
	kfree(disk_name);
	kfree(new_disk);
	pr_err("Disk initialization failed\n");
	return -ENOMEM; //CHANGE	
}

/**
 * bdr_get_bd_names() - Function that prints the list of block devices, that
 * are stored in vector.
 *
 * Vector stores only BD's that we've touched from this module.
 */
static int bdr_get_bd_names(char *buf, const struct kernel_param *kp) {

	char *names_list = NULL;
	int total_length = 0;
	int offset = 0;
	int i = 0;
	int i_increased;

	for (i = 0; i < bd_vector->size; i++) 
	{
		i_increased = i + 1;
		total_length += sprintf("%d. %s\n", i_increased, bd_vector->arr[i].middle_disk->disk_name );
	}

	names_list = (char *)kzalloc(total_length + 1, GFP_KERNEL);
	
	if (!names_list) 
	{
		pr_err("memory allocation failed\n");
		return -ENOMEM;
	}

	for (i = 0; i < bd_vector->size; i++) 
	{
		i_increased = i + 1;
		offset += sprintf(names_list + offset, "%d. %s\n", i_increased, bd_vector->arr[i].middle_disk->disk_name);
	}

	strcpy(buf, names_list);
	
	kfree(names_list);
	
	return total_length;
}

/**
 * bdr_get_bd_names() - Deletes by index*** of bdev from printed list // TODO
 */
static int bdr_delete_bd(const char *arg, const struct kernel_param *kp) { // fix it.
	int index = convert_to_int(arg) - 1;

	bdev_release(bd_vector->arr[index].bdev_handler);
	// bd_vector->arr[int(arg)].bd_disk-> // TODO: release the bio.
	bd_vector->arr[index].bdev_handler = &(struct bdev_handle){0};

	pr_info("removed bdev with index %d", index);

	return 0;
}

/**
 * This function takes the name of the BD and makes it the aim of the redirect operation.
 * @arg - "from_bd_name path"  bd_name, that will be redirected to
 */
static int bdr_set_redirect_bd(const char *arg,const struct kernel_param *kp) {
	
	int status;
	int index;
	char path[MAX_BD_NAME_LENGTH];

	if (sscanf(arg, "%d %s", &index, path) != 2) 
	{
		pr_err("Wrong input, 2 values are required\n");
		return -EINVAL;
	}

	// if (bd_vector->arr[index].bd_name != NULL) // FIX bc it should check the current_pair_index - 1 (last_pair_index)
	// {
	// 	pr_warn("This redirection pair is already set up\n");
	// 	pr_warn("Rewriting the prev. pair...\n");
	// }

	status = check_and_open_bd(path);

	current_redirect_pair_index = bd_vector->size - 1;

	if (status)
		return PTR_ERR(&status);
	
	status = create_bd(index);

	if (status) 
		return PTR_ERR(&status);
	
	return 0;
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

MODULE_PARM_DESC(get_bd_names, "Add a new one/Get BD names and indices");
module_param_cb(get_bd_names, &bdr_get_bd_ops, NULL, S_IRUGO | S_IWUSR);

MODULE_PARM_DESC(set_redirect_bd, "Input a BD name. Check if such BD exists, if not - add -> data will redirect to it");
module_param_cb(set_redirect_bd, &bdr_redirect_ops, NULL, S_IWUSR);

module_init(bdr_init);
module_exit(bdr_exit);


/**
 * ISSUES TO FIX:
 * done. Release, but fix the delete fun
 * 2. Disk capacity set, that causes submit being not called. - make init when called redirect
 * delete the set action 
 * set capacity will set it according to the redirect (final) one
 * 3. Multiple disks creation
 * 4. code styl - 2 tabs
 * если доднострочный if и без else то без скобок но с ентером
 * disk_release
 * if err - > replace with if status
 * объявления всегда сверху + пул один раз аллоцировать
 */