# F-008 Desktop AutoSave index resource boundary

## Current-code evidence and scope

- `Draw3.AutoSave.cpp::ReadTextFile` previously accepted any file length representable by `size_t`, then resized a `std::string` to that length before JSON parsing. `ReadIndex` had no explicit parse-depth limit; `ValidateIndex` could traverse an unbounded entries array. The attacker prerequisite is write access to the local Desktop AutoSave date directory, so this is a local availability/resource boundary, not evidence of remote exploitation.
- `CommitIndex` is the sole consumer of `ReadIndex`. It holds the existing named mutex, re-reads primary and backup, writes a durable new UInk first, validates a temporary index, then uses the existing `ReplaceFileW`/`MoveFileExW` publication path. This change does not alter those publication calls or delete existing indices/UInk.
- The implementation caps input and serialized output at 16 MiB, valid daily entries at 32,768, and JSON recursive parse depth at 64. `ReadTextFile` rejects over-limit length before allocation. `ValidateIndex` rejects over-limit entries before per-entry filesystem checks. A new entry is rejected before append when 32,768 entries already exist; a candidate whose serialized size exceeds 16 MiB is rejected before creating the temporary file. JsonCpp depth exceptions become `IndexReadState::Invalid`, so an existing valid backup can still be consulted.
- At 32,767 valid entries, the next request may reach exactly 32,768 if its serialized index remains within 16 MiB. At 32,768 entries, or if serialization would exceed 16 MiB, the accepted request's already durable UInk remains an orphan, the index is unchanged, and completion is Failed. No existing entry is truncated or removed. The bounded parser can still use more than 16 MiB of heap for a pathological *within-limit* JSON document; this limit is a meaningful cap, not a proven peak-RSS bound. Bounded fuzz and x86 memory measurements remain open.

## Red → green evidence

The isolated runner executes production-linked `Draw3.AutoSave.cpp` in `inkStrokeModelerTestTests.exe --desktop-autosave-index-bounds-only`; it creates unique test directories only. The over-limit case first commits a real index, appends 16 MiB of legal JSON whitespace, and resubmits the identical request. Before the fix the old reader accepted the index and returned Committed. After the fix it reports Failed and preserves the index byte-for-byte and the existing UInk. A second case adds 70 nested array levels to an unknown field: without a backup it fails without overwriting the primary; with a valid backup it accepts the idempotent request through that backup and leaves both files unchanged.

| Command / artifact (ignored `TestResults/release-hardening/`) | Exit | Result |
| --- | ---: | --- |
| `inkStrokeModelerTest.sln Debug|ARM64` native ARM64 MSBuild, `autosave-index-red-isolated-build.log` | 0 | Red test compiled against old production reader. |
| `inkStrokeModelerTestTests.exe --desktop-autosave-index-bounds-only`, `autosave-index-red-isolated-test.log` | 1 | Expected failure at test lines 642–643: oversized index was accepted/committed. |
| `inkStrokeModelerTest.sln Debug|ARM64`, `autosave-index-final-debug-arm64.log` | 0 | Final focused code and tests compiled. |
| `inkStrokeModelerTestTests.exe --desktop-autosave-index-bounds-only`, `autosave-index-final-isolated-test.log` | 0 | Both boundary cases and deep-primary valid-backup path passed. |
| `git diff --check` for the three owned files | 0 | No whitespace errors; UTF-8 BOM and CRLF retained. |

## Existing full-suite failure and compatibility limit

`--desktop-autosave-only` does **not** pass on this host: both before and after the fix it reports 27 assertion failures, starting with the second same-day index update (`stage=index-commit`, missing `.bak`), in `autosave-index-red-test.log` and `autosave-index-final-full-desktop-test.log`. The test's first save succeeds, but this result cannot validate repeated index replacement or the serialized-output boundary. In a separate disposable-file P/Invoke probe, `ReplaceFileW` returned `ERROR_ACCESS_DENIED` (5) in both ignored TestResults and `%TEMP%`, with flags 1 and 0; target/replacement remained intact. This points to a local API/permission or runner limitation, not yet a confirmed product defect. The [Microsoft ReplaceFileW documentation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew) separately marks `REPLACEFILE_WRITE_THROUGH` unsupported; that fact alone did not explain this host's failure because flags 0 also failed. Repeat the full suite in an unrestricted isolated test environment and on the Win7 target before upgrading either outcome to a product finding or PASS.

No GUI, real user configuration, or user document was opened or modified. The CLI subset is intentionally narrower than the full Desktop suite; it cannot establish crash recovery, durable hardware flush, or production GUI behavior.
