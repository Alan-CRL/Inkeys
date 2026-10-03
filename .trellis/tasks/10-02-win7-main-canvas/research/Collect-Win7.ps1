$ErrorActionPreference = 'Stop'
# Use APIs available in Windows 7's PowerShell 2.0.
$appRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$exePath = Join-Path $appRoot 'Inkeys.exe'
if (-not (Test-Path -LiteralPath $exePath)) { throw 'Place this script beside the diagnostic Inkeys.exe.' }
if (Get-Process -Name Inkeys -ErrorAction SilentlyContinue) { throw 'Exit the existing Inkeys instance before collecting.' }
$stamp = [DateTime]::Now.ToString('yyyyMMdd-HHmmss-fff')
$captureRoot = Join-Path (Join-Path $appRoot 'Win7-Diagnostics') $stamp
[void][IO.Directory]::CreateDirectory($captureRoot)
$hashProvider = [Security.Cryptography.SHA256]::Create()
$exeStream = [IO.File]::OpenRead($exePath)
try { $exeHash = [BitConverter]::ToString($hashProvider.ComputeHash($exeStream)).Replace('-', '') }
finally { $exeStream.Dispose(); $hashProvider.Clear() }
$started = [DateTime]::Now
$metadata = @(('exe=' + $exePath), ('sha256=' + $exeHash), ('os=' + [Environment]::OSVersion.VersionString),
    ('started=' + $started.ToString('o')), ('powershell=' + $PSVersionTable.PSVersion.ToString()))
[IO.File]::WriteAllLines((Join-Path $captureRoot 'identity.txt'), [string[]]$metadata, [Text.Encoding]::UTF8)
# The explicit early CLI probe creates no visible window and does not load app config.
# A probe failure/timeout must not prevent collecting the normal application run.
$probeIdentity = Join-Path $captureRoot 'identity.txt'
$probeProcess = $null
try {
    Write-Host 'Running isolated renderer pixel test (up to 30 seconds)...'
    $probeProcess = Start-Process -FilePath $exePath -WorkingDirectory $appRoot -PassThru -WindowStyle Hidden `
        -ArgumentList '--draw3-renderer-pixel-test' `
        -RedirectStandardOutput (Join-Path $captureRoot 'pixel-test.stdout.txt') `
        -RedirectStandardError (Join-Path $captureRoot 'pixel-test.stderr.txt')
    if ($probeProcess.WaitForExit(30000)) {
        $probeProcess.WaitForExit()
        $probeProcess.Refresh()
        $probeResult = 'pixel-test-exit=' + $probeProcess.ExitCode
    } else {
        # Terminate only this isolated child; no process-name based cleanup.
        try { $probeProcess.Kill(); [void]$probeProcess.WaitForExit(5000) } catch { }
        $probeResult = 'pixel-test-timeout=30000'
    }
} catch {
    $probeResult = 'pixel-test-error=' + $_.Exception.Message
    Write-Host ('Pixel test could not finish: ' + $_.Exception.Message)
}
[IO.File]::AppendAllText($probeIdentity, ($probeResult + "`r`n"), [Text.Encoding]::UTF8)
Write-Host 'Keep ConsoleOutput.Draw3 and ConsoleOutput.Cursor enabled.'
Write-Host 'Draw with the mouse, then continuously move eraser and laser on the canvas for at least 3 seconds each.'
Write-Host 'Exit Inkeys normally when finished. This script will then collect the logs.'
$appProcess = Start-Process -FilePath $exePath -WorkingDirectory $appRoot -PassThru -Wait `
    -RedirectStandardOutput (Join-Path $captureRoot 'console.stdout.txt') `
    -RedirectStandardError (Join-Path $captureRoot 'console.stderr.txt')
$appProcess.Refresh()
[IO.File]::AppendAllText((Join-Path $captureRoot 'identity.txt'),
    ('exit=' + $appProcess.ExitCode + "`r`nended=" + [DateTime]::Now.ToString('o') + "`r`n"), [Text.Encoding]::UTF8)
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
