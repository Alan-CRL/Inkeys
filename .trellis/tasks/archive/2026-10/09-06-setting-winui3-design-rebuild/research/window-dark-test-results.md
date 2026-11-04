# 窗口框架与深色主题：测试记录

日期：2026-09-07。范围以 approved-window-dark.md 为准。测试代理只修改 InkeysHeadlessTests 中的测试与本记录；框架和主题生产代码由对应实施代理负责。

## 已执行

- 在独立 Build/SettingWindowDarkProbe 中，使用 VS 18 Community / MSVC 14.51.36231 的 Hostarm64/arm64/cl.exe 编译原有 RunSettingSessionStateTests() 与 RunSettingDesignTests()。
- probe 复用生产纯几何、Controls、实际 HarmonyOS 字体与 ImGui/ImFluent 核心；未编译窗口后端、创建 HWND 或设备，未使用 Computer Use。
- 最终多显示器修正后的 probe 退出码 **0**，耗时约 **3.84 秒**。日志为 Build/SettingWindowDarkProbe/compile-current.log 和 run-native-minmax-final.log。
- 原有字形、DPI、设置行末项、按钮组、Details/Card/Notice、控件天然宽度与滚动条检查继续通过。
- 主会话已完成最终 frame 修复版的 ARM64 host 完整 InkeysRepo.sln 构建，退出码 **0**；随后集成 InkeysHeadlessTests.exe --no-window 退出码 **0**。
- GUI 当前因锁屏仍待主会话验收；不把编译/无窗口通过写成实际最大化、主题切换或滚动条拖动已验收。

## 新增窗口几何回归

- 六组 monitor/work area fixture：primary 1920×1080/work 1920×1040、更大 secondary 2560×1440/work 2560×1400、更小 secondary 1280×1024，以及负坐标显示器、左侧/顶部任务栏。
- 使用真实 SDK MINMAXINFO，预置基于 primary 的最大化参数及 reserved/max-track 哨兵，调用生产 ApplyWindowFrameMinimumTrack() 两次；验证仅 ptMinTrackSize 改为生产测出的最小尺寸，所有其余字段逐一保持。
- 对 fixture 提供的原生最终外框调用生产 InsetWindowFrameRect()，检查客户区等于目标 work area；这些外框是场景数据，不是自动执行 USER32 后的采样，也不把数学往返当作多屏消息协议验证。
- 四种系统 frame 像素宽度，检查八方向正常窗口 resize；相同点在最大化后全部不再返回 resize。
- 遍历客户区右侧 32 像素轨道的多个 y，包括客户区上下边界；普通/最大化均为 Client，确保保护整条滚动条轨道。
- 检查 frame/client 往返与 half-open 外框边界；缩放后的最小 client 仅额外加一次系统 frame，显示器工作区不足时受实际工作区限制。
- 15 组标题栏宽度/倍率：主题切换按钮稳定可达，与 drag/minimize/maximize/close 互斥，版本区域位于其左侧。
- 保留原有 resident/epoch/字体重建事件/显隐等 SessionState 回归。

上述测试调用生产 Setting.Layout.h 的几何函数，没有在测试中另写消息坐标算法。实际 NCCALCSIZE、GETMINMAXINFO、NCHITTEST 的 Win32 消息接线仍由主会话窗口检查确认。

### 最大化协议审查修正

[MINMAXINFO 官方说明](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-minmaxinfo)指出，最大化尺寸和位置仍以 primary monitor 为基准，窗口管理器会为 secondary monitor 补偿。直接填入目标显示器尺寸可能导致再次补偿。

因此，生产移除了手算最大化尺寸/位置的 helper，WM_GETMINMAXINFO 仅通过 ApplyWindowFrameMinimumTrack() 写最小跟踪尺寸；测试已移除对 ResolveMaximizedWindowFrame/MaximizedWindowFrame 的引用。静态核对当前消息处理没有另外写 ptMaxSize/ptMaxPosition。核心自动回归是实际 MINMAXINFO 非本应用所有字段保持不变；更大/更小/负原点 fixture 只补充客户区内缩场景，不实现 USER32 模拟器，不宣称真实多显示器运行已经通过。

## 新增浅/深主题回归

- 六倍率各执行浅 → 深 → 浅，共 18 次切换，始终使用同一 ImGui/atlas 会话。
- 每次重复 ApplyPalette()，验证 active palette 与旧 token 别名地址稳定；font/atlas/纹理像素指针、字体/纹理/glyph 数量、当前字号和 DPI/main 倍率不变。
- 正文、标题、控件分别通过生产 TextWidth()/ControlTextWidth() 验证实际字体绑定与测量值不变。
- 检查 ImFluent preset、ImGui 标题背景、Popup、滚动条颜色一起更新；轨道保持透明，大小不因重复切换累计缩放。
- 直接提取生产 PageHeader、Button、TextBox、Combo 的真实文字/表面顶点颜色；验证默认参数读取当前主题，idle 绘制不改变控件值。
- 四种 Notice severity 的真实填色/主文案均采用当前主题，浅/深表面保持正确的前景明暗关系。
- 保留同一会话末项后直接 EndChild 的断言检查。测试最后恢复浅色，避免跨测试污染。

这些检查不把色值表相等当作全部视觉验证；真实控件有绘制数据检查，但不代替实际 HWND/DX11 截图或主题切换动画观感。

## 配置与旧测试合同

- 静态核对 IdtConfiguration.h 的 settingDarkMode 默认 false。
- 静态核对 IdtConfiguration.cpp：SettingDarkMode 读取依次判断 isMember/isBool，随后 asBool；缺项或非布尔类型归为 false，写入为 Json::Value(bool)。未在测试里复制 JSON 解析逻辑来声称验证了真实读取入口。
- 旧 ResolveThemeMode 的“所有系统主题都强制浅色”断言替换为显式 false → Light、true → Dark。
- window_tests.cpp 的真实窗口检查更新为 overlapped caption/sizing 合同、无 WS_POPUP；刻意保留传入旧 WS_POPUP 的测试输入，验证 WindowRole::Setting 会覆盖旧样式。
- RunWindowTests() 仍在原 --no-window 门后；本 probe 和测试代理没有执行它或其他 GUI 操作。

## 构建过程中的测试修正

首次编译发现 ImFluent::GetColorU32 只属于内部接口。测试已改用公开 ImGui::ColorConvertFloat4ToU32(ImFluent::GetStyle().Colors[...])；之后两次 CPU probe 均通过。主会话并行完整构建曾读到修改前版本，已通知重跑当前代码。

最终测试文件保持原 UTF-8 无 BOM / CRLF；没有修改生产文件，没有单独暂存或提交。主会话按本阶段授权统一暂存。
