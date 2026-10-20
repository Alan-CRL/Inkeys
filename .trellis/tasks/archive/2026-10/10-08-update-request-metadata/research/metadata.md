# Research: Update request metadata

- Query: Smallest change to remove ev, upload id without braces, append live ch, and unify updater User-Agent.
- Scope: internal
- Date: 2026-10-08

## Findings

### Files found and exact affected production files

1. `Inkeys/Inkeys/Other/Other.Config.cppm` — schema upload flags and names; change UserId to Upload/"id" and EditionVersion to NoUpload/"NaN". Keep both existing fields, default values, types, and H classification.
2. `Inkeys/Inkeys/Other/Other.Config.cpp` — UploadInfoHandler owns value conversion and URL encoding; remove braces from the local `valueText` for Info.UserId before encoding.
3. `Inkeys/Inkeys/Net/Net.Update.cpp` — GetRefererInfo assembles the final Referer; append encoded channel from existing GetUpdateTargetSnapshot().channel.
4. `Inkeys/Inkeys/Net/Net.Update.Download.cpp` — add `User-Agent: Inkeys-Updater/3.0` to GetEditionInformation headers, matching the already fixed package-download UA.

Read-only related files: `Inkeys/IdtConfiguration.h/.cpp` own setlist and its synchronization/persistence; `Inkeys/Inkeys/UI/Setting/Setting.cpp` changes the channel; `Inkeys/Inkeys/Conv/Conv.Text.cppm` owns the existing URL encoder; `Inkeys/IdtMain.cpp` initializes identity and reads Config before spawning the update thread; `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj` compiles Config and text modules but not the production Net.Update modules.

### Schema and identity handling

- Schema evidence: Other.Config.cppm:259-264 lists UserId, EditionVersion, EditionDate, ProgramArchitecture, TargetArchitecture, WindowsEdition. Toggling only upload flags does not alter persistence.
- H is a stored field initialized from a global default, **not** a live getter. Other.Config.cppm:348-349 creates ordinary value members; :379-380 distinguishes X vs H only by canReadFromDocument. H fields are write-only to disk by normal document read, and defaults are applied from globals on Config::ReadImpl (Other.Config.cpp:737-753). Global config construction may precede runtime identity initialization, but userId is generated at IdtMain.cpp:2081-2082, config.ReadAll runs at :2437, and the update thread starts at :2691.
- Other.Config.cpp:610 converts a stored field into a local string; :611 normalizes line breaks; :614-617 URL-encodes and adds `key=value;`. Strip `{` and `}` from the local string before :616. Do not mutate Info.UserId, ::userId, schema default, or WriteDocumentHandler.
- The smallest explicit condition is `JoinPath(groupPath, name) == "Info.UserId"` (the handler already has groupPath and JoinPath). Erase braces only for that field using existing standard library facilities, with a brief Chinese comment explaining server header requirements and persistence preservation. A new upload-mode enum would also work but is unnecessary for one field and broadens the schema contract.
- Conv.Text.cppm:39-46 preserves ASCII alphanumeric and `-_.~`; therefore a stripped GUID keeps all hex digits and four hyphens unchanged and emits neither braces nor `%7B` / `%7D`.

### Live channel and locking

