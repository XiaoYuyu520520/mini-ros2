#!/bin/bash
#
# ============================================================================
# 【重要】一键启动说明 / 必须人工启动的步骤
# ----------------------------------------------------------------------------
# 本脚本只启动：底盘驱动(car.sh)、导航(nav.sh)、AI 推理(start.py)。
#
# 它【不会】启动 /cmd_vel -> /car_cmd_vel 转发控制程序：
#     /root/Desktop/code/node/ai_cmd/cmd/cmd_vel转发控制.py
#
# 原因：该程序掌握车辆的启动/控制权（含手动劫持、红绿灯/斑马线决策），
#       必须由人工确认现场安全后手动启动，不能由一键脚本自动拉起。
#
# 因此：Nav2 发布的是 /cmd_vel，而底盘 racecar_driver 只监听 /car_cmd_vel。
#       在手动执行下面的命令之前，Nav2 无法控制车辆：
#
#     conda deactivate
#     source ~/Desktop/racecar/install/setup.bash
#     cd /root/Desktop/code/node/ai_cmd/cmd/
#     python3 cmd_vel转发控制.py
# ============================================================================

# 安装 tmux（如果没装）
#apt update && apt install -y tmux

# 会话名
SESSION="racecar"

# 检查是否已有同名会话，避免重复执行产生重复节点/窗口冲突
if tmux has-session -t "$SESSION" 2>/dev/null; then
  echo "检测到已存在的 tmux 会话 '$SESSION'，直接进入，不再重复启动。"
  echo "如需重启，请先执行: tmux kill-session -t $SESSION"
  tmux attach -t "$SESSION"
  exit 0
fi

# 创建一个会话
tmux new-session -d -s "$SESSION"

# 第一个窗口：底盘
tmux send-keys -t "$SESSION:0" "
source ~/Desktop/racecar/install/setup.bash
cd ~/Desktop/racecar/
bash car.sh
" C-m

# 新建第二个窗口：导航
tmux new-window -t "$SESSION:1"
tmux send-keys -t "$SESSION:1" "
source ~/Desktop/racecar/install/setup.bash
cd ~/Desktop/racecar/
bash nav.sh
" C-m

# 新建第三个窗口：AI推理
tmux new-window -t "$SESSION:2"
tmux send-keys -t "$SESSION:2" "
source ~/Desktop/racecar/install/setup.bash
cd ~/Desktop/code/node/ai_cmd/
python3 start.py
" C-m

# 进入会话
tmux attach -t "$SESSION"
