$ErrorActionPreference = 'Stop'
# 仅使用 Win7 PowerShell 2.0 API，三阶段共用一次采集目录。
$appRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$collector = Join-Path $appRoot 'Collect-Win7.ps1'
$exePath = Join-Path $appRoot 'Inkeys.exe'
$psExe = Join-Path $PSHOME 'powershell.exe'
$stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')
$captureRoot = Join-Path (Join-Path $appRoot 'Win7-Diagnostics') $stamp
if (Test-Path -LiteralPath $captureRoot) { throw 'Capture directory already exists; use a new run.' }
[void][IO.Directory]::CreateDirectory($captureRoot)
$summaryPath = Join-Path $captureRoot 'summary.txt'
$observationsPath = Join-Path $captureRoot 'observations.txt'
$summary = @('format=win7-guarded-1', ('started=' + [DateTime]::Now.ToString('o')),
    ('app-root=' + $appRoot), ('powershell=' + $PSVersionTable.PSVersion.ToString()),
    'probe-timeout-ms=900000', 'expected-stages=01-pixel-probe,02-original-shader,03-guarded-shader',
    'expected-probe-drivers=warp,hardware', 'expected-probe-schema=guarded-20261008',
    'expected-cases-per-driver=105', 'matrix-cases-per-driver=69', 'guarded-cases-per-driver=18',
    'win7-diagnostics=1', 'status=in-progress')
