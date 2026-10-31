# 双画布实施计划（已实现并通过自动验证）

- [x] 用户批准父任务规划后才start本子任务；读取本PRD/design、manifest及父任务最新双画布研究。
- [x] 记录源文件编码/BOM/CRLF及dirty边界。只负责Window Service双画布policy和必要scene接线，不修改其他子任务的Display/Bar代码。
- [x] 将scene到gap/NonRude的最小策略和window-local区域计算形成可测生产helper；不新建通用窗口框架。
- [x] 接首次Show、白板window-mode双窗事务、bounds/重建与销毁；失败读回、HRGN归属、逆序回滚/未收敛重试，同值no-op。
- [x] 检查IdtState进入失败、Entering取消、退出重试和Exiting重入均消费相同已应用状态；保留Freeze锚点与原显隐/激活次序。
- [x] 有意义无窗口测试覆盖Desktop/PPT/Whiteboard、两角色、负屏坐标、1px不乘DPI、bounds和HWND换代、第一/第二窗失败与资源释放；客户端/Host/doc尺寸不变，不额外resize。
- [x] Trellis check审阅全部本子任务diff；不顺手修复现有COM/Topmost架构，未实测Shell行为如实记录。
- [x] 与主栏子任务完成后由父任务按既有solution Debug|ARM64构建并执行InkeysHeadlessTests --no-window，最少必要验证，不重复无理由全量构建。
- [x] 将实际实现决策与验证写research/validation.md及相关spec；未经用户新的明确授权不GUI、不commit/push。

## Context校验提示
2026-10-19 task.py validate通过。rendering-and-ui.md、draw3-integration.md、runtime-and-rendering.md部分超出32768字节注入上限；注入内容会截断，不等于已经完整阅读。实施及check代理须按本任务研究中的章节/源码锚点，用rg定位并显式补读相关spec全文段落（白板/双画布/窗口所有权/resize/Bar贴底与输入呈现），不能仅依赖manifest自动注入。无需拆分或改写既有spec来消除该警告。

## 实施授权更新
2026-10-19：用户明确回复“批准，并开始实现”。以上规划门禁的批准条件已满足；按既有计划进入实施，GUI与commit/push仍未授权。

## 执行完成记录
2026-10-19：所列实现/静态检查/完整solution及无窗口验证已完成，详见父任务research/validation.md。涉及真实Shell/窗口视觉的项目仍未经GUI验证；此处勾选实施步骤不代表已取得实机GUI结果。未commit/push，任务保留in_progress待用户验收。
