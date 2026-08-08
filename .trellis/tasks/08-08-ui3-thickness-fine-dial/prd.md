# 墨迹粗细 Fine Dial / 3× 高精度 2.5D 刻度盘模式

## Goal

在 UI3 绘制属性的现有 Preview / Slider 粗细控制之上增加持久 FineDial View Mode，让用户通过沿用现有 3× relative-drag 语义的 2.5D 刻度盘进行连续、可惯性、可边界回弹的精细粗细选择，同时保持既有候选值、Hold、Popup、预设、笔型切换和生命周期契约。

## Background

- 现有 `BarThicknessPreviewTouchDragTravelScale` 与 `ProjectRelativePreviewWidth(...)` 已定义 3× precision；FineDial 是该精度的视觉和交互扩展，不是新的 precision algorithm。
- 当前真实数据链为 input → `thicknessSliderCandidateWidth` → `drawAttributePenThickness` / normalized animation → geometry/render；拖动中仅更新 candidate，正常手势完成时才调用一次 `SetPenWidth(..., true)`。
- FineDial 是独立于旧 `08-07-ui3-thickness-slider-preview-popup` 的新任务，不修改旧任务产物。

## Requirements

### ViewMode 与生命周期

- 使用唯一 `ThicknessViewMode { Preview, Slider, FineDial }` 表达粗细视觉模式，禁止通过多个 FineDial 布尔值拼装视图状态；`thicknessSliderPinned` 最多保留为 Slider 的退出策略。
- FineDial 进入后 Pointer Up 不退出；preset 和支持的 pen-type 切换不退出；有效点击已展开的小三角直接动画返回 Preview。
- DrawAttributeBar 关闭、fold、离开 Pen Mode、`ThicknessSliderAvailable()` 失效、capture/cancel 或 offSignal 必须结束 FineDial 的交互/物理并清理 stale state。
- Hit-testing 由 target ViewMode 决定；退出动画可以继续绘制，但 target 已关闭时不得继续命中。

### 激活与输入优先级

- Slider 可见且可交互时提供相互独立的 Drag Activation Zone 与更外侧 Click Activation Zone；所有间距/尺寸为命名 DIP 常量，并由 `panelScale`、zoom、`previewSide` 推导。
- Drag Zone 按下立即进入 FineDial drag；Click Zone 仅在移动未超过现有 5 DIP slop、抬起仍有效且未命中实际 Popup 时进入持久 FineDial。
- 普通 Slider drag 进入外侧 Click Zone、保持按下且 X 在容差内稳定约 500 ms 后进入 FineDial；离区、X 明显移动或抬起取消该 dwell。
- Fine activation dwell 优先于 Hold dwell，并重置/隐藏 Hold UI；真正进入 FineDial 后，新的有效 drag 仍完整支持 Hold。
- 所有激活方式在切换帧以当前 pointer X 和当前 candidate/visual value 重新锚定，粗细不得跳变。
- 现有 direct-touch Preview gesture 保持 Preview 模式，不因本任务自动进入 FineDial。

### 候选值、Hold 与程序化变化

- FineDial drag 继续使用现有 3× distance-per-unit；连续视觉值允许亚整数和有限 overscroll，logical candidate 始终 `round + clamp` 到当前笔型整数范围。
- drag + inertia + settle 是同一 selection chain；过程中只更新 candidate/Popup，settle 后至多提交一次最终选择，禁止惯性逐帧 `SetPenWidth`。
- Hold lock 后 FineDial/Popup 保持显示，visual/candidate 冻结，pointer movement 不再改值，真实 Pointer Up 不启动惯性并只结束本次 lock interaction；ViewMode 仍为 FineDial。
- 惯性期间有效点击小三角时停止物理链，提交当前取整并夹取后的 candidate 一次，然后直接返回 Preview。
- preset 或支持的 pen-type 有效切换先取消旧物理/candidate且不提交，由现有 `drawAttributePenThickness` / normalized transition 驱动 FineDial 转到新值；旧 inertia 不得覆盖新值。
- 无效按钮按压不改变业务值；暂停的物理链从原 visual value 恢复并重置时间基准。

### 2.5D 视觉、Popup 与动画