[IO.File]::WriteAllLines($summaryPath, [string[]]$summary, [Text.Encoding]::UTF8)
[IO.File]::WriteAllText($observationsPath, "以下内容为用户观察，不代表自动像素验收。`r`n", [Text.Encoding]::UTF8)
$previousDiagnostics = [Environment]::GetEnvironmentVariable('INKEYS_DRAW3_WIN7_DIAGNOSTICS', 'Process')
$previousFullUpdate = [Environment]::GetEnvironmentVariable('INKEYS_DRAW3_ULW_FULL_UPDATE', 'Process')
$previousGuardedShader = [Environment]::GetEnvironmentVariable('INKEYS_DRAW3_WIN7_GUARDED_SHADER', 'Process')
$anyFailure = $false
$interrupted = $false
$anyIncomplete = $false
$anyUnavailable = $false
$finishedStages = 0
try {
    if (-not (Test-Path -LiteralPath $collector)) { throw 'Collect-Win7.ps1 is missing.' }
    if (-not (Test-Path -LiteralPath $exePath)) { throw 'Inkeys.exe is missing.' }
    if (-not (Test-Path -LiteralPath $psExe)) { throw 'Windows PowerShell executable is missing.' }
    $hashProvider = [Security.Cryptography.SHA256]::Create()
    $exeStream = [IO.File]::OpenRead($exePath)
    try { $exeHash = [BitConverter]::ToString($hashProvider.ComputeHash($exeStream)).Replace('-', '') }
    finally { $exeStream.Dispose(); $hashProvider.Clear() }
    [IO.File]::AppendAllText($summaryPath, ('exe-sha256=' + $exeHash + "`r`n"), [Text.Encoding]::UTF8)
    # manifest 是身份记录；直接 Collect 仍可独立使用，不引入 PS3 JSON API。
    $manifestPath = Join-Path $appRoot 'diagnostic-manifest.json'
    if (Test-Path -LiteralPath $manifestPath) {
        $manifestText = [IO.File]::ReadAllText($manifestPath)
        $manifestExeHash = [regex]::Match($manifestText, '"exe_sha256"\s*:\s*"([A-Fa-f0-9]{64})"').Groups[1].Value
        $manifestSchema = [regex]::Match($manifestText, '"probe_schema"\s*:\s*"([^"]*)"').Groups[1].Value
        $manifestPackage = [regex]::Match($manifestText, '"package_id"\s*:\s*"([^"]*)"').Groups[1].Value
        $manifestCases = [regex]::Match($manifestText, '"expected_cases_per_driver"\s*:\s*(\d+)(?=\s*[,}])').Groups[1].Value
        $manifestMatrixCases = [regex]::Match($manifestText, '"matrix_cases_per_driver"\s*:\s*(\d+)(?=\s*[,}])').Groups[1].Value
        $manifestGuardedCases = [regex]::Match($manifestText, '"guarded_cases_per_driver"\s*:\s*(\d+)(?=\s*[,}])').Groups[1].Value
        [IO.File]::AppendAllText($summaryPath, ('package-id=' + $manifestPackage + "`r`n" +
            'package-probe-schema=' + $manifestSchema + "`r`n"), [Text.Encoding]::UTF8)
        if (-not $manifestExeHash -or $manifestExeHash -ne $exeHash -or
            $manifestSchema -ne 'guarded-20261008' -or
            $manifestPackage -ne 'Inkeys-Win7-WARP-Fix-x64-Release' -or
            $manifestCases -ne '105' -or $manifestMatrixCases -ne '69' -or $manifestGuardedCases -ne '18') {
            throw 'Package manifest identity does not match this EXE/schema. Extract the complete new package.'
        }
    } else {
        $anyIncomplete = $true
        [IO.File]::AppendAllText($summaryPath, "package-manifest=missing`r`n", [Text.Encoding]::UTF8)
    }
    foreach ($identityFile in @($manifestPath, $collector, $MyInvocation.MyCommand.Path,
        (Join-Path $appRoot 'Run-Win7-Diagnostics.cmd'))) {
        if (Test-Path -LiteralPath $identityFile) {
            $identityHashProvider = [Security.Cryptography.SHA256]::Create()
            $identityStream = [IO.File]::OpenRead($identityFile)
            try { $identityHash = [BitConverter]::ToString($identityHashProvider.ComputeHash($identityStream)).Replace('-', '') }
            finally { $identityStream.Dispose(); $identityHashProvider.Clear() }
            [IO.File]::AppendAllText($summaryPath, ('file-sha256-' + [IO.Path]::GetFileName($identityFile) +
                '=' + $identityHash + "`r`n"), [Text.Encoding]::UTF8)
        }
    }
    Write-Host '请先正常退出已经运行的 Inkeys。此流程先自检，再启动两轮应用。'
    Write-Host '第一阶段自动完成 WARP 和硬件矩阵：每个后端最多 15 分钟，最坏共 30 分钟；没有人为等待。'
    Write-Host '旧 Win7 上原版自检 FAIL 是已知对照，18 项 guarded 候选结果会独立记录；FAIL 仍继续两轮应用。'
    Write-Host '硬件不可用会明确记录 unavailable，不当成通过；请勿提前关闭采集窗口。'
    Write-Host ('最终请回传这个完整根目录（包含三个阶段），不要只复制其中的日志：' + $captureRoot)
    Write-Host '两轮只切换完整 shader 的控制结构：保留薄线、抗锯齿、光影和全部工具公式，不删除功能。'
    Write-Host '每轮新建空白测试页，先普通笔约 5 秒、细笔和粗细变化、荧光笔，再等待 3 秒。'
    Write-Host '再测试橡皮、激光、撤回/重做、选择模式穿透和定格后返回书写；最后正常退出并输入观察。'
    $env:INKEYS_DRAW3_WIN7_DIAGNOSTICS = '1'
    foreach ($stageName in @('01-pixel-probe', '02-original-shader', '03-guarded-shader')) {
        $stageRoot = Join-Path $captureRoot $stageName
        $stageStarted = [DateTime]::Now.ToString('o')
        $stageFull = ''
        $stageGuarded = '0'
        if ($stageName -eq '03-guarded-shader') { $stageGuarded = '1' }
        # 正式 Win7 WARP 已自动选兼容 shader；显式 0 保留原版对照，三阶段均清除 full=1。
        [Environment]::SetEnvironmentVariable('INKEYS_DRAW3_ULW_FULL_UPDATE', $stageFull, 'Process')
        [Environment]::SetEnvironmentVariable('INKEYS_DRAW3_WIN7_GUARDED_SHADER', $stageGuarded, 'Process')
        [IO.File]::AppendAllText($summaryPath, ($stageName + '-started=' + $stageStarted +
            "`r`n" + $stageName + '-path=' + $stageRoot + "`r`n" +
            $stageName + '-ulw-full-update=' + $stageFull + "`r`n" +
            $stageName + '-win7-guarded-shader=' + $stageGuarded + "`r`n"), [Text.Encoding]::UTF8)
        Write-Host ''
        Write-Host ('当前阶段：' + $stageName)
        if ($stageName -ne '01-pixel-probe') {
            Write-Host '请新建空白测试页：普通笔、细笔/变粗细、荧光笔；等 3 秒，再橡皮/激光、撤回重做、选择穿透和定格返回。'
        }
        $stageExit = $null
        $stageComplete = $false
        try {
            # 保留子采集器的启动/异常输出；应用的原始双流仍单独保存在阶段目录。
            $collectorStdout = Join-Path $captureRoot ($stageName + '.collector.stdout.txt')
            $collectorStderr = Join-Path $captureRoot ($stageName + '.collector.stderr.txt')
            $ErrorActionPreference = 'Continue'
            try {
                if ($stageName -eq '01-pixel-probe') {
                    & $psExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $collector -ProbeOnly -CaptureRoot $stageRoot > $collectorStdout 2> $collectorStderr
                } else {
                    & $psExe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $collector -SkipProbe -CaptureRoot $stageRoot > $collectorStdout 2> $collectorStderr
                }
                $stageExit = $LASTEXITCODE
            } finally { $ErrorActionPreference = 'Stop' }
            $identityPath = Join-Path $stageRoot 'identity.txt'
            if (Test-Path -LiteralPath $identityPath) {
                $identityLines = [IO.File]::ReadAllLines($identityPath)
                $identity = @{}
                foreach ($identityLine in $identityLines) {
                    $separator = $identityLine.IndexOf('=')
                    if ($separator -gt 0) {
                        $identity[$identityLine.Substring(0, $separator)] = $identityLine.Substring($separator + 1)
                    }
                }
                $stageComplete = $identity['capture-schema'] -eq 'win7-guarded-1' -and $identity['sha256'] -eq $exeHash -and
                    $identity['ulw-full-update'] -eq $stageFull -and $identity['win7-diagnostics'] -eq '1' -and
                    $identity['win7-guarded-shader'] -eq $stageGuarded -and
                    $identity['probe-expected-cases'] -eq '105' -and $identity['probe-matrix-expected'] -eq '69' -and
                    $identity['probe-guarded-expected'] -eq '18'
                if ($stageName -eq '01-pixel-probe') {
                    foreach ($probeDriver in @('warp', 'hardware')) {
                        $prefix = 'pixel-test'
                        if ($probeDriver -eq 'hardware') { $prefix = 'pixel-test-hardware' }
                        foreach ($probeKey in @('requested-driver', 'driver-line', 'probe-schema', 'identity-status', 'exit', 'status', 'timeout', 'error', 'not-run-schema-mismatch',
                            'baseline-production-line', 'baseline-diagnostic-line', 'baseline-sequence-line', 'baseline-matrix-line',
                            'guarded-status', 'guarded-terminal-count', 'guarded-terminal-failures',
                            'guarded-reference-unavailable', 'guarded-summary-line')) {
                            $identityKey = $prefix + '-' + $probeKey
                            if ($identity.ContainsKey($identityKey)) {
                                [IO.File]::AppendAllText($summaryPath, ($stageName + '-' + $identityKey + '=' +
                                    $identity[$identityKey] + "`r`n"), [Text.Encoding]::UTF8)
                            }
                        }
                        if (-not $identity.ContainsKey($prefix + '-exit') -or
                            $identity[$prefix + '-requested-driver'] -ne $probeDriver -or
                            $identity[$prefix + '-probe-schema'] -ne 'guarded-20261008' -or
                            $identity[$prefix + '-identity-status'] -ne 'matched' -or
                            $identity[$prefix + '-driver-line'] -ne ('[Draw3PixelTest] driver=' + $probeDriver + ' requested=' + $probeDriver)) {
                            $stageComplete = $false
                        }
                        if ($probeDriver -eq 'hardware' -and $identity[$prefix + '-exit'] -eq '3' -and
                            $identity[$prefix + '-status'] -eq 'unavailable' -and
                            $identity[$prefix + '-guarded-status'] -eq 'unavailable') { $anyUnavailable = $true }
                        elseif ($identity[$prefix + '-guarded-status'] -eq 'incomplete' -or
                            -not $identity.ContainsKey($prefix + '-guarded-status') -or
                            $identity[$prefix + '-guarded-terminal-count'] -ne '18') { $stageComplete = $false }
                        elseif ($identity[$prefix + '-guarded-status'] -ne 'PASS') { $anyFailure = $true }
                        foreach ($requiredOutput in @('stdout', 'stderr')) {
                            if (-not (Test-Path -LiteralPath (Join-Path $stageRoot ($prefix + '.' + $requiredOutput + '.txt')))) {
                                $stageComplete = $false
                            }
                        }
                    }
                } else {
                    if (-not $identity.ContainsKey('exit') -or $identity['pixel-test-skipped'] -ne '1' -or
                        $identity['pixel-test-hardware-skipped'] -ne '1' -or
                        $identity['ulw-full-update'] -ne $stageFull -or $identity['win7-diagnostics'] -ne '1' -or
                        $identity['win7-guarded-shader'] -ne $stageGuarded) {
                        $stageComplete = $false
                    }
                    foreach ($requiredOutput in @('stdout', 'stderr')) {
                        if (-not (Test-Path -LiteralPath (Join-Path $stageRoot ('console.' + $requiredOutput + '.txt')))) {
                            $stageComplete = $false
                        }
                    }
                }
            }
        } catch {
            $anyFailure = $true
            [IO.File]::AppendAllText($summaryPath, ($stageName + '-error=' + $_.Exception.Message + "`r`n"), [Text.Encoding]::UTF8)
        }
        if ($null -eq $stageExit -or $stageExit -ne 0 -or -not $stageComplete) { $anyFailure = $true }
        if (-not $stageComplete) { $anyIncomplete = $true }
        $stageStatus = 'incomplete'
        if ($stageComplete) {
            $stageStatus = 'captured'
            if ($stageExit -ne 0) { $stageStatus = 'captured-with-failure' }
        }
        [IO.File]::AppendAllText($summaryPath, ($stageName + '-collector-exit=' + $stageExit +
            "`r`n" + $stageName + '-status=' + $stageStatus + "`r`n" + $stageName +
            '-ended=' + [DateTime]::Now.ToString('o') + "`r`n"), [Text.Encoding]::UTF8)
        if ($stageName -ne '01-pixel-probe') {
            $observation = Read-Host '请输入本轮观察（出墨、细笔/变宽/荧光笔效果、橡皮激光、撤回重做、穿透及定格返回；未测项请说明）'
            if ([string]::IsNullOrEmpty($observation)) { $observation = '[未提供观察]' }
            [IO.File]::AppendAllText($observationsPath, ($stageName + '=' + $observation + "`r`n"), [Text.Encoding]::UTF8)
        }
        # GPU 自检失败是待调查证据，仍继续两轮正常应用采集。
        $finishedStages++
    }
} catch {
    $anyFailure = $true
    $interrupted = $true
    [IO.File]::AppendAllText($summaryPath, ('workflow-error=' + $_.Exception.Message + "`r`n"), [Text.Encoding]::UTF8)
    Write-Host ('采集未完成：' + $_.Exception.Message)
} finally {
    # 恢复调用者三个环境原值，避免对照覆盖值影响后续启动。
    [Environment]::SetEnvironmentVariable('INKEYS_DRAW3_WIN7_DIAGNOSTICS', $previousDiagnostics, 'Process')
    [Environment]::SetEnvironmentVariable('INKEYS_DRAW3_ULW_FULL_UPDATE', $previousFullUpdate, 'Process')
    [Environment]::SetEnvironmentVariable('INKEYS_DRAW3_WIN7_GUARDED_SHADER', $previousGuardedShader, 'Process')
    $status = 'completed'
    if ($anyUnavailable) { $status = 'completed-with-unavailable' }
    if ($anyFailure) { $status = 'completed-with-failures' }
    if ($interrupted -or $anyIncomplete -or $finishedStages -ne 3) {
        $status = 'incomplete'
        $anyFailure = $true
    }
    [IO.File]::AppendAllText($summaryPath, ('status=' + $status + "`r`nended=" +
        [DateTime]::Now.ToString('o') + "`r`n"), [Text.Encoding]::UTF8)
}
Write-Host ''
Write-Host ('请回传整个目录：' + $captureRoot)
Write-Host '请回传整个根目录及三个阶段，包含 WARP/硬件四份自检输出和两轮观察。'
Write-Host '原版 FAIL 与候选结果分别记录；采集 completed、退出码 0 均不代表 Win7 画布或效果通过。'
if ($anyFailure) { exit 1 }
exit 0
