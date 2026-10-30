---
layout: default
title: 1 快速开始 · Windows 上手
---

# 1.6 Windows 上手

<div class="overview">
<div class="label">本页总览</div>
<p>本节介绍在 Windows 上把 mini-ros2 跑起来的三件事：<strong>下载并解压</strong>、
<strong>加载环境变量</strong>（<code>start/加载模拟ROS环境变量.ps1</code>）、
<strong>用两个终端一头发、一头收来验证</strong>，最后才是连上机器人。</p>
<ul>
  <li><a href="#download">下载与解压</a></li>
  <li><a href="#env">加载环境变量（PowerShell）</a> —— 含解除脚本执行限制</li>
  <li><a href="#verify">验证：两个终端发 / 收话题</a></li>
  <li><a href="#connect">连上机器人</a></li>
  <li><a href="#mission">跑通第一个导航任务</a></li>
</ul>
</div>

<a id="download"></a>

## 1.6.1 下载与解压

去 <a href="https://github.com/XiaoYuyu520520/mini-ros2/releases">Releases</a> 下载
<code>mini-ros2-windows-x86_64-&lt;版本&gt;.zip</code>，解压到任意目录。

<div class="callout note">
<span class="label">注意</span>
<p><strong>解压后保持路径原样，不要把 exe 挪走。</strong>启动脚本按仓库相对路径找文件
（<code>bridge/windows/build/</code>、<code>nav2/ros2_shim/build/</code>、
<code>nav2/nav2_extracted/navigation2-1.5.1/</code>），拍平了谁也找不到谁。</p>
</div>

解压后的目录结构大致是这样：

```
mini-ros2/
├─ start/          一键入口：加载环境变量、起连接器、起 Windows 守护进程
├─ bridge/         Windows 守护进程 + ros2 命令行 + Ubuntu 仿真 + 文档
├─ nav2/           共享内存 ROS2 兼容层 + 移植的 nav2
├─ gRPC/           跨机通信 SDK
├─ code/           小车的建图 + 导航工作区
├─ tools/          仓库工具：跑测试、文档体检、结构检查
└─ package.py      打发布包
```

Windows 包**不需要 MSYS2、不需要 ROS2** —— 运行时的 88 个 DLL 已经跟在 exe 旁边了。

<a id="env"></a>

## 1.6.2 加载环境变量（PowerShell）

<div class="callout warn">
<span class="label">重要</span>
<p>这是 Windows 上最容易踩的一步：<strong>必须「点源」，不能「执行」。</strong></p>
</div>

### 第一步：解除脚本执行限制（首次才需要）

<div class="callout warn">
<span class="label">重要</span>
<p>大多数人的 PowerShell 默认执行策略是 <code>Restricted</code> ——
<strong>任何 <code>.ps1</code> 都不让跑</strong>，点源时会报：</p>
<pre><code>无法加载文件 ...\加载模拟ROS环境变量.ps1，因为在此系统上禁止运行脚本。
有关详细信息，请参阅 about_Execution_Policies。</code></pre>
<p>这不是脚本的问题，是 Windows 的默认安全策略。先解除再点源。</p>
</div>

先看当前策略：

```powershell
Get-ExecutionPolicy -List
```

**推荐：只对当前用户解除（不需要管理员，重启后依然有效）**

```powershell
Set-ExecutionPolicy -Scope CurrentUser -ExecutionPolicy RemoteSigned
```

`RemoteSigned` 的含义是：本地写的脚本可以直接跑，从网上下载的脚本必须有签名。
本项目从 zip 解压出来的脚本属于后者时要配合下面的 `Unblock-File`。

**想「全局解除」（所有用户，需要管理员权限）**

```powershell
# 以管理员身份打开 PowerShell，然后：
Set-ExecutionPolicy -Scope LocalMachine -ExecutionPolicy RemoteSigned
```

<div class="callout note">
<span class="label">注意</span>
<p>不建议用 <code>Unrestricted</code>：它连从网上下载的未签名脚本也放行。
<code>RemoteSigned</code> 已经足够跑本项目的脚本，风险更小。</p>
</div>

**如果 zip 是从浏览器下载的**，文件会带「来自 Internet」的标记（Mark-of-the-Web），
`RemoteSigned` 下仍可能被拦。解压后对脚本解除锁定：

```powershell
Get-ChildItem -Path .\start, .\bridge -Recurse -Filter *.ps1 -ErrorAction SilentlyContinue |
  Unblock-File
```

