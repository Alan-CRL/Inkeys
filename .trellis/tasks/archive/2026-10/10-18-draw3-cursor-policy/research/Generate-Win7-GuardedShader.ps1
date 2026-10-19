param(
    [Parameter(Mandatory = $true)][string]$Fxc,
    [Parameter(Mandatory = $true)][string]$ProductionPixelCso,
    [Parameter(Mandatory = $true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$researchRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [IO.Path]::GetFullPath((Join-Path $researchRoot '../../../../../..'))
$assetsRoot = Join-Path $repoRoot 'Inkeys/Inkeys/Drawing/Draw3/Assets'
$headerPath = Join-Path $repoRoot 'Inkeys/Inkeys/Drawing/Draw3/Draw3.Win7GuardedShader.h'
[void][IO.Directory]::CreateDirectory($OutputDirectory)
function Get-Sha256([byte[]]$Bytes) {
    $provider = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($provider.ComputeHash($Bytes)).Replace('-', '') }
    finally { $provider.Clear() }
}
function Get-Chunks([byte[]]$Bytes) {
    if ([Text.Encoding]::ASCII.GetString($Bytes, 0, 4) -ne 'DXBC') { throw 'Invalid DXBC.' }
    $chunks = @{}
    for ($index = 0; $index -lt [BitConverter]::ToUInt32($Bytes, 28); ++$index) {
        $offset = [BitConverter]::ToUInt32($Bytes, 32 + 4 * $index)
        $tag = [Text.Encoding]::ASCII.GetString($Bytes, $offset, 4)
        $length = [BitConverter]::ToUInt32($Bytes, $offset + 4)
        $payload = New-Object byte[] $length
        [Array]::Copy($Bytes, $offset + 8, $payload, 0, $length)
        $chunks[$tag] = Get-Sha256 $payload
    }
    return $chunks
}
$productionPath = Join-Path $assetsRoot 'inkPixelShader.hlsl'
$production = [IO.File]::ReadAllText($productionPath)
$anchor = '    //float aaWidth = fwidth(d);'
$start = $production.IndexOf($anchor, [StringComparison]::Ordinal)
$ending = "    return output;`r`n}"
$end = $production.LastIndexOf($ending, [StringComparison]::Ordinal) + '    return output;'.Length
if ($start -lt 0 -or $end -le $start -or $production.LastIndexOf($anchor, [StringComparison]::Ordinal) -ne $start) {
    throw 'DefaultInk anchors changed; review transformation.'
}
$tail = $production.Substring($start, $end - $start)
$indented = ($tail -split "`r`n" | ForEach-Object { if ($_.Length) { '    ' + $_ } else { $_ } }) -join "`r`n"
# 保留全部数学和未知 type 的旧 d=0 输出，只改变尾部控制流所在的正向分支。
$gate = "    // Diagnostic candidate: preserve DefaultInk math and unknown-type output.`r`n    if (type == 0 || type == 3 || type < 0 || type > 21)`r`n    {`r`n"
$fallback = "`r`n    }`r`n    OperatorOutput unreachableOutput;`r`n    unreachableOutput.add = 0.0;`r`n    unreachableOutput.retain = 1.0;`r`n    return unreachableOutput;"
$candidate = $production.Substring(0, $start) + $gate + $indented + $fallback + $production.Substring($end)
$restored = ($indented -split "`r`n" | ForEach-Object { if ($_.Length) { $_.Substring(4) } else { $_ } }) -join "`r`n"
if ($restored -cne $tail -or -not $candidate.StartsWith($production.Substring(0, $start), [StringComparison]::Ordinal) -or
    -not $candidate.EndsWith($production.Substring($end), [StringComparison]::Ordinal)) { throw 'Math/prefix/suffix preservation failed.' }
$sourcePath = Join-Path $OutputDirectory 'GuardedDefaultOutput.hlsl'
[IO.File]::WriteAllText($sourcePath, $candidate, [Text.Encoding]::ASCII)
$includeHashes = @{}
foreach ($include in @('ink.hlsli', 'laserParticleCommon.hlsli', 'EraserGripVisual.h')) {
    $path = Join-Path $assetsRoot $include
    $includeHashes[$include] = Get-Sha256 ([IO.File]::ReadAllBytes($path))
    [IO.File]::WriteAllText((Join-Path $OutputDirectory $include), [IO.File]::ReadAllText($path), [Text.Encoding]::ASCII)
}
$objectPath = Join-Path $OutputDirectory 'GuardedDefaultOutput.cso'
& $Fxc /nologo /T ps_5_0 /E main /O3 /Fo $objectPath $sourcePath
if ($LASTEXITCODE -ne 0) { throw 'Guarded shader compile failed.' }
$dump = @(& $Fxc /nologo /dumpbin $objectPath)
if ($LASTEXITCODE -ne 0) { throw 'Guarded dump failed.' }
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'GuardedDefaultOutput.asm'), [string[]]$dump)
$bytes = [IO.File]::ReadAllBytes($objectPath)
$hash = Get-Sha256 $bytes
$candidateChunks = Get-Chunks $bytes
$productionChunks = Get-Chunks ([IO.File]::ReadAllBytes($ProductionPixelCso))
foreach ($signature in @('ISGN', 'OSGN')) {
    if ($candidateChunks[$signature] -cne $productionChunks[$signature]) { throw ('Signature changed: ' + $signature) }
}
# 源码 guard 不足以证明优化后控制流：所有 DefaultInk 尾部导数/discard 必须真正位于 if 内。
$depth = 0
$guardedInstructions = @()
foreach ($line in $dump) {
    $trimmed = $line.Trim()
    if ($trimmed -match '^endif\b') { --$depth }
    if ($trimmed -match '^if_') { ++$depth }
    if ($trimmed -match '^(deriv_|discard)') { $guardedInstructions += @{ instruction=$trimmed; depth=$depth } }
}
if ($depth -ne 0 -or $guardedInstructions.Count -lt 4 -or @($guardedInstructions | Where-Object { $_.depth -lt 1 }).Count -ne 0) {
    throw 'Derivative/discard escaped positive route guards.'
}
$header = "#pragma once`r`n`r`n// Generated complete diagnostic candidate; see Generate-Win7-GuardedShader.ps1.`r`nnamespace Draw3Win7GuardedShader`r`n{`r`n`tinline constexpr char Sha256[] = `"$hash`";`r`n`tinline constexpr unsigned char Bytecode[] = {`r`n"
for ($index = 0; $index -lt $bytes.Length; $index += 16) {
    $last = [Math]::Min($index + 15, $bytes.Length - 1)
    $header += "`t`t" + (($bytes[$index..$last] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', ') + ",`r`n"
}
$header += "`t};`r`n}`r`n"
[IO.File]::WriteAllText($headerPath, $header, (New-Object Text.UTF8Encoding($true)))
$evidence = @{ profile='ps_5_0'; options='/O3'; byte_count=$bytes.Length; sha256=$hash;
    production_source_sha256=(Get-Sha256 ([IO.File]::ReadAllBytes($productionPath)));
    production_pixel_sha256=(Get-Sha256 ([IO.File]::ReadAllBytes($ProductionPixelCso)));
    includes=$includeHashes; candidate_chunks=$candidateChunks; production_chunks=$productionChunks;
    tail_original_sha256=(Get-Sha256 ([Text.Encoding]::UTF8.GetBytes($tail)));
    tail_restored_sha256=(Get-Sha256 ([Text.Encoding]::UTF8.GetBytes($restored)));
    math_preserved=$true; unknown_type_preserved=$true; guarded_instructions=$guardedInstructions }
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'guarded-source-evidence.json'), ($evidence | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
Write-Host "GuardedDefaultOutput bytes=$($bytes.Length) SHA256=$hash; original math and signatures preserved."