- 使用 D2D1.1 primitive 与解析圆柱投影绘制，不使用 D3D mesh/shader、DirectManipulation 或复杂 effect chain。
- 每个有效整数值对应 tick；5 的倍数更长并显示数字；普通 tick 灰色，中心 tick 与上下实心 selector 为白色/中性色，禁止使用当前 pen color。
- 中心相邻 tick spacing 必须精确对应现有 3× mapping 的 1-unit pointer travel；远端通过 `sin/cos` 形成 spacing compression、轻微 depth/Y shift、scale 与 alpha fade。
- min/max 外不生成非法 tick；左右 tick 与 label 一起平滑渐隐，并以轻量 envelope/亮暗边形成克制的圆柱厚度感。
- Slider track/Thumb 与 FineDial 协调交叉过渡；FineDial→Preview 是一次直接转换，不出现肉眼可见的 Slider 中间阶段。Hold UI 不随 Slider 隐藏。
- Popup 在 FineDial 中固定于中心 selector X，沿 `previewSide` 的 outward direction 动画移到 DrawAttributeBar 外侧并保留合理 gap；FineDial 端不使用 `penTypeSafeRight`。
- FineDial 激活后关闭 Overflow badge/popup 的交互状态并使用现有动画退场，不修改 `thicknessPreviewOverflow`；FineDial 完全退出后再按现有 Preview 逻辑恢复。
- Panel expand/collapse、side flip 与 main-bar 动画期间，Dial、selector、Popup 和命中区均连续复用 `CalculateBarThicknessPreviewGeometry` 的 `panelScale`/`previewSide`。

### Physics 与性能

- 使用固定大小时间窗口估计 release velocity；速度按 `dt` 推进、clamp 大 dt、限制最大速度并使用保守指数衰减。
- inertia 中再次抓取从当前 visual position 接管；同向 swipe 只保留有限且衰减的 residual contribution，反向 swipe 自然抵消或反向。
- 边界使用连续非线性 rubber-band；logical thickness 不越界。释放/惯性越界后使用高阻尼 spring-back，并在停止前平滑吸附最近有效整数。
- FineDial inactive、无退出动画且无 physics 时必须在入口直接跳过 tick generation、projection、text、geometry 和物理工作，也不得持续请求渲染。
- Active 时只计算可见角度范围并设置固定 tick 上限；稳定帧不得进行 heap 扩容、字符串生成、MeasureText/CreateTextLayout 或 D2D/COM resource 创建。
- brush/format 复用现有 cache；selector geometry 与 major labels lazy-cache；device reset 必须清理 FineDial 相关 cache。

## Hard Constraints

- 不改变现有 3× precision 比例、整数 thickness 范围、preset 值或 `SetPenWidth` 业务语义。
- 不建立与现有 Slider 并行的 thickness 数据模型，不创建通用 animation/physics framework。
- 不破坏既有 Slider、Preview、Hold animation/lock、Popup、Preview X/safe bound、overflow、preset 或 pen-type behavior。
- 不修改传统悬浮栏、绘制引擎、配置、i18n、资源文件或公共 API。
- 禁止启动 GUI；允许静态分析、无窗口测试、完整 build 和 git 检查。
- 最终任务保持 `in_progress`，不 commit、不 archive。

## Acceptance Criteria

