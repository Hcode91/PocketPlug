#!/bin/bash
# usage: padtest.sh name "VFPADKEY script" [end_step] ; renders the last frames and saves name.png
cd /home/claude/vft/t1
END=${3:-7000000}
T=$(( (END*4578/150000) - 60 ))
DISC="$(ls /tmp/vf/Scooby*.bin)" GPIOD=0 VFPAD=1 VFPADKEY="$2" VFLOAD=../menu.state VFRENDER=$T VFSND=1 VFSUB=1 ../vf /tmp/vf/70004.bin $END | grep -aE "^snd key on|^ring" | awk '{print $1,$2,$3,$4,$5}' | sort | uniq -c | sort -rn | head -4 > $1.log
python3 -I -c "
from PIL import Image
import sys
Image.open('fb.ppm').save('$1.png')"
