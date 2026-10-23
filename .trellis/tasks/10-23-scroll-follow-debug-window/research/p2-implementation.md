# P2 实施：普通诊断窗口与最小正式 Mag 排除入口

日期：2026-10-23。P2 only；P1 CV 及其测试由另一角色拥有，本阶段未修改；不 commit/push，不运行可见 GUI。

## 窗口与 API

- `WindowRole::ScrollFollowDebug` 追加在 Count 前，旧角色数值不变。
- `UsesSettingThread` 仅负责既有 setting owner thread/queue 的路由；`IsSetting` 的 borderless、焦点和 Drawpad owner 行为保留。CreateGroup/DestroyGroup/Submit/SubmitNoWait/HideAll 覆盖新角色。
- 诊断角色固定普通 `WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN`、`WS_EX_APPWINDOW`，清 TOPMOST/LAYERED/NOACTIVATE/TOOLWINDOW，owner=null。不加入 OverlayReady、overlay topmost、白板 owner/minimize 数组。
- 新普通头/源 `Inkeys/Inkeys/UI/ScrollFollowDebug/ScrollFollowDebug.h/.cpp`：`Handle()`、`WindowProc()`、`Open(HICON = nullptr, const RECT* initialBounds = nullptr) noexcept`、`Destroyed()`。WindowSpec factory 私有且 cpp 内 import `Inkeys.Window`；窗口创建/关闭/销毁都走 Service，WM_CLOSE 不 PostQuitMessage。initialBounds 只指定初始位置，供现有短夹具/屏幕外合同复用同一产品 factory。
- 骨架左部蓝色预览、右部统计占位、顶部状态和底部事件占位；不启动 timer、Transport、图像复制、Capture 或 Tracker。P3 再接真实帧和完整控件。
- 正常启动只在 `--scroll-follow-debug-window` 时、Draw3 fallback 与必需窗口/客户端成功后执行一次 Open。关闭后没有自动重开路径。console/verbose 两个原开关未耦合。
- `BuildCurrentFilterList` 第 11 个角色是诊断窗口；每次仍从当前 Service 读取 HWND 并走原 valid/process/child/dedupe 检查，不缓存句柄、不用捕获亲和属性。

## 定向测试合同

`InkeysHeadlessTests.exe --exit-visibility-gate-test` 中追加产品诊断窗口的屏幕外合同，仍经 `--window-contract-only` 的现有调用链覆盖；未改 animation_tests.cpp。本新子用例所有 HWND 初始位置为 -25000，复用产品 Open factory 与 WM_CLOSE，检查设置线程归属、普通样式/owner null、OverlayReady 不依赖可选窗口、根 topmost 刷新不收编、同步/异步 HideAll、关闭清空角色/owner ID/生产 HWND、owner 线程继续存活、显式重建及 StopAndJoin 资源清理。原完整 window-contract 会显示部分旧测试窗，本角色不运行它。

新增严格单参 `Inkeys.exe --scroll-follow-debug-exclusion-test`，与已有早退内部 dispatcher 一起拒绝额外参数、prefix 变体、错位参数；Harness 关闭时返回 2，在读取用户配置/正常启动窗口前退出。

- 私有 `RunFreezeCaptureLiveCase(bool debugExclusion)`；已有 `RunFreezeCaptureLiveTest()` 原 no-arg 包装选择 false，保留四阶段/静止 5 秒/20 次/取消/Freeze 的原默认路线；新入口选择 true。
- 新模式复用原 MagnifierThread、Capture Start/Stop/回调、原 Mag HWND pair、私有 Build/ScrollFollow/live-PID-tick 输出与安全/桌面门、90 秒 watchdog 和清理顺序。不开另一个 capture，不直接调用 Mag 或修改算法/生产调度。
- 只在新模式把夹具自己 Source 降到普通 z-band；诊断窗口从真实产品 Open factory 创建，owner null、非 TOPMOST，按实际 WindowRect/ClientToScreen 得到几何。覆盖裁剪 `256x192` 放在已知蓝色预览客户区内，既不依赖标题栏尺寸，也不靠缩小/移动窗口代替排除。
- 每个屏幕证明检查：原 63 点网格里所有不在诊断窗 outer bounds 的源样本；蓝色客户区 9 点；WindowFromPoint 命中自己的诊断 HWND；逐像素精确验证未覆盖的原 `(64,64,256,192)` Source ROI。每阶段的三帧正式回调前、后均核验，覆盖中途丢失即失败。
- 正式回调仍检查全部 63 个源样本及真实覆盖裁剪每个 BGRA 像素，沿用原 Capture 对比容差，至少三帧/阶段。新短模式静止 phase1 与改变图案 phase2，保存两张只含已验证专用内容的 covered-roi PNG。
- Capture Stop/drain 后经生产 WM_CLOSE 验证 Service 句柄和过滤列表均移除，显式再 Open 验证当前有效 HWND 仅出现一次，最后关闭仍保留 Service。新模式跳过长 tail，日志明确 `long_tail_skipped=1`。
- GUI 实际证明由 main 运行。本角色只交付入口和静态/构建证据，不能由 filter membership 或构建通过宣称像素排除成功。

