# 荧光笔起笔端帽稳定性实施计划

## 1. 几何闸门

- [x] 在统一 `BuildHighlighterGeometry` 中识别全局 Start + 未锁定状态。
- [x] 非 short mark 返回空 primitives/bounds，添加简短中文注释说明首次可见契约。
- [x] 保留 shortStrokeMode 的确定性 `12×50px` 路径。

## 2. 锁定与分层复核

- [x] 验证真实点累计 12px 时锁定方向，预测不参与。
- [x] 验证 `CommitStablePrefixToL1` 在锁定前不提交。
- [x] 验证追加 Move、L1 切片和完成态不改写 start anchor/direction。

## 3. 自动化几何测试

- [x] 覆盖 `<12px` 活动态不可见与 Up short mark。
- [x] 覆盖 `=12px`、刚超过 12px、长直线和长曲线。
- [x] 保存首次可见 start cap 并与追加点、L1 切片、Up 完成态比较。
- [x] 覆盖重复点、Cancelled 和非 Start 切片。

## 4. 质量门

- [x] 运行测试工程全部几何用例。
- [x] ARM64 Debug/Release、x64/x86 Release 全解决方案构建。
- [x] 运行 `trellis-check`、格式/编码检查和人工荧光笔视觉回归；用户确认旧“缺角”为橡皮擦除结果。
