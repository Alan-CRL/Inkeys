# 名称卡片与下拉弹层细化（用户截图反馈）

## 用户本轮明确要求

- 名称选择区内联于上方“创建快捷方式”卡片，类似截图二 CheckboxGroup 的单外框、标题/内容分隔线；不是上下两张独立卡片。
- 下拉选项弹层从下拉按钮下方出现，不遮挡左侧文字；打开、关闭都有动画，关闭效果沿用设置块退出的视觉语言。
- 下方“缺失时创建桌面快捷方式”说明明确体现“启动时”。
- 用户提供的是参考截图，不包含 GUI/Computer Use 测试授权；仍仅允许无窗口验证、无 commit/push。

## 已确认源码证据

- Setting.Pages.cpp 的 DesktopShortcuts 当前分别调用 ButtonGroupRow 和 BeginCard("##desktop-shortcut-name")，导致两个独立圆角外框。
- Setting.Controls.cpp::CheckboxGroup 在一个 BeginCard 内绘制标题、横向分隔线和内容区域，是图二的现有实现参照。
- 当前 ComboBox 封装虽然设置按钮下方初始位置，但尚无打开/关闭弹层动画；需核对 ImGui 真实 popup 几何、viewport 限制和 page/card motion 变换，按实际原因修复，不凭猜测全局改定位。
- 原创建条目说明仍提“更多选项”，新页已无该入口，应同步去掉这句过期引导，准确说明按下方名称创建桌面快捷方式。

## 实施边界

- 最小修改设置页面及必要控件辅助；复用既有卡片布局、动效时钟、速率和关闭动画选项。
- 下拉弹层的位置/动画需求限定当前名称选择控件。若需共享辅助，用可选行为或既有机制接入，保持其他 Combo 默认行为；只有证据证明共享现有行为本身必须修正时才扩大，并向主会话报告。
- 打开/退出动画须连续，中途反向从当前状态续接；退出外观仍绘制但不可继续选择/提交；关闭全局动画即时到位。点击外部、再次点击入口、选择、Esc/Tab、离页/隐藏/语言变化应正确结束；不缓存跨帧裸 ImGuiWindow/DrawList/字体指针。
- 不改变名称保存、配置、两个开关业务语义或真实桌面动作。
- 各语言修正上方创建说明的过期“更多选项”引导和下方补建说明的启动时机。

## 验证

- 实际 Headless 控件/DrawData：同一外框和分隔线、左右布局及窄窗/DPI；弹层真实 bounds 在按钮下方、与左侧标签不相交（空间不足时允许有界缩短/滚动，不跑到左侧）；开/关中间帧、终态、反向、全局动画关闭、选择一次、退出无输入。
- 运行足以覆盖本次 UI 改动的既有设置/控件/动效定向 no-window 测试，不无理由重跑耗时全量 WARP 基准。
- 实际 C++ 验证仍构建完整 InkeysRepo.sln Debug|ARM64，动态发现 ARM64 MSBuild；采用本任务已验证 canonical Path、MSBUILDDISABLENODEREUSE=1、/m:1，同次 PowerShell 调用且 >=5min。沙箱 vcpkg/stdin 问题可用相同命令 require_escalated；不改 SDK/依赖/项目配置。
- 测试子进程 TEMP/TMP 指向 workspace Build scratch，GUI 子系统 CLI 必须 Process.WaitForExit 取得真实退出码。
- i18n sync/check、BOM/EOL/diff 检查；不重新生成/移动/删除此前已回收的历史任务 PNG。
