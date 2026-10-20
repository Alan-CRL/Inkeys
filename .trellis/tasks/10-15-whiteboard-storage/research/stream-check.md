# Streaming UInk review

Owned scope: uink_page_file.cppm/.cpp and uink_page_file_tests.cpp only.

Fixed: consumer standard includes causing MSVC imported UInkGuid operator== and std::get compilation failures; malformed public directory size/identity guard before layout indexing; duplicate/zero page IDs and append-only unchanged page/workspace/device identity gates before creating a target; header/page count consistency; release scan scratch and last decoded object before sequential page verification.

Tests added: explicit production workspaceType 1 + private workspace marker and page extension roundtrip across 100 nonempty pages; malformed directory; duplicate IDs before file creation; reorder/foreign workspace; immutable previous version; content tamper with restored file timestamp (SHA gate). Existing tests cover replacement, truncation, write failure and stale revision.

peakBufferBytes is only a serialized-buffer high-water estimate, not decoded model/allocator/process RAM. Directory remains O(page count); temporary page data is O(one page); sequential scanner does not retain all page histories. Full scan validation decodes each page sequentially and repeats skeleton work; no performance benchmark claimed.

Validation: diff whitespace review passed; parent exclusively owns MSBuild/test execution to avoid concurrent builds. Await final test result. No GUI, commit or push.

## Owned-version cleanup follow-up

Added TryRemoveOwnedUInkPageFile(directory) noexcept for the worker's recorded self-created immutable versions only. Opens READ|DELETE with FILE_SHARE_READ, rejects leaf reparse points/directories, verifies complete saved revision (volume/fileIndex/length/mtime/SHA) under held handle, then FileDispositionInfo marks that exact object. No guessed-path DeleteFile or unsafe fallback. Ownership is a caller contract; storage retains current and one backup and cleans only after successful index publication.

Tests: matching version removed; missing version returns false; same-length content tamper with restored mtime retained; same bytes and mtime at replaced path but different fileIndex retained (original retained too). Scoped UTF-8/CRLF and whitespace checks passed. Parent owns compile/test execution.

## Completed build/test diagnosis

Parent reports standalone test project compiled successfully. uink-tests.log ends with `UInk page file tests: 0 failures`; combined --uink-file-only exits 1 with 88 existing `UInk FAILED line` assertions. First is uink_tests.cpp:1761, TestFullFileSave replacing existing application-owned source via SaveUInkFile. The original remains unchanged; downstream content/recovery assertions cascade. Existing source files uink_file.cpp, uink_codec.cpp and uink_tests.cpp have no current diff.

Isolated ctypes Win32 probes (no UInk implementation linked) created fresh original/replacement files and invoked ReplaceFileW with backup and REPLACEFILE_WRITE_THROUGH. In workspace TestResults, both with no open handle and with a FILE_READ_ATTRIBUTES/all-share pin failed with ERROR_ACCESS_DENIED (5), original preserved and no backup. Repeated in system temp, matching TestFullFileSave location, also failed with error 5 and original preserved. All probe-created files/directories were cleaned without recursive deletion. This establishes a current runner/filesystem permission limitation at the underlying replacement primitive; exact ACL/token cause was not established. It does not prove every one of the 88 assertions has an identical cause. No code/config permission workaround or old-module changes applied. No suite rerun/build/GUI.

## Final verification

Parent reran the identical full UInk executable outside the sandbox through tool-approved escalation: exit 0, All UInk persistence tests passed; UInk page file tests: 0 failures. The new isolated --uink-page-file-only also passed in the sandbox. Original logs were moved to ignored TestResults/whiteboard-v1-2026-10-15/.
