param([string]$Label = 'build-after')
$ErrorActionPreference = 'Stop'
$repo = (Get-Location).Path
if (!(Test-Path -LiteralPath (Join-Path $repo 'InkeysRepo.sln'))) { throw 'Run from the existing repository root.' }
$installation = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -prerelease -products '*' -property installationPath
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\arm64\MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild)) { throw 'Native ARM64 MSBuild is missing.' }
$buildSearchPath = $env:Path
$powerShellDirectory = Split-Path -Parent (Get-Command pwsh.exe -ErrorAction Stop).Source
# 仅规范化当前调用，避免Codex的Path/PATH重复键与节点等待链。
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $powerShellDirectory + ';' + $buildSearchPath
$env:MSBUILDDISABLENODEREUSE = '1'
$started = [DateTimeOffset]::UtcNow
Write-Output 'Direct native ARM64 solution build; supervised budget 20 minutes.'
& $msbuild InkeysRepo.sln /t:Build /m:1 /p:Configuration=Debug /p:Platform=ARM64 /nr:false /nologo /v:minimal 2>&1 |
    Tee-Object -FilePath (Join-Path $PSScriptRoot "$Label.stdout.log")
$buildExit = $LASTEXITCODE
[ordered]@{
    exitCode=$buildExit; startedUtc=$started; endedUtc=[DateTimeOffset]::UtcNow;
    budgetSeconds=1200; msbuild=$msbuild; solution='InkeysRepo.sln'; configuration='Debug|ARM64'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot "$Label.result.json") -Encoding utf8
exit $buildExit
