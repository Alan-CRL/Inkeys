# Research: H live/fixed source binding and independent output switches

> 最新用户确认：UpdateChannel 的文件开关与 Referer 开关都设为 true；main.json 保存实时通道副本，deploy.json 原有持久化继续保留，事实源仍是 setlist.UpdateChannel。本文早期关于 ch file=false 的建议已被这项明确要求替代。

- Query: Minimal safe H upgrade for live addresses and fixed values, independent file/Referer output, ev persistence without upload, and schema-based live channel.
- Scope: internal
- Date: 2026-10-08

## Findings

### Superseded design

This report supersedes `research/metadata.md` recommendations to keep H as cached values and append ch in Net.Update.GetRefererInfo. The user has explicitly expanded scope to upgrade H source semantics. GetRefererInfo should remain its current one-line Config::GetUploadInfo delegation; channel now belongs in the schema.

### Existing behavior and affected files

- `Inkeys/Inkeys/Other/Other.Config.cppm:254-264`: H entries are all Info metadata, currently ordinary std::wstring members initialized from global values; X and H share the same declaration macro (:347-349).
- `Other.Config.cppm:379-380`: the only H distinction is canReadFromDocument=false. H is not currently a live getter.
- `Other.Config.cpp:515-519`: DefaultValueHandler assigns all selected values, including H, from current default expressions.
- `Other.Config.cpp:543-554`: ReadDocumentHandler normally skips H, but includeWriteOnly=true permits H assignment.
- `Other.Config.cpp:581-584`: WriteDocumentHandler currently writes every field; no file-output switch exists.
- `Other.Config.cpp:606-617`: UploadInfoHandler only writes Upload values and uses a local valueText before URL encoding.
- `Other.Config.cppm:397-413`: Config::operator= locks both Config instances, serializes other with OverlayDocument, and calls ApplyDocument(...,true), including the current cached H values.
- `IdtMain.cpp:2501`: configOnce=config uses this operator. Source-bound H will intentionally remain live in both instances; configOnce's ordinary X fields remain historical copied state.
- Full production-source search found no external direct consumers of `config.Info.*` or the six H member names. Changing those H members to a read-only binding type therefore has no discovered call-site edits. This is still a C++ API type change; preserve member names and document the new Snapshot access.
- `Net.Update.Download.cpp:135-140` needs the fixed UA added; :184 already uses Inkeys-Updater/3.0 for download. No Net.Update.cpp change is necessary in the new design.

Minimum production file set: Other.Config.cppm, Other.Config.cpp, Net.Update.Download.cpp. Config's global module fragment needs the existing `IdtConfiguration.h` declarations to bind the channel address and mutex. Keep the schema tutorial comments in Other.Config.cppm consistent with the new H syntax; leave X examples and all unrelated formatting intact.

### Recommended exact H interface

Keep X unchanged; its current file persistence stays enabled and its existing ConfigUploadMode controls Referer output. Introduce independent literal bool switches for H:

```cpp
H(writeToFile, writeToReferer, refererName, valueType, valueName, source)
```

Example schema entries (illustrative names for the typed binding helper):

```cpp
H(true,  true,  "id",  std::wstring, UserId,              &::userId)
H(true,  false, "ev",  std::wstring, EditionVersion,      &::editionVersion)
H(true,  true,  "ed",  std::wstring, EditionDate,         &::editionDate)
H(true,  true,  "pa",  std::wstring, ProgramArchitecture, &::programArchitecture)
H(true,  false, "NaN", std::wstring, TargetArchitecture,  &::targetArchitecture)
H(true,  true,  "we",  std::wstring, WindowsEdition,      &::windowsEdition)
H(false, true,  "ch",  std::string,  UpdateChannel,
  ConfigDetail::BindConfigSource(&::setlist.UpdateChannel, &::setlistUpdateMutex))
```

The channel descriptor expression contains its comma inside parentheses, so it remains one macro argument. Do not add a second channel persistence path: UpdateChannel remains owned by deploy.json/SetListStruct, hence its H file flag is false. EditionVersion remains true/false, preserving main.json Info.EditionVersion.

A fixed-value example is `H(true,false,"NaN",std::wstring,SomeFixedName,std::wstring{L"fixed"})`; this illustrates supported syntax, **not** a request to add dummy production schema fields.

### Typed source implementation shape

Use a small `ConfigReadOnlyValue<T>` binding object for H members, initialized by the schema source. It stores either an owned fixed T, or a const T* with an optional shared_mutex*. Constructors accept an exact fixed T/value convertible to T, a typed const T*, or the typed pointer+mutex descriptor returned by BindConfigSource. Avoid void*, reinterpret_cast, untyped strings, std::function, a callback registry, or ownership of source globals.

