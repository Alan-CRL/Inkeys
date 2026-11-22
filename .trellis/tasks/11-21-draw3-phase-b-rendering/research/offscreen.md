# L0 清除与激光 CS 的离屏验证

2026-11-21，调查基线 `0f17910e`。产品自检复用现有 ARM64 Debug 产物（当日 15:56），另用临时目录内的原生 MSVC / D3D11 控制台探针独立比较 API 和验证嵌入资源。两个探针均无窗口、交换链或 Present。

## 当前 L0 的正确性

普通持续帧在 `Draw3.DrawingController.cpp:23675` 清上一次写过的 L0。只含稳定形状且未变化时可保留；已清且此后未写入时直接跳过。绑定 L0 **先**登记本批保守矩形，多次绑定求单一外包矩形。未知写入、无 Context1 或无驱动 ClearView 能力均使用整屏路径。已知区域向外扩 2 px 后裁剪，完全在屏外跳过，大于半屏退整屏，恰好半屏仍走矩形。两个分量的单位值是 Add BGRA8 全 0、Retain R16F 为 1。单点圆头另外覆盖第二端点偏移。这里跟踪的是自上次清除以来的层写域，最终显示的 frameDirty 另需包含旧画面和新画面。详见 `Draw3.Renderer.cpp:252–343,714–720`、`Draw3.StrokeGeometry.cpp` 的 DrawL0LiveComposite 及 `.trellis/spec/native-desktop/draw3-integration.md:736`。

| 本轮实际运行的产品自检 | 结果与覆盖 |
| --- | --- |
| `Build\ARM64\Debug\Inkeys.exe --draw3-renderer-upload-test` | 退出 0，`[Draw3Upload] PASS failures=0 no_hwnd=1 no_swapchain=1`。WARP 64² readback 同时看 Add 和 Retain，证明指定区恢复、区外哨兵保留、第二次空清跳过、未知写入和缺 Context1 回退。动态 discard、append 和 DEFAULT 三条上传路径通过；L0 专门断言在第一个模式运行。 |
| `--draw3-pen-taper-test` | 退出 0，498975 个断言、0 失败。包含预测骤消时的实时锚点、持久笔画不含预测，以及终态修正能跨过实时 committedIndex 的反例。 |
| `--draw3-laser-resources-test` | 退出 0，`LaserResources failures=0 hwnd=0`。覆盖激光画布表面按工具分配与退役。粒子 CS 单独验证如下。 |

阶段 A 的 R7 上屏只验过普通笔圆环。历史压力测过 HD 4600 / UHD 620 / UHD 730 / 骁龙，但测量对象包括调度、队列与外部负载，不能用这些结果确定清除面积阈值。见 [阶段 A 验收](../../archive/2026-11/11-20-draw3-gpu-stress-latency/prd.md) 和 [压力诊断第八轮](../../archive/2026-11/11-20-draw3-gpu-stress-latency/research/gpu-stress-latency-experiments.md)。

## 4K 本机硬件清除对照

Qualcomm Adreno X1-85，硬件 FL11_0，ClearView 能力为真。两个 3840×2160 默认纹理，Add BGRA8 / Retain R16F。每样本先向同一矩形 MRT 绘入非恒等颜色；比较两次整屏 ClearRenderTargetView 和同矩形两次 ClearView，均恢复单位值；随后采样两张纹理并输出 32×32 合成小块。GPU timestamp 从前置 Draw 之后到依赖读取 Draw 之后。每样本提交并等待读数，前五轮预热排除，剩下每格 32 次，面积轮换、整／局部顺序交替，排除 disjoint。下表以微秒计，分位数为同条件样本排序结果。

| 已写面积 | 整屏 p50 | 矩形 p50 | 整屏 p95 | 矩形 p95 |
| --- | ---: | ---: | ---: | ---: |
| 64×64，0.05% | 100.1 | 43.5 | 1443.0 | 1023.8 |
| 480×270，1.56% | 97.7 | 47.1 | 385.0 | 1040.3 |
| 1920×1080，25% | 98.5 | 51.0 | 699.0 | 1015.2 |
| 1920×2160，50% | 98.4 | 61.9 | 990.2 | 1179.7 |
| 2880×2160，75% | 98.3 | 72.2 | 1417.8 | 600.5 |

在本机小面积写入场景，局部双清的中位耗时有优势，75% 也没有出现中位数交叉。p95 波动大且方向不一致。这个探针验证小区域路径有实际价值；整屏双清逻辑覆盖约 47.46 MiB（3840×2160×6 B），实际总线流量与硬件快速清除、解压、后续读取有关。当前 50% 可作为保守初值。本轮没有建立普适拐点，不能从该表算出产品每帧节省时间或待机功耗。严格 idle 的关注点是有无错误唤醒／清除，静置功率需另外测。

下一轮在 HD 4600 和目标兆芯实机，固定驱动、电源、分辨率与负载，分别测持续书写／多指远离／抬笔／纯橡皮或激光／静置。先核对两种颜色结果相同，再对照若干千帧整屏／矩形／跳过的 GPU 分位数、有效样本、清除次数、提交到可见帧延迟与闲置唤醒。区分 4K 对角长笔大包围盒和真实高覆盖，不按像素数单独判胜。

API 边界见 Microsoft 的 [ClearView](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_1/nf-d3d11_1-id3d11devicecontext1-clearview)、[ClearRenderTargetView](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-clearrendertargetview) 及 [GPU 时间戳有效性](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_query_data_timestamp_disjoint)。

## 产品嵌入的粒子 CS

用 `LoadLibraryExW` 按资源映射当前 Inkeys.exe，读取类型 SHADER、303 Update / 304 Emit 的实际字节码。在 Adreno 硬件和 WARP 分别创建这两个 CS、2048×80 B 结构化 UAV 和 staging。对 32 个线程组 reset；从槽 2046 开始发射 4 个，固定 seed；更新 20 ms；最后按墙钟推进 3 秒。每步解绑 UAV 后复制、Map 并遍历全部 2048 槽，核对跨环槽号、alive、位置有限、半径／透明度及 age。

| 设备 | Reset | Emit | Step | Expire | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| 本机 Adreno FL11_0 | 0 | 4 | 4 | 0 | PASS |
| 本机 WARP FL11_0 | 0 | 4 | 4 | 0 | PASS |

这证明当前产物的两个 CS 可实际执行和过期。设置→Host→Controller、实际粒子合成像素、真实 Win7 WARP、兆芯硬件驱动和屏幕效果，仍属 B3 的独立验收。
