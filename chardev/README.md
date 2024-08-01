## Char devices

1. **hwprocess** - simple mockup that just initialises
2. **xorrand** - implementation of algorithm "xorwow" from p. 5 of Marsaglia, "Xorshift RNGs"
3. **xorrcd** - final version, that represents 'urandom' type driver

## Usage

If some folder doesn't have *Makefile* - move the main one and change the object file

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
