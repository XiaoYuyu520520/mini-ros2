Write-Host ("WindowsDir   = [" + $global:Ros2ShimState.WindowsDir + "]")
Write-Host ("EnvImported  = [" + $global:Ros2ShimState.EnvImported + "]")
Write-Host ("hashtable    = " + ($global:Ros2ShimState -is [hashtable]))
Write-Host ("bat exists   = " + (Test-Path -LiteralPath (Join-Path $global:Ros2ShimState.WindowsDir 'bridge_env.bat')))
Write-Host "--- Import-Ros2Env -Force (re-read, must not throw) ---"
Import-Ros2Env -Force
Write-Host ("ROS2_BRIDGE_BUILD = " + $env:ROS2_BRIDGE_BUILD)
Write-Host "--- ros2 then works ---"
ros2 topic list -t
Write-Host ("exit=" + $LASTEXITCODE)
