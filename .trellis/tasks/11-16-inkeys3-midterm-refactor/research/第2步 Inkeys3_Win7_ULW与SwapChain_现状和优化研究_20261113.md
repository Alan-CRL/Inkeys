# Inkeys3 Win7 ULW 与 DXGI SwapChain：现状、发现及异步流水线优化研究

> **文档性质：研究备忘 / 未来参考；不是已批准的架构方案，也不构成实施任务。**  
> 整理日期：2026-11-13  
> 代码基准：`Alan-CRL/Inkeys` 仓库公开 `dev`，所核对的提交 `215184d4e096f262b8b4bf11471cb5b14fa53884`。  
> 范围：Draw3 **透明桌面/PPT 批注窗口**的 Win7 ULW 呈现；与 DComp 的区别；潜在 CPU/GPU 流水线化。**不代表对 `whiteboard3` 的完整代码审查。**

## 0. 一页结论

1. **Win7 下的现有桌面 DrawPad**：使用 D3D11（优先硬件，失败可转 WARP）绘制，创建 `IDXGIFactory2::CreateSwapChainForHwnd` 交换链；虽然配置 `DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`，但 ULW 呈现时**不调用 `swapChain->Present1()`**。Draw3 使用 `GetBuffer(0)` 得到 GPU RenderTarget，然后经 Staging 读回、DIB 像素处理、`UpdateLayeredWindowIndirect()` 呈现在透明分层窗口。
2. **自动呈现模式只有两条**：Win8+ 优先 `DirectCompositionVisualTree`，失败或不可用时 `UlwDirtyRect`；`DwmBlurBehind`、`DwmBlurBehind2` 保留代码和枚举，但当前初始化入口会拒绝它们。辅助 `DrawpadPresentation` 仍由 ULW 负责必要的前置透明画面。
3. **交换链与 ULW 不是依赖关系**：ULW 只需要 CPU 可读的预乘 Alpha 像素；当前 ULW 创建 HWND SwapChain，主要由于 `InkRenderer` 的最终 RenderTarget 和尺寸管理依赖 `GetBuffer(0)` / `ResizeBuffers()`。理论上可以改为普通 `ID3D11Texture2D`，但目前没有必要贸然删除。
4. **现有“双缓冲 Flip”没有变成流水线**：代码始终引用 buffer 0，且 `CopySubresourceRegion → 阻塞式 Map → 拷贝/处理 DIB → UpdateLayeredWindowIndirect` 在同一次调用内顺序完成。仅设置 `BufferCount=2` 不会自动重叠 GPU 与 CPU 任务。
5. **可研究的主要优化**：将 GPU 读回与 ULW 提交解耦；使用 2～3 个 Staging Slot、GPU 完成检测、低排队深度和最新帧/累计脏区策略；ULW 线程独立处理 DIB 和 `UpdateLayeredWindowIndirect()`。是否需要第二个 GPU RenderTarget、第二张 DIB、保留 SwapChain，均待实测决定。
6. **Win7 FLIP 的争议应谨慎记录**：Inkeys 历史研发记录及用户实测反馈指出 Win7 SP1 + KB2670838 下此配置可用；Microsoft 文档则将 D3D11 Flip Model 的正式呈现支持列为 Win8 起，且对 Win7 使用 `FLIP_SEQUENTIAL` 给出失败警告。两者并不允许简单推出“Win7 完整支持 Flip Present”。应在目标设备上记录 `CreateSwapChainForHwnd` HRESULT、`GetDesc1()` 实际返回值与是否执行 `Present1()`；在没有该复测前不要断言现有 Win7 软件无法运行，也不要声称其已经进行真正的 Flip 呈现。

---

## 1. 问题背景与研究动机

Inkeys3 的 Win7 桌面批注需要在其它应用/PPT 上方呈现**逐像素透明**的墨迹窗口，同时尽可能使用硬件 GPU 完成 Draw3 的 L0/L1/L2 墨迹合成。早先讨论出现两个问题：

