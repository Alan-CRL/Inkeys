# 荧光笔起笔端帽稳定性设计

## 1. 根因

`AppendNewModeledPoints` 只在真实路径达到 12px 后锁定起始方向，但 `BuildHighlighterGeometry` 在锁定前仍能从短路径生成活动 body。随着后续点改变切线，已经可见的 start cap 会旋转；完成态重建又可能使用不同方向。

## 2. 单一可见性闸门

在统一 `BuildHighlighterGeometry` 入口加入条件：

```text
boundary includes global Start
and not shortStrokeMode
and start direction is not locked
=> return empty geometry
```

该入口已经同时服务活动 L0、L1 稳定提交和完成态，因此闸门不会在调用点复制。`shortStrokeMode` 只由 Up 的不足 12px 确定路径启用，允许生成固定 short mark。

## 3. 锁定和分层

- 锁定方向继续由真实点累计长度触发，预测不参与。
- 锁定值是从按下点到首个达到 12px 的真实路径位置的归一化弦方向。
- 全局 Start 几何读取锁定值；内部切片使用自己的边界语义。
- `CommitStablePrefixToL1` 继续在未锁定时拒绝提交，因此首次可见之前不会留下旧方向的 L1 像素。

## 4. 可测观测

测试专用帮助函数读取生产几何输出中首个 Start primitive 的起点和方向，不向正式模块导出新的运行 API。测试保存第一帧可见值，并在追加点、切片、完成态重建后作容差比较。

## 5. 风险

主要风险是把内部切片误判为全局 Start，导致稳定前缀消失；测试必须同时覆盖完整边界与 L1 切片边界。另一个风险是短划也被闸门吞掉，因此 shortStrokeMode 必须先于未锁定检查。
