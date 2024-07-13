#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/blkdev.h>

MODULE_DESCRIPTION("Block device checker");
MODULE_AUTHOR("qrutyy");
MODULE_LICENSE("Dual MIT/GPL");

#define INIT_VECTOR_CAP 4
#define MAX_BD_NAME_LENGTH 15

static char *current_bd_name;

typedef struct vector {
    int size;
    int capacity;
    struct bdev_handle* arr;
} vector;

static vector *bd_vector;

void vector_init(vector *v) {
    if (v == NULL) {
        pw_warn("Vector is NULL\n");
    }
    v->capacity = INIT_VECTOR_CAP;
    v->size = 0;
    v->arr = kzalloc(sizeof(struct block_device*) * v->capacity, GFP_KERNEL);
    if (v->arr == NULL) {
        pr_warn("memory allocation failed\n"); // TODO: exit the module
    }
}

static void vector_add_bdev(struct bdev_handle* current_bdev_handle, char* bd_name) {
    pr_debug("all is cool 2\n");
    
    if (bd_vector->size < bd_vector->capacity) {
        pr_info("vector wasn't resized\n");
    }
    else {
        pr_info("vector was resized\n");
        pr_debug("all is cool\n");

        bd_vector->capacity *= 2; // TODO: make coef. smaller 
        bd_vector->arr = krealloc(bd_vector->arr, bd_vector->capacity, GFP_KERNEL);
    }

    bd_vector->arr[bd_vector->size++] = *current_bdev_handle;
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

    kfree(current_bd_name);
    kfree(bd_vector->arr);
    
    pr_info("BD checker module exit\n");
}

static void set_bd_name(char *arg) {

    ssize_t len = strlen(arg) + 1;

    if (current_bd_name) {
        kfree(current_bd_name);
        current_bd_name = NULL;
    }

    current_bd_name = kzalloc(sizeof(char) * len, GFP_KERNEL);
    if (current_bd_name == NULL) {
        pr_warn("memory allocation failed\n"); // TODO: exit the module
    }
    // Using GFP_KERNEL means that allocation function can put the current process to sleep, waiting for a page, when called in low-memory situations.

    strncpy(current_bd_name, arg, len - 1);
}

// static struct bdev_handle* get_bd_by_handler(bdev_handle bdev_handler) {
//     if (bd_vector == NULL || name == NULL) {
//         return NULL;
//     }

//     for (int i = 0; i < bd_vector->size; i ++) {
//         if (bd_vector->arr[i] == bdev_handler) {
//             return &bd_vector->arr[i];
//         }
//     }
//     pr_warn("no BD with such name\n");
// }

static void bcm_submit_bio(struct bio *bio) {
    pr_info("submit called\n");
}

static const struct block_device_operations bcm_bio_ops = {
    .owner = THIS_MODULE,
    .submit_bio = bcm_submit_bio,
};

static struct bdev_handle* open_bd(char* bd_name) {
    return bdev_open_by_path(bd_name, BLK_OPEN_WRITE | BLK_OPEN_READ, NULL, NULL);
}

/*
 * Creates BD defering to its name. Adds it to the vector and set ups the disk
 * @bd_name: name of creating BD
*/
static struct bdev_handle* create_bd(char* bd_name) {

    int error;
    int new_major;
    struct gendisk* new_disk;
    struct bdev_handle* current_bdev_handle;

    new_major = register_blkdev(0, bd_name);
    
    if (new_major < 0) {
        printk(KERN_ERR "unable to register mybdev block device\n");
        return -EBUSY;
    }
    
    current_bdev_handle = open_bd(bd_name);

    new_disk = blk_alloc_disk(NUMA_NO_NODE);
    new_disk->major = new_major;
    new_disk->first_minor = 1;
    new_disk->minors = 1;
    new_disk->flags = GENHD_FL_NO_PART;
    new_disk->fops = &bcm_bio_ops;
    new_disk->private_data = current_bdev_handle;
    
    if (bd_name) {
        strcpy(new_disk->disk_name, bd_name);
    }
    else {
        pr_warn("bd_name is NULL\n");
        return NULL; // TODO: edit return code
    }

    set_capacity(new_disk, 2048); // TODO: edit size
    current_bdev_handle->bdev->bd_disk = new_disk;

    error = add_disk((get_bd_handler_by_name(current_bd_name))->bdev->bd_disk);
    
    if (error) { 
        put_disk((get_bd_handler_by_name(current_bd_name))->bdev->bd_disk);
    }

    return current_bdev_handle;
}

/*
 * Checks if name is occupied, if so - opens the BD, if not - creates it.
 */
static void check_and_create_bd(void) {
    
    int status;
    struct bdev_handle* current_bdev_handle;

    status = lookup_bdev(current_bd_name, NULL);

    if (status < 0) {
        pr_info("%s name is free\n", current_bd_name);
        current_bdev_handle = create_bd(current_bd_name);
    }
    else {
        pr_info("%s name is occupied\n", current_bd_name);
        current_bdev_handle = open_bd(current_bd_name);
    }
    pr_debug("all is cool\n");
    
    vector_add_bdev(current_bdev_handle, current_bd_name);

    pr_debug("CAC: lookup returned %d\n", status);
}

// static void bcm_submit_bio(struct bio *bio) {

//     /* The type used for indexing onto a disc or disc partition.
    
//     Linux always considers sectors to be 512 bytes long independently
//     of the devices real block size. */ 

//     sector_t sector = bio->bi_iter.bi_sector; // current_sector 
//     sector_t nr_sectors = bio_sectors(bio); 
// }

// static void bcm_clone_bio(char *arg, const struct kernel_param *kp) {
    
//     struct bio *cloned_bio;
//     int index;
//     char new_bd_name[MAX_BD_NAME_LENGTH];

//     sscanf(arg, "%d %s", index, new_bd_name);
    
//     bcm_set_bd_name(new_bd_name);
//     check_and_create_bd();

//     bd_to_clone_from
//     cloned_bio = bio_alloc_clone(get_bd_by_name(bd_name), bio, REQ_OP_WRITE, GFP_KERNEL);
    
//     submit_bio(bio);
    
// }
/* 
 * Checks if the name isn't occupied by other BD. In case it isn't - 
 * creates a BD with such name (create_bd)
 */
static int bcm_check_bd(const char *arg, const struct kernel_param *kp) {

    set_bd_name(arg);
    check_and_create_bd();

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

// static const struct kernel_param_ops bcm_clone_bio_ops = {
//     .set = bcm_clone_bio;
//     .get = NULL;
// };

// 3rd param - args for operations

MODULE_PARM_DESC(bd_name, "Check if such BD exists, if not - add");
module_param_cb(bd_name, &bcm_check_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(delete_bd, "Delete BD");
module_param_cb(delete_bd, &bcm_delete_ops, NULL, S_IWUSR);

MODULE_PARM_DESC(get_bd_names, "Get BD names and indices");
module_param_cb(get_bd_names, &bcm_get_bd_ops, NULL, S_IRUGO);

// MODULE_PARM_DESC(clone_bio_bd, "Clone bio from 1st to 2nd new (1index 2name)");
// module_param_cb(clone_bio_bd, &bcm_clone_bio_ops, NULL S_IWUSR);

module_init(bcm_init);
module_exit(bcm_exit);