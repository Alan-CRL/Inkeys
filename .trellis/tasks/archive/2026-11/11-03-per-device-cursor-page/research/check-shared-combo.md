# 共享下拉消费者复核补充（2026-11-04）

## 结论

只读复核当前新增教学安全回归及 shared-combo-consumers.md，未发现明确问题；未编辑产品或测试。共享控件修复覆盖普通ComboRow、ComboPreviewRow、vector/字符数组直接调用和显式BelowComboMotion；教学安全经RenderGeneral(Interaction)的ComboRow("teaching-safety")进入同一Design::ComboBox，没有消费者特判。

重新搜索Inkeys产品源码（排除additional）未发现直接ImGui/ImFluent BeginCombo/ComboBox/Combo调用绕过该实现。已盘点的手掌橡皮、PPT闲置时长、快捷方式预设、诊断区等消费者也复用此控件；不必为教学安全增加另一套位置逻辑。

## 新测试有效性

- fixture使用真实RenderGeneral(Interaction)，将实际宿主客户区设置为480 DIP高，使用host真实ScrollMax和实际NavRect滚动定位教学安全。明确检查NavIdAlive及入口处于客户区底部，不复制控件的above/below预算公式作为测试模型。
- F4按下后的首次draw返回后立即检查实际popup上开位置，断言发生在F4释放的下一帧之前；因此没有用后续AutoResize收敛结果冒充首帧正确。
- 目标是生产Mode4标签和真实item3作用域ID，先确认导航item存活，再取WindowRectRelToAbs命中矩形。用真实鼠标Down/Up选择最后项，不直接写draft或伪造点击事件。
- teachingDown/Up的safetyModeChanged求和严格为1；仅将这一个预期事件从副本清除后用现有noEdits检查其它GeneralEvents。另比较autoCollapse、topInterval、rightClickClose和allowMultipleInstances与操作前草稿，覆盖同一Interaction页的其它偏好。
- 测试已接在CheckGeneralSections内，general-only和常规语言矩阵都会执行；general-only使用实际20语言资源和1/1.5/1.875三档。没有为消费者添加产品分支、文案或新UI API。

`git diff --check --ignore-submodules=all`通过。本轮唯一新增源码为测试，产品已通过上一轮五项验证的实现没有改变。该新测试的构建/执行仍等待主代理最终退出结果；不将上一轮general-ui通过复述为本次新fixture已通过。没有GUI、commit或归档。

## 本轮复核源码SHA

```json
{
  "InkeysHeadlessTests/setting_design_tests.cpp": "7852548bdd9d8b4025a4d23f1f7db3d21609c990aecdd13438b770c7c0ef60ae",
  "Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp": "b9f69b25649bda3baf6a404adcda1cd6bf9c7aa691662a8ee0245ecc9894c0e8",
  "Inkeys/Inkeys/UI/Setting/Setting.Controls.h": "e61a491aedcb19e848d91d9d9ec7c97d33d6105e1b84ad7d84f98c6369509801",
  "Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp": "653feee1750ef73df583039b1a1165154ef2462018d61aacbfe6388c89fd0a03"
}
```

## 共享消费者最终闭环

全量产品入口审计确认教学安全、光标、PPT闲置时长、快捷方式预设等均通过同一Design::ComboBox；没有页面位置特判或绕过的直接ImGui/ImFluent下拉。新增真实RenderGeneral(Interaction)教学安全位于客户区底部的首帧上开/鼠标末项单事件回归，已在general-only的20语言×3DPI全部通过。新增用例后完整solution再次退出0，五项受影响无窗口检查再次全部退出0。产品Inkeys.exe SHA与补用例前完全相同，证明本次仅补验证，共享产品实现未变。40项源码身份保持。没有GUI、commit、push或归档。
