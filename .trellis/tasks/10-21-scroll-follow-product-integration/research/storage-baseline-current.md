# Research: 既有 Presentation 自动保存基线的路径长度失败

- Query: 解释 `TestIndexFailureRecoveryAndForeignConflict` 中 `independentlySaved` 的失败；追踪 target、workspace、session 与 storage track 的选择，判断是否属于本轮滚动批注源码回归。
- Scope: mixed（仓库源码、主会话无窗口 CLI 日志与只读 HEAD 对比、Microsoft Win32 路径文档）
- Date: 2026-10-21

## Findings

### 结论与证据等级

**本次独立基线失败可由测试临时根路径长度解释，没有证据表明它是当前滚动批注功能引入的源码回归。** 主会话保持同一测试二进制、同一 `--presentation-autosave-only` 参数，仅继续缩短仓库内私有 TEMP/TMP 后，整个该 CLI 回归已退出 0。没有为此修改 PPT 保存实现、测试或工程，也没有重编来改变结果。

原失败日志仅打印复合断言文本，没有打印 `independentlySaved.status`、`storageTrack` 或 Win32 error 的实际值。因此下文的 **`IoError / SlideIdSession` 是沿现有源码与路径预算得到的强推断，不能写成该次运行直接采集的 completion 值**。直接实测事实是三次运行的路径、退出码、耗时与日志内容。

### 运行证据

下列命令均由主会话运行，本研究代理没有另行启动测试、MSBuild 或 GUI。

| TEMP/TMP 根（不计末尾分隔符） | 字符数 | exit | 耗时 | 直接观测 |
| --- | ---: | ---: | ---: | --- |
| `Build/ScrollFollow/storage-temp-presentation-autosave-48c21fe195704aaab9492de592db01e1` 的仓库绝对路径 | 116 | 1 | 13139 ms | 多条保存/加载连锁断言失败 |
| `D:\Project\Inkeys\Repo\Inkeys\Build\sfp-94de65ce` | 48 | 1 | 3654 ms | stderr 仅剩 `independentlySaved.status == Committed && independentlySaved.storageTrack == SlideIdSession` 一条；stdout 0 B |
| `D:\Project\Inkeys\Repo\Inkeys\Build\tfc` | 39 | 0 | 3537 ms | 同一二进制、同一 flag；stdout/stderr 均 0 B |

日志保存在：

- `Build/ScrollFollow/storage-baseline-presentation-autosave{,-stderr}.log` 与同名 `.json`。
- `Build/ScrollFollow/storage-baseline-presentation-autosave-short{,-stderr}.log` 与同名 `.json`。
- `Build/ScrollFollow/storage-baseline-presentation-autosave-min{,-stderr}.log` 与同名 `.json`。

相关但独立的阶段 4 基线由主会话提供：完整 `inkStrokeModelerTest.sln Debug|ARM64` 构建 exit 0、55.10 s；`--uink-file-only` exit 0、100433 ms；`--uink-presentation-only` exit 0、149 ms；`--desktop-autosave-only` exit 0、2135 ms。构建日志为 `Build/ScrollFollow/storage-baseline-build-arm64*`。这些结果不是滚动批注产品持久化功能通过的证明。

### Files found

