# Research: implementation baseline validation

- Date: 2026-10-21
- Scope: existing ARM64 binaries before capture implementation; no rebuild claimed.

## Results

- `Scripts/i18n.ps1 check`: exit 0, en-US and zh-TW each 425/425, PASS.
- Existing `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` with the default sandbox TEMP: exit 1, 567 failures, predominantly configuration writes and their round-trips (including startup placement and keyboard config). This is baseline behavior before this task changes product code.
- The same executable with `TEMP` and `TMP` set to the writable repository directory `Build/ScrollFollow/TestTemp`: exit 0, `PASS animation correctness`, keyboard shortcut config 0 failures.
- Complete successful run output: `Build/ScrollFollow/baseline-headless.log` (generated, not tracked).

## Contract for subsequent checks

Use an existing writable test temp directory inside `Build/ScrollFollow/` for file-system tests. The default sandbox temp directory caused configuration persistence failures; do not modify unrelated configuration production logic to bypass this environment problem. The rerun changes only the process environment. This does not replace rebuilding or post-change regression tests.
