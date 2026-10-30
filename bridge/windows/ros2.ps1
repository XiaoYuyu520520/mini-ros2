# ros2.ps1 - the PowerShell way in: keep the bridge environment in this window and
# get a `ros2` command that behaves like the one in the docs.
#
#     . E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1    # once per window
#     ros2 topic list -t
#     ros2 topic echo /scan --count 3
#
# or one shot, without dot-sourcing first:
#
#     & E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1 topic list -t
#
# There is also ros2_shell.bat, which is this file for people who would rather
# type a bat: a bat cannot set variables in the PowerShell session that called it,
# so that script hands the job over to this one and lets it open a *new* window,
# prepared the same way a dot-source would prepare the window it runs in.
#
# Two functions come out of it:
#
#   Import-Ros2Env  copies PATH and every ROS2_* / NAV2_* variable out of
#                   bridge_env.bat into the current session. bridge_env.bat stays
#                   the single source of truth -- no value is repeated here, so
#                   the two cannot drift. `-Force` re-reads the bat, which is
#                   what you want after editing it.
#
#   ros2            makes sure the bridge daemon is up (on a cold start it starts
#                   it and waits for the agent's topics to be mirrored), then runs
#                   build\ros2.exe with the arguments it was given. Output and
#                   exit code come straight from the CLI.
#
# ros2.bat remains the cmd.exe entry point and does the same three things; this
# file exists because a bat cannot set variables in a PowerShell session (cmd
# would run as a child process and take its environment with it).

# ---------------------------------------------------------------------------
# Where this file lives, and whether the environment has been imported yet.
#
# Neither can live in `$script:`: this file is normally dot-sourced from the
# PowerShell profile, and once the profile has finished running its scope is
# gone -- `$script:` reads back empty inside the functions below, which silently
# loses the directory and the "already imported" flag. `$global:` is the one
# scope that outlives all of that, and one hashtable is enough to hold both.
# ---------------------------------------------------------------------------
if (-not $global:Ros2ShimState) {
    $global:Ros2ShimState = @{ WindowsDir = $null; EnvImported = $false }
}
$state = $global:Ros2ShimState

if (-not $state.WindowsDir) {
    # $MyInvocation is the dot-source that got us here; $PSScriptRoot is the
    # host's own idea of the directory. Inside a *function* both of those can
    # point at the caller instead of at this file, which is why the directory is
    # resolved here, once, and only accepted when bridge_env.bat is really in it.
    $candidates = @(
        Split-Path -Parent $MyInvocation.MyCommand.Path
        $PSScriptRoot
        $state.WindowsDir
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'bridge_env.bat'))) {
            $state.WindowsDir = $candidate
            break
        }
    }
    if (-not $state.WindowsDir) {
        # A warning and not a throw: a profile that dot-sources this file must
        # never be able to take the shell down. The functions below report the
        # real error if they are called anyway.
        Write-Warning ("ros2.ps1: no bridge_env.bat next to this file (looked in: " +
                       (($candidates | Where-Object { $_ }) -join ', ') + ")")
    }
}

function Import-Ros2Env {
    [CmdletBinding()]
    param([switch] $Force)

    if ($state.EnvImported -and -not $Force) { return }
    if (-not $state.WindowsDir) { throw "ros2.ps1: I do not know where bridge_env.bat is" }
    $bat = Join-Path $state.WindowsDir 'bridge_env.bat'

    # `cmd /c "call bridge_env.bat >nul && set"` prints the environment the bat
    # just produced, one NAME=VALUE per line. Only PATH and the bridge's own
    # prefixes are taken: importing everything would also drag cmd's private
    # variables (PROMPT, ComSpec, ...) into the session for no reason.
    $lines = & "$env:SystemRoot\System32\cmd.exe" /c "call `"$bat`" >nul && set"
    if ($LASTEXITCODE -ne 0) { throw "bridge_env.bat failed with $LASTEXITCODE" }

    $taken = 0
    foreach ($line in $lines) {
        $eq = $line.IndexOf('=')
        if ($eq -lt 1) { continue }
        $name = $line.Substring(0, $eq)
        if ($name -eq 'PATH' -or $name -match '^(ROS2|NAV2)_') {
            Set-Item -Path "Env:$name" -Value $line.Substring($eq + 1)
            $taken++
        }
    }

    # The CLI, the daemon and the round-trip probes all live in one directory;
    # having it on PATH is what makes `ros2.exe`, `ros2_bridge_daemon.exe` and
    # `bridge_cdr_check.exe` usable by bare name in this window.
    $build = $env:ROS2_BRIDGE_BUILD
    if ($build -and (Test-Path -LiteralPath $build) -and
        ($env:PATH -split ';' -notcontains $build)) {
        $env:PATH = "$build;$env:PATH"
    }

    $state.EnvImported = $true
    Write-Verbose "Import-Ros2Env: $taken variable(s) from bridge_env.bat; build dir $build"
}

function ros2 {
    [CmdletBinding()]
    param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $Ros2Args)

    Import-Ros2Env

    if (-not (Get-Process -Name ros2_bridge_daemon -ErrorAction SilentlyContinue)) {
        # Cold start. wait_bridge_ready.bat starts the daemon and polls for the
        # ready file, so the first command in a fresh window does not race the LAN
        # scan and print an empty graph. It returns 1 when the start failed.
        Write-Host "[ros2] bridge daemon is not running - starting it" -ForegroundColor DarkGray
        & (Join-Path $state.WindowsDir 'wait_bridge_ready.bat')
        if ($LASTEXITCODE -ne 0) {
            Write-Error "the bridge daemon could not be started (see $env:ROS2_BRIDGE_LOG)"
            return
        }
    }

    if (-not $env:ROS2_BRIDGE_BUILD) { throw "ros2: ROS2_BRIDGE_BUILD is not set" }
    & (Join-Path $env:ROS2_BRIDGE_BUILD 'ros2.exe') @Ros2Args
}

# Dot-sourcing should leave the window ready, so the variables are imported right
# away instead of waiting for the first `ros2` call. If this file is dot-sourced
# from a profile it may run before the tree is built, and that must not take the
# shell down with it.
try { Import-Ros2Env } catch { Write-Warning "ros2.ps1: $_" }

# `& ros2.ps1 topic list -t` -- the script has no param block, so everything after
# the path lands in $args and is forwarded. When the file is dot-sourced, $args is
# forwarded too but the shell is left alive; only a real invocation exits.
if ($args.Count -gt 0) {
    ros2 @args
    if ($MyInvocation.InvocationName -ne '.') { exit $LASTEXITCODE }
}
