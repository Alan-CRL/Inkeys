# Bug Analysis: UI3 主栏拖动交接与呈现 tuple 分裂

## 1. Root Cause Category

- **Category**: B - Cross-Layer Contract；E - Implicit Assumption；D - Test Coverage Gap
- **Specific Cause**: 交互线程、渲染线程、实际 HWND 与成功呈现快照之间存在四个未被同一合同约束的边界：release tuple 可早于直移 phase 交接；过期 D2D 帧假设帧内 translation 仍代表实际窗口；viewport 与 present mapping 分别决定全量替换；二维命中分别读取 X/Y 快照。

## 2. Why Fixes Failed

1. 只扩大业务 dirty：不能保证 `UpdateLayeredWindowIndirect` 使用空 `prcDirty`，窗口 size/source 改变时仍可能留下旧像素和旧消息区域。
2. 只在松手后请求渲染：请求顺序不能阻止渲染线程在 phase 交接前消费已经稳定的 release tuple。
3. 过期帧回退到帧内位移：保证了帧内几何自洽，却会覆盖交互线程已成功 `SetWindowPos` 的真实位置，形成单帧闪回。
4. 单独修正 X 或 Y：每个公式都正确仍无法阻止两次 seqlock 读取组合出从未上屏的二维坐标。

## 3. Prevention Mechanisms

| Priority | Mechanism | Specific Action | Status |
| --- | --- | --- | --- |
| P0 | Architecture | release tuple 在待吸收直移仍由 `Dragging` phase 持有时只允许 Retry，吸收和 `PositionUpdate()` 先于释放布局 | DONE |
| P0 | Architecture | ULW 在 `directWindowDragMutex` 内以实际已呈现 translation 解析过期帧目的地 | DONE |
| P0 | Architecture | viewport 与 present mapping 合并为唯一整窗替换布尔值，并同时控制 damage 与 `prcDirty` | DONE |
| P0 | Architecture | 主体、抓手二维命中一次捕获成功快照并同时逆映射 X/Y | DONE |
| P1 | Test Coverage | 为交接矩阵、current/stale/publishing translation、整窗替换和非恒等二维映射增加 Headless 回归 | DONE |
| P1 | Documentation | 将上述顺序和失败矩阵写入 native-desktop 渲染/底栏合同 | DONE |

## 4. Systematic Expansion

- **Similar Issues**: 其他 layered HWND 若允许窗口线程/交互线程直移、渲染线程异步 ULW，也需要区分目标状态、实际 HWND 状态和最后成功内容快照。
- **Design Improvement**: 跨线程呈现决策应尽量收敛为单一成功事务布尔值或 tuple；不能让 damage、window size/source 和 hit snapshot 各自推导“当前帧”。
- **Process Improvement**: 审查 resize/translation 修复时，同时检查 D2D clip、ULW `prcDirty`、`pptDst/psize/pptSrc`、成功快照提交和消息坐标消费，不以单个红框或单个坐标公式通过为完成。

## 5. Knowledge Capture

- [x] 更新 `.trellis/spec/native-desktop/rendering-and-ui.md` 的脏区和底栏事务合同。
- [x] 当前任务记录四类根因、失败修法与对应回归测试。
- [x] 未创建额外 issue；本任务已包含根修复与验证范围。
- [x] 按仓库要求不创建 commit。
