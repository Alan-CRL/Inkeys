param(
    [Parameter(Mandatory = $true)][string]$ClPath,
    [Parameter(Mandatory = $true)][string]$ProviderRoot,
    [string]$PythonPath = 'python',
    [string]$CacheDirectory = '',
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
# 归档层级会变化，以 solution 标记定位仓库。
$taskProbeRepo = $PSScriptRoot
while ($taskProbeRepo -and -not (Test-Path -LiteralPath (Join-Path $taskProbeRepo 'InkeysRepo.sln') -PathType Leaf)) {
    $taskProbeRepo = Split-Path -Parent $taskProbeRepo
}
if (-not $taskProbeRepo) { throw 'Repository root (InkeysRepo.sln) was not found.' }
if (-not $CacheDirectory) { $CacheDirectory = Join-Path $taskProbeRepo 'Inkeys/Cache/ARM64/Debug' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskProbeRepo 'Build/CanvasFreezeInvestigation/presentation-load-probe' }
$taskProbeOut = [IO.Path]::GetFullPath($OutputDirectory)
$taskProbeAllowedOut = [IO.Path]::GetFullPath((Join-Path $taskProbeRepo 'Build/CanvasFreezeInvestigation/presentation-load-probe')).TrimEnd('\') + '\'
if (-not ($taskProbeOut + '\').StartsWith($taskProbeAllowedOut, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputDirectory must stay within Build/CanvasFreezeInvestigation/presentation-load-probe.'
}
New-Item -ItemType Directory -Path $taskProbeOut -Force | Out-Null

# 生成器只摘取当前源码，不编译、不改工程文件。
& $PythonPath (Join-Path $PSScriptRoot 'make-presentation-load-probe.py')
if ($LASTEXITCODE -ne 0) { throw 'Original-source fragment generation failed.' }

$taskProbeReferences = [ordered]@{
    'Inkeys.Drawing.Draw3.presentation_auto_save' = 'Draw3.PresentationAutoSave.cppm.ifc'
    'draw3.uink_model' = 'uink_model.cppm.ifc'
    'draw3.uink_codec' = 'uink_codec.cppm.ifc'
    'draw3.uink_file' = 'uink_file.cppm.ifc'
    'draw3.uink_draw3_export' = 'uink_draw3_export.cppm.ifc'
    'draw3.uink_draw3_import' = 'uink_draw3_import.cppm.ifc'
}
$taskProbeObjectNames = @(
    'Draw3.PresentationAutoSave.obj', 'Draw3.PresentationAutoSave.cppm.obj',
    'Draw3.Presentation.obj', 'Draw3.Bridge.obj',
    'uink_model.obj', 'uink_model.cppm.obj',
    'uink_codec.obj', 'uink_codec.cppm.obj', 'uink_codec_encode.obj',
    'uink_file.obj', 'uink_file.cppm.obj',
    'uink_draw3_export.obj', 'uink_draw3_export.cppm.obj',
    'uink_draw3_import.obj', 'uink_draw3_import.cppm.obj'
)
$taskProbeArguments = @(
    '/nologo', '/std:c++20', '/permissive-', '/EHsc', '/MT', '/Od', '/fp:precise', '/utf-8',
    '/DWIN32', '/DNDEBUG', '/D_CONSOLE', '/D_UNICODE', '/DUNICODE',
    '/D_CRT_SECURE_NO_WARNINGS', '/DIMGUI_DISABLE_SSE',
    '/D_ARM64_WINAPI_PARTITION_DESKTOP_SDK_AVAILABLE=1',
    ('/I' + (Join-Path $taskProbeRepo 'Inkeys/Inkeys/Drawing/Draw3')),
    ('/I' + (Join-Path $ProviderRoot 'include')),
    '/ifcSearchDir', $CacheDirectory
)
foreach ($taskProbeReference in $taskProbeReferences.GetEnumerator()) {
    $taskProbeIfc = Join-Path $CacheDirectory $taskProbeReference.Value
    if (-not (Test-Path -LiteralPath $taskProbeIfc)) { throw "Missing product IFC: $taskProbeIfc" }
    $taskProbeArguments += @('/reference', ($taskProbeReference.Key + '=' + $taskProbeIfc))
}
foreach ($taskProbeObjectName in $taskProbeObjectNames) {
    $taskProbeObject = Join-Path $CacheDirectory $taskProbeObjectName
    if (-not (Test-Path -LiteralPath $taskProbeObject)) { throw "Missing product object: $taskProbeObject" }
    $taskProbeArguments += $taskProbeObject
}
$taskProbeArguments += @(
    (Join-Path $PSScriptRoot 'presentation-load-sharing-probe.cpp'),
    ('/Fo' + (Join-Path $taskProbeOut 'presentation-load-sharing-probe.obj')),
    ('/Fe' + (Join-Path $taskProbeOut 'presentation-load-sharing-probe.exe')),
    ('/Fd' + (Join-Path $taskProbeOut 'presentation-load-sharing-probe.pdb')),
    '/link', '/SUBSYSTEM:CONSOLE', '/MACHINE:ARM64',
    ('/LIBPATH:' + (Join-Path $ProviderRoot 'lib')),
    'jsoncpp.lib', 'bcrypt.lib', 'normaliz.lib', 'ole32.lib', 'user32.lib'
)

# 复用已完成产品编译的 IFC/obj，要求同一 MSVC 编译器与 /MT、NDEBUG ABI。
# 此脚本应由已初始化 ARM64 C++ 环境的主会话调用；不修改 PATH 或工具链配置。
& $ClPath @taskProbeArguments 2>&1 | Tee-Object -FilePath (Join-Path $taskProbeOut 'build.log')
$taskProbeBuildCode = $LASTEXITCODE
Set-Content -LiteralPath (Join-Path $taskProbeOut 'build.exitcode') -Value $taskProbeBuildCode
if ($taskProbeBuildCode -ne 0) { exit $taskProbeBuildCode }
& (Join-Path $taskProbeOut 'presentation-load-sharing-probe.exe') $taskProbeOut 2>&1 |
    Tee-Object -FilePath (Join-Path $taskProbeOut 'run.log')
$taskProbeRunCode = $LASTEXITCODE
Set-Content -LiteralPath (Join-Path $taskProbeOut 'run.exitcode') -Value $taskProbeRunCode
exit $taskProbeRunCode
