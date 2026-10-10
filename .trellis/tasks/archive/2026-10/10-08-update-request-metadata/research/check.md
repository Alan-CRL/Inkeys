# Update request metadata review

## Findings (fixed)
- File: InkeysHeadlessTests/config_metadata_tests.cpp
- Issue: The X sequence-copy assertion called Snapshot().front() without checking whether the copied sequence was empty. A copy regression could cause undefined behavior instead of a reported test failure.
- Fix: Save the snapshot once and require size()==1 before accessing front(). Only this test file was edited by the check agent; its UTF-8/LF format was preserved.

## Findings (not fixed)
- File: Inkeys/Inkeys/Other/Other.Config.cppm, Config::operator=
- Observation: Top-level group assignment copies valid X atomics/sequences correctly and copies H descriptors without acquiring a source lock or overwriting source values. Unlike the former JSON roundtrip, it preserves an in-memory Bar entry with an empty Id or an invalid Size enum exactly; the former codec rejected an empty Id and normalized an invalid Size to TwoTwo.
- Reason left unchanged: This is a judgment about legacy invalid-memory-state semantics, not a mechanical defect in the approved live-H contract. No active production producer of these invalid entries was found: file decoding rejects/normalizes them and Bar.Load normalizes fixed entries. Parent and implement agent have been informed. Recommendation: retain the simpler descriptor/state copy for valid supported states; if exact legacy invalid-state behavior is required, scope an X-only codec copy explicitly.

## Source review
- Reviewed latest PRD/design/implement/check manifests, h-binding research (old file=false channel suggestions superseded), native desktop configuration/C++/build specs, and current task diffs.
- H constructors are explicit; pointer construction checks null without dereferencing; live addresses and source mutexes have global lifetime. No eager source read occurs during inline global Config initialization.
- ReadAll/ReadMini/default/reset handlers do not read or assign H. Config assignment copies H descriptors/owned constants and X state; self-assignment and two-Config lock acquisition remain safe.
- File and Referer switches are independently routed. Disabled file output removes only the known H key; disabled sinks do not snapshot the source. Shared production sink helpers have four-combination regression coverage without dummy production schema entries.
- Schema emits id/ed/pa/we/ch, omits ev, persists all seven Info fields including braced UserId, EditionVersion, and live UpdateChannel. Only the sending Info.UserId string is stripped before URL encoding.
- setlistUpdateMutex is held only while copying the channel. Reviewed every production setlistUpdateMutex critical section in IdtMain, IdtConfiguration, Setting and Net.Update; none calls a Config method while holding that source lock. JSON, URL encoding, file writes and network requests occur after source-lock release.
- Both GetEditionInformation and DownloadEdition explicitly set Inkeys-Updater/3.0. Redirect/HTTP fallback code is unchanged.
- New tests compile/use the actual Config module, private temp JSON, synthetic IDs, X scalar/sequence copying, fixed/live/null bindings, malicious H reads, reset protection, runtime channel changes, and synchronized concurrent channel writer. No GUI or real update requests are used.
- Main-owned configuration spec contains the corresponding H contract. Other task and Win7 ULW files were not edited.

## Verification
- Lint: no configured first-party lint command was found; scoped git diff --check passes (exit 0).
- TypeCheck: PASS. Complete InkeysRepo.sln Debug|ARM64 build (solution-build.exit.txt=0) reports Build succeeded, 146 warnings, 0 errors, elapsed 1:09.86. ARM64 Debug headless build also passes (headless-host-build3.exit.txt=0, 6 warnings/0 errors). Checked final logs and exit files directly; check agent did not overlap implement builds.
- Tests: PASS. Coordinated host InkeysHeadlessTests --no-window exits 0 (headless-host-tests.exit.txt); log reports Config metadata tests: 0 failures and PASS animation correctness. This build includes the assertion guard.
- Format: existing UTF-8/BOM and CRLF checked against task's pre-change on-disk file-format-baseline.json (six files), pass. Git normalizes repository blobs to LF; comparison uses the actual disk baseline rather than treating git-blob LF as the original checkout format.
- Environment: initial restricted-runner C1902 vanished in the normal host build. MSVC C1001 on the nested generic-lambda copy expansion vanished after implement agent simplified it to equivalent top-level group assignment. The new test source reuses existing Config-item CRT define for legacy included headers. Initial restricted test run failed 11 file-write/replacement assertions (4 new, 7 existing); the identical host run passes all, confirming a runner/environment issue. No production file-writer workaround or dependency/toolchain change was made.

## Final gate

- Source review completed; no task production defect requiring a reviewer fix was found. The sequence assertion guard was fixed and included in the passing suite.
- Existing-file encoding/BOM/CRLF and scoped git diff --check were rechecked after final implementation updates, pass.
- Coordinated full solution, headless build and no-window test suite all pass; all referenced log/exit filenames are relative to this research directory and remain valid if the task is archived.
- The legacy invalid X in-memory entry copy difference remains recorded above as an explicit judgment boundary; no production caller generating such invalid entries was found.
