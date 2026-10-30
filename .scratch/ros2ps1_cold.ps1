$sw = [System.Diagnostics.Stopwatch]::StartNew()
. E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1
Write-Host "dot-source done at $([math]::Round($sw.Elapsed.TotalSeconds,2))s"
Write-Host "daemon present right after dot-source = $([bool](Get-Process ros2_bridge_daemon -ErrorAction SilentlyContinue))"
Write-Host "--- first ros2 (cold) ---"
ros2 topic list -t
Write-Host "--- first command finished at $([math]::Round($sw.Elapsed.TotalSeconds,2))s, exit $LASTEXITCODE ---"
Write-Host "--- second ros2 (warm) ---"
ros2 topic list -t
Write-Host "--- second finished at $([math]::Round($sw.Elapsed.TotalSeconds,2))s, exit $LASTEXITCODE ---"
