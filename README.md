# LDD

Linux Device Drivers workshop - a collection of modules and char drivers implementations, made for kernel study. 


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
./xorrand >> testrand.txt
python3 plots.py // u can specify the filename inside the script
```

## Author: [@qrutyy](https://github.com/qrutyy)

## License

Distributed under the [MIT License](https://choosealicense.com/licenses/mit/). 

