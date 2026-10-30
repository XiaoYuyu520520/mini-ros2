# Is anything *persisted*, or is every window prepared on the fly?
#
#   pwsh -NoProfile -File .scratch/env_persistence_check.ps1
#
# Every child starts with -UseNewEnvironment, which rebuilds the environment from
# the registry for this user -- the same environment Explorer hands a program you
# double-click. That is what makes these probes honest: a plain child process
# inherits whatever this script already has, so even an empty registry would look
# "dirty" from inside it.
#
#   1. cmd, new environment   -> the registry, straight up
#   2. pwsh -NoProfile        -> new environment, nothing of ours runs
#   3. pwsh                   -> new environment, the profile runs (a new window)
#
# Nothing installed and nothing automatic looks like: 1) empty, 2) zero/none,
# 3) zero/none. Anything showing up in 3) but not in 2) is the profile preparing
# windows on purpose -- that single line is what to watch after editing it.

$probe = Join-Path $PSScriptRoot 'ros2_env_probe_child.ps1'

function Start-Fresh([string[]] $psArgs) {
    $out = Join-Path $env:TEMP ('ros2_probe_' + [guid]::NewGuid().ToString('N') + '.txt')
    Start-Process -FilePath 'pwsh.exe' -ArgumentList (@($psArgs) + @('-File', $probe)) `
        -UseNewEnvironment -NoNewWindow -Wait -RedirectStandardOutput $out
    $text = Get-Content -LiteralPath $out -Raw
    Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
    $result = @{}
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match '^(\w+)=(.*)$') { $result[$Matches[1]] = $Matches[2].Trim() }
    }
    return $result
}

function Write-Probe([hashtable] $r) {
    $type = if ($r['CMDTYPE']) { $r['CMDTYPE'] } else { 'none' }
    Write-Host ("   {0} ROS2_*/NAV2_* variable(s); ros2: {1} match(es), type {2}" -f
                $r['VARS'], $r['CMDCOUNT'], $type)
}

Write-Host '1) cmd, environment rebuilt from the registry:'
$out = Join-Path $env:TEMP 'ros2_fresh_cmd.txt'
Start-Process -FilePath 'cmd.exe' -ArgumentList '/c', 'set ROS2_ & set NAV2_ & echo ----' `
    -UseNewEnvironment -NoNewWindow -Wait -RedirectStandardOutput $out
$vars = @(Get-Content -LiteralPath $out | Where-Object { $_ -ne '----' })
Remove-Item -LiteralPath $out -ErrorAction SilentlyContinue
if ($vars.Count -eq 0) {
    Write-Host '   no ROS2_* / NAV2_* variables -> the registry is untouched' -ForegroundColor Green
} else {
    Write-Host "   $($vars.Count) variable(s) -> SOMETHING IS PERSISTED" -ForegroundColor Red
    $vars | ForEach-Object { Write-Host "   $_" }
}

Write-Host '2) pwsh -NoProfile, new environment (nothing of ours runs):'
Write-Probe (Start-Fresh @('-NoProfile'))
Write-Host '3) pwsh, new environment (the profile runs - this is a new window):'
Write-Probe (Start-Fresh @())
