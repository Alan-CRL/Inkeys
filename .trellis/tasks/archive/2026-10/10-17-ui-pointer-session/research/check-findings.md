# Full-scope review follow-up

Fresh trellis-check read the complete implementation before changing it. Approved fixes:

1. GetCursorPos at WM_INPUT dispatch loses historical movement while message queue is delayed. Preserve full MSG.pt/time in existing owner DispatchMessageW scope; use it for Raw and native Down metadata. Official MSG contract: cursor position/time when message was posted, not current cursor. https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msg
2. Matching only messageTime can bind an earlier unclaimed outside click in the same millisecond. Match timestamp and full screen point, retain FIFO among matching candidates; ignore unrelated same-time edges until the matching candidate or bounded pending cancellation.
3. Native Down dispatched late used wall clock while raw moves used historical ticks. Normalize posted DWORD time onto GetTickCount64 for samples; keep wall clock for hold/dwell.
4. PageControl previously accepted pen-promoted WM_MOUSE input, while Raw provider rejects promoted packets. Preserve confirmed pen legacy path with distinct source isolation and existing capture semantics; don't change Bar's existing pen filter, hooks or add WM_POINTER.

First full Debug|ARM64 solution build started around 12:35 local 2026-10-17. PptCOM DLL/TLB build completed; C++ dependency scan produced no compiler errors before review fixes were approved. Main intentionally interrupted obsolete build at 12:45 to apply those fixes; process exited1 due Ctrl-C, not reported as a source failure. Log retained at Build/ARM64/Debug/ui-pointer-session-build-interrupted.log. Final version must be rebuilt and actual tests run before delivery.

All four findings above were fixed by the independent checker before final rebuild. Added Window.MessageDispatch.h and minimal Window.cpp dispatch scope, Source::Pen, strict native/Raw screen+time binding and no-window regressions. Static diff, XML, UTF-8/BOM/CRLF checks passed. Final build started around 12:49 local; no further production edits permitted until it exits.

Confirmed out-of-scope residual: MouseHook.cpp uses private clickCollapsePending while Bar still clears ConfirmaNoMouMsgSignal. Existing Selection/clickRecover delay can still collapse the Bar; no hook files or cancellation API calls were changed. User explicitly excluded hook changes this task. Real Win7/elevated foreground/pen capture/secure desktop behavior remains device verification, not proven by static review.
