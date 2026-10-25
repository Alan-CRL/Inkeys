# 设置内容与排版十项修正（2026-10-24）

用户要求直接继续设置页增量，沿用父任务。上一轮外框修复已提交3a28f8ac，当前不延续commit授权；不GUI/Office/Computer Use，不commit/push，不结束/归档父任务。两张既有CSO与其他旧日志保留。

## 要求与实现边界

1. 绘制两项笔震动反馈统一笔图标。
2. PPT显示加载页块保留，移除toggle；保留加载页实现和既有kPptLoadingPageEnabled=false门禁，读取与所有配置序列化入口统一false，不接受本地旧true恢复功能。
3. PPT控件显隐及其下方实验项使用各自语义图标。
4. 四插件详情仅保留返回箭头，移除“插件”文字链接，页面标题/源码链接不受影响。
5. SuperTop使用置顶图标；指示器仅移除UI编辑器，保留说明块，实际值/行为不变。
6. DDB逐软件项使用通用应用/窗口图标。
7. PptCOM.CheckCOM=20261024a；两处设置显示从真实pptComVersion读。构建并同步同次DLL/TLB至Inkeys资源，不只在UI伪装版本；不改ABI/GUID/AssemblyVersion/manifest。
8. 显示桌面采用非锁屏桌面图标；ClassIsland快速换课采用与同组相同的铅笔图标。
9. 脏区调试/帧率整体移至调试软件页，保存/运行通知不变；版本页更新状态及动作原码前移，随后蓝色info版本块，去PNG及“版本信息”分组标题。保留所有真实版本与CI元数据、更新状态/动作和链接。CI详情以临时内存fixture调用真实renderer验证，生产signal1元数据结构保持空值/CI注入，不残留假值。
10. 赞助说明在图片块下方，逐行居中、Body与TextPrimary，保持可换行与原链接。

## 验证设计

- UI与PPT配置/托管版本由独立实现代理处理；主会话整合、补真实CI/CD和赞助renderer回归，独立检查代理核对全部十项。
- 最小抽取CicdContent/RenderCicdDetails与RenderSponsorThanks供headless直接调用生产逻辑；UI层外链动作仍由Setting.cpp消费枚举，不在测试renderer里打开浏览器。
- CI夹具覆盖长URL/工具链、多语言、明暗主题、窄/常规宽、系统DPI倍率、外层滚动访问最后一行、实际导航点击及移除长fixture后重测量。临时值只存在测试局部内存，测试结束清空；未向生产settingCICD赋假值。
- 赞助夹具检查真实TextPrimary顶点、完整换行高度和逐行几何居中。
- PPT持久化回归覆盖旧true/false/缺项/错误类型，真实CapturePositions/CaptureSettings→Prepare/Commit输出false，保持位置、记忆和其他设置。
- 先按现有Release|AnyCPU构建PptCOM，复制同次DLL/TLB至native资源并核对SHA-256；随后ARM64原生MSBuild完整InkeysRepo.sln Debug|ARM64，运行managed mocks与native --no-window，不打开真实Office/设置窗口。
- 使用原有工具链、PATH workaround和正常主机权限；不因环境问题改工程或依赖。编码/BOM/CRLF、i18n、diff检查并保留两张CSO字节。

## 最终验证结果

- **编译通过（exit 0）**：ARM64 原生 MSBuild 构建现有 `InkeysRepo.sln Debug|ARM64`；同一 PowerShell invocation 先使用 PATH workaround。先按现有 `PptCOM.csproj Release|AnyCPU` 构建托管组件，再手动同步同次 DLL/TLB；未修改 solution、project、依赖、工具链。日志为 `settings-content-pptcom-build.log`、`settings-content-layout-build-final.log`。
- **原生无窗口回归通过（exit 0）**：`Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`，包括新增真实 CI/CD 和赞助 renderer 的三语言、三倍率、明暗主题、260/620 像素宽度共 36 组布局输入，以及旧 PPT 配置的真实 WriteJournal 写回边界。末尾 `PASS animation correctness`，完整证据 `settings-content-layout-tests.log`。
- **托管 mocks 通过（exit 0）**：`PptCOM.Tests/bin/Release/PptCOM.Tests.exe`；末尾 `PASS PptCOM descriptor ownership and session/owner contracts`。未运行需要真实 Office 的 dispatch fixture。日志 `settings-content-pptcom-tests.log`。
- **版本与交付产物通过**：用 Windows PowerShell 的 .NET Framework 实际加载同步后的 `Inkeys/PptCOM.dll` 并调用 `CheckCOM()`，返回 `20261024a`。纯字节解析最终 native EXE 的 `DLL/222` 资源，与同次托管构建 DLL / Inkeys 内嵌源文件完全一致（SHA-256 `aea8a90053cc5b0d0912ac61c33496e29ffc03bd520404bd9322a11e8789f18f`）；TLB 同次构建且字节与 HEAD 一致，ABI 未变化。现有资源校验入口会刷新旧的落盘 DLL，无需额外改加载器。
- **CI 数据清理通过**：生产 `signal1` 元数据结构与 HEAD 逐字节一致；生产源码/EXE 中无 `example.invalid` 等夹具值。长工具链/末行和 URL 仅为测试局部内存，清空夹具后真实 renderer 高度恢复；真实外链返回动作由页面消费，测试不打开浏览器。产物证据 `settings-content-artifact-verification.json`。
- **i18n 通过（exit 0）**：现有 `Scripts/i18n.ps1 check` 检查繁体与英文均 490/490，源键集合未变化。
- **编码与最小差异通过**：19 个本轮已跟踪文本文件保持 UTF-8/BOM 与 CRLF；`git diff --check` 通过。两张既有 CSO 的 SHA-256 与本轮基线一致，未改其字节；不覆盖或清理旧日志/其他既有改动。
- **独立检查**：`settings-content-layout-review.md` 逐项审查十项需求与配置、版本/CI、赞助渲染边界，无阻断发现。其“待主会话验证”由本节完成，未把源码审查当作实机验收。

首次新增测试编译因前置声明缺失出现 C3861，已补最小声明后重新完整构建并通过；最初尝试用 PowerShell Core 加载 .NET Framework 组件因宿主不匹配失败，改用既有 Windows PowerShell 后实际版本调用成功，未为环境改产品源码。现有第三方转换等 warning 未顺手清理。

本轮未启动产品窗口、浏览器或真实 Office，也未执行 Computer Use；自动回归不代表真实界面或 Office 实机验收。当前增量保留未提交，不 commit/push；父 Trellis 任务保持 `in_progress`，历史待验收项不勾选。
