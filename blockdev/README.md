## BDRM 
BDRM is a driver that implements log-structured storing based on data structure B+ tree.
For more info. - see [presentation](https://github.com/qrutyy/ldd/blob/main/blockdev/bdrm/LogStructuredStoringBasedOnB+Tree.pdf)

### Usage
Initialisation:
```bash
make
insmod bdrm.ko
echo "index path" > /sys/module/bdrm/parameters/set_redirect_bd
```
***index** - postfix for a 'device in the middle' (prefix is bdr)*, **path** - to which block device to redirect

Sending requests: 

**f.e:**
```bash
echo "1 /dev/vdb" > /sys/module/bdrm/parameters/set_redirect_bd
cat /sys/module/bdrm/parameters/get_bd_names // to get the links
```
Writing
```
dd if=/dev/urandom of=/dev/bdr1 oflag=direct bs=2K count=10;
```
Reading
```
dd of=test2.txt if=/dev/bdr1 iflag=direct bs=4K count=10; 
```
