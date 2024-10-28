# BDRM 
BDRM is a block device driver that implements log-structured storage based on B+ data structure.
Driver is based on BIO request management and supports BIO split.

For more info - see [presentation v1](https://github.com/qrutyy/ldd/blob/main/blockdev/bdrm/LogStructuredStoringBasedOnB+Tree.pdf)

***Compatable with Linux Kernel 6.8***

## Usage
Highly recommended to test/use the driver using a VM, to protect your data from corruption.
### Initialisation:
```bash
make
insmod bdrm.ko
echo "index path" > /sys/module/bdrm/parameters/set_redirect_bd
```
**index** - postfix for a 'device in the middle' (prefix is bdr), **path** - to which block device to redirect

*Can be reduced to `make`, `make ins` and `make set`*

### Sending requests: 

**Initialised f.e:**
```bash
echo "1 /dev/vdb" > /sys/module/bdrm/parameters/set_redirect_bd
cat /sys/module/bdrm/parameters/get_bd_names // to get the links
```
### Writing
```
dd if=/dev/urandom of=/dev/bdr1 oflag=direct bs=2K count=10;
```
### Reading
```
dd of=test2.txt if=/dev/bdr1 iflag=direct bs=4K count=10; 
```

### Testing
After making some changes you can check a lot of obvious cases using auto-tests:
```
python3 test/autotest.py -n=5 -fs=-1 -bs=0
```
For parameter description - run:
```
python3 test/autotest.py -c
```

## License

Distributed under the [GPL-2.0 License](https://github.com/qrutyy/ldd/blob/main/LICENSE). 
