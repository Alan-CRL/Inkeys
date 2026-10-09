# 实施与验证（2026-10-09）

## 实现

本轮在当前 fluent 工作区实施，不创建分支、工作区、commit 或 push。主会话维护 task/spec/audit；实施代理只修改设置产品与现有回归，未修改 vendor、工程、公开 Setting API、配置 schema 或显示枚举。

- Setting.Layout.h 增加 Debug 矩形；版本、主题左移一个 32 DIP + 8 DIP 槽位，caption 位置不变。Setting.cpp 的 WM_NCHITTEST 使用相同矩形返回 HTCLIENT，仍由 Win32 处理拖动、双击和贴靠。
- 360 DIP 极窄预算加入 Debug 后原图标后间距会让拖动区缩至 86 DIP；已与主会话确认让标题文字完全让位并仅将图标后的空白压至 6 DIP，以保留 96 DIP 拖动区。正常 720 DIP 及以上不改变间距。空/反向标题 clip 不再提交字形绘制。
- Setting.Shell.cpp 的产品/测试共用 RenderDebugButton 只绘制 Bug；保持主题槽一致的 32x32 DIP 命中和悬停、按压、选中底色，取消主题 tooltip，Debug 无 tooltip。该 helper 只在导航模态存在且鼠标处于 Debug 框内时临时允许标题栏命中，恢复 HoveredWindow 后再返回；点击关闭 overlay/flyout 并复用原 navigateTo。重复点击不重播内容路由。
- 删除 footer Debug 按钮、测量及额外预留。纯内部 ResolveNavigationPaneGeometry 移除不再使用的 expanded/debugWidth/fraction 参数；固定底部四动作保持，中段回收 36 DIP。
- NavigationState::ClearIndicator 清空稳定 item/window 锚点及 X/Y 补间；Debug 路由进入和渲染时清空，Debug 点击当帧无蓝条，返回普通页首次绑定新锚点直接归位。浮层关闭限于入口点击，不在 Debug 每帧强制关闭，因此该页仍能重新打开窄窗导航。
- 四 consoleToggle 按 TouchArea/PptCOM/Draw3/Cursor 顺序完整移至 Debug“控制台输出”，原 ID/i18n/字段/QueueConfigWrite 保存保持；实验页保留其他选项。入口与四项无 IDT_RELEASE 门禁，其他 DRAW3_RTS_DIAGNOSTICS 保持。
- 同一 Display Snapshot 的既有摘要与每屏字段原样拼成全文，段间换行，传入 Design::RenderEdidCard。该 helper 按现有 TextHeight/真实字体换行与实际 CardPadding 测量，设置本帧完整高度/内容约束，沿用主题 Card；缩放、宽度、文本变化当帧重测，卡片内无滚动，整页承载长内容。空快照保持提示。

## 实际验证

原始输出全部在忽略目录 Inkeys/Cache/debug-entry-20261009/。

| 验证 | 结果 |
| --- | --- |
| 最终完整 Solution Debug ARM64 | build-final-result.json exit 0，27.8279769 秒；build-final.log 9 个既有 vendor warning、0 error；Inkeys.exe 与 InkeysHeadlessTests.exe 已重编 |
| 构建方式 | vswhere 动态定位 ARM64 原生 MSBuild；同 PowerShell Remove-Item Env:PATH、唯一 env:Path 与 MSBUILDDISABLENODEREUSE=1；1800 秒进程等待上限；/m:1 /p:CL_MPCount=1 /nr:false；正常用户权限 |
| 最终完整 --no-window | headless-final-result.json exit 0，114.2260286 秒；stderr 为空，stdout 最终 PASS animation correctness |
| EDID 真实 DrawData | 1/1.25/1.5/1.875/2/4 六倍率各 40 帧；浅/深、空快照、单屏、多屏、长名称、窄宽与内容变短；首次帧、同帧宽度/文本变化的完整卡片高度、ScrollMax=0、严格实际字形 x/y 范围、整页滚到底能看到最后字形均通过 |
| Debug 真实连续帧 | 1/1.5/1.875 三倍率各 541 帧；展开/紧凑/overlay、浅/深、动画启停、中途进入、快速反向、重复点击、Debug 页重开 overlay、实际模态子菜单上的标题栏点击；当帧蓝条消失、锚点/补间清空、返回普通页直接归位、后续普通动画恢复均通过 |
| 既有回归 | 旧导航三倍率各 684 帧/24 次导航，蓝条滚动及赞助回归各 928 帧（含新增 Debug 部分），颜色悬停 0 violations |
| 标题栏和侧栏几何 | 360/720/960、多倍率；theme/Debug 独立 32 DIP 客户区，caption/drag 不重叠；footer 四动作、无 Debug 预留、最小窗口中段滚动通过 |
| 国际化 | Scripts/i18n.ps1 check：en-US、zh-TW 各 370/370，PASS |
| 格式 | git diff --check PASS；encoding-check.json 八个产品/测试文件 UTF-8、原 BOM、纯 CRLF 均 PASS；最终 Git 状态没有无关构建产物改动 |

## 首轮失败及环境区分

- build-first 在受限 runner 中 exit 1，115.7480222 秒。第一有意义错误为未修改 Window.cpp:1 的 C1902 Program database manager mismatch，主产品和 Headless 均相同。cl/mspdbcore/mspdbsrv 均 35229 版本一致；历史任务也记录同环境问题。正常用户权限执行相同构建后解决，没有改工具链或项目配置。
- build-normal exit 0；其墙钟记录 13549.493407 秒包含宿主暂停/恢复跳跃，不能视为实际编译性能。vcpkg 首尝 pwsh 未找到后自动回退系统 WindowsPowerShell，标准 AppLocal 部署步骤完成；最终构建仅在当前进程 Path 补实际 pwsh 目录，未改 vendor/project。
- headless-first exit 1，115.3138672 秒，共 643 项新测试失败：640 项把纹理化抗锯齿边框当成字形，3 项 modal 前置遗漏紧凑父行“首次导航、第二次打开”的既有合同。卡片高度/内部 ScrollMax/父滚动和 Debug 蓝条各模式当时无失败。修正为只采样 TextPrimary 实际字形（仍严格检查 x/y）并真实两次点击打开模态；最终全套通过。保留首轮日志，未放宽字形边界或修改产品以适应错误用例。
- 本轮测试新增 C4244 已通过 float 字面值修正；未处理无关既有 vendor warnings。

## 验证边界

未启动 GUI、Computer Use 或浏览器。真实鼠标拖动、双击/贴靠、最终窗口观感、HWND/DX11 呈现和跨屏 DPI 仍需人工验收。未执行实际 Release 构建；IDT_RELEASE 可见性来自源码门禁审计，不外推运行验证。后续由独立 trellis-check 审查；父子任务保持进行中，不自动归档。
