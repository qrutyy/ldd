#include <linux/blkdev.h>
#include <linux/moduleparam.h>
// #include <linux/block/bdev.h>

MODULE_DESCRIPTION("Block Device Redirect Module");
MODULE_AUTHOR("qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

#define INIT_VECTOR_CAP 4
#define MAX_BD_NAME_LENGTH 15

static char *current_bd_name;
static char *current_bd_path;
static struct bd_manager *current_redirect_bd_manager;

typedef struct vector {
    int size;
    int capacity;
    struct bd_manager *arr;
} vector;

typedef struct bd_manager {
    char *bd_path;
    char *bd_name;
    struct bdev_handle *bdev_handler;
} bd_manager;

static vector *bd_vector;

static int vector_init(void) {
    bd_vector = kzalloc(sizeof(vector), GFP_KERNEL);
    if (!bd_vector) {
        pr_err("memory allocation failed\n");
        return -ENOMEM;
    }

    bd_vector->capacity = INIT_VECTOR_CAP;
    bd_vector->size = 0;
    bd_vector->arr = kzalloc(sizeof(struct bd_manager *) * bd_vector->capacity, GFP_KERNEL);
    if (!bd_vector->arr) {
        pr_err("memory allocation failed\n");
        return -ENOMEM;
    }
    return 0;
}

static int vector_add_bdev(struct bd_manager *current_bdev_manager) {
    pr_info("25\n");
    if (!bd_vector) pr_info("38198321\n");
    if (bd_vector->size < bd_vector->capacity) {
        pr_info("vector wasn't resized\n");
    } else {
        pr_info("vector was resized\n");

        bd_vector->capacity *= 2; // TODO: make coef. smaller
        pr_info("35\n");
        bd_vector->arr = krealloc(bd_vector->arr, bd_vector->capacity, GFP_KERNEL);
        if (!bd_vector->arr) {
            pr_info("vector's array allocation failed\n");
            return -ENOMEM;
        }
    }
	pr_info("1\n");

    bd_vector->arr[bd_vector->size++] = *current_bdev_manager;
	pr_info("2\n");
    return 0;
}

/* Simply converts char to int */
static int convert_to_int(const char *arg) {
    long number;
    int res = kstrtol(arg, 10, &number);

    if (res != 0) {
        return res;
    } else {
        return (int)number;
    }
}

static int __init bdrm_init(void) {

    pr_info("BD checker module init\n");

    vector_init();

    return 0;
}

static void __exit bdrm_exit(void) {
    if (current_bd_name != NULL) {
        kfree(current_bd_name);
    }
    if (bd_vector->arr != NULL) {
        kfree(bd_vector->arr);
    }

    pr_info("BD checker module exit\n");
}

/**
 * bdrm_submit_bio() - takes the provided bio, allocates a clone (child)
 * for a redirect_bd (that prev. had a disk allocation depending on provided
 * bio's one). Although, it changes the way both bio's will end and submits
 * them.
 * @bio - Expected bio request
 */
static void bdrm_submit_bio(struct bio *bio) {

    if (!current_redirect_bd_manager) {
        pr_err("Redirect_bd wasn't set\n");
        return;
    }

    struct bio *clone;
    int disk_size;
    int error;

    disk_size = get_capacity(bio->bi_bdev->bd_disk);

    error = add_disk(current_redirect_bd_manager->bdev_handler->bdev->bd_disk);

    if (error) {
        put_disk(current_redirect_bd_manager->bdev_handler->bdev->bd_disk);
    }

    clone = bio_alloc_clone(current_redirect_bd_manager->bdev_handler->bdev, bio,
                                                    GFP_KERNEL, bio->bi_pool);

    if (!clone) {
        pr_err("bio allocation failed\n");
        return;
    }

    clone->bi_end_io = bio->bi_end_io; // how to close the parent, when child dies
	
	// bio_endio(clone);

    submit_bio(clone);

    // bio_endio(bio);

free_disk:
}

static const struct block_device_operations bdrm_bio_ops = {
        .owner = THIS_MODULE,
        .submit_bio = bdrm_submit_bio,
};

/**
 * set_bd_name_and_path() - Sets static current_bd_name and current_bd_paht
 * according to @arg
 * @arg - user input from parameter
 */
static int set_bd_name_and_path(char *arg) {

    ssize_t len = strlen(arg);

    if (current_bd_name) {
        kfree(current_bd_name);
        current_bd_name = NULL;
    }
    if (current_bd_path) {
        kfree(current_bd_path);
        current_bd_path = NULL;
    }

    current_bd_name = kzalloc(sizeof(char) * len + 1, GFP_KERNEL);
    current_bd_path = kzalloc(sizeof(char) * len, GFP_KERNEL);

    if (!current_bd_name) {
        pr_err("memory allocation failed\n");
        return -ENOMEM;
    } else if (!current_bd_path) {
        kfree(current_bd_name);
        pr_err("memory allocation failed\n");
        return -ENOMEM;
    }
    // Using GFP_KERNEL means that allocation function can put the current process
    // to sleep, waiting for a page, when called in low-memory situations.

    strncpy(current_bd_name, arg, len);
    strncpy(current_bd_path, arg, len - 1); // -1 due to removing the \n

    pr_info("name set up succesfully\n");

    return 0;
}

static struct bdev_handle *open_bd(char *bd_path) {
    return bdev_open_by_path(bd_path, BLK_OPEN_WRITE | BLK_OPEN_READ, NULL, NULL);
}

/**
 * create_bd() - Creates BD defering to its name. Adds it to the vector.
 * @bd_name: name of creating BD
 *
 * DOESN'T SET UP THE DISK'S capacity, bc we need parent-disk, to clone the
 * capacity
 */
static struct bd_manager *create_bd(void) {

    int new_major;
    struct gendisk *new_disk;
    struct bdev_handle *current_bdev_handle;
    struct bd_manager *current_bdev_manager;

    new_major = register_blkdev(0, current_bd_name);
	pr_info("5\n");

    if (new_major < 0) {
        pr_err("unable to register mybdev block device\n");
        return NULL;
    }

	pr_info("6\n");

    new_disk = blk_alloc_disk(NUMA_NO_NODE);
	pr_info("7\n");
    new_disk->major = new_major;
    new_disk->first_minor = 1;
    new_disk->minors = 1;
    new_disk->flags = GENHD_FL_NO_PART;
    new_disk->fops = &bdrm_bio_ops;
	pr_info("8\n");
	// current_bdev_handle->bdev = bdev_alloc(new_disk, 0); no such function wtf.

    // current_bdev_handle = open_bd(current_bd_path);
	pr_info("9\n");
    if (IS_ERR(current_bdev_handle)) {
        pr_info("%s\n", current_bd_name);
        pr_info("%s\n", current_bd_path);
        goto free_bd_meta;
    }

    new_disk->private_data = current_bdev_handle;

    if (current_bd_name) {
        strcpy(new_disk->disk_name, current_bd_name);
    }
    /* all in all - it can't happen, due to prev. checks in set_bd_name_and_path
     */
    else {
        pr_warn("bd_name is NULL, nothing to copy\n");
        goto free_bd_meta;
    }

    current_bdev_handle->bdev->bd_disk = new_disk;
    current_bdev_manager->bdev_handler = current_bdev_handle;
    current_bdev_manager->bd_name = current_bd_name;
    current_bdev_manager->bd_path = current_bd_path;

    return current_bdev_manager;

free_bd_meta:
    kfree(current_bd_name);
    kfree(current_bd_path);
    del_gendisk(new_disk);
    put_disk(new_disk);
    return NULL;
}

/**
 * check_and_create_bd() - Checks if name is occupied, if so - opens the BD, if
 * not - creates it.
 */
static int check_and_create_bd(void) {

    int status;
    int error;
    struct bdev_handle *current_bdev_handle = kmalloc(sizeof(struct bdev_handle), GFP_KERNEL);
    struct bd_manager *current_bdev_manager = kmalloc(sizeof(struct bd_manager), GFP_KERNEL);

    current_bdev_handle = open_bd(current_bd_path);

    if (IS_ERR(current_bdev_handle)) { // idk if this branch works))))))))
        pr_info("%s name is free\n", current_bd_path);
        current_bdev_manager = create_bd();
        
		if (!current_bdev_manager) {
            pr_err("smth went wrong in create_bd()\n");
            goto free_bdev;
            
        } else {
			current_bdev_handle = current_bdev_manager->bdev_handler;
		}
    }

    pr_info("check and create: lookup returned %d\n", status);

	current_bdev_manager->bdev_handler = current_bdev_handle;
    current_bdev_manager->bd_name = current_bd_name;
    
	error = vector_add_bdev(current_bdev_manager);
    
	if (error) {
		pr_err("vector add failed\n");
        goto free_bdev;
    }

    pr_info("vector succesfully supplemented\n");

    return 0;

free_bdev:
    kfree(current_bdev_handle);
    kfree(current_bdev_manager);
    return -ENOMEM;
}

/**
 * bdrm_check_bd() - Checks if the name isn't occupied by other BD. In case it
 * isn't - creates a BD with such name (create_bd)
 */
static int bdrm_check_bd(const char *arg, const struct kernel_param *kp) {
    int error = 0;

    error = set_bd_name_and_path(arg);

    if (error) {
        return -ENOMEM;
    }

    error = check_and_create_bd();

    if (error) {
        return -ENOMEM;
    }

    return 0;
}

/**
 * bdrm_get_bd_names() - Function that prints the list of block devices, that
 * are stored in vector.
 *
 * Vector stores only BD's that were touched from this module.
 */

static int bdrm_get_bd_names(char *buf, const struct kernel_param *kp) {

    char *names_list = NULL;
    int total_length = 0;
    int offset = 0;

    for (int i = 0; i < bd_vector->size; ++i) {
        total_length +=
                strlen(bd_vector->arr[i].bdev_handler->bdev->bd_disk->disk_name) +
                5; // 5 for the index number and dot
    }

    names_list = (char *)kzalloc(total_length + 1, GFP_KERNEL);
    if (!names_list) {
        pr_err("memory allocation failed\n"); // TODO: exit the module
        return NULL;
    }

    // Concatenate each name to the string
    for (int i = 0; i < bd_vector->size; ++i) {
        int name_length =
                strlen(bd_vector->arr[i].bdev_handler->bdev->bd_disk->disk_name);
        offset += sprintf(names_list + offset, "%d. %s\n", i + 1,
                                            bd_vector->arr[i].bdev_handler->bdev->bd_disk->disk_name);
    }

    strcpy(buf, names_list);

    return total_length;
}

/**
 * bdrm_get_bd_names() - Deletes by index*** of bdev from printed list
 */
static int bdrm_delete_bd(const char *arg, const struct kernel_param *kp) { // fix it.
    int index = convert_to_int(arg) - 1;

    bdev_release(bd_vector->arr[index].bdev_handler);
    // bd_vector->arr[int(arg)].bd_disk-> // TODO: release the bio.
    bd_vector->arr[index].bdev_handler = &(struct bdev_handle){0};

    pr_info("removed bdev with index %d", index);

    return 0;
}

static int bdrm_set_redirect_bd(const char *arg,const struct kernel_param *kp) {
    int index = convert_to_int(arg) - 1;

    current_redirect_bd_manager = &bd_vector->arr[index];
    if (!current_redirect_bd_manager) {
        pr_err("MISS FAULT\n");
        return -EINVAL; // invalid argument
    }
    return 0;
}

static const struct kernel_param_ops bdrm_check_ops = {
        .set = bdrm_check_bd,
        .get = NULL,
};

static const struct kernel_param_ops bdrm_delete_ops = {
        .set = bdrm_delete_bd,
        .get = NULL,
};

static const struct kernel_param_ops bdrm_get_bd_ops = {
        .set = NULL,
        .get = bdrm_get_bd_names,
};

static const struct kernel_param_ops bdrm_redirect_ops = {
        .set = bdrm_set_redirect_bd,
        .get = NULL,
};

// 3rd param - args for operations

MODULE_PARM_DESC(check_bd_name,
                                 "Input a BD name. Check if such BD exists, if not - add");
module_param_cb(check_bd_name, &bdrm_check_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(delete_bd, "Delete BD");
module_param_cb(delete_bd, &bdrm_delete_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(get_bd_names, "Get BD names and indices");
module_param_cb(get_bd_names, &bdrm_get_bd_ops, NULL, S_IRUGO);

MODULE_PARM_DESC(set_redirect_bd,
                                 "Input a BD name. Check if such BD exists, if not - add");
module_param_cb(set_redirect_bd, &bdrm_check_ops, NULL, S_IWUSR);

module_init(bdrm_init);
module_exit(bdrm_exit);
