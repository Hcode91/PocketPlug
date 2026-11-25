#!/bin/bash
# usage: sgtest.sh name "VFPADKEY script" [end_step] ; from sg.state (Save Game screen at 215000 ms)
cd /home/claude/vft/t1
END=${3:-7110000}
T=$(( (END*4578/150000) - 60 ))
DISC="$(ls /tmp/vf/Scooby*.bin)" GPIOD=0 VFPAD=1 VFPADKEY="$2" VFLOAD=../sg.state VFRENDER=$T VFSUB=1 ../vf /tmp/vf/70004.bin $END | grep -aE "^ring" | awk '{print $3}' | sort -n | uniq | awk 'NR==1{s=$1;p=$1;next} $1!=p+1{print s"-"p; s=$1} {p=$1} END{if (NR) print s"-"p}' > $1.log
python3 -I -c "
from PIL import Image
Image.open('fb.ppm').save('$1.png')"
