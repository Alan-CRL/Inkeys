# 首轮实体设备测试缺陷复盘

## Bug Analysis: Touch/Pen 无输入与鼠标宽度突变

### 1. Root Cause Category

- **Category**: B / D / E — 跨层契约、实机覆盖和隐式假设同时缺失。
- **Specific Cause**:
  - RTS 多点不是单一 COM 开关。初版只设置 `IRealTimeStylus3::MultiTouchEnabled=TRUE`，遗漏 HWND 的 `MICROSOFT_TABLETPENSERVICE_PROPERTY` 和 `WM_TABLET_QUERYSYSTEMGESTURESTATUS` 多点 opt-in。
  - Pen 路径遗漏主 Inkeys 已使用的 press-and-hold / flick 禁用，系统手势可能延迟或接管输入。
  - 模拟压感沿用旧单笔估算器，却假设 RTS 最新快照与旧逐帧鼠标具有相同采样节奏；第一份速度会直接回写整段起笔，后续直径变化上限也过高。
  - QCOM ARM64 驱动接受 DirectComposition premultiplied swapchain 与 visual tree，但实体设备上透明像素仍显示为黑色；原实现把 API 初始化成功误当成可见 alpha 正确。

### 2. Why Fixes Failed

1. 初版静态检查聚焦 COM、队列、ABA 和 L0/L1/L2，没有沿 `HWND -> Tablet service -> RTS callback` 检查窗口握手。
2. ARM64 构建只能证明 SDK 和链接正确，不能证明实体 Pen/Touch 会产生 callback。
3. 复用了已有宽度估算器，但没有重新验证“Move 可覆盖”对速度采样和首样本响应的影响。
4. 主 Inkeys 的 RTS 文件被对照过，窗口过程中的 Tablet PC 标志没有一并纳入研究范围。
5. 透明呈现验证只检查 HRESULT 和 active mode 日志，没有在真实桌面背景上验证最终合成结果。

### 3. Prevention Mechanisms

| Priority | Mechanism | Specific Action | Status |
|---|---|---|---|
| P0 | Architecture | HWND 属性、窗口消息和 `IRealTimeStylus3` 三处统一启用多点 | DONE |
| P0 | Runtime behavior | 禁用 press-and-hold/flick，降低 Pen 被系统手势接管的可能 | DONE |
| P0 | Width invariant | 真实速度先低通；首样本不回写；时间/空间双限速 | DONE |
| P0 | Visible alpha | QCOM ARM64 优先 `UlwDirtyRect`；失败时仍保留 DComp/DWM 回退 | DONE |
| P1 | Documentation | 把三段式 opt-in 和宽度采样规则写入 native spec/cross-layer guide | DONE |
| P1 | Hardware test | 实体 Mouse/Pen/双 Touch 与桌面透明背景重新执行完整矩阵 | TODO |

### 4. Systematic Expansion

- **Similar Issues**: 任何重建 HWND、替换窗口过程或新增透明覆盖窗口的工作，都可能再次漏掉 Tablet Pen Service 属性。
- **Design Improvement**: 速度语义属于真实 input snapshot，不属于 modeler output；未来真实压感也应在 snapshot 边界归一化后再进入模型。
- **Process Improvement**: RTS 任务的最低验证必须包含实体 Pen/Touch callback，`SendInput` 和普通鼠标不能代替。

### 5. Bayesian Update

| Hypothesis | Prior | Evidence | Updated confidence |
|---|---:|---|---:|
| Touch 缺少窗口级多点 opt-in | 45% | Microsoft 文档明确要求窗口属性/消息，源码两处均缺失 | 95% |
| Pen 被系统手势路径接管 | 30% | 主 Inkeys 禁用 flick/hold，当前测试程序遗漏 | 80% |
| 宽度突变来自首速度回写和过高追随率 | 25% | 源码存在直接回写与 `12 × baseDiameter/s` 上限，截图症状一致 | 95% |
| 黑底来自 QCOM DComp 可见 alpha 驱动行为 | 50% | DComp 全链成功、L2/backbuffer 仍以透明清屏，但实体输出为黑色 | 90% |

实体设备第二轮结果仍是最终判据；Computer Use 的合成鼠标拖拽没有进入 RTS，不能替代硬件验证。

### 6. Knowledge Capture

- [x] 更新 `.trellis/spec/native/runtime-and-rendering.md`。
- [x] 更新 `.trellis/spec/guides/cross-layer-thinking-guide.md`。
- [x] 保留实体设备第二轮验证为未完成项。
