
# Mclick

bash script written on C++ to perform MBR/MBL
click/hold action via /dev/uinput

I make it bk i cant find alternative.

## Usage:
```bash
mclick [l/r] [options]
```
#### Click options:
  
  -h, --hold <ms> | Hold duration
  
  -cs, --clickspeed <ms> | Delay between clicks
  
  -t, --time <ms> | Continuous click duration

#### Other options:

-d, --debug | Enable verbose output
### You can use release files like script
``` bash
/"file designation"/mclick [l/r] [options]
```
#### Or move it to your terminal scripts location
```bash
mv /"file designation"/mclick /bin/ 
# Then you always can use script without designation
mclick [l/r] [options]
```