set -eu
tmp=$(mktemp -d)
cd "$tmp"
mkdir -p scripts
touch scripts/ai_第一个点后回终点.py scripts/detct.py scripts/end_3_12.py scripts/get.py \
      "scripts/go copy.py" "scripts/go copy_多点需要切换.py" scripts/go.py scripts/go_2c.py \
      scripts/goal_l.py scripts/go一条路径.py "scripts/一条路径+csv.py" \
      scripts/人工智能赛道初始版本代码.py scripts/会跳过几个点.py \
      "scripts/第二个点nav2打断，然后转圈圈.py" scripts/终点不停.py scripts/跑一圈以内.py \
      scripts/重复载入完成圈数.py \
      scripts/ai_test.csv scripts/ai_test1.csv scripts/ai_test2.csv scripts/sim_room12.csv

cat > CMakeLists.txt <<'CMAKE_EOF'
cmake_minimum_required(VERSION 3.16)
project(racecar LANGUAGES NONE)
CMAKE_EOF

cat >> CMakeLists.txt <<'CMAKE_EOF_BLOCK'
# Install Python executables
#
# 每个脚本都要装：`racecar_ctl.py run <脚本>` 的 ros2 后端就是 `ros2 run racecar <脚本>`，
# 这里只装四个的话，另外十三个在车上根本没有可执行文件。航线 CSV 也一起装 —— 脚本按自己的
# __file__ 找航线，装上去的那份旁边没有 csv，就只能退回随包的 ai_test.csv。
# 名字里有空格的必须加引号，CMake 不然会把一个路径当成两个参数。
install(PROGRAMS
  scripts/ai_第一个点后回终点.py
  scripts/detct.py
  scripts/end_3_12.py
  scripts/get.py
  "scripts/go copy.py"
  "scripts/go copy_多点需要切换.py"
  scripts/go.py
  scripts/go_2c.py
  scripts/goal_l.py
  scripts/go一条路径.py
  scripts/一条路径+csv.py
  scripts/人工智能赛道初始版本代码.py
  scripts/会跳过几个点.py
  scripts/第二个点nav2打断，然后转圈圈.py
  scripts/终点不停.py
  scripts/跑一圈以内.py
  scripts/重复载入完成圈数.py
  DESTINATION lib/${PROJECT_NAME}
)

install(FILES
  scripts/ai_test.csv
  scripts/ai_test1.csv
  scripts/ai_test2.csv
  scripts/sim_room12.csv
  DESTINATION lib/${PROJECT_NAME}
)
CMAKE_EOF_BLOCK

echo "=== the block cmake is being handed ==="
sed -n '3,$p' CMakeLists.txt
echo "=== configure ==="
cmake -S . -B build -G Ninja > configure.log 2>&1 || { cat configure.log; exit 1; }
echo "configure ok"
echo "=== install ==="
cmake --install build --prefix "$tmp/prefix" > install.log 2>&1 || { cat install.log; exit 1; }
echo "install ok"
echo "=== landed: ==="
ls prefix/lib/racecar | wc -l
ls prefix/lib/racecar
echo "=== checks ==="
test -f "prefix/lib/racecar/go copy.py" && echo "ok  quoted name with a space installed"
test -f "prefix/lib/racecar/第二个点nav2打断，然后转圈圈.py" && echo "ok  chinese + full-width comma name installed"
test -f "prefix/lib/racecar/一条路径+csv.py" && echo "ok  name with a plus installed"
test -x "prefix/lib/racecar/终点不停.py" && echo "ok  PROGRAMS kept the exec bit"
test -f "prefix/lib/racecar/ai_test.csv" && echo "ok  csv installed next to the scripts"
rm -rf "$tmp"
echo "probe done"
