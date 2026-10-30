```bash
  本项目编程语言占比，统计时间2026/09/12/21:38
C / C++：32,505 行（32.5%） Python：7,388 行（7.4%）
Shell / 脚本：2,965 行（3.0%） (含 Bash / zsh / PowerShell)
其他语言：1,114 行（1.1%） (含 Rust 495 / D 607 / TS 6)
```


# ⚠️ 重要：速度话题转发必须人工启动

一键启动脚本 `bash start_all.sh` 只会启动 **底盘(car.sh)、导航(nav.sh)、AI 推理(start.py)**，
它**故意不启动** `/cmd_vel -> /car_cmd_vel` 的转发控制程序：

```bash
/root/Desktop/code/node/ai_cmd/cmd/cmd_vel转发控制.py
```

- Nav2 规划/控制发布的是话题 **`/cmd_vel`**；
- 底盘驱动 `racecar_driver` 只订阅 **`/car_cmd_vel`**；
- 二者之间靠 `cmd_vel转发控制.py` 桥接（同时负责手动劫持、红绿灯/斑马线停车决策）。

该程序掌握车辆的启动/控制权，**必须由人工确认现场安全后手动启动**，不能由一键脚本自动拉起。
在手动启动它之前，一键启动后 Nav2 无法控制车辆。

```bash
conda deactivate
source ~/Desktop/racecar/install/setup.bash
cd /root/Desktop/code/node/ai_cmd/cmd/
python3 cmd_vel转发控制.py
```

---

# ROS2 使用Ninja编译
colcon build --cmake-args -G Ninja

conda deactivate

source /opt/ros/humble/setup.bash

export PATH=/usr/bin:/bin:/usr/sbin:/sbin

which python3

colcon build --cmake-args -G Ninja -DPYTHON_EXECUTABLE=/usr/bin/python3


########################################################
#ifconfig eth1 down           #关键命令，专门针对这个开发板
########################################################


#启动底盘
#ROS 2 (Humble) 中，你需要运行以下命令来刷新当前终端的环境：
source ~/Desktop/racecar/install/setup.bash
cd /root/Desktop/racecar/
bash car.sh



#导航
source ~/Desktop/racecar/install/setup.bash
cd /root/Desktop/racecar/
bash nav.sh




#ai推理
source ~/Desktop/racecar/install/setup.bash
cd /root/Desktop/code/node/ai_cmd/
python3 start.py



cd /root/Desktop/code/ && bash start_all.sh


#自动控制
cd /root/Desktop/code/node/ai_cmd/cmd/

conda deactivate

source ~/Desktop/racecar/install/setup.bash

python3 cmd_vel转发控制.py





#发点 & 发车
conda deactivate
source ~/Desktop/racecar/install/setup.bash
cd /root/Desktop/code/nav2/
python3 Nav2.py




#重启所有
cd /root/Desktop/code/
bash kill_reset_robot.sh






########################################################
                 下面命令不常用
########################################################

#退出conda
conda deactivate



cd /root/Desktop/code/node/ai_cmd/
python3 start.py




cd /root/Desktop/code/node/ai_cmd/cmd/

conda deactivate

source ~/Desktop/racecar/install/setup.bash

python3 cmd_vel转发控制.py






cd /root/Desktop/code/
bash kill_reset_robot.sh






#退出conda   键盘控制
conda deactivate
cd /root/Desktop/racecar/src/racecar/scripts/
python3 racecar_teleop.py








#速度桥接
conda deactivate
source ~/Desktop/racecar/install/setup.bash
cat > /tmp/cmdvel_relay.py <<'EOF'
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist

class CmdVelRelay(Node):
    def __init__(self):
        super().__init__('cmd_vel_to_car_cmd_vel_relay')
        self.pub = self.create_publisher(Twist, '/car_cmd_vel', 10)
        self.sub = self.create_subscription(Twist, '/cmd_vel', self.cb, 10)
        self.get_logger().info('Relaying /cmd_vel -> /car_cmd_vel')

    def cb(self, msg):
        self.pub.publish(msg)

