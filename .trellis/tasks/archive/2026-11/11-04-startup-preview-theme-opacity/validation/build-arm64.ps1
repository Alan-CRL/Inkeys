param([string]$Label = 'build-arm64', [string]$OutputDirectory = '')
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../../../..')).Path
if (!(Test-Path -LiteralPath (Join-Path $repo 'InkeysRepo.sln'))) { throw 'Expected solution is missing.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -property installationPath
$msbuild = Join-Path $installation 'MSBuild/Current/Bin/arm64/MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild)) { throw 'ARM64 native MSBuild is missing.' }

# Codex 的 Path/PATH 重复键仅在本次构建进程环境内归一。
$buildSearchPath = $env:Path
$powerShellDirectory = Split-Path -Parent (Get-Command pwsh.exe -ErrorAction Stop).Source
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $powerShellDirectory + ';' + $buildSearchPath
$env:MSBUILDDISABLENODEREUSE = '1'
$started = [DateTimeOffset]::UtcNow
$arguments = @('InkeysRepo.sln', '/t:Build', '/m:1', '/p:Configuration=Debug', '/p:Platform=ARM64', '/nr:false', '/nologo', '/v:minimal')
if ($OutputDirectory) {
    $outputPath = [System.IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory)).TrimEnd('\', '/')
    if (!$outputPath.StartsWith($repo + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) { throw 'Validation output must stay in the workspace.' }
    [System.IO.Directory]::CreateDirectory($outputPath) | Out-Null
    $arguments += "/p:OutDir=$outputPath\"
}
# 直接继承当前无窗口命令会话，避免 Start-Process 重定向等待链无输出。
# 调用方监控会话并提供15分钟预算；无需另建构建系统或改变项目配置。
Write-Output 'Starting direct ARM64 solution build; supervised budget 15 minutes.'
Push-Location -LiteralPath $repo
try {
    & $msbuild @arguments 2>&1 | Tee-Object -FilePath (Join-Path $PSScriptRoot "$Label.stdout.log")
    $exitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
$result = [ordered]@{ command = $msbuild; arguments = $arguments; startedUtc = $started; endedUtc = [DateTimeOffset]::UtcNow; exitCode = $exitCode; timedOut = $false }
$result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $PSScriptRoot "$Label.result.json") -Encoding utf8
Write-Output "Build exit code: $exitCode"
exit $exitCode
