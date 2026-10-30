$err = $null
$tokens = $null
[void][System.Management.Automation.Language.Parser]::ParseFile('E:\DesktopData\app\code\CPU\bridge\windows\ros2.ps1', [ref]$tokens, [ref]$err)
Write-Host "parse errors: $($err.Count)"
foreach ($e in $err) { Write-Host $e.Message }
