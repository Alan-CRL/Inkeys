# UI3 白板工作区

## Goal

提供独立于 PPT 的进程内临时白板工作区，复用 Freeze 作为全屏墨绿色背景，并让 Draw3、Bar、分页控件和窗口组在进入、翻页、最小化、恢复与退出时保持一致。

## Requirements

- 白板使用独立 Draw3 文档和页索引；内容在本次进程内保留，程序重启后清空。
- Freeze 绘制主显示器完整范围，背景色为 `#123B32`；旧 Freeze、恢复提示和 PPT loading 不得覆盖白板。
- 左右分页控件均为 `230x80 DIP`，距屏幕左右和底边 `5 DIP`；每个控件复用三个真实的 Bar `twoTwo` 按钮及 Bar 的布局、动画、光效、主题和脏区合同。
- 上一页和下一页复用 Bar SVG 几何；页码按钮保留标准交互动画但点击无业务动作。末页右按钮显示 Add，点击追加空白页。
- 翻页事务中只锁住输入。未变化的箭头、文字和 Add/Arrow 语义保持稳定；只为真实变化的页码内容执行主栏同款切换动画。追加事务期间右按钮始终保持 Add。
- 进入白板及任意 workspace 状态切换时收起绘制属性、几何属性、更多菜单、笔属性菜单和粗细预览，并撤销辅助面板 capture；白板进入稳定态时这些面板仍保持关闭。
- 白板激活时隐藏 PPT 控件并暂停 PPT 页码发布；退出后按最新 COM 状态恢复。
- A2 固定按钮归一化为 `Whiteboard(twoOne)` 与 `Freeze(twoOne)`；白板激活时隐藏 Freeze，Whiteboard 扩展为 `twoTwo`，显示关闭图标与“关闭白板”。
- 白板选择模式仅作为不可绘制的拖拽状态，不切换图标、不显示 Selection ULW、暂不平移画布；白板始终使用主 Drawpad。
- 进入白板时主栏动画停靠到屏幕底边上方 `5 DIP`，保留左右展开方向并锁住水平滑动；离开底栏、收起主栏或退出白板时解除该锁。
- Whiteboard 窗口组保持 `HWND_NOTOPMOST`。Freeze 是唯一 `WS_EX_APPWINDOW`、可激活和任务栏锚点；Drawpad 可激活但保持 `WS_EX_TOOLWINDOW`，其余辅助 UI 不出现在任务栏。
- Window Service 统一处理白板窗口组最小化/恢复，并保存各成员此前可见性；周期刷新不得把窗口组重新推到 TOPMOST。
- 白板退出必须恢复 Presentation 的 click-through、主栏折叠和 dock 状态、窗口 activation style 与 topmost 顺序。
- Whiteboard 的 D2D/GDI present 使用 Bar 相同的红色 dirty / 绿色 present 调试语义；不得增加蓝框。借用的 COM 资源必须存活到对应 `EndDraw` 完成。

## Acceptance Criteria

- [x] 白板使用独立 Draw3 workspace/page runtime，Presentation 与 Whiteboard 内容和页码不串线。
- [x] 分页控件采用 `230x80 DIP` 与三个标准 Bar `twoTwo` 按钮，SVG、主题、动画、光效和脏区行为复用主栏合同。
- [x] 分页状态归一化并锁存上一稳定帧的 Previous enabled 与 Add/Arrow 语义；`switching=true` 只禁用输入。
- [x] 进入白板和 workspace 状态切换会收起所有辅助面板并撤销 capture。
- [x] Freeze 是唯一任务栏/激活锚点，Drawpad 与辅助 UI 的 style、owner、NOTOPMOST 和窗口组最小化/恢复合同有 headless 覆盖。
- [x] 退出顺序恢复 Presentation click-through、Bar 折叠/dock 状态和 topmost。
- [x] Whiteboard/Bar 的 present 事务持有 D2D/GDI COM 资源至 `EndDraw`，失败路径统一结算。
- [x] `Debug|ARM64`、`Debug|x64` 完整 Solution 构建与两架构 `--no-window` 测试通过。
- [ ] 可见 GUI 验收：连续 50 次进入/退出、任务栏最小化/恢复、桌面首次点击穿透、D2D Debug Layer。

## Scope Boundaries

- 不实现白板磁盘保存、导出、缩放、平移手势或独立设置页。
- 分页控件不支持拖动和位置持久化；页码按钮暂不打开页管理界面。
- 当前任务不通过可见窗口测试突破仓库的无窗口执行约束。