- 最终已经由 ULW 呈现，为何还在 Win7 使用 `CreateSwapChainForHwnd()` 和 `FLIP_SEQUENTIAL`？
- 既然存在双缓冲，能否像 DComp 那样，在 **CPU 提交上一帧 ULW** 期间让 **GPU 绘制下一帧**，减少绘制线程的阻塞？

本研究分别讨论**渲染资源**、**交换链显示机制**与**ULW 窗口提交**，避免混用“SwapChain 存在”“Flip Model 真正 Present”“有 GPU 加速”“有透明窗口”等不同概念。

## 2. 已从 dev 代码确认的事实

### 2.1 呈现后端选择

定义位于 `Draw3.TransparentPresentation.cppm`：

```cpp
enum class TransparentPresentMode {
    UlwDirtyRect,
    DirectCompositionVisualTree,
    DwmBlurBehind,
    DwmBlurBehind2
};
```

生产自动候选数组位于 `Draw3.TransparentPresentation.cpp`：

```cpp
constexpr TransparentPresentMode kTransparentPresentModes[] = {
    TransparentPresentMode::DirectCompositionVisualTree,
    TransparentPresentMode::UlwDirtyRect
};
```

- 检测 `dcomp.dll` / `DCompositionCreateDevice`；不可用时尝试 ULW。
- `TryInitialize()` 在窗口配置前拒绝两种 DWM Blur Behind 模式；即使强制选择也不能经当前正常入口启用。
- DComp 模式将交换链绑定到 `IDCompositionVisual::SetContent()`，通过 `Present1()` 提交 GPU 帧；`DwmFlush()` 只在特定输出交接屏障中调用，不是每帧都调用。
- ULW 模式会将 HWND 调整为 `WS_EX_LAYERED`，以 `UpdateLayeredWindowIndirect()` 提交逐像素 Alpha 内容。
- `DrawpadPresentation` 是辅助前置输出目标，仍走 ULW；其读回资源采用需要时准备、隐藏后回收的机制。

### 2.2 ULW 下实际创建的 DXGI 交换链

`TransparentPresentation.cpp` 中 `CreateSwapChain()` 的关键配置：

```cpp
DXGI_SWAP_CHAIN_DESC1 description = {};
description.Width = width;
description.Height = height;
description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
description.SampleDesc.Count = 1;
description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
description.BufferCount = 2;
description.Scaling = DXGI_SCALING_STRETCH;
description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
description.AlphaMode = IsGpuMode(mode)
    ? DXGI_ALPHA_MODE_PREMULTIPLIED
    : DXGI_ALPHA_MODE_UNSPECIFIED;
```

当 `mode == UlwDirtyRect` 时：

- 使用 `IDXGIFactory2::CreateSwapChainForHwnd(device, primaryHwnd, ...)`。
- `AlphaMode == DXGI_ALPHA_MODE_UNSPECIFIED`；这**不表示**复制出来的 BGRA 像素没有 Alpha。透明度最终由 ULW 的 `AC_SRC_ALPHA` 使用。
- 先尝试 Win8.1+ 的 waitable 选项；失败后再次尝试普通 SwapChain，**不改变** `SwapEffect`。
- 代码没有针对 Win7 单独选择 `DXGI_SWAP_EFFECT_DISCARD/SEQUENTIAL` 的分支。
- 即使对象有两个 buffer，现有 ULW 代码**没有通过 Present 轮换它们**。

### 2.3 RenderTarget 来自 SwapChain Buffer 0

`Draw3.Renderer.cpp`：

```cpp
swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), ...);
device->CreateRenderTargetView(backBufferTexture.Get(), nullptr, ...);
```

Draw3 在该 RenderTarget 上合成出最终透明画面。`InkRenderer::Resize()` 调用 `swapChain->ResizeBuffers()`；这解释了 Renderer 与 SwapChain 的直接耦合。

