# Managed PPT session/cache/owner implementation

## Boundary and evidence
Owner: managed implementer; files under PptCOM and PptCOM.Tests only (plus this research note). Native changes and full solution verification belong to root. Existing application binding, ROT enumeration, raw page reads, event/poll compatibility, old COM interface IID and method order are retained. No additional COM owner or GUI was created.

Confirmed baseline: PptCOM.cs placed RefreshPresentationDescriptorIfNeeded after the full binding/monitor pass and then Thread.Sleep(500). Events only set pending, so their descriptor work could wait until the next slow pass. This is a static scheduling bound, not a measured Office latency. presentationBindingRevision previously advanced on FullCleanup, not on every show; it could not guard a modal confirmation across End/Begin on the same binding.

## Public contract (final)
Existing coclass C44270BE-9A52-400F-AD7C-ED42050A77D8 additionally implements a separately queried IUnknown interface:

```csharp
[Guid("D7A63F98-43B8-4BA1-A875-B55A5A04DC3E")]
interface IPptCOMSessionState {
    string GetSlideShowState(long afterRevision);
    int EndSlideShowIfSession(long expectedShowSessionRevision);
}
```

GetSlideShowState returns empty string if afterRevision equals the cached stateRevision, otherwise the immutable cached JSON. It does not read Office, clone topology, or serialize. Owner-generated envelope fields are exactly:
- schemaVersion: 1
- stateRevision: positive int64, changes only when the published value changes
- showSessionRevision: per-show int64 (0 only before first show); Begin always advances it
- bindingRevision: int64 equal to descriptor.bindingRevision
- lifecycle: Active / Inactive / Unknown
- pageStatus: Valid / EndScreen / Unknown (Valid and EndScreen only under Active)
- descriptor: nested unchanged 12-field v1 descriptor

EndScreen descriptor uses status=Unavailable, currentPage=0, a validated positive totalPage, identity/HWND when readable, and no fake SlideID. Native uses pageStatus to project -1/total; it must not pass this descriptor as a valid Draw3 page target. Optional missing View.State does not block an otherwise self-consistent current slide. Unrecognized states or busy failures do not invent an end screen. The official ppSlideShowDone constant is 5; Black/White/Paused are still a valid page. Source: https://learn.microsoft.com/en-us/office/vba/api/powerpoint.ppslideshowstate (read 2026-09-25).

EndSlideShowIfSession returns 1 only when the owner invoked captured View.Exit and it returned successfully; 0 for stale/inactive/unknown/mismatched HWND; -1 for unavailable owner, failure, queue saturation, or 2000 ms timeout. Native must only change selection after 1 and its own final token revalidation. An already executing Office call can outlast the caller timeout; true lifecycle observation remains authoritative. Timed-out queued work is cancelled and cannot execute on a later wake.

## Implementation and ownership
- SlideShowSessionCache separates event generation, per-show epoch, binding revision, and published state revision. Events update lightweight staging only, then wake the existing owner; no callback JSON/topology work.
- Begin is staged Unknown until its window and existing raw state reads are installed. Owner positive probes cannot override an unfinished Begin. FinishBegin uses session+binding so an intervening page event cannot permanently lock staging; a true End/new Begin invalidates the old completion.
- Owner COM reads capture generation/session/binding. Late reads and positive lifecycle probes are rejected after changes. A valid late-bound descriptor adopts HWND (including WPS); a newly observed HWND advances the show epoch.
- End is a confirmed inactive observation. FullCleanup preserves confirmed Inactive across binding invalidation, so native cannot lose the End edge to an immediately following Unknown. Unconfirmed failures remain Unknown and never imply exit.
- Owner mailbox uses AutoResetEvent. Wakeups run descriptor refresh and guarded commands before slow maintenance, including when an event coincides with its deadline. ROT/monitor cadence remains 500 ms after each maintenance pass; wakes never advance that deadline. Existing forcePolling behavior is retained. Changed raw page values request descriptor refresh during maintenance.
- Guarded exit executes on this same owner. It checks current session and captured HWND, acquires one View, then revalidates session/generation immediately before Exit; it never replaces a captured window with a new session window. Temporary View and any malformed COM-valued scalar are released; borrowed window remains borrowed.
- Existing GetPresentationDescriptor schema and method slots remain unchanged. Same-binding busy retention additionally requires the same show epoch so a previous show's stable descriptor is not retained into a new one.

