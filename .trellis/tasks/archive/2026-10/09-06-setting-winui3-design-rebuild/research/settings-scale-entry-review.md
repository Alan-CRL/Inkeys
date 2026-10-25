# 设置缩放与开屏策略独立审查（2026-10-24）

Active task: `.trellis/tasks/09-06-setting-winui3-design-rebuild`。本次审查只读产品代码和差异，仅写入本研究记录；未构建、运行测试、启动 GUI、修改配置或 commit/push。父任务保持 `in_progress`。

## 结论

未发现未解决的阻断性实现问题。前轮四个插件 PNG 差异、既有两张 CSO 不属于本轮审查修正范围。

## 已核对

- `UpdateSettingScale` 最终仅取 `DpiScale(dpi)`；手动倍率变量、控制行、提交路径和配置字段均已退役。`WM_DPICHANGED` 仍请求原有字体重建，窗口最小尺寸按系统 DPI 计算；主栏 `UI.Bar.Zoom2`、任务栏基础量、PPT 缩放路径保持。
- `GeneralDraft` 删除字段后的生产及测试聚合初始化已适配；新 `settingEntryAnimation=1` 放在尾部默认字段，避免其他草稿错位。
- `SettingEntryAnimation` 仅读取整数 0–2；缺项、错误类型、越界回退 1，捕获时越界也输出 1。`SettingGlobalScale` 不再读取且保存显式删除，不受 AutoClean 影响。
- `SessionState::SetVisible` 只在隐藏到显示时递增 ShowSerial；重复激活不递增，`ReleaseResident` 不清零。策略要求 serial 非零，首开模式只接受 serial==1，因此隐藏预热不占用、首次尺寸不符或总动画关闭后第二次也不会补播。
- `ConfigureHomeMotion` 仅用 entryAllowed 决定本次 Pending/Skipped；enabled、delta 与 pointerAllowed 保持原用途，未关闭主页悬停/视差。
- 新增真实页面测试覆盖首开小视口、第二次大视口不补播、始终模式后续播放、关闭只停开屏、总动画关闭仍消耗首开；SessionState 测试覆盖重复显示、resident 释放与新进程。
- 通用页测试保留主栏拖放状态验证，新增模式行存在/旧设置缩放行不存在、键盘选择仅一次变更事件等检查。测试未由审查代理执行。

## 发现及修复复核

1. 原启动尺寸注释仍写“高 DPI 叠加用户倍率”；现已确认改为“高系统 DPI 下，启动窗口仍需完整落在工作区内”。
2. 新选项说明原未限定首开模式，选择“始终”时可能产生歧义；现已确认简体、繁体、英语都以“选择首次启动时后”或等价表达限定说明。

两项建议均已修复，无未解决审查意见。三语言对应 `StartUp.SettingEntryAnimation` 子键和选项顺序一致。主会话报告文案修复后的 i18n check 490/490 与完整 Debug|ARM64 Solution exit 0；本代理已查看构建日志的目标输出，但未独立重跑构建。无窗口全套验证由主会话继续记录，本文不提前宣称测试通过。
