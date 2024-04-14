#include <linux/module.h> // required by all modules
#include <linux/kernel.h> // required for sysinfo
#include <linux/init.h> // used by module_init, module_exit macros
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kdev_t.h> // major-minor macros
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/uaccess.h> // copy from/to user
#include <linux/cdev.h> // for fops registration

MODULE_DESCRIPTION( "Xorwow Pseudo-Random Generator Demo");
MODULE_AUTHOR("qrutyy");
MODULE_VERSION("0.1");
MODULE_LICENSE("Dual MIT/GPL");

struct xwrand_state {
    u32 a, b, c, d, e;
    u32 cnt;
};

typedef struct xwrand_state xwrand_t;

static xwrand_t gstate =  { 0 };

static const int minor = 0;

static dev_t xw_dev = 0; // uses to handle char device 

static struct cdev xw_cdev; // maps devices to file_operations

static const ssize_t buf_sz = 4 * 1024; // size of the buffer 

static char *seed = "452764364:706985783:2521395330:1263432680:2960490940:2680793543";
module_param(seed, charp, 0440); 
MODULE_PARM_DESC(seed, "Xorwow seed value in form of a string\"a:b:c:d:e:cnt\", " 
		"where a .. cnt are 32-bit unsigned integer values"); 
		// passing cl-args to module

/* charp - pointer to a string or char
 * Last parameter:
 * 0440 - only group and seed can rw
 * @perm is 0 if the variable is not to appear in sysfs, or 0444
 * for world-readable, 0644 for root-writable, etc.
 *
 * For better understanding, see chmod at wiki.
*/

static int major = 0;
module_param(major, int, 0);
MODULE_PARM_DESC(major, "Charecter device major number (default = 0) for automatic allocation");

// add docs for methods
/* Used input variables:
 * *file - pointer to kernel structure that never appears in user space (open file descriptor). It represents all that refer to open file. It has current reading position, file operations, private data ('open' system call sets it to NULL before calling the 'open' method. We can use it like a storage, or just ignore, but dont forget to free the resources before closing the file) 
 * *inode - structure for representing the files (different from the '*file'). It has two fields: i_rdev (for inodes that represent device files, it contains actual device nu,ber) and i_cdev (pointer to a kernel internal structure that represents char device - TODO)

 * */

static u32 xwrand(xwrand_t *state) {
    /* Algorithm "xorwow" from p. 5 of Marsaglia, "Xorshift RNGs" */
    u32 t  = state->e;
 
    u32 s  = state->a;  /* Perform a contrived 32-bit shift. */
    state->e = state->d;
    state->d = state->c;
    state->c = state->b;
    state->b = s;
 
    t ^= t >> 2;
    t ^= t << 1;
    t ^= s ^ (s << 4);
    state->a = t;
    state->cnt += 362437;
    return t + state->cnt;
}

// static xwrand_t xwrand_init(u32 a, u32 b, u32 c, u32 d, u32 e, u32 cnt) {
//     xwrand_t ret = {a, b, c, d, e, cnt};	
//     xwrand(&ret);
//     return ret;
// }


// we want to have unique random number(state) for each file -> we need to allocate some memmory to store it  
int xw_open(struct inode *inode, struct file *file) {
	xwrand_t *state = kzalloc(sizeof(*state), GFP_KERNEL);
	u64 jff = 0;
	
	if (state == NULL) {
		return -ENOMEM; // Insufficient kernel memmory was available
	}
	
	*state = gstate;
	state->a ^= xwrand(&gstate);
	state->b ^= ((u64)file) & 0xFFFFFFFF;
	state->c ^= ((u64)file) >> 32;
	jff = get_jiffies_64(); // da heck? 
	state->d ^= ((u64)jff) & 0xFFFFFFFF;
	state->e ^= ((u64)jff) >> 32;
	xwrand(state);
	file->private_data = state;
	pr_info("LOG: File open (%p)\n", file);
    return 0;
}

// zakritie faila
int xw_release(struct inode *inode, struct file *file) {
	kfree(file->private_data);
	pr_info("LOG: File closed (%p)\n", file);
        return 0;
}

