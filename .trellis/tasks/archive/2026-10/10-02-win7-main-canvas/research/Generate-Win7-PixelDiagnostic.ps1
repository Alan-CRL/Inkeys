param(
    [Parameter(Mandatory = $true)][string]$Fxc,
    [Parameter(Mandatory = $true)][string]$ProductionVertexCso,
    [string]$ProductionPixelCso,
    [string]$OutputDirectory = (Join-Path $env:TEMP ([Guid]::NewGuid().ToString('N')))
)
$ErrorActionPreference = 'Stop'
$researchRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = [IO.Path]::GetFullPath((Join-Path $researchRoot '../../../..'))
$headerPath = Join-Path $repoRoot 'Inkeys/Inkeys/Drawing/Draw3/Draw3.PixelDiagnosticShaders.h'
$assetsRoot = Join-Path $repoRoot 'Inkeys/Inkeys/Drawing/Draw3/Assets'
if (-not $ProductionPixelCso) { $ProductionPixelCso = Join-Path $assetsRoot 'inkPixelShader.cso' }
[void][IO.Directory]::CreateDirectory($OutputDirectory)
function Get-BytesSha256([byte[]]$Bytes) {
    $provider = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($provider.ComputeHash($Bytes)).Replace('-', '') }
    finally { $provider.Clear() }
}
function Get-DxbcChunkHashes([byte[]]$Bytes) {
    if ([Text.Encoding]::ASCII.GetString($Bytes, 0, 4) -ne 'DXBC') { throw 'Invalid DXBC container.' }
    $chunkHashes = @{}
    $count = [BitConverter]::ToUInt32($Bytes, 28)
    for ($index = 0; $index -lt $count; ++$index) {
        $offset = [BitConverter]::ToUInt32($Bytes, 32 + 4 * $index)
        $tag = [Text.Encoding]::ASCII.GetString($Bytes, $offset, 4)
        $length = [BitConverter]::ToUInt32($Bytes, $offset + 4)
        $payload = New-Object byte[] $length
        [Array]::Copy($Bytes, $offset + 8, $payload, 0, $length)
        $chunkHashes[$tag] = Get-BytesSha256 $payload
    }
    return $chunkHashes
}
# FXC cannot read UTF-8 BOM; production copies below are also temporary ASCII input.
$sourcePath = Join-Path $OutputDirectory 'Win7-PixelDiagnostic.hlsl'
[IO.File]::WriteAllText($sourcePath,
    [IO.File]::ReadAllText((Join-Path $researchRoot 'Win7-PixelDiagnostic.hlsl')), [Text.Encoding]::ASCII)
