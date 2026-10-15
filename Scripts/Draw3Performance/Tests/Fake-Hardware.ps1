#requires -Version 5.1
param([string]$HardwareQuery,[string]$CaptureRoot)
$ErrorActionPreference='Stop'
if ($HardwareQuery -eq 'computer') { Start-Sleep -Seconds 9 }
$status='available'; $errors='[]'
if ($HardwareQuery -eq 'os') { $status='unavailable'; $errors='[{"field":"*","reason":"fixture unavailable"}]' }
if ($HardwareQuery -eq 'gpu') { $status='partial'; $errors='[{"field":"DriverVersion","reason":"fixture null"}]' }
$json='{"query":"'+$HardwareQuery+'","status":"'+$status+'","values":[{"fixture":true}],"errors":'+$errors+'}'
[IO.File]::WriteAllText((Join-Path $CaptureRoot ('hardware-'+$HardwareQuery+'.json')),$json,(New-Object Text.UTF8Encoding($true)))
exit 0