- ch belongs in `Net.Update.cpp::GetRefererInfo`, **not** a new H schema entry. The channel fact source remains `setlist.UpdateChannel` in deploy.json; a stored H field would freeze its value until a Config defaults/read operation and create a second cached source. No migration or new persistence field is needed.
- GetUpdateTargetSnapshot (Net.Update.cpp:44-47) already reads channel under shared_lock(setlistUpdateMutex), returning by value and releasing the lock before return. UI write entry points use unique_lock (Setting.cpp:378-382, :696-699); config reading/writing snapshots the same state (IdtConfiguration.cpp:318-322, :528-536). Net.Update.cpp:639 can also replace the channel following server-selected fallback.
- Recommended body shape: obtain `string referer = Inkeys::config.GetUploadInfo();`, then `const auto updateTarget = GetUpdateTargetSnapshot();`, then append `"ch=" + StringToUrlencode(updateTarget.channel) + ";"`. Existing `import Inkeys.Conv.Text` at Net.Update.cpp:20 provides the encoder.
- Config::GetUploadInfo has shared_lock(rwMutex) at Other.Config.cpp:721 and returns after that lock is released. The separate snapshot call therefore **does not nest Config rwMutex with setlistUpdateMutex**. Do not add any setlist access or lock inside Config schema traversal. No network/file I/O is performed under either lock.
- A change immediately after a channel snapshot cannot alter the in-flight request. Each later GetRefererInfo invocation takes a new snapshot; this meets the latest-value requirement for subsequent calls. Do not change GetEditionInfo's channel selection arguments or existing update retry behavior.
- Both update-check and package download call GetRefererInfo (Net.Update.cpp:179 and :415), so both include id/ed/pa/we/ch. Final schema iteration order gives id, ed, pa, we, with ch appended last.

### User-Agent and preserved behavior

- GetEditionInformation initializes explicit request headers at Net.Update.Download.cpp:133-140, but has no UA. Add the existing download UA line from :184 immediately before Referer, following the existing layout.
- Leave URLs, FetchUpdate, HTTPS/HTTP fallback, body limits, and cross-host Referer deletion untouched. No change to cpp-httplib or dependency versions is necessary (project pinned cpp-httplib 0.22.0).

### Related specs

- `.trellis/spec/native-desktop/index.md`: Config ownership and complete solution dependency validation.
- `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`: two existing Config systems remain distinct; existing SetListStruct fields keep their owner.
- `.trellis/spec/native-desktop/cpp-conventions.md`: minimal diff, preserve encoding/EOL, Chinese comments for non-obvious behavior, use existing state synchronization.
- `.trellis/spec/native-desktop/build-and-compatibility.md`: InkeysRepo.sln Debug|ARM64, native ARM64 MSBuild, PptCOM dependency, no dependency/toolchain edits.

### Smallest appropriate validation

1. Review the four-file diff and byte-level BOM/newline preservation; confirm schema persistence/H defaults are unchanged, only upload flags/names change, and id sanitation touches the local string.
2. Static assertions/check of schema keys and both UA header blocks; test brace removal using sample braced GUID, plain GUID, empty/Error values. Confirm encoding preserves the GUID and escapes a synthetic channel containing separator characters. These sample checks supplement source inspection and do not prove full product execution.
3. Build full `InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64` using installed ARM64-native MSBuild located through VS discovery. Normalize PATH and disable node reuse in the same PowerShell invocation, permit at least five minutes, examine exit code and first meaningful error. Do not build the product vcxproj alone.
4. Existing Headless target includes production Config modules (InkeysHeadlessTests.vcxproj:104-107), but searches found no GetUploadInfo or Info.UserId coverage in current source tests. It also does not compile Net.Update, so running the existing suite would not prove ch/UA behavior. A new dedicated test harness/project or Net import expansion is disproportionate to this small task. Existing `--no-window` option is available at animation_tests.cpp:1640 if broader tests are independently needed, but no GUI or real-server identity request is authorized.

## External References

No external lookup was needed for implementation: this task follows existing project schema, synchronization, and request-header patterns. External 123 log visibility, cache rules, and anti-abuse policy remain unverified and are outside this implementation scope.

## Caveats / Not Found

- No existing dedicated metadata validation script or native upload-info test entry was found. Product --config-json-boundary-test tests malformed file preservation, not upload metadata.
- Findings are from current source inspection; no builds, GUI, git operations, server requests, or production edits were performed by this researcher.
- H defaults are refreshed on Config reads/resets, not on each upload. This is suitable for current device metadata but is why live update channel must remain outside schema storage.
- Keep Win7 ULW work and others' changes intact. Research ownership is this file only.