- [ ] 1. 现有 directTouchPreviewGesture 不会错误激活 FineDial。
- [ ] 2. Slider 可见后，Drag Activation Zone 可直接进入 FineDial drag。
- [ ] 3. Click Activation Zone 可通过单击进入持久 FineDial。
- [ ] 4. Click Zone 与 Slider 保持更大的防误触间距。
- [ ] 5. Popup 区域被排除，不会因点击 Popup 激活 FineDial。
- [ ] 6. Slider drag 向 outward zone 移动并 X 基本稳定 0.5s，可激活 FineDial。
- [ ] 7. Activation dwell 中不会出现 Hold-to-lock 提示。
- [ ] 8. FineDial drag 内 Hold-to-lock 仍正常工作。
- [ ] 9. FineDial activation 不产生 thickness jump。
- [ ] 10. FineDial Pointer Up 后仍保持 FineDial View Mode。
- [ ] 11. Slider track 和 Thumb 在 FineDial 中通过动画隐藏。
- [ ] 12. Hold UI 不会因为隐藏 Slider 而被一起隐藏。
- [ ] 13. Overflow Hint 在 FineDial 中正确退出。
- [ ] 14. FineDial 退出后 Overflow 可按原业务状态恢复。
- [ ] 15. FineDial 根据 previewSide 正确上下镜像。
- [ ] 16. 普通整数 tick 为灰色。
- [ ] 17. 5 倍数 tick 更长，并显示数值。
- [ ] 18. 当前中心 tick 为白色。
- [ ] 19. 中心存在上/下两个实心 selector triangles。
- [ ] 20. 不使用当前 Pen Color 染 FineDial。
- [ ] 21. 中心 tick spacing 与现有 3× mapping 的 1-unit travel 对应。
- [ ] 22. 左右远端具有自然 perspective compression。
- [ ] 23. 左右远端透明度平滑消失。
- [ ] 24. FineDial 具有轻微圆柱边缘 / depth 感。
- [ ] 25. 不存在真实 D3D mesh / shader。
- [ ] 26. min/max 外不存在非法 tick。
- [ ] 27. 到 min/max 继续拖动存在有限 resistance / rubber-band。
- [ ] 28. overscroll 时真实 thickness 不越界。
- [ ] 29. overscroll 松手后自然 spring-back。
- [ ] 30. 普通 release 存在短促惯性。
- [ ] 31. inertia 与 FPS 无关，使用 dt。
- [ ] 32. 大 dt 会 clamp，不会瞬间飞走。
- [ ] 33. 连续同方向 swipe 可以有限加速。
- [ ] 34. 反方向 swipe 能自然减速 / 反向。
- [ ] 35. velocity 有明确上限。
- [ ] 36. Pointer Down during inertia 从当前 visual position 接管，无 jump。
- [ ] 37. FineDial 拖动视觉是 continuous，而不是 integer step teleport。
- [ ] 38. logical thickness 仍然保持现有整数语义。
- [ ] 39. Hold lock 后 FineDial 不消失。
- [ ] 40. Hold lock 后 Pointer movement 不再改变 thickness。
- [ ] 41. Hold lock release 不启动 inertia。
- [ ] 42. Hold release 后 FineDial View Mode 仍保持。
- [ ] 43. Popup FineDial 模式下固定在中心选择轴。
- [ ] 44. Popup 会动画移动到 DrawAttributeBar 外侧。
- [ ] 45. Popup 与 DrawAttributeBar 保持合理间隙。
- [ ] 46. FineDial Popup 不再使用 penTypeSafeRight。
- [ ] 47. preset thickness 时 FineDial 不退出。
- [ ] 48. preset thickness 时 Dial 使用已有 thickness animation 转过去。
- [ ] 49. pen type change 时 FineDial 不退出。
- [ ] 50. pen type change 后 range / ticks 正确更新。
- [ ] 51. 旧 inertia 不会覆盖 preset / pen-type 的新值。
- [ ] 52. FineDial 下点击小三角可直接动画返回 Preview。
- [ ] 53. DrawAttributeBar expand/collapse 中 FineDial 几何正确跟随。
- [ ] 54. panel 完全隐藏后 FineDial 不继续 physics/render work。
- [ ] 55. FineDial 不可见稳定状态没有 tick iteration。
- [ ] 56. FineDial 不可见稳定状态没有 inertia update。
- [ ] 57. 稳定帧不重复创建 TextLayout / D2D resource。
- [ ] 58. device resource reset 正确处理 FineDial cache。
- [ ] 59. WM_CAPTURECHANGED / cancel 不留下 stale gesture state。
- [ ] 60. 现有普通 Slider 行为没有 regression。
- [ ] 61. 现有 Preview 行为没有 regression。
- [ ] 62. 现有 Hold animation 没有 regression。
- [ ] 63. 现有 Preview safe bound 在普通 Slider 模式仍正常。
- [ ] 64. `git diff --check` 通过。
- [ ] 65. ARM64 host 完整 build 通过。

## Out of Scope

- 第二轮 physics feel 调优、真实 3D、GPU shader、DirectManipulation、配置化物理参数和通用刻度盘组件。
- GUI 自动化或人工视觉验收；最终必须把未运行项目标为 `NOT VERIFIED`，不能推断为 PASS。

## Blocking Questions

- 无。用户已经批准最终计划，并确认小三角退出提交当前候选、preset/pen-type 切换取消旧候选且不提交。
