# rubblish —— `code/Desktop` 的杂物间

`code/Desktop` 只留建图和导航（`racecar/`，见 `../racecar/README.md`）。
**不是建图 / 导航的东西都搬到这里**，原样保留，随时可以搬回去。

```text
rubblish/
├── code/                        真车 ~/Desktop/code/：AI 推理、相机、小工具、启动脚本
│   ├── node/ai_cmd/                 yolov5 + dvpp 推理（start.py 那套"一键启动"）
│   ├── node/camera_test/            相机取流 / 硬解码
│   ├── cv_tools/  tools/            零散小工具
│   ├── start_all.sh kill_reset_robot.sh nav.sh gmapping.sh save.sh
│   ├── readme.md                    真车启动顺序、/cmd_vel -> /car_cmd_vel 转发说明
│   └── 键盘遥控.md
├── racecar/                     真车 ~/Desktop/racecar/ 里非建图 / 导航的部分
│   ├── car.sh  racecar_init.sh      底盘启动
│   ├── 环境变量参考.md              真车环境变量备忘（PATH / ROS_DOMAIN_ID 那类）
│   ├── 14_串口配置/                 串口 udev 规则（car / encoder / imu / laser）
│   ├── src/                         racecar_driver、encoder、hipnuc_imu、serial-foxy
│   └── chassis_legacy/              从 racecar 包里挪出来的底盘 / 视觉文件，见它自己的 README
└── session-ses_*.md             早期会话记录
└── stray_saves/                 两个意外存到仓库根目录的图：`racecar_ctl.py save-map -f NAME`
                               曾经把 NAME 当成相对当前目录的路径（已修），见下面。
```

这个目录**不参与** `tools/check_docs.py` 的 Markdown 门禁：里面的 `.md` 记录的是"搬了什么"，
不是要维护的文档（会话记录里光围栏就有几百个不成对的）。旁边的 `racecar/` 仍然要过门禁。

2026-09-18 修掉的那个坑：`save-map -f NAME` 以前把 NAME 当相对路径，图会落到调用者当时的目录里
（仓库根目录那两份就是这么来的，已挪到 `stray_saves/`）。现在 `-f NAME` 和 `nav --map NAME`
一样，都按 `racecar/map/NAME` 解析。

搬回去的办法：按上表把东西挪回 `racecar/` 里的原路径即可，`code/Desktop/racecar` 里已经没有
任何文件引用这里（`CMakeLists.txt` 里那几行 `add_executable` 本来就是注释）。
