# Bar 非全屏 HWND 与脏区呈现优化：技术设计草案

> 状态：规划中。本文描述候选架构和保守默认值，不代表已批准实现。

## 1. 设计原则

1. 将“资源容量”与“当前窗口大小”解耦。
2. 将资源重建限制在设备 epoch、DPI 或布局容量发生变化时。
3. 动画帧只更新内容、脏区和当前 HWND 矩形。
4. 优先消除全屏资源；只有基准证明必要时才引入 staging。
5. 对 Win7 SP1 未验证的 ULW 组合采取保守全脏策略。

## 2. 核心几何模型

维护三个矩形和三个坐标空间：

- `capacityRect`：最大容量画布，覆盖所有合法状态及效果外扩。
- `windowRect`：当前帧实际 HWND 在屏幕中的矩形。
- `damageRect`：稳定画布坐标中的累计内容损伤。
- 屏幕坐标：用于 `pptDst`。
- 最大容量画布坐标：用于布局、D2D 绘制和持久资源。
- HWND 本地坐标：用于 `psize` 和 `prcDirty`。

`pptSrc` 选择 `windowRect` 对应的容量画布起点。提交前执行：

```text
damageRect（容量画布坐标）
  -> 与当前源区域求交
  -> 减去 pptSrc
  -> 裁剪到 [0, psize.width) × [0, psize.height)
  -> prcDirty（HWND 本地坐标）
```

如果 `windowRect`、`pptSrc` 或坐标映射发生变化，保守地把整个新 `psize` 作为 `prcDirty`。窗口移动后旧屏幕位置的恢复由窗口系统处理，不要求在 backing surface 内擦除旧屏幕区域。

## 3. 生命周期

### 3.1 容量纪元

以下事件触发重新计算最大容量：

- Bar 启动；
- DPI 或主要显示器变化；
- 布局、缩放、组件集合或动画外扩上限发生变化；
- 当前内容首次突破已有容量。

重新计算时枚举所有合法 UI 状态，合并几何范围，再加入阴影、模糊、光照、描边、抗锯齿和安全 padding。只有新容量无法由现有资源承载时，才重建 target 和相关资源。

### 3.2 普通帧

1. 推进动画并派生布局。
2. 用现有元素追踪器累计新旧几何的联合脏区。
3. 计算当前可见包围盒，应用 padding、尺寸量化或滞回得到 `windowRect`。
4. 若窗口/源映射变化，将当前窗口设为全脏；否则映射既有 `damageRect`。
5. 用脏区裁剪 D2D 绘制。
6. 通过 ULW 一次提交位置、大小、源起点、混合参数和内容。
7. 仅在完整事务成功后提交脏区快照；失败则保留并升级为全脏重试。

## 4. 呈现路线

### 4.1 首选基线：缩小后的 GDI-compatible D2D target

```text
持久最大容量 D2D target
  -> GetDC(COPY)
  -> UpdateLayeredWindowIndirect(pptDst, psize, pptSrc, prcDirty)
```

优点：最大程度保留现有 D2D 1.1、效果链、缓存和事务模型。主要变化是目标不再达到 4K 全屏大小。缺点是 `GetDC(COPY)` 仍可能同步整个最大容量 target。

该路线必须先实现并测量。若最大容量只有 Bar 的有限范围，全量同步可能已经低于继续优化的收益阈值。

### 4.2 条件路线：持久 dirty staging 和 DIB/HDC

仅当基准确认 `GetDC(COPY)` 仍是主要瓶颈时研究：

```text
持久 D2D target 的 damageRect
  -> CopyFromBitmap 到持久 CPU_READ staging
  -> Map 并 memcpy 脏行到持久 DIB Section
  -> ULW(prcDirty)
```

- staging 不按帧创建，可按 256/512/1024 等容量分档或只增不减。
- DIB/HDC 至少覆盖最大可提交窗口范围，并持续保存未变化像素。
- 两次复制都限制为脏区；额外的 Map 同步成本必须纳入基准。
- 窗口范围变化帧先按全窗口内容刷新 DIB，避免新暴露区域无效。

### 4.3 暂不采用的路线

- 每帧创建脏区临时位图。
- 每帧按精确内容包围盒重建 D2D target。
- 将整个绘制管线替换为 IWICBitmap/CPU 光栅。
- 假设存在标准 D2D 1.1 API，可将任意脏区一次复制到普通 HDC。
- 为得到小 HDC 而每帧创建或调整 GDI-compatible scratch target。

## 5. HWND 尺寸策略候选

后续研究按以下顺序比较：

1. 固定安全 padding 的当前内容包围盒。
2. 尺寸和位置按小网格量化，降低频繁变化。
3. 扩大立即生效、缩小延迟若干帧的滞回。
4. 对高频动画组件使用预先计算的局部状态包围盒，避免逐像素改变 HWND。

无论选择哪一种，都不改变最大容量资源；它只决定每帧的 `windowRect`。

## 6. 兼容性和失败处理

- API 边界保持在 Windows 7 SP1 + KB2670838 可用的 D2D 1.1 和 layered-window 能力内。
- 尺寸或源映射变化帧默认全脏，以规避不同系统版本对组合更新的细节差异。
- 保留当前 `GetDC -> ULW -> ReleaseDC -> EndDraw` 的分阶段结果检查和提交事务。
- 设备丢失、ULW 失败或任一同步步骤失败时，不推进已提交脏区快照；下一帧全脏重试。

## 7. 性能验证模型

至少记录：

- 当前 4K 全屏基线；
- 非全屏最大容量 target + 当前 GetDC 路线；
- 可选 staging/DIB 路线；
- 静止、简单悬停、面板动画和最复杂状态四类负载；
- 100%、150%、200% DPI；
- `GetDC`、ULW、Map/memcpy 和整帧耗时；
- `damageArea / windowArea / capacityArea`；
- PresentMon 中下方 flip-model 窗口的 presentation mode。

只有“非全屏基线仍明显受 `GetDC` 限制，并且 staging 的端到端帧时间更低”时，才选择 4.2。

## 8. 风险

- 最大状态枚举遗漏效果外扩会造成裁剪。
- HWND 高频 resize/move 可能抵消减少像素面积的收益。
- `pptSrc` 变化会改变整张窗口的像素映射，不能沿用旧的局部脏区。
- staging 引入额外同步、CPU 带宽、pitch 和预乘 BGRA 正确性风险。
- layered HWND 缩小可能改善 DWM 合成条件，但 Independent Flip/MPO 仍受硬件和系统策略影响。