$header = "#pragma once`r`n`r`n// Generated diagnostic-only ps_5_0 /O3; see task research/Generate-Win7-PixelDiagnostic.ps1.`r`nnamespace Draw3PixelDiagnosticShaders`r`n{`r`n"
$productionDump = & $Fxc /nologo /dumpbin $ProductionVertexCso
if ($LASTEXITCODE -ne 0) { throw 'Production VS dump failed.' }
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'production-vs.asm'), [string[]]$productionDump)
$expectedSignature = @(
    'SV_POSITION 0 xyzw 0 POS float', 'TEXCOORD 0 xy 1 NONE float',
    'TEXCOORD 1 zw 1 NONE float', 'COLOR 0 xyzw 2 NONE float',
    'VAL_START 0 xy 3 NONE float', 'VAL_END 0 zw 3 NONE float',
    'VAL_RAD_START 0 x 4 NONE float', 'VAL_RAD_END 0 y 4 NONE float', 'VAL_TYPE 0 z 4 NONE float'
)
$compactEvidence = @()
foreach ($entry in @('ConstantOutput', 'ValidateFields', 'CursorOnly', 'CompactIntegerRoute', 'CompactFloatRoute')) {
    $objectPath = Join-Path $OutputDirectory ($entry + '.cso')
    & $Fxc /nologo /T ps_5_0 /E $entry /O3 /Fo $objectPath $sourcePath
    if ($LASTEXITCODE -ne 0) { throw ('Diagnostic compile failed: ' + $entry) }
    $dump = & $Fxc /nologo /dumpbin $objectPath
    if ($LASTEXITCODE -ne 0) { throw ('Diagnostic dump failed: ' + $entry) }
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory ($entry + '.asm')), [string[]]$dump)
    if ($entry -eq 'ValidateFields' -or $entry -eq 'CursorOnly') {
        # Use all declared signature entries, not just semantic names or Used masks.
        $normalizedVs = ($productionDump | ForEach-Object { ($_ -replace '^//\s*', '' -replace '\s+', ' ').Trim() })
        $normalizedPs = ($dump | ForEach-Object { ($_ -replace '^//\s*', '' -replace '\s+', ' ').Trim() })
        foreach ($signature in $expectedSignature) {
            if (-not ($normalizedVs | Where-Object { $_ -eq $signature -or $_.StartsWith($signature + ' ') }) -or
                -not ($normalizedPs | Where-Object { $_ -eq $signature -or $_.StartsWith($signature + ' ') })) {
                throw ('VS/PS signature mismatch: ' + $entry + ': ' + $signature)
            }
        }
        $declarations = @('dcl_input_ps linear v1.xy', 'dcl_input_ps constant v2.xyzw',
            'dcl_input_ps constant v3.xy', 'dcl_input_ps constant v3.zw',
            'dcl_input_ps constant v4.x', 'dcl_input_ps constant v4.y')
        if ($entry -eq 'ValidateFields') {
            $declarations += @('dcl_input_ps_siv linear noperspective v0.xyzw, position',
                'dcl_input_ps linear v1.zw', 'dcl_input_ps constant v4.z')
        }
        foreach ($declaration in $declarations) {
            if (-not ($dump | Where-Object { $_.Trim() -eq $declaration })) {
                throw ('Interpolation declaration mismatch: ' + $entry + ': ' + $declaration)
            }
        }
    }
    if ($entry -eq 'CompactIntegerRoute' -or $entry -eq 'CompactFloatRoute') {
        $normalizedPs = @($dump | ForEach-Object { ($_ -replace '^//\s*', '' -replace '\s+', ' ').Trim() })
        foreach ($signature in $expectedSignature) {
            if (-not ($normalizedPs | Where-Object { $_ -eq $signature -or $_.StartsWith($signature + ' ') })) { throw ('Compact signature mismatch: ' + $entry) }
        }
        $assembly = $dump -join "`n"
        if ($assembly -notmatch 'if_nz' -or $assembly -notmatch 'dcl_input_ps constant v4.z') { throw ('Compact dynamic route missing: ' + $entry) }
        if ($entry -eq 'CompactIntegerRoute' -and ($assembly -notmatch '\bftoi\b' -or $assembly -notmatch '\bige\b' -or $assembly -notmatch '\bieq\b')) { throw 'Integer route optimized away.' }
        if ($entry -eq 'CompactFloatRoute' -and ($assembly -match '\bftoi\b' -or $assembly -notmatch '\bge\b' -or $assembly -notmatch '\beq\b')) { throw 'Float route has unexpected conversion/gates.' }
    }
    $bytes = [IO.File]::ReadAllBytes($objectPath)
    $provider = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($provider.ComputeHash($bytes)).Replace('-', '') }
    finally { $provider.Clear() }
    $header += "`t// $entry SHA256=$hash bytes=$($bytes.Length)`r`n`tinline constexpr char ${entry}Sha256[] = `"$hash`";`r`n`tinline constexpr unsigned char $entry[] = {`r`n"
    for ($index = 0; $index -lt $bytes.Length; $index += 16) {
        $end = [Math]::Min($index + 15, $bytes.Length - 1)
        $header += "`t`t" + (($bytes[$index..$end] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', ') + ",`r`n"
    }
    $header += "`t};`r`n`r`n"
    $compactEvidence += @{ name=$entry; profile='ps_5_0'; options='/O3'; sha256=$hash; byte_count=$bytes.Length;
        ascii_source_sha256=(Get-BytesSha256 ([IO.File]::ReadAllBytes($sourcePath)));
        actual_declarations=@($dump | Where-Object { $_ -match '^dcl_' });
        actual_route_instructions=@($dump | Where-Object { $_ -match '^\s*(?:ftoi|ige|ieq|ge|eq|if_|else|mov o|ret)' });
        dxbc_chunk_sha256=(Get-DxbcChunkHashes $bytes) }
    Write-Host ($entry + ' ps_5_0 /O3 SHA256=' + $hash + ' bytes=' + $bytes.Length)
}

# Full-source experiments retain every unrelated tool route and resource declaration.
# The copies are disposable ASCII input, never edits to production HLSL/CSO/resources.
$productionSourcePath = Join-Path $assetsRoot 'inkPixelShader.hlsl'
$productionSource = [IO.File]::ReadAllText($productionSourcePath)
foreach ($include in @('ink.hlsli', 'laserParticleCommon.hlsli', 'EraserGripVisual.h')) {
    [IO.File]::WriteAllText((Join-Path $OutputDirectory $include),
        [IO.File]::ReadAllText((Join-Path $assetsRoot $include)), [Text.Encoding]::ASCII)
}
$cursorStartToken = '    if (type >= 4 && type <= 6)'
$laserStartToken = '    if (type == 9 || type == 10)'
$cursorEndToken = '    if (type == 8)'
$cursorStart = $productionSource.IndexOf($cursorStartToken, [StringComparison]::Ordinal)
$cursorEnd = $productionSource.IndexOf($cursorEndToken, $cursorStart, [StringComparison]::Ordinal)
$laserStart = $productionSource.IndexOf($laserStartToken, [StringComparison]::Ordinal)
if ($laserStart -lt 0 -or $cursorStart -le $laserStart -or $cursorEnd -le $cursorStart -or
    $productionSource.LastIndexOf($cursorStartToken, [StringComparison]::Ordinal) -ne $cursorStart) {
    throw 'Production Cursor/Laser route anchors changed; review full-source experiments.'
}
$cursorBlock = $productionSource.Substring($cursorStart, $cursorEnd - $cursorStart)
$entryAnchor = "    {`r`n        float2 local = input.pixPos - input.p1;"
$sentinelEntry = @"
    {
        // Diagnostic: prove entry into the original Cursor route, before its math/discard.
        if (type == 4)
        {
            OperatorOutput diagnosticSentinel;
            diagnosticSentinel.add = 0.5;
            diagnosticSentinel.retain = 0.5;
            return diagnosticSentinel;
        }
        float2 local = input.pixPos - input.p1;
"@ -replace "`r?`n", "`r`n"
if ($cursorBlock.IndexOf($entryAnchor, [StringComparison]::Ordinal) -lt 0) {
    throw 'Production Cursor entry anchor changed.'
}
$sentinelCursor = $cursorBlock.Replace($entryAnchor, $sentinelEntry)
$sentinelSource = $productionSource.Substring(0, $cursorStart) + $sentinelCursor +
    $productionSource.Substring($cursorEnd)
$aheadSource = $productionSource.Remove($cursorStart, $cursorEnd - $cursorStart).Insert($laserStart, $cursorBlock)
$fullVariants = @(
    @{ Name = 'FullRecompiled'; Source = $productionSource },
    @{ Name = 'FullCursorEntryConstant'; Source = $sentinelSource },
    @{ Name = 'FullCursorAhead'; Source = $aheadSource }
)

$mainAnchor = "OperatorOutput main(PS_INPUT input)`r`n{`r`n"
$earlyMain = $mainAnchor + @"
    // Diagnostic runtime gate precedes type conversion; remaining complete body is retained.
    if (globalShapeType == 4.0 && globalColor.a > 0.0)
    {
        OperatorOutput diagnosticEntry;
        diagnosticEntry.add = 0.5;
        diagnosticEntry.retain = 0.5;
        return diagnosticEntry;
    }
"@ + "`r`n"
if ($productionSource.IndexOf($mainAnchor, [StringComparison]::Ordinal) -lt 0) { throw 'Main entry changed.' }
$fullVariants += @(
    @{ Name = 'FullEarlyConstant'; Source = $productionSource.Replace($mainAnchor, ($earlyMain -replace "`r?`n", "`r`n")) },
    @{ Name = 'FullFloatRoute'; Source = $productionSource.Replace('int type = (int) (input.shapeType + 0.5);', 'float type = input.shapeType;') }
)
# Slice only reviewed top-level main blocks; helper definitions remain production-identical.
function Get-MainBlock([string]$Token) {
    $start = $productionSource.IndexOf($Token, $productionSource.IndexOf($mainAnchor), [StringComparison]::Ordinal)
    if ($start -lt 0) { throw ('Family anchor absent: ' + $Token) }
    $brace = $productionSource.IndexOf('{', $start)
    $depth = 0; $end = $brace
    do {
        if ($productionSource[$end] -eq '{') { ++$depth }
        elseif ($productionSource[$end] -eq '}') { --$depth }
        ++$end
    } while ($depth -gt 0 -and $end -lt $productionSource.Length)
    if ($depth -ne 0) { throw ('Family unbalanced block: ' + $Token) }
    return @{ Start = $start; End = $end; Text = $productionSource.Substring($start, $end - $start) }
}
$blocks = @(
    @{ Group='Laser'; Token='    if (type == 9 || type == 10)' },
    @{ Group='Cursor'; Token='    if (type >= 4 && type <= 6)' },
    @{ Group='Laser'; Token='    if (type == 8)' },
    @{ Group='Laser'; Token='    if (type == 13)' },
    @{ Group='Laser'; Token='    if (type == 11)' },
    @{ Group='Laser'; Token='    if (type == 12)' },
    @{ Group='History'; Token='    if (type == 14)' },
    @{ Group='History'; Token='    if (type == 15)' },
    @{ Group='TrustedSnapshot'; Token='    if (type == 20)' },
    @{ Group='Operators'; Token='    if (type == 1 || type == 2)' },
    @{ Group='Shape'; Token='    if (type >= 16 && type <= 19)' }
)
foreach ($block in $blocks) { $block.Range = Get-MainBlock $block.Token }
$laser7 = Get-MainBlock '    if (type == 7)'
$prefix = $productionSource.Substring(0, $blocks[0].Range.Start)
$tailStart = $productionSource.IndexOf('    float d = 0.0;', $blocks[-1].Range.End, [StringComparison]::Ordinal)
$tailEnd = $productionSource.LastIndexOf('}')
if ($tailStart -lt 0 -or $laser7.Start -lt $tailStart -or $tailEnd -le $laser7.End) { throw 'Default/Laser tail changed.' }
$defaultDistance = $productionSource.Substring($tailStart, $laser7.Start - $tailStart)
$defaultOutput = $productionSource.Substring($laser7.End, $tailEnd - $laser7.End)
$fallback = "    OperatorOutput diagnosticFallback;`r`n    diagnosticFallback.add = 0.0;`r`n    diagnosticFallback.retain = 1.0;`r`n    return diagnosticFallback;`r`n"
$groups = @('Laser','Operators','History','TrustedSnapshot','Shape','DefaultInk')
$familyRequests = @(@{ Name='CursorFamilyOnly'; Groups=@() })
foreach ($group in $groups) {
    $familyRequests += @{ Name=('CursorWith' + $group); Groups=@($group) }
    $familyRequests += @{ Name=('FullWithout' + $group); Groups=@($groups | Where-Object { $_ -ne $group }) }
}
foreach ($request in $familyRequests) {
    $body = $prefix
    foreach ($block in $blocks) {
        if ($block.Group -eq 'Cursor' -or $request.Groups -contains $block.Group) { $body += $block.Range.Text + "`r`n`r`n" }
    }
    if ($request.Groups -contains 'DefaultInk') {
        $body += $defaultDistance
        if ($request.Groups -contains 'Laser') { $body += $laser7.Text + "`r`n" }
        # Only removed types fall back to transparency, instead of the original default d=0 path.
        $body += "    if (type != 0 && type != 3)`r`n    {`r`n" + $fallback + "    }`r`n"
        $body += $defaultOutput
    }
    else {
        if ($request.Groups -contains 'Laser') { $body += $laser7.Text + "`r`n" }
        $body += $fallback
    }
    $body += "}`r`n"
    $fullVariants += @{ Name=$request.Name; Source=$body; Family=$true; Groups=$request.Groups }
}

# Confirm executed declarations exactly match the selected families, not variant names.
$familyBindings = @{
    Laser=@('cb1','s0','t6','t7','t9'); Operators=@('s0','t1','t2','t4','t5');
    History=@('cb2','s0','t10','t11','t12','t13'); TrustedSnapshot=@('cb3','s0','t14');
    Shape=@(); DefaultInk=@()
}
function Get-DeclaredBindings([string[]]$Dump) {
    foreach ($line in $Dump) {
        if ($line -match '^dcl_constantbuffer CB(\d+)\[') { 'cb' + $Matches[1] }
        elseif ($line -match '^dcl_sampler (s\d+),') { $Matches[1] }
        elseif ($line -match '^dcl_resource_texture2d(?:array)? .* (t\d+)$') { $Matches[1] }
    }
}
$originalDump = & $Fxc /nologo /dumpbin $ProductionPixelCso
if ($LASTEXITCODE -ne 0) { throw 'Production PS dump failed.' }
[IO.File]::WriteAllLines((Join-Path $OutputDirectory 'production-ps.asm'), [string[]]$originalDump)
$productionBytes = [IO.File]::ReadAllBytes($ProductionPixelCso)
$productionHash = Get-BytesSha256 $productionBytes
$productionChunks = Get-DxbcChunkHashes $productionBytes
$header += "`t// Expected identity from the independently recorded production resource/CSO.`r`n"
$header += "`tinline constexpr char ProductionPixelSha256[] = `"$productionHash`";`r`n`r`n"
$sourceIdentity = @()
foreach ($name in @('inkPixelShader.hlsl', 'ink.hlsli', 'laserParticleCommon.hlsli', 'EraserGripVisual.h')) {
    $sourceIdentity += @{ name = $name; sha256 = (Get-BytesSha256 ([IO.File]::ReadAllBytes((Join-Path $assetsRoot $name)))) }
}
$fullEvidence = @()
$originalResourceRows = @($originalDump | Where-Object { $_ -match '^//\s+\w+\s+(?:cbuffer|texture|sampler)\s+' })
if ($originalResourceRows.Count -lt 15) { throw 'Production resource evidence unexpectedly small.' }
foreach ($variant in $fullVariants) {
    $entry = $variant.Name
    $variantPath = Join-Path $OutputDirectory ($entry + '.hlsl')
    [IO.File]::WriteAllText($variantPath, $variant.Source, [Text.Encoding]::ASCII)
    $objectPath = Join-Path $OutputDirectory ($entry + '.cso')
    & $Fxc /nologo /T ps_5_0 /E main /O3 /Fo $objectPath $variantPath
    if ($LASTEXITCODE -ne 0) { throw ('Full-source compile failed: ' + $entry) }
    $dump = & $Fxc /nologo /dumpbin $objectPath
    if ($LASTEXITCODE -ne 0) { throw ('Full-source dump failed: ' + $entry) }
    [IO.File]::WriteAllLines((Join-Path $OutputDirectory ($entry + '.asm')), [string[]]$dump)
    $normalizedPs = @($dump | ForEach-Object { ($_ -replace '^//\s*', '' -replace '\s+', ' ').Trim() })
    foreach ($signature in $expectedSignature) {
        if (-not ($normalizedPs | Where-Object { $_ -eq $signature -or $_.StartsWith($signature + ' ') })) {
            throw ('Full VS/PS signature mismatch: ' + $entry + ': ' + $signature)
        }
    }
    if (-not $variant.Family) {
    $declarations = @('dcl_input_ps_siv linear noperspective v0.xy, position',
        'dcl_input_ps linear v1.xy', 'dcl_input_ps linear v1.zw',
        'dcl_input_ps constant v2.xyzw', 'dcl_input_ps constant v3.xy',
        'dcl_input_ps constant v3.zw', 'dcl_input_ps constant v4.x',
        'dcl_input_ps constant v4.y', 'dcl_input_ps constant v4.z')
    foreach ($declaration in $declarations) {
        if (-not ($dump | Where-Object { $_.Trim() -eq $declaration })) {
            throw ('Full interpolation mismatch: ' + $entry + ': ' + $declaration)
        }
    }
    # Every resource present in the original PS must survive; this catches constant-only folding.
    foreach ($row in $originalResourceRows) {
        if (-not ($dump -contains $row)) { throw ('Full resource changed: ' + $entry + ': ' + $row) }
    }
    $instructionCount = @($dump | Where-Object { $_ -match '^\s*(?:if_|else|ret|discard_|sample|ld_|add|mul|mov|ieq|ige)\b' }).Count
    if ($instructionCount -lt 250 -or -not ($dump | Where-Object { $_ -match '^dcl_resource_texture2darray' })) {
        throw ('Full branches absent in compiled variant: ' + $entry)
    }
    }
    $sourceBlockEvidence = @()
    if ($variant.Family) {
        $expectedBindings = @('cb0')
        foreach ($group in $variant.Groups) { $expectedBindings += $familyBindings[$group] }
        $expectedBindings = @($expectedBindings | Sort-Object -Unique)
        $actualBindings = @(Get-DeclaredBindings $dump | Sort-Object -Unique)
        if (Compare-Object $expectedBindings $actualBindings) { throw ('Family executed resource mismatch: ' + $entry) }
        $assembly = $dump -join "`n"
        if ($assembly -notmatch '\bftoi\b' -or $assembly -notmatch '\bif_nz\b' -or
            $assembly -notmatch 'ige [^\r\n]*l\(4\)' -or $assembly -notmatch 'ige [^\r\n]*l\(6\)') {
            throw ('Family integer Cursor route absent: ' + $entry)
        }
        $hasShape = $assembly -match 'ige [^\r\n]*l\(16\)' -and $assembly -match 'ige [^\r\n]*l\(19\)'
        if ($hasShape -ne ($variant.Groups -contains 'Shape')) { throw ('Family executed Shape range mismatch: ' + $entry) }
        $hasDerivative = $assembly -match '\bderiv_rtx'
        $expectsDerivative = ($variant.Groups -contains 'Laser') -or ($variant.Groups -contains 'Shape') -or ($variant.Groups -contains 'DefaultInk')
        if ($hasDerivative -ne $expectsDerivative) { throw ('Family derivative body mismatch: ' + $entry) }
        foreach ($block in $blocks) {
            $retained = $block.Group -eq 'Cursor' -or $variant.Groups -contains $block.Group
            if (($variant.Source.Contains($block.Range.Text)) -ne $retained) { throw ('Family source block mismatch: ' + $entry + ': ' + $block.Token) }
            $sourceBlockEvidence += @{ token=$block.Token; group=$block.Group; retained=$retained;
                production_ascii_block_sha256=(Get-BytesSha256 ([Text.Encoding]::ASCII.GetBytes($block.Range.Text))) }
        }
        if ($variant.Source.Contains($laser7.Text) -ne ($variant.Groups -contains 'Laser')) { throw ('Family Laser7 source mismatch: ' + $entry) }
    }
    $bytes = [IO.File]::ReadAllBytes($objectPath)
    $hash = Get-BytesSha256 $bytes
    $chunks = Get-DxbcChunkHashes $bytes
    # The original-source control must reproduce executed instructions, not metadata identity alone.
    if ($entry -eq 'FullRecompiled') {
        foreach ($tag in @('ISGN', 'OSGN', 'SHEX', 'STAT')) {
            if (-not $chunks.ContainsKey($tag) -or $chunks[$tag] -ne $productionChunks[$tag]) {
                throw ('Unchanged full source differs in executed/signature chunk: ' + $tag)
            }
        }
    }
    $assembly = $dump -join "`n"
    if ($entry -eq 'FullCursorEntryConstant' -and
        $assembly -notmatch '(?s)ieq [^\r\n]*l\(4\)\s+if_nz [^\r\n]*\s+mov o0\.xyzw, l\(0\.500000,0\.500000,0\.500000,0\.500000\)\s+mov o1\.xyzw, l\(0\.500000,0\.500000,0\.500000,0\.500000\)\s+ret') {
        throw 'Compiled Cursor entry constant route is absent.'
    }
    if ($entry -eq 'FullCursorAhead') {
        $cursorInstruction = $assembly.IndexOf('ige r0.y, r0.x, l(4)', [StringComparison]::Ordinal)
        $laserInstruction = $assembly.IndexOf('ieq r0.yz, r0.xxxx, l(0, 9, 10, 0)', [StringComparison]::Ordinal)
        if ($cursorInstruction -lt 0 -or $laserInstruction -le $cursorInstruction) {
            throw 'Compiled Cursor route did not move ahead of Laser.'
        }
    }
    if ($entry -eq 'FullEarlyConstant' -and ($assembly -notmatch 'cb0\[0\]\.z' -or $assembly -notmatch 'cb0\[1\]\.w' -or $assembly.IndexOf('mov o0.xyzw, l(0.500000') -gt $assembly.IndexOf('ftoi'))) { throw 'Full runtime entry gate missing/too late.' }
    if ($entry -eq 'FullFloatRoute' -and ($assembly -match 'ftoi r0.x, r0.x' -or $assembly -notmatch 'eq .*v4\.z')) { throw 'Full float route changed unexpectedly.' }
    $header += "`t// $entry SHA256=$hash bytes=$($bytes.Length)`r`n"
    $header += "`tinline constexpr char ${entry}Sha256[] = `"$hash`";`r`n"
    $header += "`tinline constexpr unsigned char $entry[] = {`r`n"
    for ($index = 0; $index -lt $bytes.Length; $index += 16) {
        $end = [Math]::Min($index + 15, $bytes.Length - 1)
        $header += "`t`t" + (($bytes[$index..$end] | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', ') + ",`r`n"
    }
    $header += "`t};`r`n`r`n"
    $fullEvidence += @{ name = $entry; profile = 'ps_5_0'; options = '/O3'; byte_count = $bytes.Length;
        sha256 = $hash; ascii_source_sha256 = (Get-BytesSha256 ([IO.File]::ReadAllBytes($variantPath)));
        family = [bool]$variant.Family; groups_retained = @('Cursor') + $(if ($variant.Family) { @($variant.Groups) } else { @($groups) });
        source_block_evidence = $sourceBlockEvidence;
        actual_resource_rows = @($dump | Where-Object { $_ -match '^//\s+\w+\s+(?:cbuffer|texture|sampler)\s+' });
        actual_declarations = @($dump | Where-Object { $_ -match '^dcl_' });
        actual_declared_bindings = @(Get-DeclaredBindings $dump | Sort-Object -Unique);
        actual_route_instructions = @($dump | Where-Object { $_ -match '^\s*(?:ftoi|ige|ieq|if_|else|ret|deriv_|sample)' });
        selected_instructions = @($dump | Where-Object { $_ -match '^\s*(?:add|mul|mov|if_|ieq|ige)' }).Count; dxbc_chunk_sha256 = $chunks }
    Write-Host ($entry + ' ps_5_0 /O3 SHA256=' + $hash + ' bytes=' + $bytes.Length)
}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'full-source-evidence.json'),
    (@{ production_pixel_sha256 = $productionHash; production_chunk_sha256 = $productionChunks; production_sources = $sourceIdentity;
        compact_variants = $compactEvidence; variants = $fullEvidence } | ConvertTo-Json -Depth 6), [Text.Encoding]::UTF8)

$header += "}`r`n"
[IO.File]::WriteAllText($headerPath, $header, [Text.Encoding]::ASCII)
Write-Host ('Generated: ' + $headerPath + '; read-only signature evidence: ' + $OutputDirectory)
