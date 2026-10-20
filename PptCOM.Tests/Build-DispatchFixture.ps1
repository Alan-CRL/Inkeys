param(
    [ValidateSet('x64', 'arm64')]
    [string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
$fixtureRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixtureVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $fixtureVswhere)) { throw 'Visual Studio vswhere was not found.' }
$fixtureVs = & $fixtureVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $fixtureVs) { throw 'No installed Visual Studio ARM64 C++ toolchain was found.' }

# 复用现有工程的 v143 和 SDK，不自动选择新版默认工具链或修改工程。
[xml]$fixtureProject = Get-Content -LiteralPath (Join-Path $fixtureRepo 'Inkeys\Inkeys.vcxproj') -Raw
$fixtureToolset = $fixtureProject.SelectSingleNode("//*[local-name()='PlatformToolset']").InnerText
$fixtureSdk = $fixtureProject.SelectSingleNode("//*[local-name()='WindowsTargetPlatformVersion']").InnerText
$fixtureVersionFile = Join-Path $fixtureVs "VC\Auxiliary\Build\Microsoft.VCToolsVersion.$fixtureToolset.default.txt"
if (-not (Test-Path -LiteralPath $fixtureVersionFile)) { throw "Installed toolset version file missing: $fixtureVersionFile" }
$fixtureVcVersion = (Get-Content -LiteralPath $fixtureVersionFile -Raw).Trim()
$fixtureVc = Join-Path $fixtureVs "VC\Tools\MSVC\$fixtureVcVersion"
$fixtureKit = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
if (-not $fixtureKit) { throw 'Installed Windows SDK root was not found.' }
$fixtureCl = Join-Path $fixtureVc "bin\Hostarm64\$Architecture\cl.exe"
if (-not (Test-Path -LiteralPath $fixtureCl)) { throw "ARM64 native compiler missing: $fixtureCl" }
if (-not (Test-Path -LiteralPath (Join-Path $fixtureKit "Lib\$fixtureSdk\um\$Architecture\OleAut32.lib"))) {
    throw "Project Windows SDK $fixtureSdk is not installed for $Architecture."
}

$fixtureOut = Join-Path $fixtureRepo "Build\pptcom-dispatch-fixture\$Architecture"
New-Item -ItemType Directory -Force -Path $fixtureOut | Out-Null
$fixtureDll = Join-Path $fixtureOut 'NativeDispatchFixture.dll'
$fixtureArguments = @('/nologo', '/std:c++17', '/EHsc', '/W4', '/MT', '/LD', '/utf-8', '/DUNICODE', '/D_UNICODE',
    ("/I" + (Join-Path $fixtureVc 'include')),
    ("/I" + (Join-Path $fixtureKit "Include\$fixtureSdk\shared")),
    ("/I" + (Join-Path $fixtureKit "Include\$fixtureSdk\um")),
    ("/I" + (Join-Path $fixtureKit "Include\$fixtureSdk\ucrt")),
    ("/Fo" + (Join-Path $fixtureOut 'NativeDispatchFixture.obj')),
    ("/Fe" + $fixtureDll), (Join-Path $PSScriptRoot 'NativeDispatchFixture.cpp'),
    '/link', ("/MACHINE:" + $Architecture.ToUpperInvariant()),
    ("/IMPLIB:" + (Join-Path $fixtureOut 'NativeDispatchFixture.lib')),
    ("/LIBPATH:" + (Join-Path $fixtureVc "lib\$Architecture")),
    ("/LIBPATH:" + (Join-Path $fixtureKit "Lib\$fixtureSdk\um\$Architecture")),
    ("/LIBPATH:" + (Join-Path $fixtureKit "Lib\$fixtureSdk\ucrt\$Architecture")),
    'OleAut32.lib', 'Ole32.lib', 'Uuid.lib', 'Kernel32.lib')

Write-Output "Fixture compiler: $fixtureCl"
Write-Output "Fixture toolset=$fixtureToolset, SDK=$fixtureSdk, target=$Architecture"
$fixturePreviousPath = $env:Path
try {
    $env:Path = (Split-Path -Parent $fixtureCl) + ';' + $fixturePreviousPath
    & $fixtureCl @fixtureArguments
    if ($LASTEXITCODE -ne 0) { throw "Native dispatch fixture build failed: $LASTEXITCODE" }
}
finally { $env:Path = $fixturePreviousPath }
Write-Output "Fixture DLL: $fixtureDll"
