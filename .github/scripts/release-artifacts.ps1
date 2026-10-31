function Get-PeSecurityDirectory {
    param([byte[]] $Bytes)
    if ($Bytes.Length -lt 64 -or [BitConverter]::ToUInt16($Bytes, 0) -ne 0x5a4d) {
        throw '不是有效的 PE 文件'
    }
    $peOffset = [BitConverter]::ToInt32($Bytes, 60)
    if ($peOffset -lt 64 -or $peOffset + 24 -gt $Bytes.Length -or [BitConverter]::ToUInt32($Bytes, $peOffset) -ne 0x4550) {
        throw 'PE header 无效'
    }
    $optionalOffset = $peOffset + 24
    $optionalSize = [BitConverter]::ToUInt16($Bytes, $peOffset + 20)
    $magic = [BitConverter]::ToUInt16($Bytes, $optionalOffset)
    $directoryOffset = switch ($magic) { 0x10b { $optionalOffset + 128 }; 0x20b { $optionalOffset + 144 }; default { throw 'PE optional header 无效' } }
    if ($directoryOffset + 8 -gt $optionalOffset + $optionalSize -or $optionalOffset + $optionalSize -gt $Bytes.Length) {
        throw 'PE security directory 越界'
    }
    [pscustomobject]@{
        ChecksumOffset = $optionalOffset + 64
        DirectoryOffset = $directoryOffset
        CertificateOffset = [BitConverter]::ToUInt32($Bytes, $directoryOffset)
        CertificateSize = [BitConverter]::ToUInt32($Bytes, $directoryOffset + 4)
    }
}

function Assert-ReleaseSignedFile {
    param([string] $UnsignedPath, [string] $SignedPath, [string] $SigningPolicySlug)
    $unsignedBytes = [IO.File]::ReadAllBytes($UnsignedPath)
    $signedBytes = [IO.File]::ReadAllBytes($SignedPath)
    $original = Get-PeSecurityDirectory $unsignedBytes
    $returned = Get-PeSecurityDirectory $signedBytes
    if ($original.CertificateSize -ne 0 -or $original.CertificateOffset -ne 0) {
        throw "构建输入已含证书表：$UnsignedPath"
    }
    $alignedLength = [long](($unsignedBytes.Length + 7) -band -8)
    if ($returned.ChecksumOffset -ne $original.ChecksumOffset -or $returned.DirectoryOffset -ne $original.DirectoryOffset -or
        $returned.CertificateOffset -lt $unsignedBytes.Length -or $returned.CertificateOffset -gt $alignedLength -or
        $returned.CertificateSize -lt 8 -or [long]$returned.CertificateOffset + $returned.CertificateSize -ne $signedBytes.Length) {
        throw "返回的 PE 证书表与构建输入不匹配：$SignedPath"
    }
    for ($offset = $unsignedBytes.Length; $offset -lt $returned.CertificateOffset; $offset++) {
        if ($signedBytes[$offset] -ne 0) { throw "证书前填充不是零：$SignedPath" }
    }
    $normalized = [byte[]]::new($unsignedBytes.Length)
    [Array]::Copy($signedBytes, $normalized, $normalized.Length)
    [Array]::Clear($unsignedBytes, $original.ChecksumOffset, 4)
    [Array]::Clear($unsignedBytes, $original.DirectoryOffset, 8)
    [Array]::Clear($normalized, $returned.ChecksumOffset, 4)
    [Array]::Clear($normalized, $returned.DirectoryOffset, 8)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        if ([Convert]::ToBase64String($sha256.ComputeHash($unsignedBytes)) -ne [Convert]::ToBase64String($sha256.ComputeHash($normalized))) {
            throw "签名返回改变了构建内容：$SignedPath"
        }
    } finally { $sha256.Dispose() }
    $offset = [long]$returned.CertificateOffset
    $end = $offset + $returned.CertificateSize
    while ($offset -lt $end) {
        if ($offset + 8 -gt $end) { throw 'WIN_CERTIFICATE header 越界' }
        $length = [BitConverter]::ToUInt32($signedBytes, [int]$offset)
        if ($length -le 8 -or $offset + $length -gt $end -or [BitConverter]::ToUInt16($signedBytes, [int]$offset + 6) -ne 2) {
            throw 'WIN_CERTIFICATE 类型或长度无效'
        }
        $cmsBytes = [byte[]]::new($length - 8)
        [Array]::Copy($signedBytes, $offset + 8, $cmsBytes, 0, $cmsBytes.Length)
        $cms = [Security.Cryptography.Pkcs.SignedCms]::new()
        $cms.Decode($cmsBytes)
        if ($cms.SignerInfos.Count -eq 0) { throw 'PKCS#7 没有签名者' }
        $cms.CheckSignature($true)
        $offset += ([long]$length + 7) -band -8
    }
    if ($offset -ne $end) { throw 'WIN_CERTIFICATE 对齐无效' }

    $signature = Get-AuthenticodeSignature -LiteralPath $SignedPath
    if ($null -eq $signature.SignerCertificate -or "$($signature.SignatureType)" -ne 'Authenticode') {
        throw "缺少内嵌 Authenticode 签名：$SignedPath"
    }
    if ("$($signature.Status)" -eq 'Valid') {
        return 'trusted'
    }
    if ($SigningPolicySlug -eq 'test-signing' -and "$($signature.Status)" -eq 'NotTrusted') {
        $chain = [Security.Cryptography.X509Certificates.X509Chain]::new()
        try {
            $chain.ChainPolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::NoCheck
            $null = $chain.Build($signature.SignerCertificate)
            $trustOnly = @($chain.ChainStatus).Count -gt 0
            foreach ($status in $chain.ChainStatus) {
                if ($status.Status -notin @('UntrustedRoot', 'PartialChain')) { $trustOnly = $false }
            }
            if ($trustOnly) {
                Write-Host "测试签名完整，本机未信任证书链：$SignedPath"
                return 'test-certificate-untrusted'
            }
        } finally { $chain.Dispose() }
    }
    throw "Authenticode 校验失败：$SignedPath ($($signature.Status): $($signature.StatusMessage))"
}

