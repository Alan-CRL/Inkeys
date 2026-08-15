# RTS 笔尾橡皮支持实施计划

## 1. RTS 与一致快照

- [x] 扩展 `ContactSnapshot`/`ContactRecord` 的倒转标志并同步所有发布、读取和复用路径。
- [x] 在 RTS Down/Move/Up 解码后写入 `StylusInfo::bIsInvertedCursor`，保持同步回调固定成本。
- [x] 扩展快照并发测试，覆盖倒转标志的 Down、Move、Up 和 slot 复用。

## 2. 运行时策略

- [x] 增加默认开启的配置字段和 `DrawingController` 原子 Set/Get。
- [x] 增加可测试的倒转 Pen 工具覆盖/压力屏蔽策略。
- [x] 在 Down 分离批次选择工具与 contact 有效工具，锁定倒转状态和压力屏蔽。
- [x] 对倒转笔画的 Down/Move/Up 模型输入固定传 `pressure=-1`，避免无效压力触发模型更新。
- [x] 复用既有橡皮固定宽度、无 prediction、直接提交真实点路径。

## 3. Validation

- [x] 覆盖开关、设备、倒转状态和工具矩阵，以及关闭开关后的模拟压感回退。
- [x] 运行 `git diff --check` 并检查 UTF-8 BOM + CRLF。
- [x] 使用 ARM64 MSBuild 构建完整 `inkStrokeModelerTest.sln` 的 `Debug|ARM64`。
- [x] 运行 ARM64 `inkStrokeModelerTestTests.exe`。
- [x] 将 MPP2.0 窗口动态测试清单交给用户；用户确认前不提交。

## Risk and rollback points

- `bIsInvertedCursor` 是每回调状态，但工具语义必须只在 Down 锁定，避免中途翻转导致同一几何同时绘制和擦除。
- 批次选择与单 contact 覆盖必须分离，否则倒转 Pen 会污染并发 Touch/正常 Pen 的工具继承。
- 压力只在模型消费边界屏蔽，RTS 原始归一化字段继续保留，便于诊断和未来独立设计压感橡皮。