`Snapshot() const -> T` returns a copy. For a live pointer with mutex, it takes shared_lock, copies the pointed-to value, releases the source lock, then returns the copy. Encoding and JsonCpp work occur after releasing that lock. Fixed values do not acquire a source lock. Do not dereference pointers during global Config construction: capture addresses only, because global inline Config construction can precede source string/setlist construction. All current sources have program lifetime; source lifetime must outlive the binding. Exclude/null-check unsupported null pointer sources explicitly rather than silently treating them as valid live sources.

Bindings expose read-only snapshot semantics; assigning a parsed/default value must never write through a source pointer or replace/rebind a source. Delete or do not expose assignment from T. The custom Config assignment operator should continue to copy X and document bookkeeping, while H metadata/bindings remain declared by the schema.

### Traversal and handler semantics

Add a separate SchemaWalker.HiddenValue route for H, so handlers know these are external/fixed output sources. It accepts the binding plus file flag, Referer flag, and Referer name. Keep ordinary Value for X and its existing codec/atomics/sequences. Each handler implements the small H path:

| Operation | X | H fixed/live |
| --- | --- | --- |
| Declaration | Existing member/default behavior | Read-only source binding; no eager pointer dereference |
| ApplyDefaults / ResetToDefaults | Existing selected value assignment | No-op; do not replace fixed source or reset external state |
| ApplyDocument normal | Existing selected read | No-op regardless of JSON value |
| ApplyDocument(includeWriteOnly=true) | Existing copy read | Still no-op; Config copying cannot overwrite an external source |
| OverlayDocument / Write | Existing file output | Emit Snapshot only when writeToFile=true |
| GetUploadInfo | Existing Upload mode | Emit Snapshot only when writeToReferer=true |
| Config::operator= | Existing X copy and document bookkeeping | Bindings remain live/fixed; no cached-value assignment |

Use `HandleHiddenValue` overloads rather than feeding a binding into JsonValueCodec<T> as if it were an assignable ordinary field. The write/upload overload reads `binding.Snapshot()`, then uses the **existing underlying T codec**. Defaults/read overloads should not call Snapshot at all. Source read should also be skipped when the relevant output flag is false. This avoids unnecessary locks and source reads during copy/read.

If an H file flag is false, remove only that owned schema key from the output parent via removeMember, rather than merely skipping emission: Config::Write starts from readAllDocument/loadedDocument when AutoClean=false (Other.Config.cpp:675-707), so a stale Info.UpdateChannel in existing JSON would otherwise survive. Preserve all unrelated unknown-key behavior. This defines false as exclusion from the output file, including stale values; parent should include this interpretation in the design/PRD.

File/Referer flags matrix:

| writeToFile | writeToReferer | Behavior |
| --- | --- | --- |
| true | true | Persist original typed source value; upload encoded textual snapshot |
| true | false | Persist only; applies to EditionVersion and TargetArchitecture |
| false | true | Upload only; applies to live UpdateChannel |
| false | false | Neither sink; no sink-triggered source read |

The upload-specific Info.UserId transformation stays on local valueText only, removing raw `{` and `}` **before** StringToUrlencode. Disk Info.UserId and ::userId retain their braces. Normal/braced GUID hex digits and hyphens must be preserved. Full upload output becomes id, ed, pa, we, ch, without ev.

### Lock order and channel synchronization

Existing channel synchronization is consistent at inspected production sites:

- `IdtConfiguration.cpp:318-329` uses unique_lock for ReadSetting update fields, then releases it before other processing.
- `IdtConfiguration.cpp:532-536` takes/relinquishes shared_lock for serialization snapshot.
- `Setting.cpp:378-385` updates under unique_lock and releases it before CaptureSettingJson at :386.
- `Setting.cpp:691-719` getter/setter lambdas lock only for small reads/writes.
- `Net.Update.cpp:44-54` snapshot/setter functions release source lock before returning; WriteSetting at :640 is after SetUpdateChannelSnapshot returns.
- `IdtMain.cpp:2258-2265` startup update initialization leaves the lock before Config.ReadAll/Write at :2437/:2486.

New GetUploadInfo lock order is Config rwMutex -> source shared_mutex, because GetUploadInfo holds Config shared_lock (Other.Config.cpp:721) during traversal. Write would use the same order for any persisted locked binding, but current ch is upload-only and should not be read during Write. Config::operator= holds two Config locks, but no ch lock is needed during its OverlayDocument/read because file=false/read=no-op.

