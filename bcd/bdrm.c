#include <linux/blkdev.h>
#include <linux/moduleparam.h>

MODULE_DESCRIPTION("Block Device Redirect Module");
MODULE_AUTHOR("qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

#define INIT_VECTOR_CAP 4
#define MAX_BD_NAME_LENGTH 15

static int major;
static char *current_bd_name;
static char *current_bd_path;
static struct bd_manager *current_redirect_bd_manager;

typedef struct vector {
    int size;
    int capacity;
    struct bd_manager *arr;
} vector;

/**
 * @from_disk - has higher priority when setting -> 
 * -> means that firstly we create a disk and add it to the vector, 
 * and only then we can pair it with bd
 */
typedef struct bd_manager {
    char *bd_path;
    char *bd_name;
    struct gendisk *from_disk; 
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

static int vector_add_bdev_to_disk(struct bd_manager *current_bdev_manager, char *disk_name) {
    pr_info("%s", disk_name);
    for (int i = 0; i < bd_vector->size; i ++) {
        if (bd_vector->arr[i].from_disk->disk_name == disk_name) {
            bd_vector->arr[i].bdev_handler = current_bdev_manager->bdev_handler;
            return 0;
        }
    }
    return -EINVAL;
}

static int vector_add_disk(struct gendisk *disk) {
    
    if (bd_vector->size < bd_vector->capacity) {
        pr_info("vector wasn't resized\n");
    } else {
        pr_info("vector was resized\n");

        bd_vector->capacity *= 2; // TODO: make coef. smaller
        bd_vector->arr = krealloc(bd_vector->arr, bd_vector->capacity, GFP_KERNEL);
        
        if (!bd_vector->arr) {
            pr_info("vector's array allocation failed\n");
            return -ENOMEM;
        }
    }

    bd_vector->arr[bd_vector->size].from_disk = disk;
    bd_vector->size++;
    
    return 0;
}

static struct bd_manager *vector_get_bd_manager_by_name(char *bd_name) {
    for (int i = 0; i < bd_vector->size; i ++) {
        if (bd_vector->arr[i].bd_name == bd_name) {
            return &bd_vector->arr[i];
        }
    }
    return NULL;
}

static struct bd_manager *vector_get_bd_manager_by_index(int index) {
    return &bd_vector->arr[index];
}

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
 * for a redirect_bd. Although, it changes the way both bio's will end and submits
 * them.
 * @bio - Expected bio request
 */
static void bdrm_submit_bio(struct bio *bio) {

    if (!current_redirect_bd_manager) {
        pr_err("Redirect_bd wasn't set\n");
        return;
    }

    struct bio *clone;
    int error;

    set_capacity(current_redirect_bd_manager->from_disk, get_capacity(bio->bi_bdev->bd_disk));

    // current_redirect_bd_manager->bdev_handler->bdev->bd_disk = current_redirect_bd_manager->from_disk;

    clone = bio_alloc_clone(current_redirect_bd_manager->bdev_handler->bdev, bio,
                                                    GFP_KERNEL, bio->bi_pool);

    if (!clone) {
        pr_err("bio allocation failed\n");
        return;
    }

    // clone->bi_end_io = bio->bi_end_io; // how to close the parent, when child dies
    bio_chain(clone, bio);
	
	bio_endio(bio);

    submit_bio(clone);
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

    current_bd_path = kzalloc(sizeof(char) * (len + 1), GFP_KERNEL);
    current_bd_name = kzalloc(sizeof(char) * (len), GFP_KERNEL);

    pr_info("%s", current_bd_path);

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

    strcpy(current_bd_path, arg);
    strncpy(current_bd_name, arg, len);

    pr_info("name set up succesfully\n");

    return 0;
}

static struct bdev_handle *open_bd_on_rw(char *bd_path) {
    return bdev_open_by_path(bd_path, BLK_OPEN_WRITE | BLK_OPEN_READ, NULL, NULL);
}

/**
 * init_bd() - Initialises gendisk structure, for 'local' disk
 * @bd_name: name of creating BD
 *
 * DOESN'T SET UP THE DISK'S capacity, check bdrm_submit_bio()
 */
static struct gendisk *init_disk_bd(char *bd_name) {

    int status;
    struct gendisk *new_disk;

    major = register_blkdev(0, bd_name);

    if (major < 0) {
        pr_err("unable to register mybdev block device\n");
        return NULL;
    }

    new_disk = blk_alloc_disk(NUMA_NO_NODE);

    new_disk->major = major;
    new_disk->first_minor = 1;
    new_disk->minors = 1;
    new_disk->flags = GENHD_FL_NO_PART;
    new_disk->fops = &bdrm_bio_ops;

    if (bd_name) {
        strcpy(new_disk->disk_name, bd_name); // FIX NEEDED
    } else {
        /* all in all - it can't happen, due to prev. checks in set_bd_name_and_path */
        pr_warn("bd_name is NULL, nothing to copy\n");
        
        goto free_bd_meta;
    }
    

    status = add_disk(new_disk);

    if (status) {
        put_disk(new_disk);
    }

    return new_disk;

free_bd_meta:
    del_gendisk(new_disk);
    put_disk(new_disk);
    return NULL;
}

/**
 * check_and_open_bd() - Checks if name is occupied, if so - opens the BD, if
 * not - return -EINVAL.
 */
static int check_and_open_bd(int index) {

    int status;
    int error;
    struct bdev_handle *current_bdev_handle = kmalloc(sizeof(struct bdev_handle), GFP_KERNEL);
    struct bd_manager *current_bdev_manager = kmalloc(sizeof(struct bd_manager), GFP_KERNEL);

    current_bdev_handle = open_bd_on_rw(current_bd_path);

    if (IS_ERR(current_bdev_handle)) { 
        pr_err("%s name is free\n", current_bd_name);
        pr_err("%s", current_bd_path);
        goto free_bdev;
    }

    pr_info("check and create: lookup returned %d\n", status);

	current_bdev_manager->bdev_handler = current_bdev_handle;
    current_bdev_manager->bd_name = current_bd_name;
    
	error = vector_add_bdev_to_disk(current_bdev_manager, vector_get_bd_manager_by_index(index - 1)->from_disk->disk_name);
    
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
 * Sets the name for a new BD, that will be used as 'device in the middle'.
 * Pairing is being produced in bdrm_submit_bio function.
 */
static int bdrm_set_bd_name(const char *arg, const struct kernel_param *kp) {
    
    ssize_t len = strlen(arg);

    char *disk_name;
    int status;
    struct gendisk *new_disk;

    disk_name = kzalloc(sizeof(char) * (len), GFP_KERNEL);

    if (!disk_name) {
        pr_err("memory allocation failed\n");
        return -ENOMEM;
    } 

    strncpy(disk_name, arg, len);
    disk_name[len - 1] = '\0';
    
    if (!disk_name) {
        pr_err("smth went wrong\n");
        return -ENOMEM;
    }

    pr_info("disk name set up succesfully\n");
    pr_info("%s", disk_name);

    new_disk = init_disk_bd(disk_name);
    status = vector_add_disk(new_disk);
    
    if (IS_ERR(status)) {
        return PTR_ERR(status);
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

    for (int i = 0; i < bd_vector->size; i++) {
        total_length +=
                strlen(bd_vector->arr[i].from_disk->disk_name) + 5; // 5 for the index number and dot
    }

    names_list = (char *)kzalloc(total_length + 1, GFP_KERNEL);
    if (!names_list) {
        pr_err("memory allocation failed\n");
        return -ENOMEM;
    }

    // Concatenate each name to the string
    for (int i = 0; i < bd_vector->size; i++) {
        int name_length = strlen(bd_vector->arr[i].from_disk->disk_name);
        offset += sprintf(names_list + offset, "%d. %s", i + 1,
                                            bd_vector->arr[i].from_disk->disk_name);
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

/**
 * This function takes the name of the BD and makes it the aim of the redirect operation.
 * @bd_name - bd_name, that will be redirected to
 */

static int bdrm_set_redirect_bd(const char *arg,const struct kernel_param *kp) {
    
    int status;
    int index;
    char name[MAX_BD_NAME_LENGTH];

    if (sscanf(arg, "%d %s", &index, name) != 2) {
        pr_err("wrong input\n");
        return -EINVAL;
    }

    status = set_bd_name_and_path(name);

    if (IS_ERR(status)) {
        return PTR_ERR(status);
    }

    status = check_and_open_bd(index);

    if (IS_ERR(status)) {
        return PTR_ERR(status);
    }

    current_redirect_bd_manager = vector_get_bd_manager_by_name(current_bd_name);
    
    return 0;
}

static const struct kernel_param_ops bdrm_delete_ops = {
        .set = bdrm_delete_bd,
        .get = NULL,
};

/**
 * bdrm_set_bd_name(string -> "existing_bd_name index") 
 * - opens the bd and links it with created BD at provided index
 * 
 * bdrm_get_bd_names - returns the list of created BD with their redirect.
 */
static const struct kernel_param_ops bdrm_gs_bd_ops = {
        .set = bdrm_set_bd_name,
        .get = bdrm_get_bd_names,
};

static const struct kernel_param_ops bdrm_redirect_ops = {
        .set = bdrm_set_redirect_bd,
        .get = NULL,
};

MODULE_PARM_DESC(delete_bd, "Delete BD");
module_param_cb(delete_bd, &bdrm_delete_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(manage_bd_names, "Add a new one/Get BD names and indices");
module_param_cb(manage_bd_names, &bdrm_gs_bd_ops, NULL, S_IRUGO | S_IWUSR);

MODULE_PARM_DESC(set_redirect_bd, "Input a BD name. Check if such BD exists, if not - add -> data will redirect to it");
module_param_cb(set_redirect_bd, &bdrm_redirect_ops, NULL, S_IWUSR);

module_init(bdrm_init);
module_exit(bdrm_exit);
