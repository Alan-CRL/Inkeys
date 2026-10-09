param([switch]$ProbeOnly, [switch]$SkipProbe, [string]$CaptureRoot)

$ErrorActionPreference = 'Stop'
# Use APIs available in Windows 7's PowerShell 2.0.
$appRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Join-Path $appRoot 'Inkeys.exe'
if ($ProbeOnly -and $SkipProbe) { throw 'ProbeOnly and SkipProbe cannot be combined.' }
if (-not (Test-Path -LiteralPath $exePath)) { throw 'Place this script beside the diagnostic Inkeys.exe.' }
if (Get-Process -Name Inkeys -ErrorAction SilentlyContinue) { throw 'Exit the existing Inkeys instance before collecting.' }
$stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')
if (-not $CaptureRoot) { $CaptureRoot = Join-Path (Join-Path $appRoot 'Win7-Diagnostics') $stamp }
$captureRoot = [IO.Path]::GetFullPath($CaptureRoot)
if (Test-Path -LiteralPath $captureRoot) { throw 'Capture directory already exists; preserve it and use a new directory.' }
[void][IO.Directory]::CreateDirectory($captureRoot)
$hashProvider = [Security.Cryptography.SHA256]::Create()
$exeStream = [IO.File]::OpenRead($exePath)
try { $exeHash = [BitConverter]::ToString($hashProvider.ComputeHash($exeStream)).Replace('-', '') }
finally { $exeStream.Dispose(); $hashProvider.Clear() }
$started = [DateTime]::Now
$metadata = @(('exe=' + $exePath), ('sha256=' + $exeHash), ('os=' + [Environment]::OSVersion.VersionString),
    ('started=' + $started.ToString('o')), ('powershell=' + $PSVersionTable.PSVersion.ToString()))