ssize_t xw_read(struct file *file, char __user *buf, size_t count, loff_t *offset) {
	/* __user - pointer from userspace -> u need to use copy from user (bc u dont see it in kernel mode) 
	 * copy to user + from user - copy between user and kernel space
	* */
	ssize_t nread = 0; // our _ counter
	ssize_t i = 0;
	ssize_t rest = count % buf_sz;
	ssize_t restsz = rest % sizeof(u32) ? rest / sizeof(u32) : rest / sizeof(u32) + 1;
	u32 kbuf[buf_sz/sizeof(u32)];
	// u32 *kbuf = kmalloc(buf_sz/sizeof(u32) + 1, GFP_NOIO);
	// Using GFP_KERNEL means that kmalloc can put the current process to sleep waiting for a page when called in low-memory situations.
	xwrand_t* state = file->private_data;
	for (i = 0; i < count / buf_sz; i ++) { // running throw the file
		ssize_t j = 0;
		for (j = 0; j < buf_sz / sizeof(u32); j ++) {
			kbuf[j] = xwrand(state);
		}
		if (copy_to_user(buf + nread, kbuf, buf_sz)) {
			goto read_fail;
		}
		nread += buf_sz;
	}

	for (i = 0; i < restsz; i ++) {
		kbuf[i] = xwrand(state);
	}
	if (copy_to_user(buf + nread, kbuf, rest)) {
		goto read_fail;
	}
	
	nread += rest;
	*offset += nread;
	
	if (nread != count) {
		goto read_fail;
	}
	pr_info("LOG: Read %lu\n", (unsigned long)nread);
	return nread;

read_fail:
	pr_err("Read failed. Exp %lu,got: %lu\n", (unsigned long)count, (unsigned long)nread);
	return -EFAULT;
}

/*
llof_t xw_llseek(struct file *file, loff_t offset, int origin) {
        return 0;
}
*/

/* Set file operations for our char device
 * it contains pointer to a function, in case u don't use it -> set to NULL*/
static struct file_operations xwrand_fops = {
        .owner = THIS_MODULE,
        .open = &xw_open,
        .release = &xw_release,
        .read = &xw_read,
        //.llseek = &xw_llseek
};

/* 
 * - few processes cant have identical seed, so when process will open this file for reading, we will generate seed for him. To do this we will create a global seed (gstate) (at first we generate a new_seed -> use it for generate randoms) 
*/

/* Main module algorithm
   The state array must be initialized to not be all zero in the first four words */

static int __init mod_init(void) {
	char _unused; 
	int reg_err;
	pr_info("LOG: Module initialized with value: \"%s\"\n", seed);
	if (6 != sscanf(seed, "%u:%u:%u:%u:%u:%u%c", &gstate.a, &gstate.b, &gstate.c, 
				&gstate.d, &gstate.e, &gstate.cnt, &_unused)) {
		pr_err("Wrong module param: seed\n");
		return -1;
	}
	pr_info("LOG: Input parameters: %u:%u:%u:%u:%u:%u\n", gstate.a, gstate.b, 
			gstate.c, gstate.d, gstate.e, gstate.cnt);

	if (major == 0) {
		pr_warn("Major set to %d during allocation\n", major);
		reg_err = alloc_chrdev_region(&xw_dev, minor, 1, "xwrand");
	}
	else {
		// use provided one
		xw_dev = MKDEV(major, minor);
		reg_err = register_chrdev_region(xw_dev, 1, "xwrand");
	}
	if (reg_err) {
		// main case - if the major was already in use by other device
		pr_err("Error registering device\n");
		return -1;
	}
	pr_info("Registered device with number %d:%d\n", MAJOR(xw_dev), MINOR(xw_dev));
	// to check the registered region run: cat /proc/devices
	cdev_init(&xw_cdev, &xwrand_fops);
	
	if (cdev_add(&xw_cdev, xw_dev, 1) != 0) {
		pr_err("dev add failed, unregistered chardev%d %d\n", MAJOR(xw_dev), MINOR(xw_dev));
		unregister_chrdev_region(xw_dev, 1);
		return -1;
	}
	return 0;
}

static void __exit mod_exit(void) {
	pr_info("Removing cdev\n");
	cdev_del(&xw_cdev);
	pr_info("Unregistered chardev\n");
	unregister_chrdev_region(xw_dev, 1);
	// freeing the region numbers
	pr_info("Module exited\n");
}

module_init(mod_init);
module_exit(mod_exit);