def main():
    rclpy.init()
    node = CmdVelRelay()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()
EOF

python3 /tmp/cmdvel_relay.py










cd /root/Desktop/racecar/src/multi_goal_ros2/src/,conda activate ros
python3 go.py


conda deactivate && cd /root/Desktop/code/ && python3 显示代价地图.py


#/root/Desktop/racecar/src/racecar/launch/
#/home/davinci-mini/racecar/src/racecar/map/ai_map.yaml





#启动规划
<!-- source ~/Desktop/racecar/install/setup.bash

cd /root/Desktop/code/set_goal/

cd /root/Desktop/code/set_goal/9.0/set_goal/

python3 app.py -->

















conda deactivate
source ~/Desktop/racecar/install/setup.bash
cat > /tmp/cmdvel_relay.py <<'EOF'
import sys
import threading
import termios
import tty
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist

# ==========================================
# 可配置参数
# ==========================================
HIJACK_KEY = ' '           # 劫持快捷键（默认空格）
IN_TOPIC = '/cmd_vel'      # 订阅的话题
OUT_TOPIC = '/car_cmd_vel' # 发布的话题
# ==========================================

class CmdVelRelay(Node):
    def __init__(self):
        super().__init__('cmd_vel_to_car_cmd_vel_relay')
        self.pub = self.create_publisher(Twist, OUT_TOPIC, 10)
        self.sub = self.create_subscription(Twist, IN_TOPIC, self.cb, 10)
        
        self.hijacked = False
        self.get_logger().info(f'Relaying {IN_TOPIC} -> {OUT_TOPIC}. Press SPACE to hijack/stop.')

        # 保存终端设置并开启监听线程
        self.settings = termios.tcgetattr(sys.stdin)
        self.key_thread = threading.Thread(target=self.key_listener)
        self.key_thread.daemon = True
        self.key_thread.start()

    def toggle_hijack(self):
        self.hijacked = not self.hijacked
        if self.hijacked:
            self.get_logger().warn('Hijack ON: Velocity zeroed out!')
            self.publish_zero()
        else:
            self.get_logger().info('Hijack OFF: Resuming normal relay.')

    def publish_zero(self):
        stop_msg = Twist()
        stop_msg.linear.x = 0.0
        stop_msg.linear.y = 0.0
        stop_msg.linear.z = 0.0
        stop_msg.angular.x = 0.0
        stop_msg.angular.y = 0.0
        stop_msg.angular.z = 0.0
        self.pub.publish(stop_msg)

    def key_listener(self):
        try:
            # 设置终端为 cbreak 模式以无阻塞读取单个字符
            tty.setcbreak(sys.stdin.fileno())
            while rclpy.ok():
                char = sys.stdin.read(1)
                if char == HIJACK_KEY:
                    self.toggle_hijack()
                elif char == '\x03': # 处理 Ctrl+C 退出
                    break
        finally:
            self.restore_terminal()

    def cb(self, msg):
        if self.hijacked:
            # 劫持状态下如果上游还在发速度，则持续覆盖为0
            self.publish_zero()
        else:
            self.pub.publish(msg)

    def restore_terminal(self):
        termios.tcsetattr(sys.stdin, termios.TCSADRAIN, self.settings)

    def destroy_node(self):
        self.restore_terminal()
        super().destroy_node()

def main():
    rclpy.init()
    node = CmdVelRelay()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
EOF

python3 /tmp/cmdvel_relay.py





#启动ai推理，conda内运行

#cd /root/Desktop/code/node/om_ai/ && python3 inference_dvpp_shm.py
#查看推理结果
#cd /root/Desktop/code/node/om_ai/test/ && python3 获取结果并控制.py

























































































