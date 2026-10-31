# E02 B 私有真实 wWinMain 夹具：运行前独立安全审查

日期：2026-09-30。Active task：`.trellis/tasks/09-27-integration-and-release-check`。审查当前 `git diff HEAD` 的 `IdtMain.cpp`、`ShutdownSupervisor.h/.cpp`，E02 设计/设计审查/夹具/实施记录与实际生产启动路径。本 reviewer 只写本报告；没有构建、启动 GUI、终止进程或修改产品源码。

## 结论与准许范围

**D004、D005、D003、B002 四个指定站点的私有 copied-child 自动测试均可分别由 root 运行。** 每轮需显式指定单个 site：`Inkeys.exe --shutdown-supervisor-tests --startup-failure-only D004|D005|D003|B002`，从含 `InkeysRepo.sln` 和 `.trellis` 的本仓库根目录运行完整 Debug ARM64 最新构建。此放行只针对 root 自建 `TestResults/release-hardening/.../bin/Inkeys.exe`、当前鉴权和下列生产调用链；不把未到目标的先行初始化失败算 PASS，也不放行任意真实用户安装/Office 数据测试。A 的 Arm-first 已在最终源码静态复审，D004 私有绿灯已由 root 取证；其它三站点仍无动态结论。

## 鉴权与进程隔离源码证据

- `TryRunShutdownSupervisorEarly` 在 `wWinMain` 首句业务逻辑调用（`IdtMain.cpp:653–660`），位于路径处理、正式单实例、配置、HWND 之前。新 selector 需 `argv[1]` 精确等于 `--inkeys-internal-startup-failure-child-v1`；`CommandLineToArgvW` 失败且命令含内部词时返回 70，未知 `--shutdown-supervisor-tests` 附参返回 71。新 child 鉴权非零返回使 `wWinMain` 直接返回，不会落普通 GUI。
- `AuthorizeStartupFailureChild`（`ShutdownSupervisor.cpp:1035–1087`）先要求 argc9、`IsTestDirectory`、有限 site、十进制 PID/HANDLE 在 DWORD/uintptr 范围；`GetHandleInformation` 核继承位，`GetProcessId(parent)` 必须精确等于参数且非 self。实际 parent 镜像由继承进程 HANDLE 查询，与给定绝对 `expectedParentImage` 的 volume/file-index 同一（`HasExactFailedArmChildIdentity:983–1010`）；私有 root/bin 是普通非 reparse 目录，当前 copied EXE 是 bin/Inkeys.exe 且同一文件，mapping 必须能 `FILE_MAP_WRITE` 并匹配独立 128B magic/version/size/site/failureCode。只有全部通过才安装进程内 site/pointer；`WriteMarker+Flush` 和 ack 失败均返回专用非零码，不进入产品启动。
- `RunStartupFailureProcessTest`（`:1460–1564`）用现有 `MakeUefTestDirectory` 在忽略的 TestResults 下新建唯一 root/bin，复制当前 EXE 且不覆盖；`STARTUPINFOEX` 的继承列表仅列 parent、ack、mapping 三个 HANDLE。parent 持有明确 `PROCESS_INFORMATION.hProcess`，至多等 25 秒；若旧逻辑仍活，只对该 HANDLE 调 `TerminateProcess`，`dead=false` 不计绿灯。测试自建 child 与用户现有 Inkeys/Office 进程无按名称终止路径。错误父镜像和无继承句柄负例分别要求拒绝。
- copied EXE 的真实启动工作目录由 `StartSameExecutable:266–285` 指向私有 bin；`IdtMain.cpp:765–823` 的 `globalPath`、`pluginPath` 均由当前 EXE 目录派生，配置/日志/PPT DLL/AutoSave 路径留在复制根。Release 单实例 mutex 名由 copied image 目录推导（`:867–892`），不会取用户正式 EXE 目录的 mutex 名；Debug 无正式 mutex。此处是路径与名字隔离证据，不承诺用户目录被绝对不可写地沙箱化。

## D004 之前的副作用逐项核对