| 文件 | 职责与本次相关性 |
| --- | --- |
| `inkStrokeModelerTestTests/presentation_autosave_tests.cpp` | target/snapshot 构造、故障注入与唯一失败断言；测试临时根在此生成 |
| `inkStrokeModelerTestTests/contact_input_tests.cpp` | `--presentation-autosave-only`、子命令的纯 CLI 早退入口 |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cppm` | storage track、completion、状态、测试注入与 service API |
| `Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp` | session 捕获、Base/sidecar/session 轨选择、逻辑身份隔离、版本文件保存、终态传播 |
| `inkStrokeModelerTest/draw3/uink_file.cpp` | 共享 UInk 文件事务；短临时叶名与最终 `MoveFileExW` 提交 |
| `inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj` | 直接编译实际 PresentationAutoSave/UInk 源码；Console 子系统；ProjectReference 指向既有 demo |
| `inkStrokeModelerTest.sln`、`inkStrokeModelerTest/inkStrokeModelerTest.vcxproj` | 既有独立测试 solution 与依赖工程，本研究未修改或重编 |
| `.trellis/tasks/10-21-scroll-follow-product-integration/prd.md` | 当前任务范围是滚动批注接入，并禁止改变 UInk 格式 |

### Code patterns：请求身份没有冲突

1. `MakeTarget()` 固定同一个 key、source `path:c:\lessons\deck.pptx`、`StableSlideId`、SlideID 101、binding revision 1（`presentation_autosave_tests.cpp:49`）。`MakeSnapshot()` 写 stable `workspaceType=2`、`hostId=FormatPresentationKey(target.key)`，逻辑文件 GUID 为 `1111…`、workspace GUID 为 `2222…`（同文件 `:89`）。
2. `MakeSecondSnapshot(target, true)` 保持原 target、binding、hostId、SlideID 与内容结构，仅把 file/workspace/page GUID 换成 `4444…` / `5555…` / `6666…`（同文件 `:122`）。新的 workspace 不会复用旧逻辑文件身份。
3. 原 service 依次执行 revision 1 的 index 注入失败、pending load、revision 2 成功，随后 revision 3 的 index 注入失败、pending load、revision 4 成功，始终采用注入 session `bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb`（同文件 `:1194`）。到 foreign 请求前，已有该文稿的有效 Base 索引。
4. foreign 是新的 `PresentationAutoSaveService`，启动前把 `sessionIdOverride` 改为 `cccccccc-cccc-4ccc-8ccc-cccccccccccc`，向同 root 提交同 target、revision 3、`MakeSecondSnapshot`（同文件 `:1252`）。`Start()` 捕获该 override 为 worker 的 session ID（`Draw3.PresentationAutoSave.cpp:1836`）；session 不由 workspace GUID 推导。
5. pending 容器属于各 service 的 `Impl`（同实现 `:1721`），不是跨 service 的全局状态。foreign 没有原 service 的 pending entry。新 file/workspace GUID 与旧索引不同，`ConflictsWithOtherLogicalFile()` 的 GUID 碰撞分支不应触发（同实现 `:940`）。该请求走新 session 的新文件分支，也不存在拿新 session revision 3 与旧 session revision 4 比较后拒绝的逻辑（同实现 `:1290`）。

### Code patterns：预期选择 SlideIdSession

`SelectStorageTrack()` 按 Base、slide-id sidecar、page-index sidecar、当前 session 的 slide-id、当前 session 的 page-index 构造五条轨（`Draw3.PresentationAutoSave.cpp:786`）。

- 在 foreign 请求中，Base 的 `found[0]` 命中相同 source/key，但 `sessionId` 是 `bbbb…`，所以 `current(0)` 为 false（同实现 `:831`、`:842`）。没有 source/key 矛盾；这不是 `CrossProcessConflictDeferred` 的身份错配分支。
- 当前 `cccc…` 的 session 轨为空且有效；sidecar 也没有当前会话记录。于是 `found[0]` 分支选择 `sessionTrack=3`，即 `SlideIdSession`（同实现 `:808`、`:897`、`:907`）。保留旧 Base 索引，并使用 `<root>/presentation/sessions/<cccc…>/slide-id`。
- `SavePresentation()` 在进入目录/版本文件 I/O 前，已把 `selection.Current().track` 写入 completion 的 out 参数（同实现 `:1176`）；worker 把该参数直接绑定 `completion.storageTrack`（同实现 `:1800`）。后续文件事务失败，storage track 仍应是 `SlideIdSession`。
- 保存新逻辑文件时，版本保存未 `Committed` 且并非 `SourceChanged` 会映射为 `PresentationPersistenceStatus::IoError`（同实现 `:1361`）。这是本次长 session 最终路径失败时最吻合的 **源码强推断**。原日志没有记录实际字段，不能排除未经采集的其他 I/O 站点或具体 Win32 error。

### 路径预算解释了为何 48 字符 TEMP 仍只失败这一处

测试 `MakeRoot()` 使用 `GetTempPathW()`，再追加 `\InkeysPresentationAutoSave_<36 字符 GUID>`，固定增加 64 字符（`presentation_autosave_tests.cpp:227`）。保存版本路径为 `files/<fileGuid>_<transactionGuid>.uink`，两个 GUID 均为 36 字符（`Draw3.PresentationAutoSave.cpp:614`）。

```text
Base 最终 UInk      = TEMP + 64（测试根）+ 13（\presentation）+ 85（\files\版本名）
                   = TEMP 长度 + 162
SlideIdSession UInk = TEMP + 64 + 13 + 55（\sessions\<sessionGUID>\slide-id）+ 85
                   = TEMP 长度 + 217
