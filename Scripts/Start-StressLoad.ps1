[CmdletBinding()]
param(
    [ValidateSet('arm64', 'x64')][string]$Arch = 'arm64',
    [switch]$BuildOnly,
    [switch]$NoBuild,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$ToolArgs
)

# 手动压力工具：编译 Scripts/StressLoad/StressLoad.cpp 并启动。
# 只用于人工测试，不进解决方案、CI 或自动测试；产物位于已忽略的 Build/Tools/StressLoad/<arch>/。
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($BuildOnly -and $NoBuild) { throw '-BuildOnly and -NoBuild cannot be combined.' }

$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $PSScriptRoot 'StressLoad/StressLoad.cpp'
$outDir = Join-Path $repoRoot "Build/Tools/StressLoad/$Arch"
$exePath = Join-Path $outDir 'StressLoad.exe'

function Get-HostArch {
    $native = $env:PROCESSOR_ARCHITEW6432
    if ([string]::IsNullOrEmpty($native)) { $native = $env:PROCESSOR_ARCHITECTURE }
    if ($native -ieq 'ARM64') { return 'arm64' }
    return 'x64'
}

function Invoke-Build {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found: $vswhere" }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ([string]::IsNullOrWhiteSpace($vsPath)) {
        $vsPath = & $vswhere -latest -products * -property installationPath
    }
    if ([string]::IsNullOrWhiteSpace($vsPath)) { throw 'No Visual Studio installation with C++ tools was found.' }
    $vsDevCmd = Join-Path $vsPath 'Common7/Tools/VsDevCmd.bat'
    if (-not (Test-Path $vsDevCmd)) { throw "VsDevCmd.bat not found: $vsDevCmd" }

    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $hostArch = Get-HostArch
    $objDir = Join-Path $outDir 'obj'
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    # 写临时批处理导入开发环境后编译；只影响这个子进程，不改系统设置（避免不同 PowerShell 版本的引号转义差异）。
    $buildCmd = Join-Path $objDir 'build.cmd'
    $lines = @(
        '@echo off',
        "call `"$vsDevCmd`" -no_logo -arch=$Arch -host_arch=$hostArch || exit /b 1",
        "cl.exe /nologo /std:c++20 /O2 /EHsc /MT /utf-8 /W4 /DUNICODE /D_UNICODE /Fo`"$objDir\\`" /Fe`"$exePath`" `"$source`" /link /SUBSYSTEM:WINDOWS"
    )
    [IO.File]::WriteAllLines($buildCmd, $lines, [Text.Encoding]::Default)
    Write-Host "Building StressLoad ($Arch) ..."
    & $env:ComSpec /d /c $buildCmd
    if ($LASTEXITCODE -ne 0) { throw "StressLoad build failed (exit $LASTEXITCODE)." }
    Write-Host "Built: $exePath"
}

if (-not $NoBuild) { Invoke-Build }
if ($BuildOnly) { return }
if (-not (Test-Path $exePath)) { throw "StressLoad.exe not found: $exePath (run without -NoBuild first)." }
if ($Arch -ne (Get-HostArch) -and -not ($Arch -eq 'x64' -and (Get-HostArch) -eq 'arm64')) {
    throw "Cannot run $Arch build on this machine; copy $exePath to a matching test machine."
}
Write-Host 'Starting StressLoad. Close its window to stop all load.'
if ($ToolArgs) { Start-Process -FilePath $exePath -ArgumentList $ToolArgs }
else { Start-Process -FilePath $exePath }
