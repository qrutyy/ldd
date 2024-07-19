# LDD

*Linux Device Drivers workshop* - a collection of modules (char and block drivers) implementations, made for kernel practice. 

## Usage

If some folder doesn't have *Makefile* - move the main one

### hwprocess

```bash
make
insmod hwprocess.ko
dmesg
```
*Now you see the DM output.*

### xorrand 
Use these steps to reproduce provided plots.
```bash
gcc xorrand.c -o xorrand -O2
./xorrand n >> testrand.txt
python3 plots.py // u can specify the filename inside the script
```
***n** is the amount of numbers to generate* 

### xorrcd 

```bash
make 
insmod xorrcd.ko
dmesg // check the registered major and minor of module that appeared in dev/

cd ~dev/ && sudo mknod -m a=r xorrcd c *major* *minor*

cd ~root/ && dd if=/dev/xorrcd of=./rnd bs=1M count=1
```
***bs** ammount of bites, **count** times are written to root/rnd*

### bdrm

```bash
make
insmod bdrm.ko
echo "index path" > /sys/module/bdrm/parameters/set_redirect_bd
```
***index** - postfix for a 'device in the middle' (prefix is bdr)*, **path** - to which block device to redirect

**f.e:**
```bash
echo "1 /dev/vdb" > /sys/module/bdrm/parameters/set_redirect_bd
cat /sys/module/bdrm/parameters/get_bd_names // to get the links
dd of=/dev/urandom if=/dev/test1 iflag=direct bs=2K count=10;
```

## License

Distributed under the [GPL-2.0 License](https://github.com/qrutyy/ldd/blob/main/LICENSE). 