No inspected setlistUpdateMutex critical section calls Config::ReadAll/ReadMini/Write/GetUploadInfo/operator=. Thus this new nested order does not introduce a discovered inversion. Document the invariant: release source/update locks before calling Config APIs; only copy the source while holding its lock; never perform I/O or callbacks under the source lock. Do not add an accessor callback to solve a problem that typed pointer+mutex binding already handles.

The other six globals are initialized before update-thread launch; all discovered assignments to those globals are in IdtMain startup (:87-95, :1348-1405, :2081-2082). No later global mutation was found. Their unguarded read-only bindings are appropriate under that existing lifetime assumption. Future concurrent setters would require source synchronization; Config's rwMutex does not protect external globals.

### Meaningful tests using existing production Config

This upgrade changes defaults, copy, persistence, and concurrency semantics, so production Config headless tests are now appropriate. Existing headless vcxproj compiles Other.Config.cppm/.cpp and Conv.Text.cppm (:101-109). Existing startup_preview_state_tests.cpp:17-25 supplies global Config inputs; its :43-95 fixture creates a private temp main.json and restores globalPath. animation_tests.cpp:43/:1675 integrates that fixture; --no-window is parsed at :1640.

Recommended smallest reuse:

1. Add metadata/binding assertions to the existing Config fixture or a focused new config_metadata_tests.cpp integrated in the existing runner/project. Do not create a new build system or production CLI.
2. Headless must provide exactly one definition of SetListStruct setlist and shared_mutex setlistUpdateMutex, since schema now references their addresses. Use existing IdtConfiguration.h and test-owned storage; do not compile IdtConfiguration.cpp or link the entire product into headless. Preserve existing globals at startup_preview_state_tests.cpp:17-25 and avoid duplicate definitions.
3. Construct Config, change live global identity/date/version and channel **after** construction, then invoke GetUploadInfo without ReadAll; assert latest snapshots. Changing globals back must update subsequent output.
4. Assert `id=<36-char GUID>;`, no raw braces and no encoded braces, no ev, and encoded channel with spaces/separators. Assert original UserId unchanged.
5. Write to private fixture: UserId remains braced; EditionVersion persists; ch omitted; preexisting Info.UpdateChannel removed; unrelated unknown key remains when AutoClean=false.
6. Read malicious H JSON overrides, then ResetToDefaults and Config assignment; assert no mutation of external globals/channel and live binding retained. Assert at least one X field still reads/resets/copies correctly.
7. Fixed binding Snapshot must remain the declared fixed value; pointer binding must observe subsequent source changes. Exercise the optional mutex binding with a writer that uses the same mutex and alternating complete channel strings; repeated uploads return a complete encoded snapshot, then a final synchronized channel value. Avoid hard-to-finish reader/writer starvation tests and no raw concurrent global-string mutation.
8. Existing --no-window suite validates Config behavior without real user IDs or server requests. Build product via full InkeysRepo.sln Debug|ARM64 and build existing headless target with its current ARM64 settings; normalize PATH in each MSBuild PowerShell invocation, use ARM64-native host discovered from Visual Studio, allow at least five minutes. Product solution currently does not include the separate headless project, so its build must be explicit if tests are added.

Suggested test file scope if adding a dedicated test file: `InkeysHeadlessTests/config_metadata_tests.cpp`, `InkeysHeadlessTests/animation_tests.cpp`, `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`, plus existing globals fixture only if needed for shared definitions. Preserve encoding/EOL of edited existing files.

### Related specs and external references

Read `.trellis/workflow.md`, native-desktop/index.md, configuration-i18n-and-assets.md, cpp-conventions.md, build-and-compatibility.md. Their relevant contracts remain: two config systems have distinct owners; minimal diffs, original encoding/EOL, Chinese comments, existing locks, full solution dependencies, no GUI by default. The new source-binding/read-no-op/copy-no-op/flag-output/lock-order contracts warrant a focused spec update by the parent through the spec skill after implementation.

No external references required: design is derived from production C++20 modules and existing shared_mutex/JsonCpp usage. No dependency change is proposed; 123 service behavior is unchanged and remains unverified.

## Caveats / Not Found

- Parent reports PRD updated to R1-R7 for this expanded scope. The older metadata.md remains historical.
- No existing H live binding type or upload-specific test was found; this expanded request needs actual new binding logic and meaningful tests.
- configOnce's H values will be live rather than frozen snapshots. No consumers rely on frozen Info metadata in current repository; X snapshot behavior must remain intact.
- File=false stale-key removal is a recommended precise contract, not an existing behavior. Keep removal limited to the new schema-owned key.
- No production edits, builds, git operations, GUI, or real-server requests were performed. This research writes only research/h-binding.md; earlier metadata.md remains historical and is explicitly superseded here.
