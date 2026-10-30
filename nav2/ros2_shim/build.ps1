# Configure and build the shim + shared-memory transport, on the six big cores.
#
# This file is an entry point, not an implementation. The work is `build.py`,
# which is the same file on Windows and on Linux: it resolves the toolchain
# through `tools\_toolchain.py` (MSYS2 UCRT64 here, PATH elsewhere) and applies
# the six-big-core rule through `tools\_cores.py`. So nothing here hardcodes an
# MSYS2 root, a `".exe"` suffix or a core list any more -- and the two copies of
# the build logic that let `build.ps1` drift from `build.py` are gone.
#
# The parameters are the ones this script always had, kept so existing habits and
# the docs that mention them keep working; each is forwarded, and anything after
# them is passed to `build.py` untouched:
#
#   powershell -File nav2\ros2_shim\build.ps1
#   powershell -File nav2\ros2_shim\build.ps1 -Jobs 4 -NoPin
#   powershell -File nav2\ros2_shim\build.ps1 controller_server --no-configure
#
# `-DCMAKE_BUILD_TYPE=$BuildType` needed quotes because PowerShell does not
# expand a variable right after `=` in a bare word; `build.py`'s docstring keeps
# that story, and there is nothing to work around now that the `-D` lines live in
# Python.
param(
    [string]$Msys2 = $env:CPU_MSYS2_ROOT,
    [string]$BuildType = "Release",
    [int]$Jobs = 0,
    [string]$Cores = $env:BIG6_CORES,
    [switch]$NoPin,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest
)
$ErrorActionPreference = "Stop"

$python = if ($env:PYTHON) { $env:PYTHON } else { (Get-Command python -ErrorAction Stop).Source }
if ($Cores) { $env:BIG6_CORES = $Cores }
if ($NoPin) { $env:BIG6_NO_PIN = "1" }

$forward = @((Join-Path $PSScriptRoot "build.py"), "--build-type", $BuildType)
if ($Msys2) { $forward += @("--msys2", $Msys2) }
if ($Jobs -gt 0) { $forward += @("--jobs", "$Jobs") }
if ($Rest) { $forward += $Rest }

& $python @forward
exit $LASTEXITCODE
