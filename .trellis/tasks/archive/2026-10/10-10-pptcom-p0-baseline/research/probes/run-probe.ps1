[CmdletBinding()]
param(
    [ValidateRange(1, 20)]
    [int]$Repeat = 1,
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9-]{0,63}$')]
    [string]$RunLabel = 'probe'
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot
while (!(Test-Path -LiteralPath (Join-Path $repoRoot 'InkeysRepo.sln') -PathType Leaf) -or
    !(Test-Path -LiteralPath (Join-Path $repoRoot '.trellis/workflow.md') -PathType Leaf)) {
    $parent = Split-Path -Parent $repoRoot
    if (!$parent -or $parent -eq $repoRoot) { throw '无法从当前探针位置找到本仓库；没有写入外部输出目录。' }
    $repoRoot = $parent
}
$outputRoot = Join-Path $repoRoot ('TestResults/pptcom-p0/' + $RunLabel)
$referenceRoot = Join-Path ${env:ProgramFiles(x86)} 'Reference Assemblies/Microsoft/Framework/.NETFramework/v4.0'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vsRoot) { throw '现有 Visual Studio C# 工具链不可用；没有安装或降级依赖。' }
$compiler = Join-Path $vsRoot 'MSBuild/Current/Bin/Roslyn/csc.exe'
$references = @('mscorlib.dll', 'System.dll', 'System.Core.dll') | ForEach-Object { Join-Path $referenceRoot $_ }
foreach ($required in (@($compiler) + $references)) {
    if (!(Test-Path -LiteralPath $required -PathType Leaf)) { throw "缺少真实 .NET4 引用程序集或现有编译器：$required" }
}

# 输出只在已有忽略目录；不复制/覆盖产品 DLL/TLB，不增加工程或包。
[IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$utf8 = New-Object Text.UTF8Encoding($false)
$exe = Join-Path $outputRoot 'NamedPipeP0Probe.exe'
$source = Join-Path $PSScriptRoot 'NamedPipeP0Probe.cs'
$arguments = @('/nologo', '/noconfig', '/nostdlib+', '/langversion:4', '/codepage:65001', '/target:exe', '/platform:anycpu', "/out:$exe")
$arguments += $references | ForEach-Object { "/reference:$_" }
$arguments += $source
$compileLines = @(& $compiler $arguments 2>&1 | ForEach-Object { "$_" })
$compileExit = $LASTEXITCODE
[IO.File]::WriteAllLines((Join-Path $outputRoot 'compile.log'), $compileLines, $utf8)
Write-Output "COMPILER $compiler"
Write-Output "REFERENCE_ROOT $referenceRoot"
Write-Output "COMPILE_EXIT=$compileExit"
if ($compileExit -ne 0) { $compileLines | Write-Output; exit $compileExit }

$installedNet4 = Get-ItemProperty -LiteralPath 'HKLM:/SOFTWARE/Microsoft/NET Framework Setup/NDP/v4/Full' -ErrorAction SilentlyContinue
$osVersion = Get-ItemProperty -LiteralPath 'HKLM:/SOFTWARE/Microsoft/Windows NT/CurrentVersion'
$environmentLines = @(
    "RecordedAt=$([DateTime]::UtcNow.ToString('o'))"
    "OSRegistryProductName=$($osVersion.ProductName);Build=$($osVersion.CurrentBuildNumber).$($osVersion.UBR);DisplayVersion=$($osVersion.DisplayVersion)"
    "HostArchitecture=$env:PROCESSOR_ARCHITECTURE"
    "InstalledNet4Version=$($installedNet4.Version);Release=$($installedNet4.Release)"
    'CompileTarget=.NETFramework,Version=v4.0;NO runtime proof of original .NET4 or Win7'
    "Compiler=$compiler"
    "SourceSHA256=$((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash)"
)
foreach ($reference in $references) { $environmentLines += "ReferenceSHA256=$reference|$((Get-FileHash -LiteralPath $reference -Algorithm SHA256).Hash)" }
[IO.File]::WriteAllLines((Join-Path $outputRoot 'environment.log'), $environmentLines, $utf8)
$environmentLines | Write-Output

for ($round = 1; $round -le $Repeat; $round++) {
    $stdout = Join-Path $outputRoot "run-$round.stdout.log"
    $stderr = Join-Path $outputRoot "run-$round.stderr.log"
    # 后台创建且无窗口，重定向日志。进程级超时只终止本脚本自己启动的探针。
    $process = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    if (!$process.WaitForExit(120000)) {
        $process.Kill()
        if (!$process.WaitForExit(3000)) { throw "探针终止后 3000ms 内未退出，round=$round" }
        $process.Dispose()
        throw "探针总时限 120000ms 到期，round=$round"
    }
    $process.Refresh()
    $runExit = $process.ExitCode
    $process.Dispose()
    Write-Output "RUN round=$round exit=$runExit"
    Get-Content -LiteralPath $stdout | Write-Output
    Get-Content -LiteralPath $stderr | Write-Output
    if ($runExit -ne 0) { exit $runExit }
}
exit 0