## 构建与验证记录

构建入口：项目现有 Visual Studio/MSBuild `InkeysRepo.sln Debug|ARM64`，没有创建新构建系统或改变工具链。命令为同一个 PowerShell invocation：

```powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
$taskMsbuild = & 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild/Current/Bin/arm64/MSBuild.exe
if (-not $taskMsbuild) { throw 'ARM64 MSBuild not found' }
& $taskMsbuild InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m /nr:false /nologo /v:minimal /fl '/flp:logfile=Build/ScrollFollow/p2-build.log;verbosity=normal'
exit $LASTEXITCODE
```

- XML parse 与新 header/source 各工程登记恰一次：PASS。
- 所有改动原文件 BOM 状态保持、纯 CRLF 与 UTF-8 检查：PASS。新增 UI 头/源 UTF-8 无 BOM、CRLF。
- `git diff --ignore-submodules=all --check`：PASS。忽略子模块只为本机 HiMsg 元数据读取权限问题，没有改子模块。
- 沙箱 full solution：16:39:56 开始，16:49:57 结束，exit1 / 00:10:01.15。日志为 FAILED、0 Warning(s)/0 Error(s)，原生依赖扫描未完成；PptCOM DLL/TLB 已生成，未收到任何有意义源码/编译错误。结束后 MSBuild/cl/link/mspdbsrv 均无活动进程。这是 Codex 沙箱/runner 未完成边界，不据此修改源码/工具链。
- 同一命令经正常宿主 reviewed escalation：16:51:38 开始，约 16:52:49 结束，exit0 / 00:01:10.83，117 Warning(s)/0 Error(s)，完整 solution 及 Inkeys.exe、InkeysHeadlessTests.exe 生成。首轮通过日志另存 `Build/ScrollFollow/p2-build-first-pass.log`。警告来自现有 hashlib++、Bar/Setting 数值转换、旧模块 fragment 等，未修改这些无关区域。
- 增加阶段回调后覆盖复核的最终增量 full solution 也 exit0，日志 `Build/ScrollFollow/p2-build.log`。中间一次命令环境变量名误输由 PowerShell ParserError 在构建开始前拒绝，更正后使用原正确命令通过。
- 现有 post-build 输出 `pwsh.exe is not recognized`，与 P1 正常宿主环境相同；MSBuild exit0 且两 EXE 已成功链接。未改工程或 PATH 绕过后置消息。
- `InkeysHeadlessTests.exe --exit-visibility-gate-test`：exit0 / 0.740s，`PASS offscreen scroll follow debug window contract` 和 `PASS exit visibility gate`；日志 `Build/ScrollFollow/p2-offscreen.log`。不执行会显示部分旧窗口的完整 window-contract。
- 四组严格错参（专测+普通 debug flag、相反顺序、专测 `=bad`、专测 `-extra`）各 exit2 / stdout+stderr 零字节；使用 subprocess CREATE_NO_WINDOW，未初始化窗口或进入用户配置。
- 本角色未运行任何可见 GUI、长 OpenCV 全矩阵或 UInk 全量回归。P2 实际像素排除仍待 main 运行新严格 CLI。

## 交接

产品：`D:/Project/Inkeys/Repo/Inkeys/Build/ARM64/Debug/Inkeys.exe`。main 从仓库根以唯一参数 `--scroll-follow-debug-exclusion-test` 运行。每阶段需看到 outside_pattern=N/N、blue_client=9/9、owned_client_hit=1、uncovered_roi_exact=1；总结为 `[ScrollFollowDebugExclusion] phases=2 ... covered_roi_exact=1 close_cleared=1 recreate_filter_current=1 long_tail_skipped=1`，随后 `[FreezeCaptureLive] result=PASS ... original_pair_destroyed=1`。结果仍待 main 实测，不能先勾像素排除通过。

P2 结束后释放 IdtMain.cpp、IdtMagnification.cpp/.h、工程登记和 window_tests.cpp，等 main 的 P2 gate/P3 明确 followup；不自行进入 P3。

## main 实屏尝试结果（P2 仍未验证）

main 的严格可见入口在任何窗口初始化前返回 `[FreezeCaptureLive] blocked=security-interface-before-initialization`。未绕过或改变安全层门；因此实际像素排除为 UNVERIFIED（环境阻断），不标 PASS。首次 PowerShell `&` GUI subsystem 启动未等待产品退出，`$LASTEXITCODE` 为 null；包装命令 exit0 不能作为产品测试成功证据。后续应由 main 使用 Start-Process -PassThru/WaitForExit 读取真实产品退出码。P3 按已批准的环境受限交付分支继续，实际对话框/GUI/像素验收仍独立待验。