### 2.4 Win7 ULW 的每帧数据流

```text
RTS / 鼠标输入
      ↓
Draw3 模型与 L0/L1/L2 GPU 绘制
      ↓
SwapChain.GetBuffer(0) → GPU 最终 BGRA 画面
      ↓
CopySubresourceRegion（通常仅脏区）
      ↓
Staging Texture（D3D11_USAGE_STAGING / CPU_ACCESS_READ）
      ↓
Map(D3D11_MAP_READ, flags=0)   ← 可能阻塞等 GPU Copy
      ↓
CPU 逐行复制 + Alpha/命中底层处理 → 持久 DIB
      ↓
Unmap
      ↓
UpdateLayeredWindowIndirect(ULW_ALPHA, prcDirty)
      ↓
Win7 WS_EX_LAYERED 透明窗口
```

**不是** `SwapChain.Present → ULW`。实际 Presenter 拿到 `renderer->backBufferTexture` 后读回 CPU，并自行调用 USER32 API。

`UlwDirtyRectPresenter` 当前资源包括：**一张 Staging Texture + 一张持久 DIB**。正常 ULW 路径使用同一次 `Present()` 调用顺序处理复制、Map、DIB 更新与提交。

**特殊语义需保留：**

- Primary DrawPad 的 `ComposePrimaryHitUnderlayAlpha()` 为透明区域增加微量 Alpha，服务既有命中/穿透行为；不得当普通颜色转换随意删掉。
- 辅助 `DrawpadPresentation` 的全透明帧、窗口交接、隐藏回执与读回资源回收有版本和生命周期约束。
- 本地 DIB 内容与系统已经接受的窗口内容并非同一时刻更新；ULW 调用失败时仍需追踪尚未成功提交的区域。

## 3. 对 SwapChain / Flip / ULW 的正确理解

| 概念 | 实际职责 | 与本次 Win7 ULW 的关系 |
| --- | --- | --- |
| D3D11 / Draw3 | 使用 GPU 计算并生成像素 | 是核心渲染能力，可用 Hardware 或 WARP |
| SwapChain | 提供关联的显示缓冲与呈现管理 | 当前被用作最终 GPU RenderTarget 的提供者 |
| `FLIP_SEQUENTIAL` | DXGI 翻转呈现模型配置 | 当前被配置，但 ULW 不调用 `Present1`，所以不利用 Flip 屏幕呈现/轮换 |
| `CreateSwapChainForHwnd` | 将 DXGI 交换链关联到 HWND | ULW 当前采用它，但 ULW 并不要求此对象 |
| DComp | Win8+ GPU Visual Tree 合成 | 主要桌面窗口优先后端，不适用于 Win7 |
| ULW | 提交带逐像素 Alpha 的分层窗口画面 | Win7 透明呈现实际出口，依赖 CPU 可读图像 |
| DWM | Windows 桌面合成器 | Win7 有 DWM，但不等于 DComp，亦不等于 DXGI Flip Model |

**重要区分**：Win7 的 `CreateSwapChainForHwnd` + `GetBuffer(0)` 能否成功，与 DXGI 是否支持**通过 `Present1()` 的 Flip 显示行为**是两个问题。当前 Inkeys 没有在 ULW 路径执行后者。

### Win7 兼容性资料冲突：保留事实，不作越界推断

- 微软 **Platform Update for Windows 7** 文档表示其提供**部分** DXGI 1.2 支持，包含 `CreateSwapChainForHwnd`，但不支持 DComp 与非 HWND SwapChain，也明确提醒 `DXGI_SCALING_NONE` 不可用；当前代码使用 `DXGI_SCALING_STRETCH`。
- 微软 **DXGI Flip Model / DXGI_SWAP_EFFECT** 文档将 D3D11 的 `FLIP_SEQUENTIAL` 正式支持起点标为 Win8，并说明 Win7 上使用 Flip 的失败条件。
- Inkeys 的 2026-09 研发记录载明，开发者曾反馈 Win7 SP1 + KB2670838 上当前 Flip 配置可用，审查时未独立复现；本轮亦未重新执行目标 Win7 实机测试。

