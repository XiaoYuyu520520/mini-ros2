---
layout: default
title: mini-ros2 文档
---

# mini-ros2

<p align="center">
  <a href="https://github.com/XiaoYuyu520520/mini-ros2">Repository</a> ·
  <a href="https://github.com/XiaoYuyu520520/mini-ros2/releases">Releases</a> ·
  <a href="https://github.com/XiaoYuyu520520/mini-ros2/issues">Issues</a> ·
  <img src="https://img.shields.io/badge/license-MIT-blue" alt="license">
</p>

<div class="overview">
<div class="label">项目简介</div>
<p><strong>在 Windows 上像在 Linux 一样使用 ROS2 和 nav2</strong> —— 不需要安装 ROS2，也不需要 DDS。</p>
<p>Ubuntu 上的机器人（或本仓库自带的 Python 仿真小车）通过局域网的 gRPC 把话题送到 Windows，
Windows 上的 nav2 建图、规划，再把 <code>/cmd_vel</code> 送回 Ubuntu，小车动起来。</p>
</div>

## 文档目录

### 1 快速开始

| 页面 | 内容 |
| --- | --- |
| [1.1 概述](Quick_start/overview) | 项目简介、能做什么、不能做什么、支持哪些平台 |
| [1.2 Windows 上手](Quick_start/windows) | 下载解压、**加载环境变量**（`加载模拟ROS环境变量.ps1`）、验证、连机器人、跑第一个导航任务 |
| [1.3 Ubuntu 上手](Quick_start/linux) | 起仿真小车、起真 ROS2 agent、起中继、加载环境 |
| [1.4 连接机器人](Quick_start/connect) | 启动顺序、地址端口在哪配、连不上怎么查 |

### 2 系统配置

| 页面 | 内容 |
| --- | --- |
| [2.1 环境变量配置](System_configuration/environment) | 全部变量清单与默认值、怎么改、怎么验证生效 |
| [2.2 网络与端口配置](System_configuration/network) | 三个网络角色、自动发现的原理、跨网段与防火墙 |
| [2.3 共享内存几何配置](System_configuration/shm) | 槽数与槽大小、内存代价、改完为什么必须一起重启 |

### 3 基础应用开发

| 页面 | 内容 |
| --- | --- |
| [3.1 ros2 命令行](Basic_application/ros2_cli) | `topic` / `service` / `action` / `param` / `node` 全用法 |
| [3.2 运行 nav2 导航栈](Basic_application/nav2_stack) | 起栈、参数来源、把安全链接进链、运行期换控制器 |
| [3.3 SLAM 建图](Basic_application/slam) | 建图、存图、位姿图存档、运动门槛怎么调 |
| [3.4 地图存取](Basic_application/map) | PGM / 8 位 PNG、`map_saver_cli`、`map_server` |

### 4 架构说明

| 页面 | 内容 |
| --- | --- |
| [4.1 总体架构](Architecture/overview) | 三个子项目、三层分工、图注册表、中继 |
| [4.2 共享内存传输](Architecture/shm_transport) | 环形缓冲、两平台后端对照、两条坑、性能基线 |
| [4.3 Action 实现](Architecture/action) | 六条通道、六条必须守住的约定 |

### 5 能力清单

| 页面 | 内容 |
| --- | --- |
| [5.1 已实现能力](Capability/overview) | 建图 / 定位 / 规划 / 控制 / 行为树 / 安全链，逐条实测数字 |
| [5.2 验收与实测数据](Capability/validation) | 验收结论、一键复现、五棵测试树 |

### 6 常见问题

| 页面 | 内容 |
| --- | --- |
| [6.1 功能限制与缺口](FAQ/limitations) | 还差什么、代价多少、按什么优先级补 |
| [6.2 故障排查](FAQ/troubleshooting) | 按症状查：环境变量、规划报错、std::bad_alloc、段错误… |

### 7 进阶开发

| 页面 | 内容 |
| --- | --- |
| [7.1 开发指南](Advanced/development) | 目录结构、三条铁律、加消息类型 / 加测试的步骤 |

## 快速开始（一分钟版）

**Windows** —— 下载
[Release](https://github.com/XiaoYuyu520520/mini-ros2/releases) 解压后，在 PowerShell 里：

```powershell
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

**Ubuntu** —— 起仿真和中继：

```bash
bash bridge/scripts/ubuntu_sim_ctl.sh start
bash start/connector.sh up -d
```

**跨机** —— Windows 侧点源之前设好地址：

```powershell
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

## 提示

<div class="callout note">
<span class="label">它不是官方 ROS2</span>
<p>接口兼容（<code>rclcpp</code> 头文件、消息类型、<code>ros2</code> 命令行），但底层是
<strong>共享内存 + gRPC</strong>，不是 DDS。不能和真正的 ROS2 节点在<strong>同一台机器上</strong>
直接互相发现；没有完整的 QoS、DDS 发现、多播；<code>launch</code> 文件语法只有一小部分。</p>
</div>

<div class="callout note">
<span class="label">它不是零拷贝</span>
<p>一次发布有 4 次内存拷贝。准确说是<strong>零系统调用</strong>。实测 <code>/scan</code>
（5.8 KB）延迟 ~8.8 µs、吞吐 ~88k msg/s，nav2 的 20–50 Hz 完全够用，但大点云/图像不划算。</p>
</div>
