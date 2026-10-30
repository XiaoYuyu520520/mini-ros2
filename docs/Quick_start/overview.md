---
layout: default
title: 1 快速开始 · 概述
---

# 1 快速开始

<div class="overview">
<div class="label">本页总览</div>
<p>本章节介绍 mini-ros2 是什么、能做什么、在哪些机器上跑，以及怎么用几步把它跑起来。</p>
<ul>
  <li><a href="windows">Windows 上手</a> —— 加载环境变量、连上机器人、跑通第一个导航任务</li>
  <li><a href="linux">Ubuntu 上手</a> —— 起仿真小车与 gRPC agent、起中继</li>
  <li><a href="connect">连接机器人</a> —— 跨机通信的配置与排错</li>
</ul>
</div>

## 1.1 项目简介

mini-ros2 让你**在 Windows 上像在 Linux 一样使用 ROS2 和 nav2**，不需要安装 ROS2，也不需要 DDS。

工作方式是：Ubuntu 上的机器人（或本仓库自带的 Python 仿真小车）通过局域网的 gRPC 把话题送到
Windows，Windows 上的 nav2 建图、规划，再把 `/cmd_vel` 送回 Ubuntu，小车动起来。

```
Ubuntu（真 ROS2 + 仿真）                          Windows
────────────────────────                          ──────
  真 ROS2 图 ──┐
               ├──gRPC──>  ros2_link 中继 ──gRPC──>  ros2_bridge_daemon.exe ──共享内存──> nav2 / ros2 CLI
  仿真图   ──┘             (relay.py)                  (单 peer 模型)                       (ros2_shim 兼容层)
                                                                                               │
  小车移动  <────────────────────gRPC───────────────────  <───────────────共享内存─────────────┘
```

## 1.2 能做什么

| 能力 | 状态 |
| --- | --- |
| 在 Windows 上用 `ros2 topic list` / `echo` / `pub` / `service call` / `action send_goal` / `param` | ✅ 不用装 ROS2 |
| 在 Windows 上跑 **nav2 全套**（规划、控制、行为树、AMCL、SLAM、平滑、航点、安全链） | ✅ 上游 nav2 1.5.1 源码原地编译 |
| Windows 和 Linux 之间**双向**传话题 / 服务 / 动作 | ✅ 局域网 gRPC |
| Ubuntu 上「真 ROS2」和「仿真」两个图**合并成一个**给 Windows | ✅ `start/connector.sh` |
| 同一话题两边都发布时**自动分开**（`/scan` 与 `/real/scan`） | ✅ 不丢数据 |
| 运行期调参（参数服务 + `ros2 param`） | ✅ 对着正在跑的节点读写 |
| Windows 编译 + 打包成一键运行的文件 | ✅ CI 自动出 Windows / Linux 两个包 |

## 1.3 不能做什么

<div class="callout note">
<span class="label">注意</span>
<p><strong>它不是官方 ROS2。</strong>接口兼容（<code>rclcpp</code> 的头文件、消息类型、
<code>ros2</code> 命令行），但底层是<strong>共享内存 + gRPC</strong>，不是 DDS。所以：</p>
<ul>
  <li>不能和真正的 ROS2 节点在<strong>同一台机器上</strong>直接互相发现（需要走本项目的中继）；</li>
  <li>没有完整的 QoS、DDS 发现、多播；</li>
  <li><code>launch</code> 文件语法只有一小部分。</li>
</ul>
</div>

<div class="callout note">
<span class="label">注意</span>
<p><strong>它不是零拷贝。</strong>一次发布有 4 次内存拷贝（结构体→字节→共享内存→字节→结构体）。
准确说是<strong>零系统调用（zero-syscall）</strong>。实测 <code>/scan</code>（5.8 KB）延迟
~8.8 µs、吞吐 ~88k msg/s，nav2 的 20–50 Hz 完全够用，但大点云/图像不划算。</p>
</div>

<div class="callout note">
<span class="label">注意</span>
<p><strong>它不保证和上游 nav2 行为 100% 一致。</strong>缺失的功能逐条记在
<a href="../FAQ/limitations">功能限制与缺口</a>。</p>
</div>

## 1.4 支持的平台

| 平台 | 说明 |
| --- | --- |
| **Windows 10/11 x86_64** | 主体。编译好之后<strong>不需要 MSYS2、不需要 ROS2</strong>，DLL 都在包里 |
| **Ubuntu 24.04 x86_64** | 兼容层和桥都能编能跑；CI 也会出 Linux 包 |
| **Ubuntu 22.04 arm64** | CI 出原生包（在 arm runner 上构建） |
| 其它 Linux | 没测过。需要 g++ 13+、CMake 3.20+ |

跑之前需要的东西：

| 场景 | 需要什么 |
| --- | --- |
| **用 Windows 发布包** | 什么都不用装，解压就能用（里面带 88 个 DLL） |
| **从源码编译** | Python 3.12、CMake、Ninja、一个 C++20 编译器。Windows 上推荐 MSYS2 UCRT64；Linux 上 `apt install` 见 <a href="../Advanced/development">开发指南</a> |
| **和机器人通信** | 两台机器在同一个局域网，Ubuntu 侧能 SSH |

## 1.5 接下来

* 在 Windows 上跑起来 → <a href="windows">1.6 Windows 上手</a>
* 在 Ubuntu 上跑起来 → <a href="linux">1.7 Ubuntu 上手</a>
* 已经编译好了，只想连机器人 → <a href="connect">1.8 连接机器人</a>

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><span class="ttl">—</span></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="windows">1.6 Windows 上手</a></div>
</div>
