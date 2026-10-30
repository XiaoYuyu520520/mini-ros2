# Helper for env_persistence_check.ps1: report what a *fresh* PowerShell session
# can see. Runs as -File so no quoting games are needed, and prints KEY=value
# lines that the caller parses.
$vars = @(Get-ChildItem Env: | Where-Object { $_.Name -match '^(ROS2|NAV2)_' })
$cmd  = @(Get-Command ros2 -ErrorAction SilentlyContinue)
'VARS='     + $vars.Count
'CMDCOUNT=' + $cmd.Count
'CMDTYPE='  + [string]$cmd.CommandType