**只想这次临时绕过**（关窗口即失效，不改系统设置）：

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
```

### 第二步：点源

打开 **PowerShell**，进到**项目根目录**（也就是能看到 `start`、`bridge`、`nav2` 这一层），执行：

```powershell
. .\start\加载模拟ROS环境变量.ps1
```

<div class="callout warn">
<span class="label">重要</span>
<p><code>.\start\加载模拟ROS环境变量.ps1</code> 是相对<strong>项目根目录</strong>的路径。
不要先 <code>cd start</code>，否则会去找 <code>start\start\加载模拟ROS环境变量.ps1</code>，报：</p>
<pre><code>.: 术语 '.\start\加载模拟ROS环境变量.ps1' 不会被识别为 cmdlet、函数、脚本文件或可执行程序的名称。</code></pre>
<p>已经在 <code>start</code> 目录里的话，先 <code>cd ..</code> 回到根目录再点源。</p>
</div>

然后验证：

```powershell
ros2 topic list -t
```

<div class="callout note">
<span class="label">为什么是「点源」而不是「执行」</span>
<p>PowerShell 里执行脚本会在<strong>子进程</strong>里跑，子进程一退，脚本设的环境变量跟着走。
<strong>点源</strong>（<code>.</code> 加空格前缀）是在<strong>当前会话</strong>里执行，
变量和 <code>ros2</code> 这个函数才留在这个窗口里。</p>
<p>PowerShell 7 的写法是 <code>. ./script.ps1</code>（点、空格、路径）；Windows PowerShell 5.1
是 <code>. .\script.ps1</code>。两者都要<strong>带那个点</strong>。</p>
</div>

<div class="callout note">
<span class="label">提示</span>
<p>如果 <code>ros2</code> 报「不是内部或外部命令」，说明<strong>没点源</strong>，
或者用了 <code>.\加载模拟ROS环境变量.ps1</code>（执行，不是点源）。
再点一次 <code>. .\start\加载模拟ROS环境变量.ps1</code> 即可。</p>
</div>

### 用 cmd 的话

```bat
cd /d E:\path\to\mini-ros2
call bridge\windows\bridge_env.bat
ros2 topic list -t
```

<div class="callout note">
<span class="label">注意</span>
<p><code>.bat</code> 里的 <code>set</code> 只作用于它自己那个 cmd 子进程，子进程一退环境就跟着走了，
所以在 cmd 里必须用 <code>call</code> 而不是直接执行。</p>
</div>

### 不想点源：开一个已经准备好的窗口

```powershell
.\bridge\windows\ros2_shell.bat
```

它会开一个新的 PowerShell 窗口，<code>ros2.ps1</code> 已经在里面点源好了。
子进程改不了你当前会话的变量，所以「bat 一键拿到完整环境」只有开新窗口这一条路。

### 环境是临时的

<div class="callout note">
<span class="label">提示</span>
<p>加载进去的东西只影响<strong>当前这个窗口</strong>：<code>PATH</code>、共享内存几何、
SDK 配置、日志级别。关掉窗口或 <code>endlocal</code> 就恢复原样 ——
不写注册表、不改系统 <code>PATH</code>。</p>
</div>

<a id="verify"></a>

## 1.6.3 验证：两个终端，一个发、一个收

环境加载好之后，最快的验证不是看变量，而是**自己发一条话题、自己收一条**。
两个 PowerShell 窗口各点源一次即可 —— 不需要机器人，也不需要 Ubuntu。

### [终端 1] 发布话题

```powershell
. .\start\加载模拟ROS环境变量.ps1
ros2 topic pub /demo/chatter std_msgs/msg/String "{data: hello from Windows}" -r 1
```

* `-r 1` 表示每秒 1 条；不带 `--once` 就会一直发，验证完按 **Ctrl+C** 停。
* 这个窗口每秒打印一行类似
  `publishing #12: std_msgs.msg.String(data='hello from Windows')`。

### [终端 2] 订阅并展示

```powershell
. .\start\加载模拟ROS环境变量.ps1
ros2 topic echo /demo/chatter
```

应该持续看到：

```
data: hello from Windows
---
data: hello from Windows
---
```

### 再看话题与频率

```powershell
ros2 topic list -t           # 列表里应该有 /demo/chatter [std_msgs/msg/String]
ros2 topic info /demo/chatter
ros2 topic hz /demo/chatter  # 平均约 1 Hz
```

`ros2 topic info` 里 **`Publisher count: 1`** 与 **`Subscription count: 1`** 同时出现，
说明发布窗口和订阅窗口都在这台机器的共享内存图里。

<div class="callout note">
<span class="label">提示</span>
<p>这条验证走的是<strong>本机共享内存</strong>（通道 <code>ros2shim/demo/chatter</code>），
不经过 gRPC，所以不需要 Ubuntu、不需要 agent。它证明的是：
环境变量加载成功、<code>ros2</code> 命令可用、兼容层的发布 / 订阅通路是通的。</p>
<p>想再确认守护进程那一半，另开一个窗口跑 <code>ros2 bridge status</code>（只读图，不触发等待）。</p>
</div>

