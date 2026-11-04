# Bug Analysis: CopyButton末项ImGui边界

## 1. Root Cause Category

- E（隐式假设）+ D（测试覆盖缺口）：误以为只恢复X的SetCursorScreenPos不会影响边界检查，但传入Y包含按钮后ItemSpacing，而CursorMaxPos不含spacing。
- ItemSize提交按钮后清IsSetPos；尾部SetCursorScreenPos重新置true，EndChild内部End检查Y>CursorMaxPos，触发用户截图中的原始错误。
- 证据：本地imgui.cpp的ItemSize/SetCursorScreenPos/ErrorCheckUsingSetCursorPosToExtendParentBoundaries；新增严格末项回归在坏helper不变时exit3，打印相同child窗口与imgui.cpp:10837原始断言。

## 2. Why Prior Validation Missed It

- 上轮复制测试验证了尺寸、右齐、ID、左cursor和完整内容，但每次之后绘制后续标题，窗口结束前还提交Dummy。
- 后续item执行ItemSize清标记并登记边界，因此不能证明helper作为最后一项时正确。编译也不能检测这一运行时容器协议。
- 本次先保留坏helper新增真实回归取得红证据，再修改布局并跑绿；不是关闭断言/tooltip、清内部旗标或在每页EndChild前加占位补丁。

## 3. Prevention Mechanisms

| Priority | Mechanism | Action | Status |
| --- | --- | --- | --- |
| P0 | 正确布局API | CopyButton正offset使用Indent/Unindent；offset0跳过，保留按钮ItemData和原native纵向间距 | 完成 |
| P0 | 真实末项回归 | 按钮后直接EndChild/End，无后续item，真实错误计数必须0且assert/tooltip配置不变 | 已加入，attention专项通过 |
| P1 | 边界矩阵 | root/固定child/AutoResizeY、滚动/裁剪、非零7.5DIP间距、外层Indent、恰好/更窄宽度、全部Runner DPI | 已覆盖 |
| P1 | 域规范 | 复制控件合同明确末项协议，不只测有后续item的布局 | 已写入spec |

## 4. Systematic Expansion

- 必要审计SettingRow/NavigationRow：它们先Dummy登记完整height+RowGap，再恢复到已登记边界内，无同类缺口，不扩大修改。
- Text/InfoText/AdvanceGap以Dummy提交布局，不需要借本次修复改造。
- 不引入新的布局框架，修复在共用CopyButton所有者，所有页面一起获益。

## 5. Knowledge Capture

- 已更新settings-performance-and-drawing.md复制合同与Tests Required。
- 红/绿证据保存在validation/cursor-boundary；最终命令/退出码见validation-cursor-boundary.md。
- 本仓库为应用项目，没有Trellis模板源同步目标；按用户约束不提交、不自动归档。