function Select-ReleaseSignature {
    param(
        [string] $UnsignedDirectory, [string] $SignedDirectory, [string[]] $FileNames,
        [bool] $SignRequested, [string] $SignOutcome, [string] $SigningPolicySlug,
        [string] $ArtifactConfigurationSlug
    )
    foreach ($name in $FileNames) {
        $path = Join-Path $UnsignedDirectory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -eq 0) {
            throw "本次构建产物缺失：$path"
        }
    }
    $selection = [pscustomobject]@{ Directory = $UnsignedDirectory; Status = 'skipped'; Reason = 'sign=false' }
    if (-not $SignRequested) { return $selection }
    $selection.Status = 'failed'
    if ([string]::IsNullOrWhiteSpace($ArtifactConfigurationSlug)) {
        $selection.Reason = '未设置 Artifact Configuration slug'
    } elseif ($SignOutcome -ne 'success') {
        $selection.Reason = "签名请求未成功：$SignOutcome"
    } else {
        try {
            $files = @(Get-ChildItem -LiteralPath $SignedDirectory -File -Recurse -ErrorAction Stop)
            if ($files.Count -ne $FileNames.Count) { throw '签名返回文件数量不符' }
            foreach ($name in $FileNames) {
                $path = Join-Path $SignedDirectory $name
                if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "签名返回缺少 $name" }
                $trust = Assert-ReleaseSignedFile -UnsignedPath (Join-Path $UnsignedDirectory $name) -SignedPath $path -SigningPolicySlug $SigningPolicySlug
                Write-Host "$name : $trust"
            }
            $selection.Directory = $SignedDirectory
            $selection.Status = 'signed'
            $selection.Reason = '签名、文件集合和本次构建内容已核实'
            return $selection
        } catch {
            $selection.Reason = $_.Exception.Message
        }
    }
    Write-Host "::warning::签名失败，使用本次未签名产物：$($selection.Reason)"
    return $selection
}

