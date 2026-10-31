$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/release-artifacts.ps1"

function Assert-Equal($Actual, $Expected, [string] $Label) {
    if ($Actual -ne $Expected) { throw "$Label : expected=$Expected actual=$Actual" }
    Write-Host "PASS $Label"
}
function Assert-Throws([scriptblock] $Action, [string] $Label) {
    $caught = $false
    try { & $Action | Out-Null } catch { $caught = $true }
    if (-not $caught) { throw "没有拒绝：$Label" }
    Write-Host "PASS $Label"
}
function New-UnsignedFixture([byte] $Marker) {
    $bytes = [byte[]]::new(512)
    $bytes[0] = 0x4d; $bytes[1] = 0x5a
    [Array]::Copy([BitConverter]::GetBytes([int]128), 0, $bytes, 60, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]0x4550), 0, $bytes, 128, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint16]224), 0, $bytes, 148, 2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x10b), 0, $bytes, 152, 2)
    $bytes[400] = $Marker
    return ,$bytes
}
function New-SignedFixture([byte[]] $Unsigned, [string] $Path) {
    $cms = [Security.Cryptography.Pkcs.SignedCms]::new([Security.Cryptography.Pkcs.ContentInfo]::new([Text.Encoding]::UTF8.GetBytes('offline fixture')))
    $signer = [Security.Cryptography.Pkcs.CmsSigner]::new($script:testCertificate)
    $signer.IncludeOption = [Security.Cryptography.X509Certificates.X509IncludeOption]::EndCertOnly
    $cms.ComputeSignature($signer, $true)
    $encoded = $cms.Encode()
    $recordLength = $encoded.Length + 8
    $tableLength = ($recordLength + 7) -band -8
    $bytes = [byte[]]::new($Unsigned.Length + $tableLength)
    [Array]::Copy($Unsigned, $bytes, $Unsigned.Length)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$Unsigned.Length), 0, $bytes, 280, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$tableLength), 0, $bytes, 284, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint32]$recordLength), 0, $bytes, $Unsigned.Length, 4)
    [Array]::Copy([BitConverter]::GetBytes([uint16]0x200), 0, $bytes, $Unsigned.Length + 4, 2)
    [Array]::Copy([BitConverter]::GetBytes([uint16]2), 0, $bytes, $Unsigned.Length + 6, 2)
    [Array]::Copy($encoded, 0, $bytes, $Unsigned.Length + 8, $encoded.Length)
    [IO.File]::WriteAllBytes($Path, $bytes)
}
function Get-AuthenticodeSignature {
    param([string] $LiteralPath)
    [pscustomobject]@{ Status = $script:signatureStatus; StatusMessage = 'mock'; SignatureType = 'Authenticode'; SignerCertificate = $script:testCertificate }
}

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$testRoot = Join-Path $tempRoot ("Inkeys-release-artifacts-" + [Guid]::NewGuid().ToString('N'))
$rsa = [Security.Cryptography.RSA]::Create(2048)
$request = [Security.Cryptography.X509Certificates.CertificateRequest]::new('CN=Inkeys Offline Test', $rsa, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
$script:testCertificate = $request.CreateSelfSigned([DateTimeOffset]::UtcNow.AddDays(-1), [DateTimeOffset]::UtcNow.AddDays(1))
$script:signatureStatus = 'Valid'
$savedEnvironment = @{}
foreach ($name in @('GITHUB_API_URL', 'GITHUB_REPOSITORY', 'GITHUB_RUN_ID', 'GITHUB_TOKEN', 'EXE_CONFIG', 'PPTCOM_CONFIG', 'SIGN_REQUESTED', 'GITHUB_OUTPUT', 'GITHUB_STEP_SUMMARY', 'WIN32_ID', 'X64_ID', 'ARM64_ID', 'SIGNING_INPUT_ID')) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    $unsignedDir = Join-Path $testRoot 'unsigned'
    $signedDir = Join-Path $testRoot 'signed'
    New-Item -ItemType Directory -Path $unsignedDir, $signedDir | Out-Null
    $names = @('PptCOM.dll', 'Inkeys.exe', 'Inkeys64.exe', 'InkeysArm64.exe')
    for ($index = 0; $index -lt $names.Count; $index++) {
        $bytes = New-UnsignedFixture ($index + 1)
        [IO.File]::WriteAllBytes((Join-Path $unsignedDir $names[$index]), $bytes)
        New-SignedFixture $bytes (Join-Path $signedDir $names[$index])
    }
    $base = @{ UnsignedDirectory = $unsignedDir; SignedDirectory = $signedDir; SignRequested = $true; SignOutcome = 'success'; SigningPolicySlug = 'release-signing' }
    # 执行 workflow 中真实的配置解析块，核实环境变量缺失不会在回退前抛错。
    $workflow = Get-Content -LiteralPath "$PSScriptRoot/../workflows/build-windows.yml" -Raw
    $configurations = @(
        @{ Variable = 'PPTCOM_CONFIG'; Peer = 'EXE_CONFIG'; Slug = 'pptcom-dll'; PeerSlug = 'exe'; Block = [regex]::Match($workflow, '(?ms)^    - name: Resolve explicit PptCOM artifact configuration\r?\n.*?^      run: \|\r?\n(?<run>.*?)(?=^    - name:)') },
        @{ Variable = 'EXE_CONFIG'; Peer = 'PPTCOM_CONFIG'; Slug = 'exe'; PeerSlug = 'pptcom-dll'; Block = [regex]::Match($workflow, '(?ms)^      - name: Resolve explicit EXE artifact configuration\r?\n.*?^        run: \|\r?\n(?<run>.*?)(?=^      - name:)') }
    )
    $env:GITHUB_OUTPUT = Join-Path $testRoot 'config-output.txt'
    $env:SIGN_REQUESTED = 'true'
    foreach ($configuration in $configurations) {
        if (-not $configuration.Block.Success) { throw "找不到 $($configuration.Variable) 配置解析步骤" }
        $run = [scriptblock]::Create($configuration.Block.Groups['run'].Value)
        [Environment]::SetEnvironmentVariable($configuration.Peer, " $($configuration.PeerSlug) ")
        foreach ($config in @($null, '', '  ', $configuration.PeerSlug, " $($configuration.PeerSlug) ")) {
            [Environment]::SetEnvironmentVariable($configuration.Variable, $config)
            [IO.File]::WriteAllText($env:GITHUB_OUTPUT, '')
            & $run
            Assert-Equal ((Get-Content -LiteralPath $env:GITHUB_OUTPUT -Raw).Trim()) 'configured=false' "actual workflow rejects missing/duplicate $($configuration.Variable) [$config] without failure"
        }
        [Environment]::SetEnvironmentVariable($configuration.Variable, " $($configuration.Slug) ")
        [IO.File]::WriteAllText($env:GITHUB_OUTPUT, '')
        & $run
        Assert-Equal ((Get-Content -LiteralPath $env:GITHUB_OUTPUT) -join '/') "configured=true/slug=$($configuration.Slug)" "actual workflow preserves explicit $($configuration.Variable) slug"
        [Environment]::SetEnvironmentVariable($configuration.Peer, $null)
        [IO.File]::WriteAllText($env:GITHUB_OUTPUT, '')
        & $run
        Assert-Equal ((Get-Content -LiteralPath $env:GITHUB_OUTPUT) -join '/') "configured=true/slug=$($configuration.Slug)" "actual workflow allows independent $($configuration.Variable) configuration"
        $env:SIGN_REQUESTED = 'false'
        [Environment]::SetEnvironmentVariable($configuration.Variable, $null)
        [IO.File]::WriteAllText($env:GITHUB_OUTPUT, '')
        & $run
        Assert-Equal ((Get-Content -LiteralPath $env:GITHUB_OUTPUT -Raw).Trim()) 'configured=false' "actual workflow tolerates unset $($configuration.Variable) when sign=false"
        $env:SIGN_REQUESTED = 'true'
    }
    $dll = $base.Clone(); $dll.FileNames = @('PptCOM.dll'); $dll.ArtifactConfigurationSlug = 'pptcom-dll'
    $exe = $base.Clone(); $exe.FileNames = $names[1..3]; $exe.ArtifactConfigurationSlug = 'exe'
    $dllSignedDir = Join-Path $testRoot 'signed-dll'
    $exeSignedDir = Join-Path $testRoot 'signed-exe'
    New-Item -ItemType Directory -Path $dllSignedDir, $exeSignedDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $signedDir $names[0]) -Destination $dllSignedDir
    foreach ($name in $names[1..3]) { Copy-Item -LiteralPath (Join-Path $signedDir $name) -Destination $exeSignedDir }
    $dll.SignedDirectory = $dllSignedDir; $exe.SignedDirectory = $exeSignedDir
    Assert-Equal (Select-ReleaseSignature @dll).Status 'signed' 'DLL success'
    Assert-Equal (Select-ReleaseSignature @exe).Status 'signed' 'complete EXE success'
    foreach ($dllOutcome in @('success', 'failure')) {
        foreach ($exeOutcome in @('success', 'failure')) {
            $dll.SignOutcome = $dllOutcome; $exe.SignOutcome = $exeOutcome
            $actual = "$( (Select-ReleaseSignature @dll).Status )/$( (Select-ReleaseSignature @exe).Status )"
            $expected = "$(if ($dllOutcome -eq 'success') { 'signed' } else { 'failed' })/$(if ($exeOutcome -eq 'success') { 'signed' } else { 'failed' })"
            Assert-Equal $actual $expected "independent DLL/EXE $dllOutcome/$exeOutcome"
        }
    }
    $dll.SignRequested = $false; $exe.SignRequested = $false
    Assert-Equal (Select-ReleaseSignature @dll).Status 'skipped' 'skip DLL request'
    Assert-Equal (Select-ReleaseSignature @exe).Status 'skipped' 'skip EXE request'
    $dll.SignRequested = $true; $exe.SignRequested = $true
    $dll.SignOutcome = 'success'; $exe.SignOutcome = 'success'
    $dll.ArtifactConfigurationSlug = ''
    Assert-Equal (Select-ReleaseSignature @dll).Status 'failed' 'missing explicit DLL configuration'
    $dll.ArtifactConfigurationSlug = 'pptcom-dll'
    $exe.ArtifactConfigurationSlug = ''
    Assert-Equal (Select-ReleaseSignature @exe).Status 'failed' 'missing explicit EXE configuration'
    $exe.ArtifactConfigurationSlug = 'exe'
    $exe.SignedDirectory = (Join-Path $testRoot 'missing')
    Assert-Equal (Select-ReleaseSignature @exe).Status 'failed' 'missing signed output fallback'
    $exe.SignedDirectory = $dllSignedDir
    Assert-Equal (Select-ReleaseSignature @exe).Status 'failed' 'DLL cannot be used as EXE signed output'
    $exe.SignedDirectory = $exeSignedDir
    $missingPath = Join-Path $exeSignedDir 'Inkeys64.exe'
    $savedExe = [IO.File]::ReadAllBytes($missingPath)
    [IO.File]::Delete($missingPath)
    Assert-Equal (Select-ReleaseSignature @exe).Directory $unsignedDir 'incomplete EXE set uses entire unsigned group'
    [IO.File]::WriteAllBytes($missingPath, $savedExe)
    $dll.UnsignedDirectory = Join-Path $testRoot 'missing'
    Assert-Throws { Select-ReleaseSignature @dll } 'missing build/download output is fatal'
    $dll.UnsignedDirectory = $unsignedDir

    $script:signatureStatus = 'NotTrusted'
    $dll.SigningPolicySlug = 'test-signing'
    Assert-Equal (Select-ReleaseSignature @dll).Status 'signed' 'test certificate trust is separate from signature integrity'
    $dll.SigningPolicySlug = 'release-signing'
    Assert-Equal (Select-ReleaseSignature @dll).Status 'failed' 'release requires trusted certificate'
    $dll.SigningPolicySlug = 'test-signing'
    $script:signatureStatus = 'HashMismatch'
    Assert-Equal (Select-ReleaseSignature @dll).Status 'failed' 'test certificate never permits HashMismatch'
    $script:signatureStatus = 'Valid'
    $signedPath = Join-Path $dllSignedDir 'PptCOM.dll'
    $originalSigned = [IO.File]::ReadAllBytes($signedPath)
    $changed = $originalSigned.Clone(); $changed[400] = 77
    [IO.File]::WriteAllBytes($signedPath, $changed)
    Assert-Equal (Select-ReleaseSignature @dll).Status 'failed' 'different build payload is rejected'
    $changed = $originalSigned.Clone()
    $recordLength = [BitConverter]::ToUInt32($changed, 512)
    $changed[512 + $recordLength - 2] = $changed[512 + $recordLength - 2] -bxor 1
    [IO.File]::WriteAllBytes($signedPath, $changed)
    Assert-Equal (Select-ReleaseSignature @dll).Status 'failed' 'PKCS7 corruption rejected even when mock reports Valid'
    [IO.File]::WriteAllBytes($signedPath, $originalSigned)

    $env:GITHUB_API_URL = 'https://example.invalid'; $env:GITHUB_REPOSITORY = 'offline/test'; $env:GITHUB_RUN_ID = '500'; $env:GITHUB_TOKEN = 'offline-placeholder'
    $script:metadata = [pscustomobject]@{ id = 101; name = 'PptCOM[Unsigned]'; expired = $false; workflow_run = [pscustomobject]@{ id = 500 } }
    $script:deletes = @()
    $script:listRequests = @()
    function Invoke-RestMethod {
        param([string] $Uri, $Headers, [string] $Method, $ErrorAction)
        if ($Method -eq 'Delete') { $script:deletes += $Uri; return }
        if ($Uri -match '/runs/(\d+)/artifacts\?') {
            $script:listRequests += $Uri
            return [pscustomobject]@{ artifacts = @($script:metadata) }
        }
        return $script:metadata
    }
    Remove-ReleaseArtifact -ArtifactId '101' -ExpectedName 'PptCOM[Unsigned]'
    Assert-Equal $script:deletes.Count 1 'delete only exact current-run artifact ID'
    $script:metadata.workflow_run.id = 499
    Assert-Throws { Remove-ReleaseArtifact -ArtifactId '101' -ExpectedName 'PptCOM[Unsigned]' } 'cross-run same-name artifact cannot be deleted'
    $script:metadata.workflow_run.id = 500
    Assert-Throws { Remove-ReleaseArtifact -ArtifactId '101' -ExpectedName 'PptCOM[Signed]' } 'wrong-name artifact cannot be deleted'
    Assert-Equal $script:deletes.Count 1 'rejected deletes never reach API'
    Assert-Equal (Get-ReleaseRunArtifacts -Names @('PptCOM[Unsigned]')).id 101 'resolve current attempt ID'
    $script:metadata.id = 202
    Assert-Equal (Get-ReleaseRunArtifacts -Names @('PptCOM[Unsigned]')).id 202 'rerun uses replaced artifact ID'
    Assert-Equal (@($script:listRequests | Where-Object { $_ -notmatch '/runs/500/artifacts\?' }).Count) 0 'artifact lookup stays run scoped'
    Assert-Throws { Get-ReleaseRunArtifacts -Names @('Missing') } 'missing upload artifact is fatal'

    $cleanupBlock = [regex]::Match($workflow, '(?ms)^      - name: Clean current-run intermediate EXE artifacts\r?\n(?<step>.*?)(?=^      - )')
    if (-not $cleanupBlock.Success) { throw '找不到 EXE 清理步骤' }
    Assert-Equal ($cleanupBlock.Groups['step'].Value -match '(?m)^        continue-on-error: true\r?$') $true 'saved Package cleanup failure is nonfatal in actual workflow'
    $cleanupRun = [regex]::Match($cleanupBlock.Value, '(?ms)^        run: \|\r?\n(?<run>.*)')
    $env:WIN32_ID = '101'; $env:X64_ID = '202'; $env:ARM64_ID = '303'; $env:SIGNING_INPUT_ID = ''
    $env:GITHUB_STEP_SUMMARY = Join-Path $testRoot 'cleanup-summary.txt'
    $script:deletes = @()
    function Invoke-RestMethod {
        param([string] $Uri, $Headers, [string] $Method, $ErrorAction)
        $id = [regex]::Match($Uri, '/artifacts/(\d+)$').Groups[1].Value
        if ($Method -eq 'Delete') {
            if ($id -eq '202') { throw 'simulated cleanup API failure' }
            $script:deletes += $Uri
            return
        }
        $name = switch ($id) { '101' { 'Inkeys[Unsigned]' }; '202' { 'Inkeys64[Unsigned]' }; '303' { 'InkeysArm64[Unsigned]' }; default { throw 'unexpected cleanup ID' } }
        [pscustomobject]@{ id = $id; name = $name; workflow_run = [pscustomobject]@{ id = 500 } }
    }
    Assert-Throws { & ([scriptblock]::Create($cleanupRun.Groups['run'].Value)) } 'partial cleanup API failure is explicitly surfaced'
    Assert-Equal $script:deletes.Count 1 'partial cleanup retains remaining IDs after first delete'
    Assert-Equal ((Get-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Raw) -match 'simulated cleanup API failure') $true 'cleanup warning is recorded for delivered Package'
    Write-Host 'All offline release-artifact checks passed.'
} finally {
    foreach ($name in $savedEnvironment.Keys) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name]) }
    $script:testCertificate.Dispose(); $rsa.Dispose()
    $resolvedRoot = [IO.Path]::GetFullPath($testRoot)
    if (-not $resolvedRoot.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolvedRoot -Leaf) -notlike 'Inkeys-release-artifacts-*') {
        throw '拒绝清理超出本次 temp 目录的路径'
    }
    if (Test-Path -LiteralPath $resolvedRoot) { Remove-Item -LiteralPath $resolvedRoot -Recurse -Force }
}
