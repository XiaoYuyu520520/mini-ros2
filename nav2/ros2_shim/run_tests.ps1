# Full test suite for the shim and the shared-memory transport.
#
# These tests are ctest tests now -- 32 in this tree, 1 in `nav2\shm_transport`
# -- so this is a thin wrapper over `tools\run_ctest.py` rather than the
# hand-rolled list of `.exe` names it used to be. That list could not see a test
# added later, could not see `shm_transport` at all (it lives in the other tree),
# and hardcoded this desk's Python interpreter path.
#
#   powershell -File nav2\ros2_shim\run_tests.ps1
#   powershell -File nav2\ros2_shim\run_tests.ps1 -R remap
#   powershell -File nav2\ros2_shim\run_tests.ps1 --repeat until-fail:20
#
# Anything after the named parameters goes to ctest for the shim tree, exactly
# as it does in `python tools\run_ctest.py shim`; the single `shm_transport`
# test is then run without them, because a `-R` filter written for this tree
# matches nothing over there and ctest calls "no tests matched" an error.
# On Linux, use `tools/run_ctest.py` directly.
param(
    [string]$Python = $env:PYTHON,
    [string]$Msys2 = $env:CPU_MSYS2_ROOT,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Rest
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

$py = if ($Python) { $Python } else { (Get-Command python -ErrorAction Stop).Source }
$runner = Join-Path $root "tools\run_ctest.py"
if ($Msys2) { $env:CPU_MSYS2_ROOT = $Msys2 }

$failed = 0
foreach ($tree in @("shim", "shm")) {
    Write-Host "`n=== $tree ===" -ForegroundColor Cyan
    if ($tree -eq "shim" -and $Rest) { & $py $runner $tree @Rest } else { & $py $runner $tree }
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $tree (exit $LASTEXITCODE)" -ForegroundColor Red
        $failed++
    }
}

if ($failed -eq 0) {
    Write-Host "`n=== ALL TEST STEPS PASSED ===" -ForegroundColor Green
    exit 0
}
Write-Host "`n=== $failed TREE(S) FAILED ===" -ForegroundColor Red
exit 1
