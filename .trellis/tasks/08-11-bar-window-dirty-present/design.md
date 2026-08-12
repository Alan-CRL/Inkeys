# Bar 非全屏 HWND 与脏区呈现优化：技术设计草案

> 状态：已批准实施。默认先落地缩小后的 GDI-compatible D2D target 与动态 HWND，不预先引入 staging/DIB。

## 1. 设计原则

1. 将“资源容量”与“当前窗口大小”解耦。
2. 将资源重建限制在设备 epoch、DPI 或布局容量发生变化时。
3. 动画批次开始时预留完整扫掠包络；批次内只更新内容和脏区，不逐帧改变 HWND 尺寸。
4. 优先消除全屏资源；只有基准证明必要时才引入 staging。
5. 对 Win7 SP1 未验证的 ULW 组合采取保守全脏策略。

## 2. 核心几何模型

维护四个矩形、一个容量原点和四个坐标空间：

- `capacitySize`：相对 Bar 锚点的最大持久资源尺寸，不包含锚点在显示器上的可拖动距离。
- `capacityOrigin`：容量 surface 左上角在显示器布局坐标中的原点；动画批次内稳定，移动 Bar 时可以平移，但同尺寸资源继续复用。
- `reservedEnvelope`：当前动画/交互批次已经为 HWND 保留的容量内范围。
- `viewportRect`：本次实际提交的 HWND 视口，通常等于保留包络经像素对齐后的结果。
- `damageRect`：显示器布局坐标中的累计内容损伤。
- 显示器布局坐标：现有控件布局继续使用的稳定坐标。
- 容量 surface 坐标：`layout - capacityOrigin`，用于 D2D target。
- HWND 客户区坐标：`surface - viewportRect.left/top`，用于输入和 `prcDirty`。
- 屏幕坐标：`monitorOrigin + layout`，用于 `pptDst` 和跨窗口比较。

`pptSrc` 选择 `viewportRect` 对应的容量起点。提交满足：

```text
pptSrc = { viewportRect.left, viewportRect.top }
pptDst = monitorOrigin + capacityOrigin + viewportRect.left/top
psize  = viewportRect.size
```

对任一布局点 `L`，屏幕结果为：

```text
pptDst + ((L - capacityOrigin) - pptSrc) = monitorOrigin + L
```

所以改变 viewport 不会移动页面内容。提交前的脏区映射为：

```text
damageRect（显示器布局坐标）
  -> 减去 capacityOrigin，得到容量 surface 坐标
  -> 与 viewportRect 求交
  -> 减去 viewportRect.left/top
  -> 裁剪到 [0, psize.width) × [0, psize.height)
  -> prcDirty（HWND 本地坐标）
```

输入做完全相反的映射：`layout = client + viewportRect.left/top + capacityOrigin`。如果 `viewportRect`、`pptSrc` 或坐标映射发生变化，保守地把整个新 `psize` 作为 `prcDirty`。窗口移动后旧屏幕位置的恢复由窗口系统处理，不要求在 backing surface 内擦除旧屏幕区域。

## 3. 生命周期

### 3.1 容量纪元

以下事件触发重新计算最大容量：

- Bar 启动；
- DPI 或主要显示器变化；
- 布局、缩放、组件集合或动画外扩上限发生变化；
- 当前内容首次突破已有容量。

重新计算时按锚点枚举所有合法 UI 状态，合并相对几何范围，再加入阴影、模糊、光照、描边、抗锯齿和安全 padding。主按钮能在屏幕上移动只改变 `capacityOrigin`，不扩大 `capacitySize`。只有新容量无法由现有资源承载时，才重建 target 和相关资源。

`EnsureDeviceResources` 需要同时记录 device generation 与实际 `capacitySize`；当前实现只比较 generation，未来不能用它判断同一 device 下的容量变化。

### 3.2 动画包络纪元

以布局目标提交或连续交互开始作为一个 envelope generation：

1. 从已成功呈现的当前边界开始，而不是从刚更新但尚未提交的状态开始。
2. 对每个顶层包络生产者收集当前、可选中间关键帧、目标及 Back 曲线超调范围。
3. 加入固定效果 padding、已知越界子视觉和连续交互的完整合法域。
4. 与当前 `reservedEnvelope` 合并；只有超出时才在下一次 ULW 中扩张。
5. 批次内保持该范围。重定向只换 generation 并继续复用已保留范围。
6. 所有时间线结束、输入捕获释放且最终内容成功提交后，计算静态边界并在下一次提交中只缩小一次。

Back 曲线常量固定，现有实现最大超调约 5%。包络生产者可以对每段 `start -> middle -> target` 使用精确曲线极值或保守 5% 标量区间，不需要逐帧采样。优先为少量顶层 Surface 建立显式 `PredictEnvelope`，再登记真正可能越过 Surface 的 Popup、Preview、光影和调试文字；不建议每次扫描全部 Shape/SVG/Word 并复制动画对象。