- 两个旧/新 `update.json` 执行/旧 EXE 路径在 `IdtMain.cpp:968,1198` 加 `!isolatedStartupFailure` 门；`IDT_RELEASE` 的 AutomaticUpdate 线程在 D004 后且也 gated (`:2204`)。不发网络更新、不运行安装目录旧 EXE。
- SuperTop 整支（读取 deploy、`LaunchSurperTop`、清理 `superTop_try.signal`）由 `:1349` 的同一 auth-only 门挡住。启动注册表 Query/Set (`:2068–2073`) 与 shortcut/DDB helper (`:2135–2137`) 发生在 D004 之后且同样 gated。后期 PPTLinkage、TopWindow、Bar 初始化、Freeze/StateMonitoring、Magnifier 线程（`:2689–2728`）全部仅普通产品创建；D004 到达前不会启动它们。当前选择 `SetFlag(2)` 后安装 CrashHandler (`:901–902`)，真实 UEF 分支的 mode2 不争自动重启意图；异常路径若意外发生仍限私有 EXE 数据根。
- D004 前读 `Inkeys::Config::ReadMini` 和后续 `ReadSetting` 的文件根来自 private `globalPath`；`Other.Config.cpp:729` 的 main JSON 是 `globalPath + Inkeys\\Config\\main.json`。日志/老日志清理位于 private bin/log。`getDeviceGUID` 查询本机标识并生成值，但未见该函数写真实用户配置。D004 之前有 root 可写探针、日志线程和可选 StartupPreview；这些只应作用私有目录/本 child。若真实 copied-root 因权限或缺失资源先行报其它错误，test 必须判未到目标，不能算有效 RED。
- `IdtMain.cpp:~1781–1801` 先执行真实 `CoInitializeEx`，auth-only D004 将真实 HRESULT 保存进 observation 并选择原 fatal 分支；如果 COM 真成功，分支后保留对等 `CoUninitialize`，不将成功初始化当未拥有资源。`PublishAuthorizedStartupFailure` 先发布目标，`PublishFatalStartupFailure` 在当前 A 源码中先 `SetOffSignal(1)`、再 `Startup::ReportFailure`、失败帧等待；`ShowStartupMessage` 前的 auth-only `HoldAuthorizedStartupBoundary` 需同一 expected failureCode 且 `failurePublished==1`，记录 gate/当前 offSignal/Arm state 后 `Sleep(INFINITE)`。此前 RED 记录的是 A 修补前真实 wWinMain 中意图0/Arm0；最新 GREEN 则应为同 gate 意图1/Arm已建立，不能把两个源码状态混写。

## D005、D003、B002 分站点扩展审查（以最新 A 补丁为准）

