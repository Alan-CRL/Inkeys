# 共享下拉消费者审计（2026-11-04）

用户要求同类问题全部从控件解决。源码盘点确认：

- Setting.Controls.cpp 的字符数组重载是唯一产品绘制实现；vector重载转发至它。
- ComboRow、ComboPreviewRow均调用同一ComboBox。教学安全位于Setting.Pages.cpp的RenderGeneral(Interaction)，ID为teaching-safety，经ComboRow进入。
- 其它直接消费者：手掌橡皮预览比例、PPT闲置时长、桌面/白板快捷方式名称预设，以及设置诊断区响应模型/标尺来源/屏幕笔beta，均调用Design::ComboBox。
- 对Inkeys/Inkeys/UI与整个产品源码（排除additional第三方/未编译legacy）的ComboBox/BeginCombo搜索，未发现直接ImGui或ImFluent生产下拉绕过路径。测试中的上游对比fixture不属于产品消费者。

因此现有共享上下预算修复已经覆盖这些入口，不增加教学安全或光标专属位置分支。增补真实教学安全底部选择回归，由同一控件代码得到结果。
