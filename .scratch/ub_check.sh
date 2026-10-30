cd ~/code/CPU/code/Desktop/racecar
echo "== big files =="
find . -type f -size +200k -printf "%s %p\n" 2>/dev/null | sort -rn | head -5
echo "== sensor dir =="
ls -la src/openslam_gmapping/include/gmapping/sensor/ 2>&1 | head
echo "== counts =="
find . -type f | wc -l
echo "== racecar scripts =="
ls src/racecar/scripts | head -25
