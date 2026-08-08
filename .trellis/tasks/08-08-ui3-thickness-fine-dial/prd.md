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

- Slider 可见且可交互时提供连续相接的 Drag Activation Zone 与更外侧 Click Activation Zone；两者组成 FineDial 专用 activation corridor，Popup 排除或动画瞬态也只能 consume，禁止落入普通 Slider 绝对投影。
- Drag Zone 按下仅 armed，水平移动超过现有 5 DIP slop 后以当帧位置/值进入 FineDial；Click Zone 按下当帧立即进入持久 FineDial 并可继续拖动。
- 普通 Slider drag 进入外侧 Click Zone 后，只要连续留区 `1000 ms` 即进入 FineDial；区域内 X/Y 移动不重置，也不再要求额外 outward 位移，只有离区、抬起/cancel 或生命周期失效才取消 dwell。
- Fine activation dwell 优先于 Hold dwell，并重置/隐藏 Hold UI；真正进入 FineDial 后，新的有效 drag 仍完整支持 Hold。
- Slider-drag recognition 先在 activation region 中心显示约 `0.5` 的基础 Dial/ticks，dwell 在 1 秒内从 `0.5` 推进到 `1.0`；离区平滑退回暗态，整个 gesture 结束后才平滑退到 0。正式 ViewMode 激活后再动画显示 labels、中心线和 selectors，并把几何从 recognition center 移到最终位置。
- 所有激活方式在切换帧以当前 pointer X 和当前 candidate/visual value 重新锚定，粗细不得跳变。
- 现有 direct-touch Preview gesture 保持 Preview 模式，不因本任务自动进入 FineDial。

### 候选值、Hold 与程序化变化

- FineDial drag 使用 Brush 当前 Slider 量程推导的唯一 3× canonical distance-per-unit；所有支持笔型共用该单位行程，但连续视觉值、`round + clamp` 候选和 min/max 仍使用当前笔型自己的范围。
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
- 正常 FineDial→Preview 且属性面板仍展开、主栏未折叠时，Popup 锁存当前实际渲染中心并原地 scale/fade；快速 hide→show 从锁存几何连续重定向。面板收起、fold、availability/capture/lifecycle 退出不使用该锁存，保留原有追随几何。
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

- Brush FineDial 保持现有 3× precision 作为 canonical unit；不改变普通 Slider mapping、各笔型整数 thickness 范围、preset 值或 `SetPenWidth` 业务语义。
- 不建立与现有 Slider 并行的 thickness 数据模型，不创建通用 animation/physics framework。
- 不破坏既有 Slider、Preview、Hold animation/lock、Popup、Preview X/safe bound、overflow、preset 或 pen-type behavior。
- 不修改传统悬浮栏、绘制引擎、配置、i18n、资源文件或公共 API。
- 禁止启动 GUI；允许静态分析、无窗口测试、完整 build 和 git 检查。
- 最终任务保持 `in_progress`，不 commit、不 archive。

## Acceptance Criteria

- [ ] 1. 现有 directTouchPreviewGesture 不会错误激活 FineDial。
- [ ] 2. Slider 可见后，Drag Activation Zone 按下只 armed，越过 5 DIP 水平 slop 后零跳变进入 FineDial drag。
- [ ] 3. Click Activation Zone Pointer Down 当帧进入持久 FineDial，并可继续按住拖动。
- [ ] 4. Drag/Click Zone 共边界且整个 activation corridor 不落入普通 Slider direct-adjust。
- [ ] 5. Popup 区域被排除并 consume，不会激活 FineDial 或触发 Slider 跳值。
- [ ] 6. Slider drag 连续留在 dwell region 1000 ms 可激活；区域内 X/Y 移动不重计时，也不要求额外 outward gate。
- [ ] 7. Activation dwell 中不会出现 Hold-to-lock 提示。
- [ ] 8. FineDial drag 内 Hold-to-lock 仍正常工作。
- [ ] 9. FineDial activation 不产生 thickness jump。
- [ ] 10. FineDial Pointer Up 后仍保持 FineDial View Mode。
- [ ] 11. Slider track 和 Thumb 在 FineDial 中通过动画隐藏。
- [ ] 12. Hold UI 不会因为隐藏 Slider 而被一起隐藏。
- [ ] 13. Overflow Hint 在 FineDial 中正确退出。
- [ ] 14. FineDial 退出后 Overflow 可按原业务状态恢复。
- [ ] 15. FineDial 根据 previewSide 正确上下镜像。
- [ ] 16. Recognition preview 在 activation region 中心以约 0.5 暗态显示 envelope 与灰色 tick，dwell 1 秒平滑到 1.0。
- [ ] 17. 正式激活后 5 倍数 tick 才动画显示数值；preview 阶段不查询 label layout。
- [ ] 18. 正式激活后当前中心白线短促动画出现。
- [ ] 19. 正式激活后上/下两个实心 selector triangles 短促动画出现。
- [ ] 20. 不使用当前 Pen Color 染 FineDial。
- [ ] 21. Renderer、drag/velocity/physics/rubber-band 共用 Brush 量程推导的 canonical 3× unit travel；Highlighter 每 1 单位物理距离与 Brush 相同。
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
- [ ] 45. 正常 FineDial→Preview 时 Popup 锁存实际中心原地退出且快速反转无 teleport；panel/fold/lifecycle 退出不锁存。
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