## Checks and actual outcomes
Commands ran from repository root, no interactive GUI:

```powershell
$pptManagedBuild = & "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe" -latest -products '*' -find 'MSBuild/Current/Bin/arm64/MSBuild.exe'
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
& $pptManagedBuild PptCOM.Tests/PptCOM.Tests.csproj /p:Configuration=Release /p:Platform=AnyCPU /p:SolutionDir=D:\Project\Inkeys\Repo\Inkeys-draw\ /nr:false /nologo /verbosity:minimal /fl '/flp:logfile=.trellis/tasks/09-25-ppt-ui3-scene-and-page-sync/research/managed-build-final.log;verbosity=normal'
& ./PptCOM.Tests/bin/Release/PptCOM.Tests.exe
git diff --check -- PptCOM PptCOM.Tests
```

Final build exit 0, zero warnings/errors; TlbExp succeeded and project copied DLL/TLB to Inkeys. Final test exit 0: PASS PptCOM descriptor ownership and session/owner contracts. Logs: managed-build-final.log and managed-tests-final.log. Earlier successful pre-audit logs: managed-build.log and managed-tests.log. Initial logger command used an incorrectly escaped semicolon and created an empty log directory; only that empty directory was removed before retry. No source change was used to bypass environment issues. Roslyn server pipe connections timed out at 20 seconds and compiler fallback succeeded; this contributed build delay, not a source/test failure.

Working copies retain their original UTF-8 BOM choice and CRLF; new .cs files use UTF-8 without BOM and CRLF. git diff confirms focused edits with no whole-file conversion.

Production helpers tested:
- Original PowerPoint/WPS descriptor reader acquisition/release ledger, repeated scans, busy/error/fallback paths.
- View.State 1..5, unknown state, optional missing State, busy at State/View/Slide/Count, invalid end-page total; borrowed roots and temporary release balance.
- Immutable cache, unchanged revision polling, equivalent maintenance, late page read, unknown/recovery, Begin staging, page event during Begin, stale FinishBegin and owner probe after End, binding mismatch.
- True End then rapid Begin with the same binding/HWND while native still holds old cached JSON; old exit rejected before new owner publication.
- HWND replacement and actual execution mismatch, View acquisition pumping Begin, Exit/acquisition failure, missing owner, queue timeout, shutdown release, wake without moving the ROT deadline.
- Reflection checks original 14 COM method order, old interface/class GUIDs, 12-field descriptor, public getter without Office access.

## Limits and remaining verification
Real Office/WPS end-to-end latency, idle CPU/power, event callback ordering, apartment dispatch, actual modal end/reopen, and keyboard/focus behavior: NOT VERIFIED. No real COM event timestamp or millisecond improvement is claimed. Full solution/native and hidden-window tests are root-owned.

The event path immediately invalidates old confirmations even before a new cached snapshot reaches native. The polling fallback can only observe lifecycle transitions/windows that become visible to its existing owner; a provider that suppresses Begin/End and ends/restarts between two observations while reusing the identical HWND cannot be proven to have changed sessions from those observations alone. Preserve this as a real provider validation item; do not claim that synthetic tests establish every WPS implementation's lifecycle behavior. There is no change to the Office/WPS support matrix or ROT/application selection policy.

## Final integration record

Root confirmed: full Debug|ARM64 solution, headless --no-window, managed tests, real Host hidden (including persisted cold recovery/reorder), final Bar/PageControl offscreen and diff check all exit0. Exact evidence and limitations are in verification.md. Generated .log files were preserved under D:/Project/Inkeys/Repo/Inkeys-draw/Build/Validation/ppt-ui3-scene-and-page-sync-20260925 (outside tracked task artifacts); use the same log basenames recorded above. No Office GUI/device result is implied.
