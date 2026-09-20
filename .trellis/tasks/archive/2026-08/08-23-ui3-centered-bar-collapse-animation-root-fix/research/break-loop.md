# Bug Analysis: 居中底栏收缩动画反复失效

## 1. Root Cause Category

- Category: Architecture + ownership + validation gap
- Specific Cause: “可见联合外框居中”没有直接所有者，而是依赖根位置、子动画、水平补偿、成功帧 rebase、方向锁存和 viewport 多阶段最终收敛。

## 2. Why Previous Fixes Failed

1. 居中补偿只修正最终变换，没有让布局根本身满足中心不变量。
2. 方向锁存和遗留换向清理增加了状态与重启门禁，但真实设备结果证明它们没有消除原故障。
3. 两阶段 rebase 把 correction 吸收推迟到未来成功帧，扩大了中间态数量和失败路径。
4. 纯 helper 测试验证各组件自身，却没有证明同一真实帧中根位置、主栏几何和窗口包络共同满足中心不变量。
5. 任务记录过早把静态推断写成“已确认根因”和“已修复”，没有等待原复现步骤的真实设备结果。

## 3. Prevention Mechanisms

| Priority | Mechanism | Action | Status |
| --- | --- | --- | --- |
| P0 | Single ownership | 稳定居中态从当前子几何直接反推根节点 | DONE |
| P0 | State removal | 删除 correction、rebase、方向锁存和 stale-side cleanup | DONE |
| P0 | Frame invariant | Headless 动画逐帧断言联合外框中心不变 | DONE |
| P0 | Conservative viewport | 预测阶段传播反推根节点的完整 X range | DONE |
| P1 | Snapshot consistency | hover X/Y 使用同一成功呈现 tuple | DONE |
| P1 | Real-device gate | 未完成原录屏步骤复测前不得宣称 GUI 故障已修复 | TODO |

## 4. Knowledge Capture

- 复杂动画缺陷的测试应验证用户可见不变量，而不是内部状态机按设计运转。
- 如果一个稳定约束需要补偿、延迟吸收和回滚事务才能成立，应优先检查能否让根布局直接满足约束。
- 已被真实设备否定的根因模型必须在任务和规范中显式撤销，不能让后续代理继续加固错误架构。
- 触摸指示器坐标问题保持独立范围。
