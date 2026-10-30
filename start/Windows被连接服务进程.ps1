# Windows被连接服务进程.ps1 - Windows 侧那个「被连接」的进程
#
#     .\Windows被连接服务进程.ps1 start [--agent HOST:PORT] [其它守护进程参数]
#     .\Windows被连接服务进程.ps1 run   [--agent HOST:PORT]   前台，Ctrl+C 停
#     .\Windows被连接服务进程.ps1 status
#     .\Windows被连接服务进程.ps1 stop
#
# 它是什么
# --------
# 就是 bridge\windows\ros2_bridge_daemon.exe：Windows 上**唯一**跟外部
# 通信的进程。它连上 Ubuntu 侧的 agent（仿真 / 接线器 / 真 ROS2 都行），
# 把远端话题镜像进本机共享内存图；本机 nav2 / ros2.exe 通过共享内存跟它
# 对接，所以从 nav2 的角度看，它就是一个「被连接的本地服务」。
#
# 名字里的「被连接」指的是：nav2 / ros2 CLI 在 Windows 这一侧**连着它**，
# 而它连着 Ubuntu。两个方向都由它一个人管。
#
# 连谁（默认值来自 加载模拟ROS环境变量.ps1 / bridge_env.bat）
#     ROS2_BRIDGE_AGENT=127.0.0.1:50060      本机开发（接线器跑在本机）
#     ROS2_BRIDGE_AGENT=192.168.1.113:50060   跨机（Ubuntu 上的接线器）
#     ROS2_BRIDGE_AGENT=192.168.1.113:50051   跨机、不经接线器直连仿真 agent
# 不设 --agent 也不设环境变量时，守护进程自己 LAN 扫描发现 agent。
#
# 为什么默认指到 50060（接线器）而不是 50051（仿真）
# -------------------------------------------------
# 直连 50051 只能看到仿真图；连 50060 看到的是「仿真 + 真 ROS2」合并后的
# 图，Windows 侧一次拿全。想只要仿真就把地址改成 :50051。

[CmdletBinding()]
param(
    # 动作。默认 status —— 没给动作就只想看状态，不该顺手把服务起起来。
    [Parameter(Position = 0)]
    [ValidateSet('start', 'run', 'status', 'stop', 'ensure')]
    [string] $Action = 'status',

    # 剩下的原样交给守护进程：--agent / --mirror / --uplink / --no-services …
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $Extra
)

$ErrorActionPreference = 'Stop'

# --- 定位仓库根与兼容层环境 --------------------------------------------------
$StartDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = if ($env:CPU_ROOT) { $env:CPU_ROOT } else { Split-Path -Parent $StartDir }
$env:CPU_ROOT = $Root

# 兼容层环境变量的**唯一事实源**是 bridge\windows\bridge_env.bat，
# 由 bridge\windows\ros2.ps1 的 Import-Ros2Env 读出来。这里不重复任何取值。
$Ros2Ps1 = Join-Path $Root 'bridge\windows\ros2.ps1'
if (-not (Test-Path -LiteralPath $Ros2Ps1)) {
    throw "ros2.ps1 not found: $Ros2Ps1"
}
# 点源它：导入 PATH 与所有 ROS2_*/NAV2_* 变量，并定义 `ros2` 函数。
. $Ros2Ps1

$Build = $env:ROS2_BRIDGE_BUILD
if (-not $Build) { throw 'ROS2_BRIDGE_BUILD is not set (bridge_env.bat did not export it)' }

$Daemon = Join-Path $Build 'ros2_bridge_daemon.exe'
$Cli = Join-Path $Build 'ros2.exe'

function Get-BridgeDaemon {
    Get-Process -Name 'ros2_bridge_daemon' -ErrorAction SilentlyContinue
}