**研究结论**：承认并保存这项实测反馈，同时不把它扩展成“Win7 已有和 Win8 一样的 Flip 呈现能力”。后续如需定论，请获取真实 Win7 环境的创建返回值、实际 SwapEffect、系统版本与是否曾执行 Present 的运行日志。

## 4. 为何当前双缓冲不能自动提高 ULW 并行度

当前实现的主要时间关系是：

```text
Draw3 线程：Render N → Copy N → Map 等待 N → DIB N → ULW N → Render N+1
GPU：           绘制 N → Copy N         （等待下一批命令）
```

- 只使用一次 `GetBuffer(0)`；没有为 ULW 维护独立的可轮替 GPU RenderTarget 数组。
- 没有对 ULW SwapChain 执行 `Present1()`，因此 Flip 轮换不会自然发生。
- `Map` 立即跟随 Copy，可能发生 GPU/CPU 同步等待。
- ULW API 同步返回前，同一线程不会开始下一轮的绘制任务。

因此瓶颈并不只是“缺两个 GPU 缓冲”，而是**读回、CPU 拷贝与 ULW 提交都没有流水线化**。

## 5. 候选优化：异步 ULW 流水线（尚未决定实施）

### 5.1 期望效果

```text
GPU 提交线程          Staging A / B                ULW 工作线程
渲染帧 N            Copy N → GPU 完成
渲染帧 N+1          Copy N+1 → GPU 完成          处理/提交较早的已完成帧
渲染帧 N+2          Copy N+2 → ...              处理/提交下一张最新帧
```

**目标是提高稳态吞吐并降低绘制线程阻塞；不保证单帧端到端延迟一定降低。** 过深排队反而可能让墨迹更落后。

### 5.2 第一候选资源安排

| 资源 | 初期考虑 | 说明 |
| --- | --- | --- |
| 最终 GPU RenderTarget | 可先保留现有的一个 | GPU 完成当帧 Copy 后，能继续复用目标绘制后续帧；不必先复制出双 RenderTarget |
| Staging Texture | 2 个起步，按测量评估 3 个 | 使 GPU Copy 与 CPU 消费有可交错的槽位 |
| GPU 完成标记 | 每个 Staging Slot 一个查询/同步标记 | 无需紧跟 Copy 阻塞 Map |
| 持久 DIB | 初期 1 张 | 维持已提交画面的增量更新基线，减少内存与一致性复杂度 |
| ULW 工作线程 | 1 个 | 管理自身 HDC、DIB、像素转换和窗口提交 |
| GPU 提交线程 | 保留 Draw3 绘制 owner | 负责 immediate context、Copy、查询、Map/Unmap |

**候选执行策略**：

1. 渲染当前最新帧，向空闲 Staging Slot 发出 GPU Copy；记录帧号、表面代次、内容版本和脏区。
2. 用 GPU 查询判断 Copy 已完成；`Map(D3D11_MAP_FLAG_DO_NOT_WAIT)` 作为非阻塞尝试，未就绪则下轮调度，不自旋等待。
3. **保守方案**：GPU owner 将已 Map 的数据复制到独立 CPU 数据包，Unmap 后交给 ULW 线程。所有跨线程边界都有清晰的 CPU 内存所有权。先验证这一版。
4. **后续可选方案**：允许 ULW 线程在明确的资源租约期内只读已 Map 的 Staging 内存，GPU owner 在收到“CPU 读取完成”回执后执行 Unmap；需要针对 Win7 驱动、线程同步和资源生命周期专项验证。
5. ULW 工作线程顺序更新 DIB，执行现有 Alpha 逻辑，调用 `UpdateLayeredWindowIndirect`；只在成功返回后推进“已提交内容版本”。
6. 隐藏、Resize、恢复、关闭时先停止旧代次任务的提交，再安全释放对应资源。

