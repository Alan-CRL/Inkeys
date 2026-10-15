#requires -Version 5.1
param([string]$Root,[string]$CommonPath,[string]$FinalScript,[string]$Behavior='complete')
$ErrorActionPreference='Stop'
$arguments='-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$FinalScript+'" -Root "'+$Root+'" -CommonPath "'+$CommonPath+'" -Behavior '+$Behavior
$child=Start-Process -FilePath (Join-Path $PSHOME 'powershell.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $Root 'fake-final.stdout.txt') -RedirectStandardError (Join-Path $Root 'fake-final.stderr.txt')
[IO.File]::WriteAllText((Join-Path $Root 'fake-final-pid.txt'),[string]$child.Id)
$child.Dispose()
exit 0
