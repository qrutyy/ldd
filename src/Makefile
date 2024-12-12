CONFIG_MODULE_SIG=n
name = lsbdd
# To build modules outside of the kernel tree, we run "make"
# in the kernel source tree; the Makefile these then includes this
# Makefile once again.
# This conditional selects whether we are being included from the
# kernel Makefile or not.

# Delete BlockDevice Index
DBI?=1
#Data Structure name (see README)
DS?="bt"

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
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions *.symvers *.mod *.order *.o.d
	rm -rf include/*.o include/.*.cmd include/.*.o.d

exit:
	echo "$(DBI)" > /sys/module/lsbdd/parameters/delete_bd
	rmmod lsbdd.ko

init:
	make
	make ins
	echo "$(DS)" > /sys/module/lsbdd/parameters/set_data_structure
	make set

.PHONY: modules modules_install clean

endif

