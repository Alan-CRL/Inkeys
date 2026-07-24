# Bug Analysis：换边后半程改变布局重复收拢

## 1. Root Cause Category

- **Category**：E — 隐式假设；同时存在 D — 集成场景覆盖不足。
- **Specific Cause**：旧实现将“是否继续换边关键帧”间接绑定到通用的 70% 批次加入上限，但唯一中间关键帧位于 50%。因此 50%～70% 之间虽然已经越过中点，新的布局变化仍可能沿用旧批次语义。

## 2. Why Fixes Failed

1. 上一轮只处理“超过 70% 后新建批次仍携带旧中点”的分支，修复了最后 30% 的重复关键帧，却没有消除 50%～70% 的语义空档。
2. 编译只能验证类型和语法，无法覆盖“换边动画进行中再切换绘制模式”的组合时序；缺少针对关键帧前后边界的运行回归测试。

## 3. Prevention Mechanisms

| Priority | Mechanism | Specific Action | Status |
| --- | --- | --- | --- |
| P0 | Architecture | 将 `BarUiTimelineClass::CanJoin()` 的统一默认上限设为关键帧中点 `0.5` | DONE |
| P1 | Code Review | 修改动画批次阈值前搜索所有 `CanJoin()` 调用及百分比注释 | DONE |
| P1 | Manual Test | 分别在关键帧前、恰好中点附近和关键帧后改变主栏/绘制属性目标 | TODO |

## 4. Systematic Expansion

- **Similar Issues**：主栏布局变化和绘制属性加入主栏批次共用同一默认阈值，必须保持一致。
- **Design Improvement**：批次复用边界与唯一关键帧中点统一，避免出现“关键帧已过去但仍复用原截止时间”的灰色区间。
- **Process Improvement**：动画时序修复除完整编译外，还应覆盖关键帧边界前后的组合操作。

## 5. Knowledge Capture

- [x] 更新 `.trellis/spec/native-desktop/rendering-and-ui.md`，记录 UI3 单关键帧批次的加入边界。
- [x] 当前仓库不存在 `src/templates/markdown/spec/`，无需执行模板同步。