**注意**：只有一张 DIB 时，ULW 工作线程自身仍顺序处理 DIB 拷贝与 ULW 调用；真正可重叠的是 GPU 下一帧任务与 CPU/USER32 上一帧工作。第二张 DIB 能否进一步并发，涉及 DIB 的基线复制与合成一致性，不能只靠增加张数保证收益。

### 5.3 限制队列深度和陈旧画面

- 保持最多 2～3 个 in-flight Staging Slot，不能形成无限任务队列。
- 在 ULW 提交慢时优先保留**最新完整状态**，旧中间呈现任务可合并/跳过；输入 Down/Up/Cancel 与文档历史操作仍须可靠执行。
- **脏区必须从最后一次成功提交给 ULW 的版本累计到新版本**，而不是仅依据两个相邻待绘制版本。
- 如果已有 DIB 与目标 GPU 画面的差异无法精确追踪，退回全帧更新，避免留下旧像素或出现穿透边缘。
- `UpdateLayeredWindowIndirect()` 返回成功表示窗口系统接受更新，**不等于物理屏幕已经显示**；实际“输入到可见光”的测量需要额外工具。

### 5.4 多线程资源归属建议

| 对象 / 操作 | 建议 owner | 注意事项 |
| --- | --- | --- |
| D3D11 immediate context | GPU 绘制线程 | 不要从 ULW 线程并发调用 Context 方法 |
| Copy / Query / Map / Unmap | GPU 绘制线程 | GPU 查询必须正常推进，不能永远 `DONOTFLUSH` 自旋 |
| GDI Memory DC / DIB | ULW 提交线程 | 创建、使用和销毁尽量在同一工作线程 |
| `UpdateLayeredWindowIndirect` | ULW 提交线程 | 不应持有 Window Service 锁时等待其返回 |
| HWND 创建、显示、层级、销毁 | 窗口 owner / Window Service | 通过 generation、异步命令协调工作线程 |
| 已提交内容版本 / DIB 基线 | ULW Presenter 的状态 | 仅成功提交才推进，失败或丢帧继续累计差异 |

### 5.5 优化方向与可能的利弊

| 候选方向 | 潜在收益 | 代价或风险 |
| --- | --- | --- |
| 独立 ULW 提交线程 | 避免 USER32 提交占住 Draw3 绘制循环 | 跨线程状态、关闭和 Resize 协调 |
| 双 Staging + 查询 | 减少紧跟 Copy 就阻塞 Map 的概率 | 显存/系统内存占用，查询与资源管理 |
| 脏区 Copy 和 ULW dirty | 降低大分辨率下的读回与提交量 | 必须保证跳帧后的累计区域正确 |
| 跨线程已映射内存租约 | 可能减少一次 CPU 数据包复制 | Map 生命周期、驱动兼容、调试复杂 |
| 第二 GPU Final RenderTarget | 进一步减少特定依赖下的资源争用 | 额外 GPU 纹理开销；未证明当前必需 |
| 第二 DIB | ULW 阻塞时为下一帧预处理提供可能 | 两份图像的完整像素基线与增量一致性难题 |
| ULW 去掉 HWND SwapChain，改 `Texture2D` | 解耦 Renderer/Presenter，减少无 Present 的交换链依赖 | 需要改变 `InkRenderer::Init/Resize/RTV` 接口；吞吐收益未必明显 |

## 6. 与 DComp 的能力比较

