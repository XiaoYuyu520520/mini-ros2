# 加载模拟ROS环境变量.ps1 - 加载模拟 ROS2 环境（PowerShell 版）
#
#     . E:\DesktopData\app\code\CPU\start\加载模拟ROS环境变量.ps1
#     ros2 topic list -t
#
# 为什么 PowerShell 要单独一个文件
# ------------------------------
# **`.bat` 不能给 PowerShell 会话留下环境变量。** `.bat` 跑在 cmd.exe 子进程
# 里，`set` 只影响那个子进程，子进程一退出变量跟着走。所以在 PowerShell 里
# `.\加载模拟ROS环境变量.bat` 打印得出来（那是 cmd 内部展开的），但敲 `ros2`
# 仍然「不是内部或外部命令」——PowerShell 的 PATH 从没被改过。
#
# 这不是脚本的毛病，是 Windows 的机制：`.bat` 改不了父进程的会话。
# PowerShell 唯一正确的做法是**点源**（`.` 前缀），点源在**当前会话**里执行，
# 变量和函数才会留下来。
#
# 三层关系
# --------
#   bridge\windows\bridge_env.bat    兼容层环境变量的**唯一事实源**
#        │
#        ├── bridge\windows\ros2.ps1        点源它 -> 得到 `ros2` 命令和 PATH
#        │
#        └── 本文件                            点源它 -> 再叠加仿真/接线器那一层
#
# 本文件**不重复**任何取值：兼容层那部分仍然是 `ros2.ps1` 的 `Import-Ros2Env`
# 去读 `bridge_env.bat`，这里只补仿真侧（`ros2_bridge_sim` / `ros2_link`）
# 和接线器端口那几个变量。
#
# 变量
#   ROS2_SIM_ROOT         仿真包目录        默认 <root>/bridge/ubuntu
#   ROS2_SIM_PYTHON       仿真用的 python   默认 <root>/.venv/bin/python
#   ROS2_LINK_SIM_PORT    仿真 agent 端口   默认 50051
#   ROS2_LINK_REAL_PORT   真 ROS2 agent 端口 默认 50111
#   ROS2_LINK_PORT        接线器对外端口    默认 50060
#   ROS2_BRIDGE_AGENT     守护进程连谁      默认 127.0.0.1:50060
#
# 跨机：先设 ROS2_BRIDGE_AGENT 再点源
#   $env:ROS2_BRIDGE_AGENT = '192.168.1.113:50060'
#   . E:\DesktopData\app\code\CPU\start\加载模拟ROS环境变量.ps1

# --- 定位仓库根 -------------------------------------------------------------
$StartDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ScriptRoot = Split-Path -Parent $StartDir
# CPU_ROOT 可以指到别的目录，但**只有它真的是一棵完整的树时才算数**。
# 否则上一次会话留下的旧值（比如换了目录解压后还指着上一份）会把整个脚本
# 带到没有 ros2.ps1 的地方去。
$Root = $ScriptRoot
$PreviousRoot = $env:CPU_ROOT
if ($env:CPU_ROOT -and
    (Test-Path -LiteralPath (Join-Path $env:CPU_ROOT 'bridge\windows\ros2.ps1'))) {
    $Root = $env:CPU_ROOT
}
$env:CPU_ROOT = $Root

# --- 兼容层（点源 ros2.ps1：导入 PATH 与所有 ROS2_*/NAV2_* 变量）--------------
$Ros2Ps1 = Join-Path $Root 'bridge\windows\ros2.ps1'
if (-not (Test-Path -LiteralPath $Ros2Ps1)) {
    Write-Error "ros2.ps1 not found: $Ros2Ps1" -Category ResourceUnavailable
    return
}
. $Ros2Ps1

# --- 仿真侧 -----------------------------------------------------------------
# 这些是「由根目录推出来」的路径。会话里可能还留着上一棵树的值，所以判断的
# 标准不是「变量是否为空」，而是「它是不是指向当前这棵树」：指向别的
# mini-ros2 解压目录的一律按旧值处理。
$ExpectedSimRoot = Join-Path $Root 'bridge\ubuntu'
if ($env:ROS2_SIM_ROOT) {
    $simTree = Split-Path -Parent (Split-Path -Parent $env:ROS2_SIM_ROOT)
    if ($simTree -ne $Root) {
        $env:ROS2_SIM_ROOT = $ExpectedSimRoot
    }
} else {
    $env:ROS2_SIM_ROOT = $ExpectedSimRoot
}

if ($env:ROS2_SIM_PYTHON) {
    $pythonTree = Split-Path -Parent (Split-Path -Parent $env:ROS2_SIM_PYTHON)
    if ($pythonTree -ne $Root) {
        $env:ROS2_SIM_PYTHON = Join-Path $Root '.venv\bin\python'
    }
} else {
    $env:ROS2_SIM_PYTHON = Join-Path $Root '.venv\bin\python'
}
if (-not $env:ROS2_LINK_SIM_PORT) { $env:ROS2_LINK_SIM_PORT = '50051' }
if (-not $env:ROS2_LINK_REAL_PORT){ $env:ROS2_LINK_REAL_PORT = '50111' }
if (-not $env:ROS2_LINK_PORT)     { $env:ROS2_LINK_PORT = '50060' }

# 仿真包要能被 import 到，PYTHONPATH 得指到 bridge\ubuntu 这一层。
# 用 ';' 分隔（Windows）。别的 mini-ros2 树留下的那一条（形如
# ...\bridge\ubuntu）先摘掉，否则 import 到的可能是上一棵树。
if ($env:PYTHONPATH) {
    $kept = @($env:PYTHONPATH -split ';' | Where-Object {
        $_ -and ($_ -eq $env:ROS2_SIM_ROOT -or -not ($_ -match '\\bridge\\ubuntu$'))
    })
    $env:PYTHONPATH = ($kept -join ';')
}
if (-not $env:PYTHONPATH) { $env:PYTHONPATH = $env:ROS2_SIM_ROOT }
elseif (($env:PYTHONPATH -split ';') -notcontains $env:ROS2_SIM_ROOT) {
    $env:PYTHONPATH = "$env:ROS2_SIM_ROOT;$env:PYTHONPATH"
}

# --- 默认连到哪里 -----------------------------------------------------------
# 本机开发走回环；跨机时在点源**之前**设 ROS2_BRIDGE_AGENT。
if (-not $env:ROS2_BRIDGE_AGENT) { $env:ROS2_BRIDGE_AGENT = "127.0.0.1:$env:ROS2_LINK_PORT" }

Write-Host "[sim-env] CPU_ROOT          = $env:CPU_ROOT"
Write-Host "[sim-env] ROS2_SIM_ROOT     = $env:ROS2_SIM_ROOT"
Write-Host "[sim-env] ROS2_BRIDGE_AGENT = $env:ROS2_BRIDGE_AGENT"
Write-Host "[sim-env] ros2 (compat)     = $env:ROS2_BRIDGE_BUILD"
Write-Host "[sim-env] now:  ros2 topic list -t"