- **D005：可单站点运行。** D004 之后的 Display 只查询屏幕并发布本进程快照；FullConfig 读写的 main/deploy JSON 均在 copied `globalPath` (`IdtMain.cpp:~1980–2070`)，`QueryStartupState/SetStartupState` 已被 `!isolatedStartupFailure` 门挡住。`I18n::load` 消费本 EXE 资源/私有配置；shortcut 与 DDB helper 在真实调用前有门 (`:~2135–2139`)。新 `RecordPptComFailure` 对 Publish/OpenVerified 的 bool/HANDLE 失败先 Arm，再可能 block 的日志；对 `CreateActCtx/ActivateActCtx/LoadLibraryW` 的真实失败即时保存 Win32 错误并 Arm。该站点加载的是 copied bin 中由**同一 EXE 内嵌资源经原子发布、逐字节验证**的 `PptCOM.dll`，不是用户安装目录未知 DLL；`PptCOM.csproj` 明确 DLL 是托管类库，实际 `IPptCOMServerPtr::CreateInstance` 位于后续 `PPTLinkageMain` 所用 IdtPlug-in 路径，私有 child 不启动该线程。未找到在仅 LoadLibrary/激活 manifest 时连接现有 PowerPoint 应用或打开文档的第一方调用。运行后仍应按目标 packet 核 `realInitResult`、gate、自然 old HANDLE 及无 Office 交互；这一静态审查不承诺 CLR/OS 加载器绝无系统只读访问。
- **D003：可单站点运行。** 它经过同一已 gated 的更新、SuperTop、注册表、shortcut/DDB 和 PptCOM DLL 加载，但仍位于 Window Service、Draw3 RTS、MouseHook 与 PPT 联动线程启动**之前**。字体源是 `Text.Font.cpp` 的 `FindResourceW/LoadResource/LockResource` 从本 EXE 的 TTF 资源读取；`InitializeFontCollection` 使用共享 DWrite device/factory，不从用户文档加载字体。auth-only Font 在真实 HRESULT 返回后发布目标并于原 `IDTLogger->error` 前设 gate；A 已在 gate 前 `SetOffSignal(1)`。只有 root 的已鉴权 child 可合成失败，普通启动结果判定未改。
- **B002：可单站点运行，覆盖范围较窄。** 前序仍为 copied 配置/资源，`PPTLinkageMain`、TopWindow、Freeze、StateMonitoring、Magnifier、Bar Initialization 线程均因 `isolatedStartupFailure` 跳过 (`IdtMain.cpp:~2715–2759`)，故不会调用 Bar MouseHook、真实 Bar Register 失败，也不会创建 PptCOM server/连接 Office。Window Service 建立的所有 `WindowSpec` 在传入前改为 `x/y=-32000, visible=false`；`Window.cpp:975–1019` 原样使用坐标并 `SW_HIDE`。Draw3 只附着本 child 的 Drawpad HWND，自动保存根为 copied bin/Inkeys/AutoSave (`IdtMain.cpp:~2575`)，唯一新根无旧用户 UInk。初始空 Select 画布不要求可见拦截。Main 在原应启动 Bar 线程位置发布 `SetBarStartupState(ClientRegistrationFailed)`；`ActiveSnapshot` 真实生产读取它并在失败帧/提示前 Arm。此夹具只验证 Main 的异步失败状态处理，不证明 Bar Register 自然失败、真实笔设备输入或已知 Host/Window 内部卡住。若任何屏幕外 HWND 意外可见或附着到用户窗口，测试应停并记失败；仅操作自建 PID/HWND。
- **共同副作用界限。** D005/D003/B002 前没有产品代码主动调用 `ShellExecute` 更新/旧 EXE、SuperTop 提权、注册表自启、ShortcutAssistant、DDB 或 PPTLinkage；`CrashHandler::SetFlag(2)` 保持禁自动重启。B002 的 Window Service 可能经 TaskbarList2 标记**本 child 自己的 HWND**全屏状态，属于隔离窗口的 Shell 注册动作，不应报告为“没有任何 OS API 接触”。这不访问用户 Office/文档，也不改变用户已有 Inkeys HWND。

## 限制和运行门

1. 四站点分别取证：先后顺序由 root 控制，每轮必须带显式 site selector；不得把 D004 绿灯外推 D005/D003/B002。父 suite 的无 selector 全套仍不作为初次单站点验证命令。
2. `MakeUefTestDirectory` 依赖当前工作目录确为仓库根，并会新建 TestResults 子目录；root 在运行前应确认 `TestResults`/`release-hardening` 为普通非 reparse 目录，执行时只操作本任务新建 root。源码对这两级已有属性检查；真实路径和进程身份仍要在日志里记录。
3. D004 RED 已实际记录 authorized、D004 failure/gate=1、acceptedIntent=0/arm=0、旧 child 25 秒仍活；parent 随后清理自己的 child 是**测试失败后的收尾**。D004 A 后最新私有绿灯由主会话记录 old HANDLE 约15秒自然死亡，鉴权负例继续拒绝；本 reviewer 没有重跑。D005/D003/B002 的绿灯也必须分别有目标 failure/gate、Arm、原截止、自然死亡和私有 durable marker，不能拿启动其他失败点或父清理代替。
4. 当前 copied-root 夹具未通过 OS sandbox 完全限制所有任意 Windows API；它依赖所审调用链和已列 gate。若后续源码在 D004 前新增真实系统/进程副作用，重新审。用户 Win7 SP1 仅 KB2670838、FLIP、DComp/ULW 与真机 Office/恢复结论不由此测试升级。

## Verification

只读核对实际 diff、E01/E03 保留部分、真实 Main/early parser/parent identity/CrashHandler/config/字体/Window/Draw3/PptCOM 调用链。三个源码文件编码/换行保持原状：Main UTF-8 BOM、其余无 BOM，均 CRLF；scoped `git diff --check HEAD` 退出 0。Lint/TypeCheck/Build/Tests/GUI：本 reviewer 未执行；root 独占构建与运行证据。
