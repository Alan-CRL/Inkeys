$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath 'inkStrokeModelerTest.sln')) { throw 'Run from repository root.' }
$installation = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -prerelease -products '*' -property installationPath
$msbuild = Join-Path $installation 'MSBuild\Current\Bin\arm64\MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild)) { throw 'Native ARM64 MSBuild is missing.' }
$buildSearchPath = $env:Path
$powerShellDirectory = Split-Path -Parent (Get-Command pwsh.exe -ErrorAction Stop).Source
# 与既有构建保持一致，仅在本次调用内规范化环境。
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:Path = $powerShellDirectory + ';' + $buildSearchPath
$env:MSBUILDDISABLENODEREUSE = '1'
$started = [DateTimeOffset]::UtcNow.ToString('o')
& $msbuild inkStrokeModelerTest.sln /t:Build /m:1 /p:Configuration=Debug /p:Platform=ARM64 /nr:false /nologo /v:minimal 2>&1 |
    Tee-Object -FilePath (Join-Path $PSScriptRoot 'build-shared.stdout.log')
$buildExit = $LASTEXITCODE
[ordered]@{
    exitCode=$buildExit; startedUtc=$started; endedUtc=[DateTimeOffset]::UtcNow.ToString('o');
    budgetSeconds=1200; msbuild=$msbuild; solution='inkStrokeModelerTest.sln'; configuration='Debug|ARM64';
    scope='Test-only diagnostics; production code and project configurations unchanged.'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $PSScriptRoot 'build-shared.result.json') -Encoding utf8
exit $buildExit
