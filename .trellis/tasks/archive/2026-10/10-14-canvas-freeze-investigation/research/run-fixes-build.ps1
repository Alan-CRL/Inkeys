param(
    [string]$ProviderDirectory = '',
    [string]$LogName = 'solution-fixes',
    [switch]$StorageTests
)
$ErrorActionPreference = 'Stop'
$canvasRepo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$canvasOutput = Join-Path $canvasRepo 'Build/CanvasFreezeInvestigation/fixes'
New-Item -ItemType Directory -Path $canvasOutput -Force | Out-Null
if ($LogName -notmatch '^[a-zA-Z0-9-]+$') { throw 'LogName must be a simple task log name.' }
$canvasVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$canvasVs = & $canvasVswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if (-not $canvasVs) { throw 'Visual Studio with MSBuild was not found.' }
$canvasMsbuild = Join-Path $canvasVs 'MSBuild/Current/Bin/arm64/MSBuild.exe'
if (-not (Test-Path -LiteralPath $canvasMsbuild)) { throw 'ARM64 native MSBuild was not found.' }
if (-not $ProviderDirectory) {
    $ProviderDirectory = Join-Path $canvasRepo 'VcpkgInstalled/Arm64'
}
$canvasProvider = [IO.Path]::GetFullPath($ProviderDirectory).TrimEnd('\') + '\'
$canvasTriplet = Join-Path $canvasProvider 'arm64-windows-static-v143'
foreach ($canvasRequired in @('include/absl/base/config.h', 'lib/absl_base.lib')) {
    if (-not (Test-Path -LiteralPath (Join-Path $canvasTriplet $canvasRequired))) {
        throw ('Incomplete local provider: ' + $canvasRequired)
    }
}
$canvasLog = Join-Path $canvasOutput ($LogName + '.log')
$canvasStarted = [DateTime]::UtcNow
$canvasSolutionName = if ($StorageTests) { 'inkStrokeModelerTest.sln' } else { 'InkeysRepo.sln' }
$canvasArgs = @(
    (Join-Path $canvasRepo $canvasSolutionName), '/m:1', '/nr:false', '/nologo', '/v:minimal',
    '/p:Configuration=Debug', '/p:Platform=ARM64',
    '/p:BuildInParallel=false', '/p:CL_MPCount=1',
    ('/p:VcpkgInstalledDir=' + $canvasProvider), '/p:VcpkgManifestInstall=false'
)
if ($StorageTests) { $canvasArgs += '/t:inkStrokeModelerTestTests' }
# Codex 的 Path/PATH 重复键只在本 invocation 内处理，不改全局环境或工程。
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
# 本机默认共享 PDB 服务的 /FS 对照失败；为本 invocation 使用独立端点，保留调试信息与原编译参数。
$canvasPdbEndpoint = 'canvas-freeze-fixes-' + $PID
$env:_MSPDBSRV_ENDPOINT_ = $canvasPdbEndpoint
& $canvasMsbuild @canvasArgs *> $canvasLog
$canvasExit = $LASTEXITCODE
Set-Content -LiteralPath (Join-Path $canvasOutput ($LogName + '.exitcode')) -Value $canvasExit
@{
    msbuild = $canvasMsbuild; provider = $canvasProvider; pdbEndpoint = $canvasPdbEndpoint
    solution = $canvasSolutionName; storageTestsOnly = [bool]$StorageTests
    configuration = 'Debug'; platform = 'ARM64'
    startedUtc = $canvasStarted.ToString('o'); completedUtc = [DateTime]::UtcNow.ToString('o')
    exitCode = $canvasExit; log = $canvasLog
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $canvasOutput ($LogName + '.json')) -Encoding utf8
Write-Output ('MSBuild exit=' + $canvasExit + ' log=' + $canvasLog)
exit $canvasExit
