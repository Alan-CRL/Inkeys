# 决策日志

## 已确认

| ID | 决策 | 依据 |
| --- | --- | --- |
| D-01 | 合法 T0 是 `IdtMain.cpp:743` SuperTop 块之后 | 父/helper 都可能退出，之前创建 Preview 会闪现于错误进程 |
| D-02 | Preview 开启时条件化提前 RenderPipeline，关闭时保留原位置 | `Initialize()` 已幂等返回 S_FALSE；字体可继续晚初始化 |
| D-03 | 不添加 EXE ID 1 manifest，保留资源 221 | 工程关闭 manifest；221 由 PptCOM activation context 显式使用；最小风险是提前动态 DPI API |
| D-04 | Cache/BIN 头保留 DPI，但不单列主题/语言 | 像素/几何兼容真实依赖 DPI；主题/语言已进入 visual signature |
| D-05 | 使用 160-byte 显式 little-endian v1、预乘 BGRA、IEEE CRC-32 | 避免 PNG 首启解码和 struct padding，支持防御解析 |
| D-06 | Cache 写入使用 FlushFileBuffers + MoveFileEx WRITE_THROUGH | 与现有 Draw3 autosave 的可靠性级别一致，且在后台 writer 执行 |
| D-07 | 本模块内实现小型 IEEE CRC-32 并用标准向量测试 | 第三方 zip 内部耦合不合适；Abseil CRC32C 不是目标 polynomial |
| D-08 | Dispatch order 为 Bar -> StartupPreview | Preview 可在同一 render tick 消费最新 committed proxy |
| D-09 | Preview 使用独立 owner thread | 它必须早于 Window Service；DestroyWindow 有创建线程约束 |
| D-10 | Bar commit 是唯一 100%/handoff/cache gate | 当前 PresentCompletion 已覆盖 GetDC、ULW、ReleaseDC、EndDraw 全部成功 |
| D-11 | Preview 专属阶段按 immutable conditional plan 选择 | 禁用/未执行阶段不能虚构“完成”；并行 milestone 用一次性 work units 合并 |
| D-12 | Stable cache quiet period 默认 750ms | 只用于过滤暂态视觉和合并写入，不参与启动进度 |
| D-13 | fatal red-frame 等待上限 350ms | 位于用户要求 250-500ms 范围中，失败后仍及时进入现有 popup |
| D-P01 | Valid cache 在单个 Preview HWND 内从 cache bitmap 短暂混合到 live proxy；Bar alpha 255 committed 后立即隐藏 Preview，禁止两个 layered HWND 持续半透明 cross-fade | 用户已正式批准；避免改变透明边缘覆盖率和颜色 |

## 有安全默认值

- 不新增 ARM64EC 工程配置；保持 Win32/x64/ARM64 matrix，并对已有 `_M_ARM64EC` 分支做源码审计。
- 资源 ID 计划使用当前下一可用 306 和专用 `STARTUP_PREVIEW_BIN` 类型；若第二阶段发现占用则按 resource.h 规则顺延。
- v1 payload 上限 64 MiB、单边上限 8192 px、exact stride `width*4`；这些是安全上限，不是产品视觉选项。
- embedded canonical 固定 96 DPI、中文、深色、当前默认 expanded/button state；高 DPI 首启允许缩放模糊，清晰结果只来自当前设备真实 Bar。
- Window topmost observer 采用成功 refresh 后异步 post；不做同步双向回调。

## 纯实现细节

- atomic milestone 使用 bitset/CAS 还是短锁，只要满足一次性、noexcept、低开销合同即可。
- visual signature 具体 canonical encoder 的内部 helper、cache temp suffix、owner queue 容器和日志 event 名称无需用户确认。
- 动画在给定区间内的精确帧数、渐变 stops 和 ease 参数可在实现验证时微调，但不能改变分支语义或伪造进度。
