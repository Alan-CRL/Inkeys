# 主栏任务栏适配实施计划（已实现并通过自动验证）

- [x] 用户批准父任务规划后才start本子任务；加载本规划、父共享设计及manifest。
- [x] 记录源码格式/dirty状态，明确只负责Display、Bar、必要CenteredBottomBar预览及相应用例；画布Window policy由另一子任务负责，不覆盖其他修改。
- [x] 扩展Display逐屏逻辑状态/稳定anchor，复用完整厚度；生产纯策略用例覆盖微小露出条、迟滞、Unknown/失效与多屏。
- [x] observer接非注入事件合并、条件轻量兜底及2秒结构恢复；检验同值无新generation、旧epoch拒绝、线程/资源对称。
- [x] 同一dock-line供渲染/输入/成功呈现环境及启动预览；回归顶/侧/底、真实5DIP、居中/浮动/PPT优先级。
- [x] 接明确Y过渡状态，排除稳定SetDirect覆盖及第二写者；测试快速反向、同目标幂等、animation off、X不变、拖动/恢复交接。
- [x] 最终viewport及SetWindowPos直移保护最底1物理行，验证阴影/描边/回弹/极端UI倍率及旧像素清除；不扩为PPT主栏规则修改。
- [x] 扩展InkeysHeadlessTests的bar_bottom_dock_tests、bar_display_transition_tests、display_tests、ppt_session_tests、startup_preview_state_tests及必要呈现回归，测试生产helper。
- [x] Trellis check全子任务审阅；父任务整合后执行solution Debug|ARM64和Headless --no-window，记录实际spec变化和验证结果。
- [x] 未获本任务新的明确授权不GUI、不commit/push；保证observer清理不留hook/timer。

## Context校验提示
2026-10-19 task.py validate通过。rendering-and-ui.md、draw3-integration.md、runtime-and-rendering.md部分超出32768字节注入上限；注入内容会截断，不等于已经完整阅读。实施及check代理须按本任务研究中的章节/源码锚点，用rg定位并显式补读相关spec全文段落（白板/双画布/窗口所有权/resize/Bar贴底与输入呈现），不能仅依赖manifest自动注入。无需拆分或改写既有spec来消除该警告。

## 实施授权更新
2026-10-19：用户明确回复“批准，并开始实现”。以上规划门禁的批准条件已满足；按既有计划进入实施，GUI与commit/push仍未授权。

## 执行完成记录
2026-10-19：所列实现/静态检查/完整solution及无窗口验证已完成，详见父任务research/validation.md。涉及真实Shell/窗口视觉的项目仍未经GUI验证；此处勾选实施步骤不代表已取得实机GUI结果。未commit/push，任务保留in_progress待用户验收。
