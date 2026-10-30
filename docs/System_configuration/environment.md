---
layout: default
title: 2 系统配置 · 环境变量配置
---

# 2.1 环境变量配置

<div class="overview">
<div class="label">本页总览</div>
<p>本节介绍需要配置哪些环境变量、各自的作用和默认值，以及怎么改。</p>
<ul>
  <li><a href="#load">加载环境变量</a></li>
  <li><a href="#vars">变量清单</a></li>
  <li><a href="#modify">修改与覆盖</a></li>
</ul>
</div>

<a id="load"></a>

## 2.1.1 加载环境变量

Windows 与 Linux 各有一套入口，作用相同：**设置 PATH、共享内存几何、SDK 配置、日志级别**。
环境是**临时的** —— 关窗口 / 退 shell 就恢复原样，不写注册表、不改系统 PATH。

### [Windows] PowerShell

```powershell
. .\start\加载模拟ROS环境变量.ps1
```

### [Windows] cmd

```bat
call bridge\windows\bridge_env.bat
```

### [Linux]

```bash
source ~/code/CPU/bridge/linux/bridge_env.sh
```

<a id="vars"></a>

## 2.1.2 变量清单

两边同名同值：

| 变量 | 作用 | 默认值 |
| --- | --- | --- |
| `PATH` | 构建目录 + MSYS2 运行时（Windows 专有） | — |
| `NAV2_SHM_SLOT_COUNT` | 每条通道的槽数 | `8` |
| `NAV2_SHM_SLOT_SIZE` | 普通话题的槽大小 | `524288`（512 KB） |
| `NAV2_SHM_LARGE_SLOT_SIZE` | `map`/`costmap` 类话题的槽大小 | `2097152`（2 MB） |
| `NAV2_SHM_LOG_LEVEL` | 兼容层日志级别 | `warn` |
| `NAV2_SHM_NAMESPACE` | 通道名前缀，用来隔离两套栈 | 空 |
| `NAV2_SHIM_BT_DIR` | 行为树 XML 目录 | `nav2/ros2_shim/config/bt` |
| `ROS2_GRPC_SDK_CONFIG_DIR` | SDK 发现配置目录 | `gRPC/ros2_gRPC_sdk/config` |
| `ROS2_BRIDGE_AGENT` | 守护进程连哪个 agent | 空（= 自动发现） |
| `ROS2_BRIDGE_AUTOSTART` | `ros2` 命令要不要自动拉守护进程 | `1` |
| `ROS2_BRIDGE_WAIT_MS` | 等首次同步的预算 | `30000` |
| `ROS2_BRIDGE_READY_FILE` | 就绪文件路径 | `%TEMP%\nav2_bridge.ready` |
| `ROS2_BRIDGE_LOG_DIR` | 守护进程日志目录 | `%TEMP%\nav2_bridge` |
| `ROS2_LINK_SIM_PORT` | 仿真 agent 端口 | `50051` |
| `ROS2_LINK_REAL_PORT` | 真 ROS2 agent 端口 | `50111` |
| `ROS2_LINK_PORT` | 中继对外端口 | `50060` |
| `ROS2_LINK_COLLISION` | 同名话题谁保住原名 | `prefer-sim` |

<a id="modify"></a>

## 2.1.3 修改与覆盖

<div class="callout note">
<span class="label">提示</span>
<p>要改默认值，改 <code>bridge/windows/bridge_env.bat</code> 和
<code>bridge/linux/bridge_env.sh</code> —— 它们是<strong>唯一的事实源</strong>。
点源脚本不重复任何取值，只读这两个文件。</p>
</div>

### 单次覆盖（不改文件）

```powershell
# Windows：点源之前设
$env:ROS2_BRIDGE_AGENT = '192.168.0.153:50060'
. .\start\加载模拟ROS环境变量.ps1
```

```bash
# Linux
export ROS2_BRIDGE_AGENT=192.168.0.153:50060
source ~/code/CPU/bridge/linux/bridge_env.sh
```

### 改过文件之后在当前窗口重读

```powershell
Import-Ros2Env -Force
```

<div class="callout note">
<span class="label">注意</span>
<p>PowerShell profile 里<strong>没有</strong>任何 ros2 相关的东西：新窗口要么点源一次
<code>start/加载模拟ROS环境变量.ps1</code>，要么用 <code>ros2_shell.bat</code> 开一个已经点源好的窗口。
别的窗口里 <code>ros2</code> 就是「不认识这个命令」。</p>
</div>

## 2.1.4 验证环境变量生效了

<div class="callout warn">
<span class="label">重要</span>
<p>不要用 <code>cmd /c '... &amp;&amp; echo %VAR%'</code> 验证 ——
<code>%VAR%</code> 在<strong>整条命令行解析时</strong>就展开了，比 <code>call</code> 早，
看起来像「没生效」，其实是测试写法错。</p>
</div>

推荐直接在 PowerShell 里读：

```powershell
. .\start\加载模拟ROS环境变量.ps1
$env:ROS2_BRIDGE_AGENT
$env:NAV2_SHM_SLOT_SIZE
```

或者用延迟展开：

```powershell
cmd /v:on /c 'call bridge\windows\bridge_env.bat >nul & echo !ROS2_BRIDGE_AGENT!'
```

<div class="pagenav">
<div class="prev"><span class="dir">上一页</span><a class="ttl" href="../Quick_start/connect">1.8 连接机器人</a></div>
<div class="next"><span class="dir">下一页</span><a class="ttl" href="network">2.2 网络与端口配置</a></div>
</div>
