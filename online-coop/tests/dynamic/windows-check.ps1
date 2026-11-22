param([string]$HostSource, [switch]$Sanitize)
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..\..')
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
$testArgs = @('tests/dynamic/check.py')
if ($Sanitize) { $testArgs += '--sanitize' }
& python @testArgs
if ($LASTEXITCODE) { exit $LASTEXITCODE }
if ($HostSource) {
    $luaArgs = @('tests/dynamic/check_lua.py', '--host', $HostSource)
    if ($Sanitize) { $luaArgs += '--sanitize' }
    & python @luaArgs
    exit $LASTEXITCODE
}
