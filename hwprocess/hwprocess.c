#include <linux/init.h>
#include <linux/module.h>
MODULE_LICENSE("Dual BSD/GPL");

static int hello_init(void) {
        printk(KERN_ALERT "Hello, world!\n");
	printk(KERN_INFO "The current process is \"%s\" (pid %i)\n", current->comm, current->pid);
	// current->comm - program files name
	// current->pid - current process id
	return 0;
}

static void hello_exit(void) {
        printk(KERN_ALERT "Bye, world\n");
}

module_init(hello_init);
module_exit(hello_exit);
// run ps -p #PID to get info about current process

