[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$taskDir = Split-Path -Parent $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $taskDir '../../..')).Path
$reasons = Get-Content (Join-Path $PSScriptRoot 'existing-locales-reasons.json') -Raw -Encoding UTF8 | ConvertFrom-Json -AsHashtable

function Get-Leaves {
    param([System.Collections.IDictionary]$Root, [string]$Prefix = '')
    $result = [ordered]@{}
    foreach ($key in $Root.Keys) {
        $path = if ($Prefix) { "$Prefix/$key" } else { [string]$key }
        if ($Root[$key] -is [System.Collections.IDictionary]) {
            $children = Get-Leaves -Root $Root[$key] -Prefix $path
            foreach ($child in $children.Keys) { $result[$child] = $children[$child] }
        } elseif ($Root[$key] -is [string]) {
            $result[$path] = $Root[$key]
        } else {
            throw "Unexpected non-string leaf: $path"
        }
    }
    return ,$result
}

function Convert-Cell {
    param([string]$Value)
    $escaped = [System.Net.WebUtility]::HtmlEncode($Value).Replace('|', '&#124;')
    # 显示真实换行与首尾空格，避免 Markdown 吞掉排版差异。
    $escaped = $escaped.Replace("`r", '\r').Replace("`n", '\n').Replace("`t", '\t')
    $escaped = [regex]::Replace($escaped, '^ +| +$', { param($match) '␠' * $match.Length })
    return '<code>' + $escaped + '</code>'
}

$summary = [System.Collections.Generic.List[string]]::new()
$sections = [System.Collections.Generic.List[string]]::new()
$total = 0
foreach ($locale in @('zh-CN', 'en-US', 'zh-TW')) {
    $oldPath = Join-Path $taskDir "baseline/$locale.jsonc"
    $newPath = Join-Path $repoRoot "Inkeys/src/i18n/$locale.jsonc"
    $before = Get-Leaves (Get-Content $oldPath -Raw -Encoding UTF8 | ConvertFrom-Json -AsHashtable)
    $after = Get-Leaves (Get-Content $newPath -Raw -Encoding UTF8 | ConvertFrom-Json -AsHashtable)
    $keys = [System.Collections.Generic.List[string]]::new()
    foreach ($key in $after.Keys) { $keys.Add($key) }
    foreach ($key in $before.Keys) { if (-not $after.Contains($key)) { $keys.Add($key) } }
    $rows = [System.Collections.Generic.List[string]]::new()
    $modified = 0; $added = 0; $removed = 0
    foreach ($key in $keys) {
        $hadBefore = $before.Contains($key)
        $hasAfter = $after.Contains($key)
        if ($hadBefore -and $hasAfter -and $before[$key] -ceq $after[$key]) { continue }
        if (-not $reasons.ContainsKey($key)) { throw "Missing change reason: $locale $key" }
        $type = if (-not $hadBefore) { $added++; '新增' } elseif (-not $hasAfter) { $removed++; '移除' } else { $modified++; '修改' }
        $oldValue = if ($hadBefore) { Convert-Cell $before[$key] } else { '—（原无此 key）' }
        $newValue = if ($hasAfter) { Convert-Cell $after[$key] } else { '—（已移除）' }
        $reason = [System.Net.WebUtility]::HtmlEncode([string]$reasons[$key]).Replace('|', '&#124;').Replace("`n", ' ')
        $rows.Add('| ' + $type + ' | `' + $key + '` | ' + $oldValue + ' | ' + $newValue + ' | ' + $reason + ' |')
    }
    $count = $rows.Count
    $total += $count
    $summary.Add("| $locale | $modified | $added | $removed | $count |")
    $sections.Add("## $locale`n`n共 $count 项变化。`n`n| 类型 | Key | 修改前 | 修改后 | 原因 |`n|---|---|---|---|---|`n" + ($rows -join "`n"))
}

$content = @(
    '# 现有三语翻译修改对照表',
    '',
    '对照范围：本任务实施前保存的原始快照与最终 `Inkeys/src/i18n/` 文件。仅列实际变更，未变条目省略；新增八种语言不计入此表。',
    '',
    '表中的 `\n`、`\r`、`\t` 分别代表实际换行、回车和制表符；`␠` 表示首尾空格。格式占位符按原值保留。',
    '',
    "共 **$total** 项语言文案变化（同一 key 在不同语言分别计数）。",
    '',
    '| 语言 | 修改 | 新增 | 移除 | 合计 |',
    '|---|---:|---:|---:|---:|'
) + $summary.ToArray() + @('') + $sections.ToArray() + @(
    '',
    '## 核对依据',
    '',
    '- 原始快照：`baseline/zh-CN.jsonc`、`baseline/en-US.jsonc`、`baseline/zh-TW.jsonc`。',
    '- 116 项审查报告的逐项处理与源码依据：`research/report-audit.md` 及 `research/existing-locales-fixes.md`。',
    '- 按实际资源值生成，不把保留既定地区化的文案记为修改。'
)
$outputPath = Join-Path $taskDir 'translation-changes.md'
[IO.File]::WriteAllText($outputPath, (($content -join "`r`n") + "`r`n"), [Text.UTF8Encoding]::new($false))
Write-Output "Generated $outputPath ($total changes)"