| 维度 | DComp（Win8+ 首选） | ULW（Win7 / 回退） |
| --- | --- | --- |
| 墨迹 GPU 绘制 | 支持 | 支持（硬件或 WARP） |
| GPU 结果如何到窗口 | Composition SwapChain `Present1`，DWM 使用 visual | GPU → Staging/CPU → DIB → USER32 ULW |
| 是否需要每帧 GPU→CPU 读回 | 通常不需要 | 现有实现需要 |
| 是否可实现 CPU/GPU 重叠 | 系统合成链路自然参与 | 需要应用主动构建异步流水线 |
| 当前代码有无真正流水线 | 使用 GPU 呈现路径 | 单线程顺序处理，尚未实现专门的 ULW 流水线 |
| 目标收益 | 低开销透明呈现 | 主要减少 CPU/USER32 对绘制线程的占用 |

ULW 无法仅靠增加 BufferCount 变成 DComp；改善后的低延迟表现取决于同步开销、GPU Copy、CPU 带宽、ULW API 行为和 Windows 7 图形驱动。

## 7. 关键正确性及兼容性风险

1. **Win7 实际 Flip 配置**：保留成功反馈，但必须收集目标机创建 HRESULT、实际 GetDesc1、运行日志；不要把“创建成功”说成“Win7 通过 Flip Present 显示”。
2. **预乘 Alpha**：CPU 拷贝、命中 underlay 和透明显示必须保持既有 premultiplied 合成合同。尤其 `DrawpadPresentation` 的全透明检查与资源回收不可被旧帧破坏。
3. **提交顺序**：某个旧帧 GPU 完成得晚，不能在更高版本 ULW 提交后覆盖旧像素；必要时丢弃旧版本，但保持累计脏区正确。
4. **后台、暂停与设备丢失**：无进度的查询、Win7 旧驱动、`DXGI_ERROR_DEVICE_REMOVED`、线程退出以及 HWND 代次切换都需处理；不要靠同步重建引发死锁。
5. **`prcDirty` 实效**：Microsoft 文档支持 `UPDATELAYEREDWINDOWINFO::prcDirty`，但具体目标机的内部提交开销需实测；脏区降低 GPU/CPU 拷贝量并不保证 USER32 内部完全等量减少工作。
6. **额外延迟**：流水线提高吞吐并不等于每笔更跟手。需要记录从输入快照到 GPU Copy、ULW 成功返回、实际可见的完整延迟链。
7. **资源规模**：3840×2160 BGRA8 一张约 31.64 MiB；两张全尺寸 Staging 约 63.28 MiB，另加一张 DIB 约 31.64 MiB（均不含驱动对齐与其他临时缓冲）。120 次/秒全帧读回约 3.71 GiB/s，实际脏区应尽量小。

## 8. 建议的研究/实验阶段（不代表立项或批准）

**阶段 0：测基线，不改算法。** 在 Win7 与 Win11 的 ULW 强制模式分别统计：CPU `Map` 等待、GPU Copy 完成延迟、DIB memcpy/Alpha、ULW 调用耗时、脏区面积、内存/显存、输入到最终提交时间。保留现有 Win7 实测事实与版本信息。

**阶段 1：先拆 ULW 提交线程。** 使用独立 CPU 帧数据包，保持单 Staging 和现有交换链，验证输出一致性、取消/隐藏/Resize、退出和线程锁顺序。预期主要减少绘制循环对同步 ULW 的等待。

**阶段 2：双 Staging 异步 Copy/Map。** 引入有界 Slot 队列、GPU 事件查询、完成版本检查、最新帧合并与累计脏区。验证不因为一帧排队造成额外明显手写延迟。

**阶段 3：针对数据选择资源结构。** 比较单/双 DIB、CPU 数据包与 mapped slot 租约、独立 Texture2D 与现有 SwapChain；只有确认收益与风险后再调整 Draw3 Renderer 接口。

### 必须通过的回归场景

