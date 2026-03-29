[CmdletBinding()]
param(
	[Parameter(Position = 0)]
	[ValidateSet('check', 'sync')]
	[string]$Command = 'check'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = Split-Path -Parent $PSScriptRoot
$I18nDir = Join-Path $RepoRoot '智绘教/src/i18n'
$HeaderPath = Join-Path $RepoRoot '智绘教/IdtI18nKeys.g.h'
$BaseLanguage = 'zh-CN'
$BasePath = Join-Path $I18nDir "$BaseLanguage.jsonc"

function Remove-JsoncComments {
	param([string]$InputText)

	$lines = $InputText -split "\r?\n"
	$output = [System.Collections.Generic.List[string]]::new()

	foreach ($line in $lines) {
		$commentPos = -1
		$inString = $false

		for ($i = 0; $i -lt $line.Length; $i++) {
			if ($line[$i] -eq '"') {
				$backslashCount = 0
				$j = $i - 1
				while ($j -ge 0 -and $line[$j] -eq '\') {
					$backslashCount++
					$j--
				}

				if (($backslashCount % 2) -eq 0) {
					$inString = -not $inString
				}
			}

			if (-not $inString -and ($i + 1) -lt $line.Length -and $line[$i] -eq '/' -and $line[$i + 1] -eq '/') {
				$commentPos = $i
				break
			}
		}

		if ($commentPos -ge 0) {
			$output.Add($line.Substring(0, $commentPos))
		}
		else {
			$output.Add($line)
		}
	}

	return ($output -join "`n")
}

function Get-HeaderCommentLines {
	param([string]$RawText)

	$lines = $RawText -split "\r?\n"
	$headerLines = [System.Collections.Generic.List[string]]::new()

	foreach ($line in $lines) {
		if ($line.TrimStart().StartsWith('//')) {
			$headerLines.Add($line)
			continue
		}

		if ([string]::IsNullOrWhiteSpace($line) -and $headerLines.Count -gt 0) {
			continue
		}

		break
	}

	return $headerLines
}

function Get-JsonLiteral {
	param([string]$Value)

	return ($Value | ConvertTo-Json -Compress)
}

function Load-JsoncOrdered {
	param([string]$Path)

	$rawText = Get-Content -LiteralPath $Path -Raw -Encoding UTF8
	$cleanText = Remove-JsoncComments $rawText
	$parsed = $cleanText | ConvertFrom-Json -AsHashtable

	if (-not ($parsed -is [System.Collections.IDictionary])) {
		throw "JSON 根节点必须是对象：$Path"
	}

	return @{
		RawText = $rawText
		Root = $parsed
	}
}

function Get-FlattenedEntries {
	param(
		[Parameter(Mandatory = $true)]$Node,
		[string]$Prefix = ''
	)

	$entries = [System.Collections.Generic.List[object]]::new()

	if ($Node -is [string]) {
		$entries.Add([pscustomobject]@{
				Key = $Prefix
				Value = $Node
			})
		return $entries
	}

	if (-not ($Node -is [System.Collections.IDictionary])) {
		throw "仅支持字符串叶子节点和对象节点。"
	}

	foreach ($key in $Node.Keys) {
		$childPrefix = if ([string]::IsNullOrEmpty($Prefix)) { [string]$key } else { "$Prefix/$key" }
		foreach ($entry in (Get-FlattenedEntries -Node $Node[$key] -Prefix $childPrefix)) {
			$entries.Add($entry)
		}
	}

	return $entries
}

function Get-PlaceholderTokens {
	param([string]$Value)

	return [regex]::Matches($Value, '\{[^}]*\}') | ForEach-Object { $_.Value }
}

function Get-NewlineCount {
	param([string]$Value)

	return ([regex]::Matches($Value, "`n")).Count
}

function Test-MapContainsKey {
	param(
		$Map,
		[string]$Key
	)

	return ($Map -is [System.Collections.IDictionary]) -and $Map.Contains($Key)
}

function Sync-Node {
	param(
		[Parameter(Mandatory = $true)]$BaseNode,
		$TargetNode
	)

	if ($BaseNode -is [string]) {
		if ($TargetNode -is [string]) {
			return $TargetNode
		}

		return ''
	}

	if (-not ($BaseNode -is [System.Collections.IDictionary])) {
		throw "仅支持字符串叶子节点和对象节点。"
	}

	$result = [ordered]@{}
	foreach ($key in $BaseNode.Keys) {
		$targetChild = $null
		if (Test-MapContainsKey -Map $TargetNode -Key $key) {
			$targetChild = $TargetNode[$key]
		}

		$result[$key] = Sync-Node -BaseNode $BaseNode[$key] -TargetNode $targetChild
	}

	return $result
}

function Get-JsonNodeLines {
	param(
		[Parameter(Mandatory = $true)]$Node,
		[int]$Indent = 0
	)

	if ($Node -is [string]) {
		throw '字符串节点应由调用方直接写出。'
	}

	if (-not ($Node -is [System.Collections.IDictionary])) {
		throw "仅支持字符串叶子节点和对象节点。"
	}

	$indentText = ' ' * $Indent
	$childIndentText = ' ' * ($Indent + 2)
	$lines = [System.Collections.Generic.List[string]]::new()
	$lines.Add("$indentText{")

	$keys = @($Node.Keys)
	for ($index = 0; $index -lt $keys.Count; $index++) {
		$key = [string]$keys[$index]
		$value = $Node[$key]
		$suffix = if ($index -lt ($keys.Count - 1)) { ',' } else { '' }

		if ($value -is [string]) {
			$lines.Add("$childIndentText$(Get-JsonLiteral $key): $(Get-JsonLiteral $value)$suffix")
			continue
		}

		$lines.Add("$childIndentText$(Get-JsonLiteral $key):")
		$childLines = Get-JsonNodeLines -Node $value -Indent ($Indent + 2)
		if ($suffix) {
			$childLines[$childLines.Count - 1] = $childLines[$childLines.Count - 1] + $suffix
		}

		foreach ($childLine in $childLines) {
			$lines.Add($childLine)
		}
	}

	$lines.Add("$indentText}")
	return $lines
}

function Write-JsoncFile {
	param(
		[string]$Path,
		[string[]]$HeaderLines,
		$Root
	)

	$allLines = [System.Collections.Generic.List[string]]::new()
	foreach ($line in $HeaderLines) {
		$allLines.Add($line)
	}
	foreach ($line in (Get-JsonNodeLines -Node $Root -Indent 0)) {
		$allLines.Add($line)
	}


	$content = ($allLines -join "`r`n") + "`r`n"
	Set-Content -LiteralPath $Path -Value $content -Encoding utf8NoBOM -NoNewline
}

function Convert-SegmentToIdentifier {
	param([string]$Segment)

	$sanitized = ($Segment -replace '[^A-Za-z0-9_]', '_')
	if ([string]::IsNullOrEmpty($sanitized)) {
		$sanitized = '_'
	}
	if ($sanitized -match '^[0-9]') {
		$sanitized = "K_$sanitized"
	}
	elseif ($sanitized -notmatch '^[A-Za-z_]') {
		$sanitized = "_$sanitized"
	}

	return $sanitized
}

function Get-NodeTypeName {
	param([string[]]$Segments)

	$identifierSegments = @($Segments | ForEach-Object { Convert-SegmentToIdentifier -Segment $_ })
	return 'Node__' + ($identifierSegments -join '__')
}

function Test-IdentifierConflicts {
	param(
		$Node,
		[string]$Scope = 'I18nKey'
	)

	if (-not ($Node -is [System.Collections.IDictionary])) {
		return
	}

	$identifierToKey = @{}
	foreach ($key in $Node.Keys) {
		$identifier = Convert-SegmentToIdentifier -Segment ([string]$key)
		if ($identifierToKey.ContainsKey($identifier) -and $identifierToKey[$identifier] -ne $key) {
			throw "键名生成的标识符冲突：$Scope.$identifier => $($identifierToKey[$identifier]) / $key"
		}

		$identifierToKey[$identifier] = $key
		if ($Node[$key] -is [System.Collections.IDictionary]) {
			Test-IdentifierConflicts -Node $Node[$key] -Scope "$Scope.$identifier"
		}
	}
}

function Add-KeyNodeLines {
	param(
		[System.Collections.Generic.List[string]]$Lines,
		$Node,
		[string[]]$PathSegments,
		[int]$Indent
	)

	$indentText = '    ' * $Indent

	foreach ($key in $Node.Keys) {
		$segment = [string]$key
		$identifier = Convert-SegmentToIdentifier -Segment $segment
		$currentPathSegments = @($PathSegments + $segment)
		$fullPath = $currentPathSegments -join '/'
		$value = $Node[$key]

		if ($value -is [string]) {
			$Lines.Add("$indentText" + "const char* $identifier = `"$fullPath`";")
			continue
		}

		$typeName = Get-NodeTypeName -Segments $currentPathSegments
		$Lines.Add("$indentText" + "struct $typeName")
		$Lines.Add("$indentText{")
		Add-KeyNodeLines -Lines $Lines -Node $value -PathSegments $currentPathSegments -Indent ($Indent + 1)
		$Lines.Add("$indentText" + "} $identifier{};")
	}
}

function Write-KeyHeader {
	param($BaseRoot)

	Test-IdentifierConflicts -Node $BaseRoot

	$lines = [System.Collections.Generic.List[string]]::new()
	$lines.Add('#pragma once')
	$lines.Add('')
	$lines.Add('// Generated by scripts/i18n.ps1 sync. Do not edit manually.')
	$lines.Add('')
	$lines.Add('inline constexpr struct I18nKeyRoot')
	$lines.Add('{')
	Add-KeyNodeLines -Lines $lines -Node $BaseRoot -PathSegments @() -Indent 1
	$lines.Add('} I18nKey{};')
	$lines.Add('')

	$content = $lines -join "`r`n"
	Set-Content -LiteralPath $HeaderPath -Value $content -Encoding utf8NoBOM -NoNewline
}
function Compare-Language {
	param(
		$BaseRoot,
		$TargetRoot,
		[string]$LanguageName
	)

	$baseEntries = Get-FlattenedEntries -Node $BaseRoot
	$targetEntries = Get-FlattenedEntries -Node $TargetRoot

	$baseKeys = @($baseEntries | ForEach-Object { $_.Key })
	$targetKeys = @($targetEntries | ForEach-Object { $_.Key })
	$baseKeySet = [System.Collections.Generic.HashSet[string]]::new([string[]]$baseKeys)
	$targetKeySet = [System.Collections.Generic.HashSet[string]]::new([string[]]$targetKeys)

	$missing = @($baseKeys | Where-Object { -not $targetKeySet.Contains($_) })
	$extra = @($targetKeys | Where-Object { -not $baseKeySet.Contains($_) })

	$commonKeys = @($baseKeys | Where-Object { $targetKeySet.Contains($_) })
	$baseMap = @{}
	$targetMap = @{}

	foreach ($entry in $baseEntries) {
		$baseMap[$entry.Key] = $entry.Value
	}
	foreach ($entry in $targetEntries) {
		$targetMap[$entry.Key] = $entry.Value
	}

	$placeholderMismatch = [System.Collections.Generic.List[object]]::new()
	$newlineMismatch = [System.Collections.Generic.List[object]]::new()
	foreach ($key in $commonKeys) {
		$baseTokens = @(Get-PlaceholderTokens -Value $baseMap[$key])
		$targetTokens = @(Get-PlaceholderTokens -Value $targetMap[$key])
		$hasPlaceholderMismatch = $baseTokens.Count -ne $targetTokens.Count

		if (-not $hasPlaceholderMismatch) {
			for ($index = 0; $index -lt $baseTokens.Count; $index++) {
				if ($baseTokens[$index] -ne $targetTokens[$index]) {
					$hasPlaceholderMismatch = $true
					break
				}
			}
		}

		if ($hasPlaceholderMismatch) {
			$placeholderMismatch.Add([pscustomobject]@{
					Key = $key
					Base = $baseTokens
					Target = $targetTokens
				})
		}

		$baseNewlineCount = Get-NewlineCount -Value $baseMap[$key]
		$targetNewlineCount = Get-NewlineCount -Value $targetMap[$key]
		if ($baseNewlineCount -ne $targetNewlineCount) {
			$newlineMismatch.Add([pscustomobject]@{
					Key = $key
					Base = $baseNewlineCount
					Target = $targetNewlineCount
				})
		}
	}

	$hasOrderMismatch = $false
	if ($missing.Count -eq 0 -and $extra.Count -eq 0 -and $baseKeys.Count -eq $targetKeys.Count) {
		for ($index = 0; $index -lt $baseKeys.Count; $index++) {
			if ($baseKeys[$index] -ne $targetKeys[$index]) {
				$hasOrderMismatch = $true
				break
			}
		}
	}

	return [pscustomobject]@{
		Language = $LanguageName
		Missing = @($missing)
		Extra = @($extra)
		PlaceholderMismatch = @($placeholderMismatch)
		NewlineMismatch = @($newlineMismatch)
		OrderMismatch = $hasOrderMismatch
	}
}

function Get-TargetLanguageFiles {
	return Get-ChildItem -LiteralPath $I18nDir -Filter '*.jsonc' |
		Where-Object { $_.BaseName -ne $BaseLanguage } |
		Sort-Object Name
}

function Write-CheckResult {
	param($Result)

	Write-Host "[check] $($Result.Language)"

	if ($Result.Missing.Count -eq 0 -and
		$Result.Extra.Count -eq 0 -and
		$Result.PlaceholderMismatch.Count -eq 0 -and
		$Result.NewlineMismatch.Count -eq 0 -and
		-not $Result.OrderMismatch) {
		Write-Host '  PASS'
		return
	}

	if ($Result.Missing.Count -gt 0) {
		Write-Host '  Missing keys:'
		foreach ($key in $Result.Missing) {
			Write-Host "    $key"
		}
	}

	if ($Result.Extra.Count -gt 0) {
		Write-Host '  Extra keys:'
		foreach ($key in $Result.Extra) {
			Write-Host "    $key"
		}
	}

	if ($Result.PlaceholderMismatch.Count -gt 0) {
		Write-Host '  Placeholder mismatch:'
		foreach ($item in $Result.PlaceholderMismatch) {
			Write-Host "    $($item.Key)"
		}
	}

	if ($Result.NewlineMismatch.Count -gt 0) {
		Write-Host '  Newline mismatch:'
		foreach ($item in $Result.NewlineMismatch) {
			Write-Host "    $($item.Key) (base=$($item.Base), target=$($item.Target))"
		}
	}

	if ($Result.OrderMismatch) {
		Write-Host '  Order mismatch detected.'
	}
}

function Invoke-I18nCheck {
	$base = Load-JsoncOrdered -Path $BasePath
	$hasFailure = $false

	foreach ($file in (Get-TargetLanguageFiles)) {
		$target = Load-JsoncOrdered -Path $file.FullName
		$result = Compare-Language -BaseRoot $base.Root -TargetRoot $target.Root -LanguageName $file.BaseName
		Write-CheckResult -Result $result

		if ($result.Missing.Count -gt 0 -or
			$result.Extra.Count -gt 0 -or
			$result.PlaceholderMismatch.Count -gt 0 -or
			$result.NewlineMismatch.Count -gt 0 -or
			$result.OrderMismatch) {
			$hasFailure = $true
		}
	}

	if ($hasFailure) {
		throw 'i18n check failed.'
	}
}

function Invoke-I18nSync {
	$base = Load-JsoncOrdered -Path $BasePath
	$baseHeaderLines = @(Get-HeaderCommentLines -RawText $base.RawText)

	foreach ($file in (Get-TargetLanguageFiles)) {
		$target = Load-JsoncOrdered -Path $file.FullName
		$headerLines = @(Get-HeaderCommentLines -RawText $target.RawText)
		if ($headerLines.Count -eq 0) {
			$headerLines = $baseHeaderLines
		}

		$syncedRoot = Sync-Node -BaseNode $base.Root -TargetNode $target.Root
		Write-JsoncFile -Path $file.FullName -HeaderLines $headerLines -Root $syncedRoot
		Write-Host "[sync] Updated $($file.Name)"
	}

	Write-KeyHeader -BaseRoot $base.Root
	Write-Host "[sync] Updated $(Split-Path -Leaf $HeaderPath)"

	Invoke-I18nCheck
}

switch ($Command) {
	'check' { Invoke-I18nCheck }
	'sync' { Invoke-I18nSync }
	default { throw "Unsupported command: $Command" }
}
