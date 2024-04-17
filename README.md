# LDD

*Linux Device Drivers workshop* - a collection of modules and char drivers implementations, made for kernel study. 

## Usage

For all the subprojects - move the Makefile into current folder. 

TODO: move docs in a specified file

### hwprocess

```bash
make
insmod hwprocess.ko
dmesg
```
*Now you see the DM output.*

### xorrand 
Used these steps to reproduce provided plots.
```bash
gcc xorrand.c -o xorrand -O2
./xorrand n >> testrand.txt
python3 plots.py // u can specify the filename inside the script
```
***n** is the amount of numbers to generate* 

### xorrcd 

```bash
make 
sudo insmod xorrcd.ko
sudo dmesg // check the registered major and minor of module that appeared in dev/
cd ~dev/ && sudo mknod -m a=r xorrcd c *major* *minor*
sudo su
cd ~root/ && dd if=/dev/xorrcd of=./rnd bs=1M count=1
```
*Here you are, **bs** ammount of bites, **count** times are written to root/rnd*
## Author: [@qrutyy](https://github.com/qrutyy)

## License

Distributed under the [MIT License](https://choosealicense.com/licenses/mit/). 

 

