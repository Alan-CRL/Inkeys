# 状态、输入、Draw3 与 PPT 7 commit 二次审查

- 基线 H0 `8b156fca59f0337a6afc6d722941666fcf143080`，对照 2026-09-28 当前未提交工作树。逐项读实际 diff，追当前调用链与失败路径；无 GUI、编译、采样。
- 结论：7 项已完成静态深审。一个低严重度现存视觉身份问题 F-028 在本任务工作树作最小修补、尚待构建/动态验证；旧 Eraser 枚举硬编码缺陷在 H0 祖先 `7af954bb` 已修。F-026/F-027 运行期图形失败、保存/双窗交接仍是发布风险；不能由审计“已读”升级 PASS。

| SHA | 当前代码映射与结论 | 待验证 |
| --- | --- | --- |
| `6dccc597` | Slider 连续宽度经 `Bar.Interaction::ProjectWidthFromScreenX` 到 `SetPenWidthIfRevision`；H0 的 `IsBarThicknessPresetSelected` 与 hover 把 3.2px 四舍五入为 3px，误报 3px 预设。工作树 Layout/Interaction 已复用原始值比较，F-028 已修复待验证。 | 生产 helper/GUI 粗细、Highlighter、高 DPI；既有测试无此用例 |
| `97b90fba` | HardPen/SoftPen 通过状态桥与 Down 时样式发布；当次更改 Eraser enum=3 却留 `InkPrediction` 常数2，导致倒转笔断触候选误拒。H0 祖先 `7af954bb` 已改枚举转换；本次 F-009 状态入口另修。 | 产品倒转笔断触真输入，旧样板测试不可冒充产品 |
| `39c32be1` | Laser 独立宽度/预设、Host 到 Controller 实际直径、Bar 预览保留；后续 `9064374`/`a6bf819`/`ea277bf` 改过单位与过渡。未确认该 SHA 现存严重缺陷。 | Laser DPI、触控预设、反向动画、F-025 栅格事务单独验 |
| `ca8d06e3` | 高亮有效 alpha `0.35`、Bar 显示和光标样式链仍在，后续 `97b90f` 调整长边。未确认残留缺陷。 | 真 D2D/RTS 光标像素与硬件 DPI |
| `348de674` | Down 锁存 ProductVisualStyle、兼容鼠标消息过滤、双窗回切输入；修 `017883f` 后的首 Down 穿透历史问题，H0 已包含。 | Win7/笔设备真实 WM_TOUCH/兼容鼠标序列 |
| `017883f6` | `DrawpadPresentation` 双 HWND、ready revision 与 Window Service 显隐链保留。运行期 DComp→同 HWND ULW 拒绝后 Host 静默停止及保存顺序仍是 F-027/F-026。 | 故障注入、双窗/输入、Win7、保存屏障 |
| `6303e338` | Clear 与内容真值、TimerPeriod、PptCOM 与本机诊断路径保留。F-018/F-024 为当前工作树相关独立修补。本地 Release 未定义 `IDT_RELEASE`，诊断代码编入，但四开关默认 false、用户显式开启后才本机输出；正式 publish workflow 检查宏。无默认泄露确证。 | 正式宏/产物、COM DLL/TLB、PPT/Clear/页面/故障路径 |

规范漂移：`.trellis/spec/native/runtime-and-rendering.md` 旧 `DrawingTool` 数值与当前 `Draw3.WindowControl.cppm` 不同；`draw3-integration.md` 工具列表缺 HardPen。需按代码更新规范，同时把旧序列化枚举 `StoredInkType` 与运行期 `DrawingTool` 分开说明。

复查命令：逐项 `git show --format= --no-ext-diff --no-renames <SHA> -- Inkeys/ InkeysHeadlessTests/ PptCOM/`；`git show 97b90fba:Inkeys/Inkeys/Drawing/Draw3/Draw3.InkPrediction.cpp` 与 `git show 7af954bb -- .../Draw3.InkPrediction.cpp`；`rg -n 'IDT_RELEASE|ConsoleOutput'` 查工程、头文件、工作流及产品代码。