**💡 怎么用：**
* [cite_start]**控制按键**：在运行该脚本的终端中，按下键盘的 **"U"**、**"I"**、**"O"** 键即可让车子动起来 [cite: 173]。
* [cite_start]**参数修改**：如果觉得速度不合适，可以打开 `racecar_teleop.py` 文件修改小车基础速度和打角，例如 `self.speed_mid = 1500` 和 `self.turn_mid = 90` 等参数 [cite: 175, 179, 180]。

---

### 二、 激光雷达建图与保存 (Mapping & Saving)
用于扫描未知环境并生成 2D 栅格地图。

**1. 启动底层驱动（终端 1）**
* [cite_start]进入目录：`cd /home/racecar` [cite: 190]
* [cite_start]启动底盘：`bash Run_car.sh` [cite: 196]

**2. 启动建图节点（终端 2）**
* [cite_start]启动建图脚本：`bash gmapping.sh` [cite: 198]
* [cite_start]**RViz 操作**：在弹出的 RViz2 界面中，点击左下角的 "Add"，通过 "by topic" 添加 `scan` 和 `map` 话题以查看实时构建的地图 [cite: 200, 202]。此时你可以遥控小车走动来建图。

**3. 保存地图（终端 3）**
* [cite_start]新建终端并进入目录：`cd /home/racecar` [cite: 204]
* [cite_start]执行保存指令：`bash save.sh` [cite: 205]
* [cite_start]**查看地图**：建好的地图会保存在 `/home/racecar/src/racecar/map` 目录下，文件名为 `my_map.pgm`，可以直接点击查看 [cite: 207]。

---

### 三、 多点导航 (Multi-point Navigation)
用于让小车在已知地图上自动行驶到指定的目标点。

**1. 启动底层驱动与导航节点**
* [cite_start]**终端 1**：`cd ../home/racecar` [cite: 211] [cite_start]-> 运行 `bash Run_car.sh` [cite: 212]
* [cite_start]**终端 2**：`cd ../home/racecar` [cite: 213] [cite_start]-> 运行 `bash nav.sh` [cite: 214]

**2. RViz 界面初始化操作**
* [cite_start]点击 RViz 左下角的 **"Startup"** 按钮加载导航模块 [cite: 345, 371]。
* [cite_start]等待地图界面显示完整后，使用顶部工具栏的 **"2D Pose Estimate"** 工具来校准小车的初始位置 [cite: 374, 375]。
* [cite_start]校准标准是：让地图上的**红色线与黑色线大致对齐** [cite: 376]。

**3. 下发导航目标（两种方式）**
* **方式 A：手动打点导航**
    * [cite_start]使用 RViz 顶部工具栏的 **"2D Goal Pose"**，直接在地图上点击并拖动，小车就会自动导航过去 [cite: 403]。
* **方式 B：脚本多点导航（终端 3）**
    * [cite_start]进入多点导航脚本目录：`cd ../home/racecar/src/multi_goal_ros2/src/` [cite: 215]
    * [cite_start]激活环境：`conda activate ros` [cite: 216]
    * [cite_start]执行获取点位或导航脚本：运行 `python3 get_points.py` 或 `python3 goal_1.py` [cite: 217, 221]。



cloc（功能最全面，老牌工具）
cloc（Count Lines of Code）是 Linux 下最经典、支持语言最多的统计工具。它天生会跳过常见编译产物和压缩包。

安装方式 (Ubuntu/Debian)：

Bash
apt update && apt install cloc
分析命令（推荐结合 --vcs=git 自动跳过忽略文件）：

Bash
# 普通扫描（自动跳过二进制及常见产物）
cloc /root/Desktop/

# 如果目录里是 Git 项目，加上此参数会自动剔除 .gitignore 里的编译输出
cloc --vcs=git /root/Desktop/

# 显式排除常见构建目录（如 node_modules、dist、build、target）
cloc --exclude-dir=node_modules,dist,build,target,out /root/Desktop/