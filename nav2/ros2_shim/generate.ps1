# Regenerate ROS2 messages (C++ headers + Python package).
param(
    [string]$Python = $env:PYTHON
)
$ErrorActionPreference = "Stop"
# The interpreter is -Python / $env:PYTHON, else the first `python` on
# PATH; this used to spell out one desk's virtualenv.
$py = if ($Python) { $Python } else { (Get-Command python -ErrorAction Stop).Source }
$shim = $PSScriptRoot
$nav2 = Split-Path -Parent $shim

# Scan order matters: an earlier root wins when two roots define the same
# message, so the compatibility overlay is consulted before the pristine
# upstream checkouts it patches.
$roots = @(
    (Join-Path $nav2 "third_party\overlay"),
    (Join-Path $nav2 "nav2_extracted\navigation2-1.5.1\nav2_msgs"),
    (Join-Path $nav2 "nav2_extracted\navigation2-1.5.1\nav2_dwb_controller"),
    (Join-Path $nav2 "third_party")
)

$genArgs = @((Join-Path $shim "tools\generate_messages.py"))
foreach ($r in $roots) {
    $genArgs += "--root"
    $genArgs += $r
}
$genArgs += @(
    "--exclude", "test_msgs",
    "--output", (Join-Path $shim "generated\include"),
    "--python-output", (Join-Path $shim "python")
)
& $py @genArgs
if ($LASTEXITCODE -ne 0) { throw "message generation failed" }