function Get-ReleaseRunArtifacts {
    param([string[]] $Names)
    $headers = @{ Authorization = "Bearer $env:GITHUB_TOKEN"; 'X-GitHub-Api-Version' = '2022-11-28' }
    $artifacts = @()
    $page = 1
    do {
        $url = "$env:GITHUB_API_URL/repos/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID/artifacts?per_page=100&page=$page"
        $response = Invoke-RestMethod -Uri $url -Headers $headers -ErrorAction Stop
        $artifacts += @($response.artifacts)
        $page++
    } while (@($response.artifacts).Count -eq 100)
    foreach ($name in $Names) {
        $matches = @($artifacts | Where-Object { $_.name -eq $name -and -not $_.expired })
        if ($matches.Count -ne 1) { throw "当前 run 的 $name 产物数量不是 1" }
        $matches[0]
    }
}

function Remove-ReleaseArtifact {
    param([string] $ArtifactId, [string] $ExpectedName)
    if ($ArtifactId -notmatch '^\d+$') { throw '缺少精确 artifact ID' }
    $url = "$env:GITHUB_API_URL/repos/$env:GITHUB_REPOSITORY/actions/artifacts/$ArtifactId"
    $headers = @{ Authorization = "Bearer $env:GITHUB_TOKEN"; 'X-GitHub-Api-Version' = '2022-11-28' }
    $artifact = Invoke-RestMethod -Uri $url -Headers $headers -ErrorAction Stop
    # 即使拿到了 ID，也再次核实所属 run 和名称，防止误删别次运行的同名产物。
    if ("$($artifact.workflow_run.id)" -ne $env:GITHUB_RUN_ID -or $artifact.name -ne $ExpectedName) {
        throw "拒绝删除其他 run/名称的 artifact：$ArtifactId"
    }
    Write-Host "Deleting current-run artifact: $ExpectedName ($ArtifactId)"
    $null = Invoke-RestMethod -Uri $url -Headers $headers -Method Delete -ErrorAction Stop
}

function Assert-EmbeddedPptCom {
    param([string] $ExecutablePath, [string] $DllPath, [string] $ResourceHeaderPath = "$PSScriptRoot/../../Inkeys/Resource.h")
    if (-not ('ReleaseResourceReader' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ReleaseResourceReader {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr FindResource(IntPtr module, IntPtr name, string type);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")]
    static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")]
    static extern bool FreeLibrary(IntPtr module);
    public static byte[] Read(string path, int id) {
        // 仅作为数据文件映射，跨架构读取资源，不执行入口或 DllMain。
        IntPtr module = LoadLibraryEx(path, IntPtr.Zero, 0x2);
        if (module == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
        try {
            IntPtr resource = FindResource(module, new IntPtr(id), "DLL");
            if (resource == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
            uint size = SizeofResource(module, resource);
            IntPtr data = LockResource(LoadResource(module, resource));
            if (size == 0 || data == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
            byte[] bytes = new byte[checked((int)size)];
            Marshal.Copy(data, bytes, 0, bytes.Length);
            return bytes;
        } finally { FreeLibrary(module); }
    }
}
'@
    }
    $header = Get-Content -LiteralPath $ResourceHeaderPath -Raw
    $match = [regex]::Match($header, '(?m)^#define\s+IDR_DLL1\s+(\d+)')
    if (-not $match.Success) { throw '找不到 IDR_DLL1 资源 ID' }
    $embedded = [ReleaseResourceReader]::Read([IO.Path]::GetFullPath($ExecutablePath), [int]$match.Groups[1].Value)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try { $actual = [BitConverter]::ToString($sha256.ComputeHash($embedded)).Replace('-', '') } finally { $sha256.Dispose() }
    $expected = (Get-FileHash -LiteralPath $DllPath -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw "EXE 内嵌 PptCOM.dll 与选定 artifact 不符：$ExecutablePath" }
    Write-Host "$ExecutablePath : embedded PptCOM.dll SHA256=$actual"
}
