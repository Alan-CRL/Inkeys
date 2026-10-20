param([Parameter(Mandatory=$true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$pptOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $pptOutput -Force | Out-Null
$pptControl = Join-Path $pptOutput 'ppt-control.json'
$pptStatus = Join-Path $pptOutput 'ppt-status.json'
$pptDeckPath = Join-Path $pptOutput 'canvas-freeze-three-pages.pptx'
if (Get-Process POWERPNT -ErrorAction SilentlyContinue) { throw 'Existing PowerPoint process: preserve user session.' }
$ppt = $null; $deck = $null; $show = $null
$pptLastSequence = -1
try {
    $ppt = New-Object -ComObject PowerPoint.Application
    $ppt.Visible = -1
    $deck = $ppt.Presentations.Add(-1)
    for ($page = 1; $page -le 3; $page++) {
        $slide = $deck.Slides.Add($page, 12)
        $slide.FollowMasterBackground = 0
        $slide.Background.Fill.ForeColor.RGB = 16777215
        $title = $slide.Shapes.AddTextbox(1, 40, 30, 600, 70)
        $title.TextFrame.TextRange.Text = "Canvas recovery test - page $page"
        $title.TextFrame.TextRange.Font.Size = 28
        $title.TextFrame.TextRange.Font.Color.RGB = 0
    }
    $deck.SaveAs($pptDeckPath, 24)
    $deck.SlideShowSettings.ShowType = 1
    $show = $deck.SlideShowSettings.Run()
    $pptStarted = [DateTime]::UtcNow
    while (([DateTime]::UtcNow - $pptStarted).TotalMinutes -lt 30) {
        if (Test-Path -LiteralPath $pptControl) {
            try { $request = Get-Content -LiteralPath $pptControl -Raw | ConvertFrom-Json } catch { $request = $null }
            if ($request -and $request.sequence -ne $pptLastSequence) {
                $pptLastSequence = $request.sequence
                switch ($request.action) {
                    'goto' { $show.View.GotoSlide([int]$request.page, -1) }
                    'next' { $show.View.Next() }
                    'previous' { $show.View.Previous() }
                    'black' { $show.View.State = 3 }
                    'resume' { $show.View.State = 1 }
                    'end' { $show.View.Exit(); $show = $null }
                    'restart' { if ($show) { $show.View.Exit() }; $show = $deck.SlideShowSettings.Run() }
                    'stop' { break }
                }
                if ($request.action -eq 'stop') { break }
            }
        }
        $currentPage = 0; $state = 0; $showHwnd = 0
        if ($show) {
            try { $currentPage = $show.View.Slide.SlideIndex; $state = $show.View.State; $showHwnd = $show.HWND } catch { }
        }
        @{ sequence=$pptLastSequence; currentPage=$currentPage; viewState=$state; showHwnd=$showHwnd; version=$ppt.Version; deck=$pptDeckPath } |
            ConvertTo-Json | Set-Content -LiteralPath $pptStatus -Encoding utf8
        Start-Sleep -Milliseconds 100
    }
} catch {
    $_ | Out-String | Set-Content -LiteralPath (Join-Path $pptOutput 'ppt-error.txt')
    throw
} finally {
    if ($show) { try { $show.View.Exit() } catch { } }
    if ($deck) { try { $deck.Saved = -1; $deck.Close() } catch { } }
    if ($ppt) { try { $ppt.Quit() } catch { } }
}