<div class="callout warn">
<span class="label">注意</span>
<p><strong>两个窗口都要各自点源一次。</strong>点源只作用于当前这个窗口，
新开的窗口里 <code>ros2</code> 又变回「不认识这个命令」。</p>
</div>

<div class="callout warn">
<span class="label">注意</span>
<p><code>ros2 topic pub</code> 不带 <code>--once</code> 会一直发。共享内存是
<strong>单写者</strong>模型，忘了关掉会一直占着 <code>/demo/chatter</code> 的写者，
后面的验证就会觉得「消息怪怪的」。验证完在终端 1 按 <strong>Ctrl+C</strong>。</p>
</div>

<a id="connect"></a>

## 1.6.4 连上机器人

<div class="overview">
<div class="label">前置条件</div>
<ul>
  <li>Ubuntu 侧已经起了 <code>ros2_bridge_sim</code>（仿真 agent，端口 50051）或真 ROS2 agent（端口 50111）</li>
  <li>Ubuntu 侧已经起了中继 <code>bash start/connector.sh up -d</code>（对外端口 50060）</li>
  <li>两台机器在同一个局域网</li>
</ul>
</div>

### 情况 A：Windows 和 Ubuntu 是两台机器（跨机）

在点源<strong>之前</strong>设好地址：

```powershell
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

看到 `/scan`、`/odom`、`/tf`、`/map` 这些话题就说明通了。

### 情况 B：全部跑在同一台 Ubuntu 上（回环开发）

```powershell
$env:ROS2_BRIDGE_AGENT = '127.0.0.1:50060'
. .\start\加载模拟ROS环境变量.ps1
ros2 topic list -t
```

<div class="callout warn">
<span class="label">重要</span>
<p><strong>发现不看本机。</strong><code>discovery.cpp</code> 故意把本机所有地址排除在扫描之外
（连 <code>127.0.0.0/8</code> 整段都排除），所以守护进程和 agent 在同一台机器上时
<strong>必须显式给地址</strong>，否则扫不到任何东西。</p>
</div>

### 情况 C：只在这台 Windows 上跑，没有 Ubuntu

守护进程会<strong>自动拉起</strong>并等待第一次同步完成。但没人监听端口时它会一直等到预算耗尽，
所以本机验证时先关掉自动拉起：

```powershell
$env:ROS2_BRIDGE_AUTOSTART = '0'
$env:ROS2_BRIDGE_WAIT_MS = '1500'
```

<a id="mission"></a>

## 1.6.5 跑通第一个导航任务

### 第一步：把小车放回原点

```powershell
ros2 service call /sim/reset std_srvs/srv/Trigger
```

<div class="callout warn">
<span class="label">重要</span>
<p>AMCL 拿 <code>/odom</code> 当初始位姿。车停在别处时，每一次规划都会报
<code>Start Coordinates ... was outside bounds</code>，而且<strong>不重启栈不会自愈</strong>。</p>
</div>

### 第二步：起 nav2 栈并开到目标点

```powershell
python .\bridge\scripts\run_nav2_stack.py --goal auto --report goal.json
```

等它报 `SUCCEEDED`。日志一节点一个文件，在 `%TEMP%\nav2_stack\`，
「栈活了」的标志是 `lifecycle_manager.log` 里出现 `Managed nodes are active`。

### 第三步：停栈

```powershell
python .\bridge\scripts\run_nav2_stack.py stop
```

### 完整的 Windows 侧清单

```powershell
# 加载环境（每个新窗口都要）
. .\start\加载模拟ROS环境变量.ps1

# 看状态
ros2 bridge status
ros2 topic list -t

# 复位小车
ros2 service call /sim/reset std_srvs/srv/Trigger

# 导航
python .\bridge\scripts\run_nav2_stack.py --goal auto --report goal.json
python .\bridge\scripts\run_nav2_stack.py stop
```

## 1.6.6 下一步

| 想做的事 | 去哪一页 |
| --- | --- |
| 改环境变量、共享内存几何、端口 | <a href="../System_configuration/environment">2 系统配置</a> |
| 用 <code>ros2</code> 命令的完整用法 | <a href="../Basic_application/ros2_cli">3.1 ros2 命令行</a> |
| 建图、存图、加载图 | <a href="../Basic_application/slam">3.3 SLAM 建图</a> |
| 跑不起来 | <a href="../FAQ/troubleshooting">6.2 故障排查</a> |

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="overview">1 快速开始 · 概述</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="linux">1.7 Ubuntu 上手</a></div>
</div>