```

| TEMP 长度 | Base 最终路径 | SlideIdSession 最终路径 |
| ---: | ---: | ---: |
| 116 | 278 | 333 |
| 48 | 210 | 265 |
| 39 | 201 | 256 |

`uink_file.cpp:681` 已采用同父目录 `<GUID>.tmp` 的短叶名，因此问题不在临时名重复最终 basename。48 字符 TEMP 下该 session 的短临时文件只有 227 字符，但目标版本文件仍有 265 字符。`NormalizePath()` 使用 `GetFullPathNameW`，没有为普通盘符路径追加扩展 namespace 前缀（同文件 `:221`）；create-new 分支最终用完整目标路径调用 `MoveFileExW`，失败后返回带原系统错误的 `UInkSaveStatus::IoError`（同文件 `:1217`）。

Microsoft 文档说明普通 Win32 路径的 `MAX_PATH=260` 包含终止 NUL；放宽该限制需要扩展路径前缀或相应系统/应用选择启用。265 字符目标超过普通预算，而 256 字符目标在预算内。39 字符 TEMP 的同二进制成功复测与这条源码路径一致。[Maximum Path Length Limitation](https://learn.microsoft.com/en-us/windows/win32/fileio/maximum-file-path-limitation)、[MoveFileExW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)

`CountUInkFiles(root)` 只枚举 `<root>/presentation/files`，不递归 session/sidecar（`presentation_autosave_tests.cpp:237`）。因此断言 `CountUInkFiles(root) == 3` 在 foreign session 保存失败和成功时都可以通过；它不能说明 foreign 的 session 文件已创建，也不构成新的 GC 问题。

### 相对 HEAD 与验证边界

主会话回传已实际执行的只读检查：

```text
git diff --exit-code --ignore-submodules=all HEAD --
  inkStrokeModelerTest.sln inkStrokeModelerTest inkStrokeModelerTestTests Directory.Build.props
  Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cpp
  Inkeys/Inkeys/Drawing/Draw3/Draw3.PresentationAutoSave.cppm
  Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cpp
  Inkeys/Inkeys/Drawing/Draw3/Draw3.AutoSave.cppm
  Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.cpp
  Inkeys/Inkeys/Drawing/Draw3/Draw3.Presentation.h
```

真实 exit 0、输出为空，以上实现、测试及两 legacy 工程目录相对 HEAD 无改动。研究代理遵守角色限制，没有自行执行 Git 操作。

测试专用 flag 在 `wmain` 直接返回，位于后面的普通/GPU 测试之前（`contact_input_tests.cpp:3455`）。其相关 session/atomic 子进程显式使用 `CREATE_NO_WINDOW`（`presentation_autosave_tests.cpp:558`、`:1962`）。主会话以隐藏进程运行已确认的纯 CLI；本研究没有启动应用、Office/WPS、浏览器或 Computer Use。

### Related specs

- `.trellis/workflow.md`：research 持久化与本角色边界。
- `.trellis/spec/index.md`：直接确认、合理推断、待确认的证据级别。
- `.trellis/spec/native/quality-and-validation.md`：独立 demo/测试与主产品验证分开记录；优先既有完整 solution；不修改无关代码。
- `.trellis/spec/native-desktop/draw3-integration.md:196`：当前进程 PPT 保存、同 root/session/pending 隔离、foreign track、版本文件和严格恢复合同；不能用独立 CLI 替代生产 Host 验收。
- `.trellis/spec/ppt-interop/index.md`：托管 COM 与原生画布保存边界；本次未进行 Office/WPS 验收。
- 当前 task `prd.md`：只实施滚动批注产品接线；禁止改变 UInk 格式。本次无需调整既有 PPT 行为。

## Caveats / Not Found

- 实测确认的是路径环境改变后的该 CLI 全部通过；不是对滚动批注期间/退出时的产品持久化功能、真实捕获、真实 Present、Office/WPS 或 GUI 的验收。
- 没有采集原失败 completion 的字段或 Win32 error；不能声称实测得到了 `IoError`、`SlideIdSession` 或 `ERROR_FILENAME_EXCED_RANGE`。状态/轨组合仅有明确源码分支与路径预算支持。
- 没有改注册表、long-path 策略、manifest、源码、工程、依赖或构建方式；没有为了独立基线失败污染当前 feature 的实现范围。
- 该旧测试对 `%TEMP%` 总路径预算敏感，是可单独记录的既有验证环境约束。当前基线应采用已成功的仓库短私有 TEMP/TMP；没有必要继续重复回归或把此问题作为核心接线阻塞。
