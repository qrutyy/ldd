CONFIG_MODULE_SIG=n
name = bdrm
# To build modules outside of the kernel tree, we run "make"
# in the kernel source tree; the Makefile these then includes this
# Makefile once again.
# This conditional selects whether we are being included from the
# kernel Makefile or not.
ifeq ($(KERNELRELEASE),)

    # Assume the source tree is where the running kernel was built
    # You should set KERNELDIR in the environment if it's elsewhere
    KERNELDIR ?= /lib/modules/$(shell uname -r)/build
    # The current directory is passed to sub-makes as argument
    PWD := $(shell pwd)

modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules_install

ins:
	insmod $(name).ko

set:
	echo -n "1 /dev/vdb" > /sys/module/$(name)/parameters/set_redirect_bd

lint:
	./checkpatch.pl -f --no-tree main.c btreeutils.c btreeutils.h

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.symvers *.mod *.order

.PHONY: modules modules_install clean

endif