- Win7 SP1 + KB2670838：Hardware FL11_0 与 WARP 回退；DComp 不可用时正常选择 ULW。
- 大画幅（例如 3840×2160）、小脏区、连续快速书写、大橡皮擦与全帧 Clear。
- 隐藏/显示 DrawPad；PPT/桌面目标切换；辅助 DrawpadPresentation 全透明帧。
- 窗口 Resize、显示器热插拔、设备丢失、进程停止；旧代次工作不写入新窗口。
- ULW 很慢或 GPU 很慢时不积压旧帧，不出现旧墨迹残留、笔头跳跃或画面倒退。
- Win8+ DComp 主路径不因 ULW 优化额外增加一次全屏 GPU 拷贝。

## 9. 仍未决定的问题

- 是否在未来仍保留 ULW 的 HWND SwapChain，还是仅保留普通 GPU 纹理作为 ULW RenderTarget？
- ULW 线程是否应与未来多窗口的每个 Presenter 一一对应，或共用提交服务？（需结合多画布/多显示器架构另行讨论。）
- 是否需要读取已 Map 的 Staging 内存跨线程，还是选择更容易验证的 CPU 数据包？
- GPU 最终画布缓存与读回 Slot 的最优数量是多少？是 2 还是 3？
- 如何对不同显卡、Win7 DWM 开关、不同显示器刷新率设置延迟与吞吐策略？
- 是否仍需研究历史 `DwmBlurBehind/DwmBlurBehind2` 后端？当前不应在未验证情况下重新加入自动回退链。

## 10. 源码与资料索引

### Inkeys 源码（固定提交）

- [Draw3.TransparentPresentation.cppm：模式定义、Options、Presenter 接口](https://github.com/Alan-CRL/Inkeys/blob/215184d4e096f262b8b4bf11471cb5b14fa53884/Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cppm)
- [Draw3.TransparentPresentation.cpp：后端选择、交换链创建、GPU 读回、ULW 提交](https://github.com/Alan-CRL/Inkeys/blob/215184d4e096f262b8b4bf11471cb5b14fa53884/Inkeys/Inkeys/Drawing/Draw3/Draw3.TransparentPresentation.cpp)
- [Draw3.Renderer.cpp：GetBuffer(0) 与 ResizeBuffers](https://github.com/Alan-CRL/Inkeys/blob/215184d4e096f262b8b4bf11471cb5b14fa53884/Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cpp)
- [Draw3.GraphicsInitialization.cpp：D3D11 Hardware/WARP、Feature Level、DXGI](https://github.com/Alan-CRL/Inkeys/blob/215184d4e096f262b8b4bf11471cb5b14fa53884/Inkeys/Inkeys/Drawing/Draw3/Draw3.GraphicsInitialization.cpp)
- [Draw3.Host.cpp：绘制线程、PresentationController 初始化与恢复](https://github.com/Alan-CRL/Inkeys/blob/215184d4e096f262b8b4bf11471cb5b14fa53884/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp)
- [2026-09-27 历史研发备忘：禁用 DWM Presenter；记录 Win7 FLIP 实机反馈与复测缺口](https://github.com/Alan-CRL/Inkeys/blob/215184d4e096f262b8b4bf11471cb5b14fa53884/.trellis/tasks/archive/2026-10/09-27-commit-and-security-audit/research/dwm-disable-implementation.md)

### Microsoft 文档

- [Platform Update for Windows 7：DXGI 1.2 部分支持](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)
- [IDXGIFactory2::CreateSwapChainForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd)
- [DXGI_SWAP_EFFECT：Flip Model 正式支持范围](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect)
- [DXGI Flip Model：交换链与缓冲轮换说明](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model)
- [UPDATELAYEREDWINDOWINFO：ULW_ALPHA / prcDirty](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-updatelayeredwindowinfo)
- [D3D11 多线程简介](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)
- [ID3D11DeviceContext::Map](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-map)
- [ID3D11DeviceContext::GetData](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-getdata)

---

> **保存时的最终态度**：保持现状代码事实与候选技术方向分离。ULW 流水线优化有明确的性能动机，但是否使用双 Staging、是否多线程、是否去掉 HWND SwapChain，以及收益是否超过复杂度，都必须由 Win7 实机基准与回归测试决定。
