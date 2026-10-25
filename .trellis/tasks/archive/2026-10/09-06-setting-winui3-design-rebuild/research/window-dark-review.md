# 窗口与深色阶段代码审查

日期：2026-09-07。基线：89770357。范围以 approved-window-dark.md 为准，覆盖 Setting 窗口框架、显式浅深主题、配置与测试接线；本审查没有操控窗口、重复构建、暂存或 commit。

## 结论

当前代码层没有尚未解决的确定缺陷。审查发现一处多显示器最大化协议问题，已由 frame 实现方修正并复核。真实 GUI 因桌面锁定尚未完成，此结论不等于实机视觉、Snap、任务栏或滚动条拖动已验收。

## Findings (fixed)

### 副屏最大化尺寸被系统再次补偿

- 原候选代码在 Setting.cpp 的 WM_GETMINMAXINFO 中，把实际目标显示器工作区加上 frame 后直接写入 ptMaxSize/ptMaxPosition。
- MINMAXINFO 的最大化尺寸使用主显示器协议，USER32 随后会对目标显示器补偿。若目标副屏比主屏大，直接回填目标尺寸可能被再次扩大。这由官方 MINMAXINFO 文档及 Raymond Chen 对补偿规则的说明确认：[MINMAXINFO](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-minmaxinfo)、[多显示器补偿规则](https://devblogs.microsoft.com/oldnewthing/20150501-00/?p=44964)。
- 推导用例（不是实机采样）：主屏 1920×1080，目标屏 2560×1440、工作区 2560×1400、frame 为 8px。原实现先提交目标外框 2576×1416，仍可能再接受主屏到目标屏的差额补偿，导致客户区越出目标工作区。原 Expand/Inset 往返测试未覆盖这层协议。
- 当前修复：Setting.cpp::WM_GETMINMAXINFO 保留 USER32 预填的最大化位置、尺寸和最大跟踪尺寸；只经 Setting.Layout.h::ApplyWindowFrameMinimumTrack 写最小跟踪尺寸。已恢复 WS_OVERLAPPEDWINDOW，系统负责最大化工作区与任务栏语义。
- 已删除不再被生产使用的 MaximizedWindowFrame / ResolveMaximizedWindowFrame，避免保留一套看似通过几何测试、实际不匹配消息协议的实现。
- 测试代理正在以真实 MINMAXINFO 验证除 min 两值外所有字段保持，并覆盖较大/较小副屏、负原点与不同任务栏位置的明确 fixture。fixture 中的补偿结果不能冒充 USER32 多显示器实机验证。

## 其余已核对项

| 范围 | 代码审查结果 |
| --- | --- |
| 创建与 NCCALCSIZE | Setting 专用创建只外扩一次系统 sizing frame；普通及最大化的 NCCALCSIZE 均使用同一系统 DPI 查询。真实非客户区留在 client 外，不再保留 1px 外框同时把 resize 区扩进内容。FALSE/TRUE 两种 NCCALCSIZE 输入都按对应 RECT 处理。 |
| 命中与滚动条 | NCHITTEST 读取实际 screen-space outer/client，先保护整个 client；八方向 resize 只来自 client 外、outer 内。最大化不返回边缘 resize。 |
| 标题栏按钮 | 主题按钮具有独立矩形，命中优先返回 HTCLIENT，再考虑 HTCAPTION；版本入口与 caption buttons 继续独立。自绘 caption 使用不透明 AppBase 填色，不依赖透明根背景遮挡系统强调色。 |
| 主题切换与保存 | 唯一主题点击分支更新 settingDarkMode，按原 WriteSetting 宏冻结 JSON 入 FIFO，再递增 theme serial。空闲绘制不保存。读配置严格接收 bool，缺项/非 bool 回退浅色，CaptureSettingJson 写同一 SettingDarkMode 键。 |
| DWM 线程与快照 | 渲染线程 PostMessage 仅携带 bool 与 COLORREF 值，无临时指针。窗口线程处理 DWM/backdrop；能力变化发布原子状态并请求样式帧。新路径没有 render 等待窗口、窗口再等待 render 的互等链。 |
| serial 时序 | 初始应用前读取 theme serial，应用后只消费该快照；DWM 异步发布的新 serial 留给后续帧。主题样式在 PushFluentStyle 前应用，普通主题切换没有字体重建 serial、atlas 或 device 重建调用。 |
| 深色覆盖 | AppBase/Card/Text 等默认参数引用稳定 palette 存储；全页文本、卡片、嵌套 child、弹层、Notice、滚动条及主页引导/插画/功能入口均切换语义色。剩余固定白色用于原图片 tint 或红色关闭按钮上的白色字形，不是遗漏的强制浅色页面底。原教程和赞助图片保持原色。 |
| 其他子系统 | 窗口样式修正限定 Setting 分支；Bar/PPT 的 owner、呈现、输入与业务合同未因本阶段改动。 |
| 规范 | rendering-and-ui 与 configuration-i18n-and-assets 已按本阶段显式浅深偏好、真实 NC、USER32 最大化字段保留及 FIFO 接线同步。 |

NCCALCSIZE 的客户区输入/输出判断参照 [WM_NCCALCSIZE](https://learn.microsoft.com/en-us/windows/win32/winmsg/wm-nccalcsize)；创建后触发 frame 重算的做法参照 [DWM 自定义窗口框架](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)。这些资料仅支撑协议审查，不替代本应用 GUI 测量。

## Findings (not fixed)

没有另外发现需要修改的确定代码问题。不要求扩展系统主题跟随、高对比、动画、触摸或其他 UI3 功能。

## Verification

- 静态检查：已逐项查看本阶段产品差异、真实调用路径、配置读写、i18n 两个新增键与测试接入；本代理执行 git diff --check 通过。
- 构建/类型检查：主会话统一执行完整 ARM64 host InkeysRepo.sln Debug|ARM64；本代理不重复构建。最终结论以修复后主会话验收记录为准。
- 无窗口测试：现有几何/浅深主题/字体与绘制数据回归已审读；新增最大化协议回归由测试代理完成后交主会话统一集成运行。不能用修复前的通过日志替代修复后验证。
- GUI：当前桌面锁定，尚未完成本阶段真实浅深切换、标题栏颜色、最大化/还原、右侧滚动条拖动、Snap/系统菜单及重开偏好检查。没有使用旧蓝条截图或上一阶段截图声称新实现已通过。
- 多显示器：纯函数与 MINMAXINFO fixture 能覆盖坐标及字段保留，不能证明真实 USER32、DWM 与不同 DPI 显示器消息顺序。实机覆盖不足时应在最终报告保留此边界。

主会话在最终构建、测试与 GUI 检查完成后补充验收结果；本报告只负责代码审查结论和修复证据。
