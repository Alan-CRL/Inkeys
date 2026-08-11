# UI3 近期 Git 历史与任务边界

## 检索范围

- 主范围：2026-07-09 至 2026-08-09，当前 `feature/animation` 分支。
- 依据：`git log`、相关 `git show`、当前源码以及已归档/活跃 Trellis task。
- 实施阶段 Phase 5 必须按当时 HEAD 重新生成范围，不能只复用本文件。

## 关键提交链

| Commit | 日期 | 与本任务有关的事实 |
| --- | --- | --- |
| `f9421f01` | 07-11 | 动画改为 duration-based |
| `36ba6c9e` / `bdb3b7c3` / `c766298f` | 07-12 | 共享 timeline、绘制属性批次同步、关键帧与曲线续接 |
| `4b7f4704` | 07-12 | 双击目标与消息队列的输入安全修复 |
| `22eb8cea` | 07-17 | `CanJoin` 阈值统一为 50% |
| `88091b2f` | 07-19 | Setting 已从 DX9 迁为独立 DX11，并使用内嵌 CSO |
| `b8e3d1b7` / `0e27f9b5` | 07-23/24 | 第三光休眠状态机与 Draw2 落笔通知 |
| `cde5627c` | 07-26 | UI3 device epoch、整帧串行租约、generation 重建、局部 dirty、A8/Gaussian cache 与 drawing-aware 降载基础 |
| `eb9487ca` | 07-27 | 同一动画属性每帧只提交一次最终 `SetTar` |
| `7dc8fd73` -> `76550064` | 07-28 | 放弃“动画时关闭第三 diffuse”，改为几何归一化复用遮罩 |
| `b5e4f45c` | 08-01 | 修复 divider list 重建导致 preset 双重释放/启动卡死 |
| `6f4dd2bc` | 08-01 | 晚期布局变化时主栏和按钮进入同一新动画批次 |
| `e600a06f` | 08-01 | 阻止被浮层遮挡控件继续命中 |
| `e86b64f` | 08-03 | SVG raster cache/旋转与 superellipse path cache |
| `f81638a1` | 08-09 | Fine Dial 画笔切换状态继续加固 |

从 `e86b64f` 到审计 HEAD 又有 27 个产品提交修改 Bar core；`Bar.Main.cpp` 累计约 `+5529/-899`。这说明即使 08-03 cache task 已验收，当前动画、状态和 hot path 仍需要重新建立基线。

## 已归档性能工作

### `07-26-ui3-lighting-render-performance`

已经完成：

- WARP/device epoch、整帧串行租约与 generation 资源重建。
- gradient/solid brush 复用、A8 预模糊 mask、九宫格、几何 cache。
- dirty clip、局部 Clear、控件边界剔除、ULW dirty rect、删除显式 Flush。
- Draw2 与 Bar 的 drawing activity 通知基础。

仍未完成的原任务记录：`implement.md:44-47` 中的相关自动测试、重复性能 benchmark、人工兼容场景与 Win7 验证；原 PRD 的定量性能门槛也未勾选。新任务应承接“测量和验收缺口”，不重做 cache/device 方案。

### `08-03-ui3-render-cache-optimization`

已经完成并经用户确认：SVG bitmap reuse/尺寸阈值/旋转、superellipse path reuse、device-generation invalidation。新任务只测当前 hit/miss 和剩余瓶颈，不以相同方案重复改造。

## 活跃任务重叠

### `07-17-investigate-unified-ui-d3d11-pipeline`

其 `prd.md:9-11` 仍称 Setting 使用 D3D9、没有 CSO 证据，已被 `88091b2f` 与当前源码推翻。当前 Setting 是独立 DX11 + embedded CSO。新任务不重复 Setting 迁移或 CSO 调查。

### `08-01-render-pipeline-refactor`

该 future epic 覆盖 UI2 弃用、多 HWND 拆分、Setting/PPT/背景窗共享 device、公共串行调度和窗口生命周期。它明确排在整体性能优化之后，但其 PRD 也低估了 `cde5627c` 已落地的 Bar-side shared epoch/lease 基线。

本任务只允许在当前 UI3 Bar 客户端内部审计和优化：

- 可以维护现有 device epoch、frame lease、dirty/present 与错误恢复合同。
- 不得统一 Setting/PPT/白板窗口，不得重做公共 multi-HWND scheduler，不得弃用 UI2。
- 如果 Phase 5 发现跨客户端问题，只记录到最终风险/后续任务，不扩大本任务实现范围。

## 对原始任务描述的修正

1. `Bar.Main.cpp` / `Rendering()` / `Interact()` 的规模估算准确。
2. 动画 framework 已存在于 `Bar.UI.*`；主文件的问题是编排、通用推进和全量计算仍耦合，而不是“完全没有 framework”。
3. UI3 已有真实 idle wait，不是持续 60 FPS；需要审计 wake/coalescing/shutdown，而不是重新发明动态刷新。
4. 当前 Gaussian、mask、SVG、PNG 和 superellipse 已有多层 cache；Phase 3 先测 hit/miss/invalidation。
5. drawing notification 当前只影响第三鼠标光的一次 Dormant 判断，不是整段绘图 suppress 全部 lighting。
6. Setting 已是独立 DX11 + embedded CSO；共享多窗口管线属于其他任务。
7. 近期大量新交互（More、几何、粗细 Slider/Fine Dial、颜色选择器等）继续扩张 Bar core，结构和状态安全审计范围必须覆盖这些提交，而不能只复查 7 月动画代码。
