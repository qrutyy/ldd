#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/blkdev.h>

MODULE_DESCRIPTION("Block device checker");
MODULE_AUTHOR("qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

#define INIT_VECTOR_CAP 4
#define MAX_BD_NAME_LENGTH 15

static char *current_bd_name;
static char *current_bd_path;

typedef struct vector {
    int size;
    int capacity;
    struct bdev_handle* arr;
} vector;

static vector *bd_vector;

void vector_init(vector *v) {
    v = kzalloc(sizeof(vector), GFP_KERNEL);
    if (v == NULL) {
        pr_warn("Vector is NULL\n");
        return;
    }
    
    v->capacity = INIT_VECTOR_CAP;
    v->size = 0;
    v->arr = kzalloc(sizeof(struct block_device*) * v->capacity, GFP_KERNEL);
    if (v->arr == NULL) {
        pr_warn("memory allocation failed\n"); // TODO: exit the module
        return;
    }
}

static int vector_add_bdev(struct bdev_handle* current_bdev_handle, char* bd_name) {
    
    if (bd_vector->size < bd_vector->capacity) {
        pr_info("vector wasn't resized\n");
    }
    else {
        pr_info("vector was resized\n");
        pr_debug("all is cool\n");

        bd_vector->capacity *= 2; // TODO: make coef. smaller 
        bd_vector->arr = krealloc(bd_vector->arr, bd_vector->capacity, GFP_KERNEL);
        if (bd_vector->arr == NULL) {
            pr_info("vector's array allocation failed\n");
            return -ENOMEM;
        }
    }

    bd_vector->arr[bd_vector->size++] = *current_bdev_handle;

    return 0;
}

static struct bdev_handle* get_bd_handler_by_index(int index) {
    return &bd_vector->arr[index];
}

static struct bdev_handle* get_bd_handler_by_name(char* name) {
    if (bd_vector == NULL || name == NULL) {
        return NULL;
    }

    for (int i = 0; i < bd_vector->size; i ++) {
        pr_debug("%s ?= %s\n", bd_vector->arr[i].bdev->bd_disk->disk_name, name);
        if (strcmp(bd_vector->arr[i].bdev->bd_disk->disk_name, name) == 0) {
            return &bd_vector->arr[i];
        }
    }
    pr_warn("no BD with such name\n");
    return NULL;
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

static int __init bcm_init(void) {

    pr_info("BD checker module init\n");

    vector_init(bd_vector);

    return 0;
}

static void __exit bcm_exit(void) {
    if (current_bd_name != NULL) {
        kfree(current_bd_name);
    }
    if (bd_vector->arr != NULL) {
        kfree(bd_vector->arr);
    }
    
    pr_info("BD checker module exit\n");
}

/*
 * Sets static current_bd_name and current_bd_paht according to @arg
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

    if (current_bd_name == NULL) {
        pr_warn("memory allocation failed\n"); 
        return -ENOMEM;
    }
    else if (current_bd_path == NULL) {
        kfree(current_bd_name);
        pr_warn("memory allocation failed\n");
        return -ENOMEM;
    }
    // Using GFP_KERNEL means that allocation function can put the current process to sleep, waiting for a page, when called in low-memory situations.

    strncpy(current_bd_name, arg, len); 
    strncpy(current_bd_path, arg, len - 1); // -1 due to removing the \n

    pr_info("name set up succesfully\n");

    return 0;
}

static void bcm_submit_bio(struct bio *bio) {
    pr_info("submit called\n");
}

static const struct block_device_operations bcm_bio_ops = {
    .owner = THIS_MODULE,
    .submit_bio = bcm_submit_bio,
};

static struct bdev_handle* open_bd(char* bd_path) {
    return bdev_open_by_path(bd_path, BLK_OPEN_WRITE | BLK_OPEN_READ, NULL, NULL);
}

/*
 * Creates BD defering to its name. Adds it to the vector and set ups the disk
 * @bd_name: name of creating BD
*/
static struct bdev_handle* create_bd(void) {

    int error;
    int new_major;
    struct gendisk* new_disk;
    struct bdev_handle* current_bdev_handle;

    new_major = register_blkdev(0, current_bd_name);

    if (new_major < 0) {
        printk(KERN_ERR "unable to register mybdev block device\n");
        return NULL;
    }

    current_bdev_handle = open_bd(current_bd_path);
    if (IS_ERR(current_bdev_handle)) {
        pr_err("Opening device failed, %d\n", PTR_ERR(current_bdev_handle));
        pr_info("hahahahha\n");
        pr_info("%s\n", current_bd_name);
        pr_info("%s\n", current_bd_path);
    }

    new_disk = blk_alloc_disk(NUMA_NO_NODE);
    new_disk->major = new_major;
    new_disk->first_minor = 1;
    new_disk->minors = 1;
    new_disk->flags = GENHD_FL_NO_PART;
    new_disk->fops = &bcm_bio_ops;
    new_disk->private_data = current_bdev_handle;
    pr_info("1\n");

    if (current_bd_name) {
        pr_info("2\n");
        strcpy(new_disk->disk_name, current_bd_name);
        pr_info("3\n");
    }
    else {
        pr_warn("bd_name is NULL\n");
        return NULL;
    }
    pr_info("4\n");

    int disk_size = get_capacity(current_bdev_handle->bdev->bd_disk);
    pr_info("5\n");

    if (disk_size == NULL) {
        pr_info("disk_size is null\n");
    }
    else {
        pr_info("disk size is %d\n", disk_size);
    }
    pr_info("6\n");

    set_capacity(new_disk, disk_size);
    pr_info("7\n");
    current_bdev_handle->bdev->bd_disk = new_disk;
    pr_info("8\n");

    error = add_disk((get_bd_handler_by_name(current_bd_name))->bdev->bd_disk);
    pr_info("9\n");
    if (error) { 
        put_disk((get_bd_handler_by_name(current_bd_name))->bdev->bd_disk);
    }

    return current_bdev_handle;
}

/*
 * Checks if name is occupied, if so - opens the BD, if not - creates it.
 */
static int check_and_create_bd(void) {
    
    int status;
    struct bdev_handle* current_bdev_handle;

    status = lookup_bdev(current_bd_name, NULL);

    if (status < 0) {
        pr_info("%s name is free\n", current_bd_name);
        current_bdev_handle = create_bd();
        if (current_bdev_handle == NULL) {
            pr_warn("smth went wrong in create_bd()\n");
            return -ENOMEM;
        }
    }
    else {
        pr_info("%s name is occupied\n", current_bd_name);
        current_bdev_handle = open_bd(current_bd_name);
    }
    pr_info("check and create: lookup returned %d\n", status);
    
    int err = vector_add_bdev(current_bdev_handle, current_bd_name);
    if (err) {
        return -ENOMEM;
    }

    pr_info("vector succesfully supplemented\n");
    
    return 0;
}

/* 
 * Checks if the name isn't occupied by other BD. In case it isn't - 
 * creates a BD with such name (create_bd)
 */
static int bcm_check_bd(const char *arg, const struct kernel_param *kp) {
    int err = 0;

    err = set_bd_name_and_path(arg);
    
    if (err) {
        return -ENOMEM;
    }
    
    err = check_and_create_bd();
    
    if (err) {
        return -ENOMEM;
    }

    return 0;
}

/**
 * Function that prints the list of block devices, that are stored in vector.
 * 
 * Vector stores only BD's that were touched from this module. 
 */

static int bcm_get_bd_names(char *buf, const struct kernel_param *kp) {
    
    char *names_list = NULL;
    int total_length = 0;
    int offset = 0;
    
    for (int i = 0; i < bd_vector->size; ++i) {
        total_length += strlen(bd_vector->arr[i].bdev->bd_disk->disk_name) + 5; // 5 for the index number and dot
    }
    
    names_list = (char*)kmalloc(total_length + 1, GFP_KERNEL);
    if (names_list == NULL) {
        pr_warn("memory allocation failed\n"); // TODO: exit the module
        return NULL;
    }
    
    // Concatenate each name to the string
    for (int i = 0; i < bd_vector->size; ++i) {
        int name_length = strlen(bd_vector->arr[i].bdev->bd_disk->disk_name);
        offset += sprintf(names_list + offset, "%d. %s\n", i + 1, bd_vector->arr[i].bdev->bd_disk->disk_name);
    }
    
    strcpy(buf, names_list);

    return total_length;
}

/*
 * Deletes by index*** of bdev from printed list
 */

static int bcm_delete_bd(const char *arg, const struct kernel_param *kp) {
    int index = convert_to_int(arg) - 1;
    
    bdev_release(&bd_vector->arr[index]);
    // bd_vector->arr[int(arg)].bd_disk-> // TODO: release the bio.
    bd_vector->arr[index] = (struct bdev_handle){0};
    
    pr_info("removed bdev with index %d", index);
    
    return 0;
}

static const struct kernel_param_ops bcm_check_ops = {
    .set = bcm_check_bd,
    .get = NULL,
};

static const struct kernel_param_ops bcm_delete_ops = {
    .set = bcm_delete_bd,
    .get = NULL,
};

static const struct kernel_param_ops bcm_get_bd_ops = {
    .set = NULL,
    .get = bcm_get_bd_names,
};

// 3rd param - args for operations

MODULE_PARM_DESC(check_bd_name, "Input a BD name. Check if such BD exists, if not - add");
module_param_cb(check_bd_name, &bcm_check_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(delete_bd, "Delete BD");
module_param_cb(delete_bd, &bcm_delete_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(get_bd_names, "Get BD names and indices");
module_param_cb(get_bd_names, &bcm_get_bd_ops, NULL, S_IRUGO);

// MODULE_PARM_DESC(clone_bio_bd, "Clone bio from 1st to 2nd new (1index 2name)");
// module_param_cb(clone_bio_bd, &bcm_clone_bio_ops, NULL S_IWUSR);

module_init(bcm_init);
module_exit(bcm_exit);