Slider、颜色拖动和主按钮拖动属于连续交互：前两者预留整个轨道/面板合法域，主按钮拖动保持固定资源容量和视口尺寸，只移动 `capacityOrigin/pptDst`。捕获期间不能因收缩改变客户区原点；鼠标路径可使用屏幕 delta，触摸客户区坐标必须先映射到锁定的布局域。

### 3.3 普通帧

1. 推进动画并派生布局。
2. 用现有元素追踪器累计新旧几何的联合脏区。
3. 读取当前 generation 的 `reservedEnvelope`；普通动画帧不重新决定窗口尺寸。
4. 若 viewport/源映射变化，将当前窗口设为全脏；否则映射既有 `damageRect`。
5. 用脏区裁剪 D2D 绘制。
6. 通过 ULW 一次提交位置、大小、源起点、混合参数和内容。
7. 仅在完整事务成功后提交脏区快照；失败则保留并升级为全脏重试。

Bar 的几何提交由渲染线程内的 ULW 事务单独负责。WindowService 继续负责创建、显隐、样式和 Z 序；动态模式激活后不再对 Bar 发送独立 `SetBounds/SetWindowPos`，避免一次视觉提交被拆成两个线程上的窗口操作。

### 3.4 调试覆盖层

调试边框仍属于呈现事务的一部分，并且只在脏区调试开启时存在：

- 活动帧以红框显示本帧业务 damage；
- `DebugFrameSleepLatch` 请求的 idle 前最后一帧以绿框显示该帧 damage，不再向帧率文字追加“休眠”；
- 以蓝框显示实际 HWND 本地边界，使用内缩描边保证右侧和底部不会被窗口裁剪；
- viewport 变化帧的蓝框按新窗口本地边界解析，旧红/绿/蓝框边界一并加入下一次 damage，成功提交后才更新覆盖层快照。

蓝框表示的是本次 `psize`，而不是最大容量 surface；因此可以直接观察动画批次是否只在开始扩张、结束收缩，以及静止 HWND 是否贴合内容。

## 4. 呈现路线

### 4.1 首选基线：缩小后的 GDI-compatible D2D target

```text
持久最大容量 D2D target
  -> GetDC(COPY)
  -> UpdateLayeredWindowIndirect(pptDst, psize, pptSrc, prcDirty)
  -> ReleaseDC(empty update rect, only if compatibility experiment passes)
```

优点：最大程度保留现有 D2D 1.1、效果链、缓存和事务模型。主要变化是目标不再达到 4K 全屏大小。缺点是 `GetDC(COPY)` 仍可能同步整个最大容量 target。

该路线必须先实现并测量。若最大容量只有 Bar 的有限范围，全量同步可能已经低于继续优化的收益阈值。[微软合同](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1gdiinteroprendertarget-releasedc)说明 `ReleaseDC(nullptr)` 表示整个 target 被 GDI 修改，而这里没有 GDI 写入；因此先原型验证空更新矩形，尝试消除潜在的反向整面同步。该实验不能把 `GetDC(COPY)` 本身变成脏区复制；[GetDC 合同](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nf-d2d1-id2d1gdiinteroprendertarget-getdc)明确说明调用会 flush render target。

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

## 5. HWND 尺寸策略

默认采用“批次预留、结束收缩”，而不是逐帧精确范围、尺寸网格或基于帧数的滞回：

- 静态状态：HWND 贴合当前内容 + 固定效果 padding。
- 展开批次：首帧一次扩到完整扫掠包络。
- 动画进行：保持范围不变。
- 收起批次：先保持旧范围，最终帧成功后一次缩小。
- 中途重定向：若新扫掠包络已被覆盖则零次 resize；突破时只扩一次。
- 连续交互：使用预定义交互域，捕获结束后一次收敛。

量化只用于像素/DPI 对齐，不作为主要抗抖策略；额外时间滞回仅在实测发现频繁离散业务切换时再引入。

## 6. 兼容性和失败处理

- API 边界保持在 Windows 7 SP1 + KB2670838 可用的 D2D 1.1 和 layered-window 能力内。
- 尺寸或源映射变化帧默认全脏，以规避不同系统版本对组合更新的细节差异。
- `ReleaseDC` 空更新矩形只在 Windows 7 SP1 与 Windows 11 都验证成功后启用；否则继续传 `nullptr`。
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
- 包络生产者遗漏中间关键帧、Back 超调或越界子视觉会造成动画裁剪。
- 主按钮拖动仍会高频 move HWND，但固定尺寸不会增加 resize；需要单独测量 DWM 行为。
- `pptSrc` 变化会改变整张窗口的像素映射，不能沿用旧的局部脏区。
- 客户区坐标若未统一加 viewport/capacity 偏移，会导致鼠标、触摸、Raw Input 悬停或拖动跳变。
- WindowService 的 `SetWindowPos` 与渲染线程 ULW 同时修改 Bar 会形成竞态，必须确立单一提交者。
- staging 引入额外同步、CPU 带宽、pitch 和预乘 BGRA 正确性风险。
- layered HWND 缩小可能改善 DWM 合成条件，但 Independent Flip/MPO 仍受硬件和系统策略影响。