$metadata += 'capture-schema=win7-guarded-1'
$metadata += 'probe-expected-drivers=warp,hardware'
$metadata += 'probe-timeout-ms=900000'
$metadata += 'ulw-full-update=' + $env:INKEYS_DRAW3_ULW_FULL_UPDATE
$metadata += 'win7-diagnostics=' + $env:INKEYS_DRAW3_WIN7_DIAGNOSTICS
$metadata += 'win7-guarded-shader=' + $env:INKEYS_DRAW3_WIN7_GUARDED_SHADER
$metadata += 'probe-expected-cases=105'
$metadata += 'probe-matrix-expected=69'
$metadata += 'probe-guarded-expected=18'
# Record the actual system runtime versions without broad/slow WMI discovery.
foreach ($dllName in @('d3d11.dll', 'd3d10warp.dll', 'dxgi.dll', 'user32.dll')) {
    $dllPath = Join-Path ([Environment]::SystemDirectory) $dllName
    try {
        $dllInfo = Get-Item -LiteralPath $dllPath
        $metadata += 'system-dll=' + $dllInfo.FullName + ' file-version=' + $dllInfo.VersionInfo.FileVersion
    } catch { $metadata += 'system-dll=' + $dllPath + ' error=' + $_.Exception.Message }
}
[IO.File]::WriteAllLines((Join-Path $captureRoot 'identity.txt'), [string[]]$metadata, [Text.Encoding]::UTF8)
# The explicit early CLI probe creates no visible window and does not load app config.
# A probe failure/timeout must not prevent collecting the normal application run.
# PS2 puts WindowStyle and redirects in different parameter sets. Reuse this console;
# this exact early CLI creates only a never-shown fixture HWND, not the app UI.
$probeIdentity = Join-Path $captureRoot 'identity.txt'
$probeFailure = $false
$warpSchemaMatched = $false
$warpIdentityValid = $false
$hardwareIdentityValid = $false
if ($SkipProbe) {
    [IO.File]::AppendAllText($probeIdentity, "pixel-test-skipped=1`r`npixel-test-hardware-skipped=1`r`n", [Text.Encoding]::UTF8)
} else {
    foreach ($probeDriver in @('warp', 'hardware')) {
        $probePrefix = 'pixel-test'
        $probeArgument = '--draw3-renderer-pixel-test'
        if ($probeDriver -eq 'hardware') {
            $probePrefix = 'pixel-test-hardware'
            $probeArgument = '--draw3-renderer-pixel-test-hardware'
        }
        # Never send the new hardware CLI to an older EXE: it could enter normal GUI startup.
        if ($probeDriver -eq 'hardware' -and -not $warpSchemaMatched) {
            [IO.File]::WriteAllText((Join-Path $captureRoot 'pixel-test-hardware.stdout.txt'), '', [Text.Encoding]::UTF8)
            [IO.File]::WriteAllText((Join-Path $captureRoot 'pixel-test-hardware.stderr.txt'),
                'Hardware CLI was not run because the WARP fixture schema was missing.', [Text.Encoding]::UTF8)
            [IO.File]::AppendAllText($probeIdentity,
                "pixel-test-hardware-not-run-schema-mismatch=1`r`npixel-test-hardware-status=skipped-schema-mismatch`r`n", [Text.Encoding]::UTF8)
            $hardwareStatus = 'skipped-schema-mismatch'
            continue
        }
        $probeProcess = $null
        $probeStatus = 'error'
        $probeStarted = [DateTime]::Now.ToString('o')
        $probeStdout = Join-Path $captureRoot ($probePrefix + '.stdout.txt')
        $probeStderr = Join-Path $captureRoot ($probePrefix + '.stderr.txt')
        try {
            Write-Host ('Running isolated ' + $probeDriver + ' matrix (up to 900 seconds)...')
            $probeProcess = Start-Process -FilePath $exePath -WorkingDirectory $appRoot -PassThru -NoNewWindow `
                -ArgumentList $probeArgument -RedirectStandardOutput $probeStdout -RedirectStandardError $probeStderr
            # Keep the owned process handle before it exits; do not invalidate it with Refresh.
            $null = $probeProcess.Handle
            if ($probeProcess.WaitForExit(900000)) {
                $probeProcess.WaitForExit()
                $probeExitCode = $probeProcess.ExitCode
                if ($null -eq $probeExitCode) { throw 'Pixel test exited but its exit code could not be read.' }
                $probeResult = $probePrefix + '-exit=' + $probeExitCode
                $probeStatus = 'exit-zero'
                if ($probeExitCode -ne 0) { $probeStatus = 'exit-nonzero' }
                # Exit 3 is reserved for unavailable hardware, never a GPU PASS.
                if ($probeDriver -eq 'hardware' -and $probeExitCode -eq 3) {
                    $probeStatus = 'unavailable'
                }
            } else {
                try { $probeProcess.Kill(); [void]$probeProcess.WaitForExit(5000) } catch { }
                $probeResult = $probePrefix + '-timeout=900000'
                $probeStatus = 'timeout'
            }
        } catch {
            $probeError = $_.Exception.Message
            if ($probeProcess) {
                try { $probeProcess.Kill(); [void]$probeProcess.WaitForExit(5000) } catch { }
            }
            $probeResult = $probePrefix + '-error=' + $probeError
            Write-Host ('Pixel test could not finish: ' + $probeError)
        } finally {
            if ($probeProcess) { $probeProcess.Dispose() }
        }
        if ($probeDriver -eq 'warp') { $warpStatus = $probeStatus }
        if ($probeDriver -eq 'hardware') { $hardwareStatus = $probeStatus }
        $probeMetadata = $probePrefix + '-requested-driver=' + $probeDriver + "`r`n" +
            $probePrefix + '-started=' + $probeStarted + "`r`n" + $probeResult + "`r`n" +
            $probePrefix + '-status=' + $probeStatus + "`r`n" + $probePrefix +
            '-ended=' + [DateTime]::Now.ToString('o') + "`r`n"
        $probeSchemaMatched = $false
        $probeActualDriverLine = ''
        $probeHeaderCount = 0
        $probeDriverCount = 0
        $guardedSummaryCount = 0
        $guardedCases = @{}
        $guardedReportedCases = -1
        $guardedReportedFailures = -1
        $guardedReferenceUnavailable = -1
        $guardedReportedResult = ''
        $guardedSummaryLine = ''
        $guardedTerminalsValid = $true
        $guardedActualFailures = 0
        $guardedActualReferenceUnavailable = 0
        $probeSchemaDriver = 'WARP'
        if ($probeDriver -eq 'hardware') { $probeSchemaDriver = 'Hardware' }
        # Preserve the literal driver's own header; collector labels are only requested-driver metadata.
        foreach ($probeOutputPath in @($probeStderr, $probeStdout)) {
            if (Test-Path -LiteralPath $probeOutputPath) {
                foreach ($probeLine in [IO.File]::ReadAllLines($probeOutputPath)) {
                    if ($probeLine -eq ('[Draw3PixelTest] probe_schema=guarded-20261008 driver=' +
                        $probeSchemaDriver + ' expected_cases=105 matrix_expected=69 guarded_expected=18')) {
                        $probeMetadata += $probePrefix + '-probe-schema=guarded-20261008' + "`r`n"
                        $probeSchemaMatched = $true
                        $probeHeaderCount++
                    }
                    if ($probeLine -match '^\[Draw3PixelTest\] driver=(warp|hardware) requested=(warp|hardware)$') {
                        $probeActualDriverLine = $probeLine
                        $probeDriverCount++
                        $probeMetadata += $probePrefix + '-driver-line=' + $probeLine + "`r`n"
                    }
                    # Candidate terminals are separate from the unchanged baseline's exit contract.
                    if ($probeLine -match '^\[Draw3PixelTest\] case=guarded\.(backbuffer|offscreen)\.(cursor\.(production|blend_disabled|retain)|ink\.(round|thin|varying_radius|highlighter|erase_retain|draw_retain)) result=(PASS|FAIL|ERROR)(?:\s|$)') {
                        $guardedKey = $matches[1] + '.' + $matches[2]
                        $guardedResult = $matches[5]
                        if ($guardedCases.ContainsKey($guardedKey)) { $guardedTerminalsValid = $false }
                        $guardedCases[$guardedKey] = $guardedResult
                        if ($guardedResult -ne 'PASS') { $guardedActualFailures++ }
                        $guardedReference = ''
                        $guardedComparison = ''
                        if ($probeLine -match '(?:^|\s)reference_status=(available|unavailable)(?:\s|$)') {
                            $guardedReference = $matches[1]
                        }
                        if ($probeLine -match '(?:^|\s)comparison=(PASS|FAIL|unavailable)(?:\s|$)') {
                            $guardedComparison = $matches[1]
                        }
                        if ($guardedReference -eq 'unavailable') {
                            $guardedActualReferenceUnavailable++
                            if ($guardedComparison -ne 'unavailable') { $guardedTerminalsValid = $false }
                        } elseif ($guardedReference -ne 'available' -or
                            ($guardedComparison -ne 'PASS' -and $guardedComparison -ne 'FAIL') -or
                            ($guardedComparison -eq 'FAIL' -and $guardedResult -eq 'PASS')) {
                            $guardedTerminalsValid = $false
                        }
                    }
                    if ($probeLine -match '^\[Draw3PixelTest\] guarded_cases=(\d+) guarded_failures=(\d+) reference_unavailable=(\d+) result=(PASS|FAIL) driver=(warp|hardware)$') {
                        $guardedSummaryCount++
                        $guardedReportedCases = [int]$matches[1]
                        $guardedReportedFailures = [int]$matches[2]
                        $guardedReferenceUnavailable = [int]$matches[3]
                        $guardedReportedResult = $matches[4]
                        if ($matches[5] -ne $probeDriver) { $guardedTerminalsValid = $false }
                        $guardedSummaryLine = $probeLine
                    }
                    foreach ($baselineGroup in @('production', 'diagnostic', 'sequence', 'matrix')) {
                        if ($probeLine -match ('^\[Draw3PixelTest\] ' + $baselineGroup + '_cases=')) {
                            $probeMetadata += $probePrefix + '-baseline-' + $baselineGroup + '-line=' + $probeLine + "`r`n"
                        }
                    }
                }
            }
        }
        $probeIdentityValid = $probeSchemaMatched -and $probeHeaderCount -eq 1 -and $probeDriverCount -eq 1 -and
            $probeActualDriverLine -eq ('[Draw3PixelTest] driver=' + $probeDriver + ' requested=' + $probeDriver)
        $guardedExpectedResult = 'PASS'
        if ($guardedActualFailures -ne 0) { $guardedExpectedResult = 'FAIL' }
        $guardedComplete = $probeIdentityValid -and $guardedTerminalsValid -and $guardedCases.Count -eq 18 -and
            $guardedSummaryCount -eq 1 -and $guardedReportedCases -eq 18 -and
            $guardedReportedFailures -eq $guardedActualFailures -and
            $guardedReferenceUnavailable -eq $guardedActualReferenceUnavailable -and
            $guardedReportedResult -eq $guardedExpectedResult
        $guardedStatus = 'incomplete'
        if ($guardedComplete) { $guardedStatus = $guardedReportedResult }
        if ($probeDriver -eq 'hardware' -and $probeStatus -eq 'unavailable' -and $probeIdentityValid) {
            $guardedStatus = 'unavailable'
        }
        $probeMetadata += $probePrefix + '-guarded-status=' + $guardedStatus + "`r`n" +
            $probePrefix + '-guarded-terminal-count=' + $guardedCases.Count + "`r`n" +
            $probePrefix + '-guarded-terminal-failures=' + $guardedActualFailures + "`r`n" +
            $probePrefix + '-guarded-reference-unavailable=' + $guardedReferenceUnavailable + "`r`n" +
            $probePrefix + '-guarded-summary-line=' + $guardedSummaryLine + "`r`n"
        if ($guardedStatus -ne 'PASS' -and $guardedStatus -ne 'unavailable') { $probeFailure = $true }
        $probeIdentityStatus = 'mismatch'
        if ($probeIdentityValid) { $probeIdentityStatus = 'matched' }
        $probeMetadata += $probePrefix + '-identity-status=' + $probeIdentityStatus + "`r`n"
        if ($probeDriver -eq 'warp') {
            $warpIdentityValid = $probeIdentityValid
            $warpSchemaMatched = $probeIdentityValid
        } else { $hardwareIdentityValid = $probeIdentityValid }
        [IO.File]::AppendAllText($probeIdentity, $probeMetadata, [Text.Encoding]::UTF8)
    }
    $probeFailure = $probeFailure -or -not $warpIdentityValid -or -not $hardwareIdentityValid -or
        $warpStatus -ne 'exit-zero' -or ($hardwareStatus -ne 'exit-zero' -and $hardwareStatus -ne 'unavailable')
}
if ($ProbeOnly) {
    [IO.File]::AppendAllText($probeIdentity, ('ended=' + [DateTime]::Now.ToString('o') + "`r`n"), [Text.Encoding]::UTF8)
    Write-Host ('Collected both driver tests: ' + $captureRoot)
    Write-Host 'Return the complete root, including identity.txt and all four pixel-test output files.'
    if ($probeFailure) { exit 1 }
    exit 0
}
if ($env:INKEYS_DRAW3_WIN7_DIAGNOSTICS -eq '1') {
    Write-Host 'This diagnostic run temporarily enables Draw3 and Cursor console output.'
} else { Write-Host 'Keep ConsoleOutput.Draw3 and ConsoleOutput.Cursor enabled.' }
Write-Host 'Draw with the mouse, then continuously move eraser and laser on the canvas for at least 3 seconds each.'
Write-Host 'Exit Inkeys normally when finished. This script will then collect the logs.'
$appProcess = $null
$appExitCode = $null
try {
    $appProcess = Start-Process -FilePath $exePath -WorkingDirectory $appRoot -PassThru -NoNewWindow `
        -RedirectStandardOutput (Join-Path $captureRoot 'console.stdout.txt') `
        -RedirectStandardError (Join-Path $captureRoot 'console.stderr.txt')
    # Retain the exact owned child's handle before exit, as for the isolated probe.
    $null = $appProcess.Handle
    $appProcess.WaitForExit()
    $appExitCode = $appProcess.ExitCode
    if ($null -eq $appExitCode) { throw 'Application exited but its exit code could not be read.' }
    $appResult = 'exit=' + $appExitCode
} catch {
    $appResult = 'app-error=' + $_.Exception.Message
    if ($appProcess) {
        try { $appProcess.Kill(); [void]$appProcess.WaitForExit(5000) } catch { }
    }
    Write-Host $appResult
}
[IO.File]::AppendAllText((Join-Path $captureRoot 'identity.txt'),
    ($appResult + "`r`nended=" + [DateTime]::Now.ToString('o') + "`r`n"), [Text.Encoding]::UTF8)
# Copy only logs touched during this run; do not change the original logs or config.
foreach ($logRoot in @($appRoot, (Join-Path $appRoot 'log'))) {
    if (Test-Path -LiteralPath $logRoot) {
        foreach ($logFile in Get-ChildItem -LiteralPath $logRoot -Filter 'idt*.log') {
            if (-not $logFile.PSIsContainer -and $logFile.LastWriteTime -ge $started.AddSeconds(-2)) {
                Copy-Item -LiteralPath $logFile.FullName -Destination (Join-Path $captureRoot $logFile.Name)
            }
        }
    }
}
Write-Host ('Collected: ' + $captureRoot)
Write-Host 'Return this complete directory, including both console files, identity.txt and the IDT log.'
if ($appExitCode -ne 0 -or $null -eq $appExitCode) { exit 1 }
exit 0