# 把环境里的连接目标和额外参数翻译成守护进程的命令行。
function Get-DaemonArgs {
    $args = @()
    if ($env:ROS2_BRIDGE_AGENT) { $args += @('--agent', $env:ROS2_BRIDGE_AGENT) }
    if ($env:ROS2_BRIDGE_READY_FILE) { $args += @('--ready-file', $env:ROS2_BRIDGE_READY_FILE) }
    if ($env:ROS2_BRIDGE_DAEMON_ARGS) {
        # 刻意不引号：这是一条「额外参数」的通道，跟 ros2_bridge.bat 的做法
        # 一样；加了引号，"--mirror /scan" 会变成一个守护进程解析不了的整体。
        $args += $env:ROS2_BRIDGE_DAEMON_ARGS.Split(' ', [System.StringSplitOptions]::RemoveEmptyEntries)
    }
    if ($Extra) { $args += $Extra }
    return $args
}

switch ($Action) {
    'run' {
        if (-not (Test-Path -LiteralPath $Daemon)) {
            Write-Error "$Daemon not found - build it with:  python bridge\scripts\build_bridge.py"
            exit 1
        }
        $daemonArgs = Get-DaemonArgs
        Write-Host "[win-service] daemon in the foreground ($($daemonArgs -join ' '))"
        & $Daemon @daemonArgs
        exit $LASTEXITCODE
    }

    'status' {
        if (Get-BridgeDaemon) {
            Write-Host "[win-service] daemon running, log: $($env:ROS2_BRIDGE_LOG)"
        } else {
            Write-Host "[win-service] daemon not running, log: $($env:ROS2_BRIDGE_LOG)"
        }
        if (Test-Path -LiteralPath $Cli) { & $Cli bridge status }
        exit 0
    }

    'stop' {
        $procs = @(Get-BridgeDaemon)
        if ($procs.Count -eq 0) {
            Write-Host '[win-service] daemon not running'
            if ($env:ROS2_BRIDGE_READY_FILE) {
                Remove-Item -LiteralPath $env:ROS2_BRIDGE_READY_FILE -Force -ErrorAction SilentlyContinue
            }
            exit 0
        }
        Write-Host "[win-service] stopping daemon"
        foreach ($proc in $procs) { Stop-Process -Id $proc.Id -Force }
        # 停掉之后没有任何东西在镜像远端话题了，ready 文件不能继续声称
        # 「远端话题已经在共享内存里」——否则下一次启动的 shell 会以为
        # 上一次已经同步完，白等一轮。
        if ($env:ROS2_BRIDGE_READY_FILE) {
            Remove-Item -LiteralPath $env:ROS2_BRIDGE_READY_FILE -Force -ErrorAction SilentlyContinue
        }
        exit 0
    }

    { $_ -in @('start', 'ensure') } {
        if (-not (Test-Path -LiteralPath $Daemon)) {
            Write-Error "$Daemon not found - build it with:  python bridge\scripts\build_bridge.py"
            exit 1
        }
        if (Get-BridgeDaemon) {
            Write-Host '[win-service] daemon already running'
            exit 0
        }
        $daemonArgs = Get-DaemonArgs
        Write-Host "[win-service] starting daemon ($($daemonArgs -join ' '))"
        if ($env:ROS2_BRIDGE_AGENT) {
            Write-Host "[win-service] peer: $($env:ROS2_BRIDGE_AGENT)"
        }
        # --detach：守护进程自己重新打开日志文件并丢掉继承的控制台。
        # 用 Start-Process 而不是 `&` 调后台：后者会等守护进程退出，
        # 而这里要的是「放出去就不管了」。
        $all = $daemonArgs + @('--detach')
        Start-Process -FilePath $Daemon -ArgumentList $all -WindowStyle Hidden
        Start-Sleep -Milliseconds 300
        if (-not (Get-BridgeDaemon)) {
            Write-Error "[win-service] the daemon did not come up - log: $($env:ROS2_BRIDGE_LOG)"
            exit 1
        }
        Write-Host '[win-service] started'
        exit 0
    }
}
