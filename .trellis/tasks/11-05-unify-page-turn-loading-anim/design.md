# 设计：统一翻页加载动画

状态词、规则见 [prd.md](prd.md)“页码槽状态表”。本文只写技术设计。

## 1. 现状要点（代码证据）

- 指示器：`Bar.PageLoadingIndicator.h` 的 `PageLoadingIndicator::Advance`，500ms 延迟（`PageLoadingStyle::Delay`）+ 淡入 180ms/淡出 140ms；仅在 `!content.whiteboard` 且 `navigationWaiting*` 时工作（`Bar.Scene.cpp:654`）。
- 白板：`content.switching`（`PageControl.cpp:1037`）→ 全部按钮 `enabled=false`（`Bar.Scene.cpp:729`）变浅 + `Status` 文字 + 旧 `DrawBusyRing`（`Bar.Scene.cpp:834`、`:1133`，`Bar.BusyRing.h`）。
- **`/N` 不变浅的根因**：`RetargetBarFixedButtonVisual`（`Bar.Button.cpp:2063-2077`）只给 `icon` 和 `primary`（`button.name`）设置 `pct = enabled?1:DisabledContentOpacity`，`pageTotal` 是 Scene 独立持有的 `BarUiWordClass`，从未被 retarget。
- 变浅度：`BarThemeMaterial::DisabledContentOpacity(lightWeight)`（深 0.30 / 浅 0.45）。
- 命中与视觉分离：命中 `hit.enabled`（`Bar.Scene.cpp:581-582`，基于 `content.switching`/`enabled`/`interactive`）和视觉 `enabled`（`:729`）是两处独立计算。
- 页码按钮几何：圆角矩形，`BarButtonCornerRadiusDip=4`（`Bar.Metrics.cppm:30`）；`candidate.buttons[2]` 的 `BarButtonHitSnapshot` 含 `boundsDip/radiusX/radiusY/superellipseExponent`。
- 状态来源：`PageControl.cpp::BuildPageContent`（`:1029`）组装 `BarPageSurfaceContent`（`Bar.Scene.cppm:39`）；PPT 恢复状态来自 `Ppt.Recovery.h::Project`。

## 2. 总体结构

```
BuildPageContent ──► BarPageSurfaceContent
                       ├─ loadState: None | Loading | FinalFailed | RetryableFailed   (新增)
                       ├─ interactive[3]（真实可点性，立即生效，沿用）
                       └─ 常态文字 currentPage/totalPage + 状态文字 statusPrimary/statusSecondary（新增，可空）
Scene.UpdateContent ──► PageLoadingIndicator（扩展）  ── 500ms 门控 ──► 三个输出:
                       ├─ textActive  : 是否显示状态文字
                       ├─ dimOpacity  : 变浅进度 0..1
                       └─ ringOpacity : 环显隐进度 0..1
Scene.DrawButton(id==3) ──► 内侧边缘环（options.content，沿用现有挂载点）
```

### 2.1 内容模型（PageControl → Scene）

- 新增 `enum class BarPageLoadState : uint8_t { None, Loading, RetryableFailed, FinalFailed }` 放入 `BarPageSurfaceContent`（`loadingSession` 之外）。取代用 `navigationWaitingPrevious/Next` 推断，同时保留二者以免破坏现有测试语义（或在实现时合并，见 implement.md 的取舍）。
- 映射（`BuildPageContent`）：
  - 白板 `pageSwitching`、PPT `navigationWaiting`/`pagePreparing`、PPT `Recovering`、调试延迟开关 → `Loading`。
  - 失败类 `retryAvailable==true` → `RetryableFailed`；`false` → `FinalFailed`。
  - 其余 → `None`。
- 文字双份：`content.currentPage/totalPage` 永远是**常态**（页码 + `/N`）；`statusPrimary/statusSecondary`（`std::optional<std::wstring>`）为“状态达成后”要显示的文字：
  - 翻页等待：`{nullopt, Wait}`；`Recovering`：`{nullopt, Recovering}`；失败类可重试：`{原因词, Retry}`；终态失败：`{nullopt, 原因词}`。
  - `kind=Status` 与白板左槽 `Loading` 的旧路径删除。
- 命中锁：`interactive[]` 立即生效（沿用）。新增：`recovery.kind != None` 时上/下一页 `interactive=false`；`Loading` 时三者均 false（白板沿用 `content.switching`）；`RetryableFailed` 时 `interactive[1]=true`，`FinalFailed` 全 false。
- 重试可点的时机：`RetryableFailed` 的页码按钮在状态文字出现（`textActive`）之前不可点——避免用户点到一个看起来还是 `1/56` 的按钮。其余按钮的不可点立即生效。

### 2.2 指示器扩展（`PageLoadingIndicator`）

输入从 `bool loading` 扩成 `BarPageLoadState state`。内部：

- `deadline`/`episode` 的语义不变：任何非 `None` 状态进入时 `deadline=now+500ms`；回到 `None` 立即清除；状态在非 `None` 之间切换（如 `Loading`→`FinalFailed`）**不重置 deadline**，因为文字、变浅已显示。
- 三个独立的“目标 + 进度”轨道，共用同一个 deadline：
  - `text`：`state!=None && now>=deadline`，**无淡入**，到点立即切换文字（内容变化仍走 `TransitionToString` 的现有文字过渡）。
  - `dim`：`state!=None && now>=deadline` → 1；沿用 180ms 入 / 140ms 出曲线（`EaseOutCubic/EaseInCubic`）。
  - `ring`：`(state==Loading) && now>=deadline` → 1；`Loading→FinalFailed` 时 `ring` 目标变 0，环退场，`dim` 保持 1（满足“环退场、保持灰”）。
- `scale`/环缩放（0.75→1）：新环不再缩放（环贴边，缩放没意义），去掉 `scale`。
- `NeedsFrames`：`moving`（任一轨道在过渡）或 `animations && ring>0`；`FinalFailed`/`RetryableFailed` 稳定后不再要帧，Scene 能回到 idle。
- `WakeDeadline`：沿用，使 500ms 到点唤醒（输入 worker 的 `PageLoadingDeadline`）。
- 动画关闭（`BarUiAnimationEnabled=false`）：deadline 仍生效，到点后 `dim/ring` 直接为目标值，环静止在起始相位。

### 2.3 变浅

- 视觉 `enabled` 改为：`visualEnabled = baseEnabled && !(dimActive && locked(id))`，其中 `dimActive = indicator.dimTarget`，`locked(id)`：
  - `Loading`/`FinalFailed`：整窗口所有内容 `locked=true`（上/下一页、页码、更多、拖动柄、菜单等）。
  - `RetryableFailed`：仅 `interactive[i]==false` 的 2–4 号按钮 `locked`；页码按钮保持亮。
- 因 `dim` 进度由 `RetargetBarFixedButtonVisual` 的 `pct` 过渡承担，Scene 只需在 `dimTarget` 翻转时改 `enabled` 参数；但 `dim` 轨道进度本身用于环 + 控制文字，不需要自己插值内容透明度（避免双重过渡）。
- **补 `pageTotal`**：在 `Bar.Scene.cpp:741` 之前，对 `id==3` 额外 `pageTotal.pct.SetTar(visualEnabled?1.0:BarThemeMaterial::DisabledContentOpacity(lightWeight), BarButtonHoverTransitionDuration)`，与 `RetargetBarFixedButtonVisual` 同口径（保持 `visible` 约束：不可见时 0）。这是白板 `/N` 不变浅的修复，也使 PPT 的 `/N` 变浅。
- 现有的 `content.switching` 保留作为“命中锁”，但不再作为视觉 enabled 条件（视觉改由指示器门控 500ms），避免白板变浅早于 500ms。

### 2.4 加载环

**几何**：以页码按钮当前可见边框的内缩路径为中心线。

- `bounds = Bounds(pageButtons[2]->button)`（DIP），`r = min(radiusX, w/2, h/2)`（`BarButtonCornerRadiusDip=4`），线宽 `W = 1.5 DIP`，内缩 `inset = frameThickness/2 + 0.75 + gap(≈1.0)`；中心线圆角矩形：`rect.deflate(inset+W/2)`，`radius = max(r - (inset+W/2), 0.5)`。线宽/内缩常量放进 `PageLoadingStyle`，在实机截图后微调。
- 只用 `radiusX/radiusY`；若按钮有 `superellipseExponent>0` 则退化为圆角矩形近似（页码按钮目前不使用 superellipse，核对一次）。
- 路径几何：`ID2D1PathGeometry`（`CreateRoundedRectangleGeometry` 的 outline 用 `ID2D1RoundedRectangleGeometry`，从顶部中点起点需手工 Path，保证起点固定为顶部中点、顺时针）。缓存 key = (宽, 高, radius, scale)，尺寸动画（数字位数变化引起按钮宽度变化）时重建；设备资源重建（`ReleaseDeviceResources`）时清空。

**弧段绘制（首选：dash）**

- 周长 `P = ID2D1Geometry::ComputeLength`；弧长 `L(t)`，起点 `s(t)`（沿路径距离）。
- 用自定义 dash 画一段：`dashes = {L/W, (P-L)/W}`（单位为线宽倍数），`dashOffset = -s/W`；`startCap/endCap = ROUND`，`dashCap = ROUND`，`lineJoin = ROUND`。
- 圆角端帽（D2D `ROUND` cap）会在两端各延长 `W/2`，弧长按 `L-W` 补偿。
- 每帧创建 `ID2D1StrokeStyle`（只改 dash 数组）。若实测帧开销或分配可见，退路：把弧段拆成采样点（`ComputePointAtLength`，约 24–48 点）画 `DrawLine` 链，或预生成固定 dash 样式 + 变换旋转（仅角度变化时可复用，长度变化时不行）。**实现第一步先做 1 小时的原型比较**，不满足就换方案（见 implement.md）。
- 起点/终点圆角：使用 `D2D1_CAP_STYLE_ROUND`（决策见 prd）。

**非线性运动**（indeterminate，参考 Material 环形进度）：

- 由指示器积累的 `phase`（秒，乘 `speed`）驱动，**不依赖全局 angle**；设备资源重建时从 `phase` 重建，满足“保持相位”。
- 周期 `T=1.6 s` 一轮；`tailPos = P * (rot(t) + ease(u))`、`headPos = P * (rot(t) + ease(clamp(u+lag)))` 形式：`rot(t)` 为缓慢匀速基础旋转（约 0.25 圈/s），`u = frac(t/T)`；头快尾慢→头慢尾快交替，弧长在 `Lmin=0.10P` 与 `Lmax=0.65P` 之间平滑伸缩（使用 `cubic-bezier(.4,0,.2,1)` 类缓动）。
- 具体函数在原型阶段用 `BarUiApplyCurve` 的现有曲线表达，验证一圈内无速度突变、端点连续。
- `animations==false` 时 `phase` 不推进，弧段固定为起点附近 `0.25P`。

**颜色**：`IconPrimary`（亮色），alpha = `ring` 轨道进度；无轨道（删除 `ringTrack` 与 `TrackWidth`）。

**绘制层次**：沿用 `options.content` 挂在 `id==3` 上（`Bar.Scene.cpp:791`），在按钮内容之上、`DrawBusyRing` 位置之前。路径/画刷/stroke 的懒创建与失败降级（`ringFailed`）机制保留。

**损伤区域**：从原来的小圆包络改成页码按钮包络 + `AntialiasOutset`；`indicatorChanged` 时 `Include` 整个页码按钮矩形。

### 2.5 删除项

- `Bar.BusyRing.h`、`Scene::DrawBusyRing`、`switchingTimed`、`switchingSince`、`IncludeButton(3)` 的白板持续帧逻辑（`Bar.Scene.cpp:671-674`）；工程文件中移除 `Bar.BusyRing.h`（如有登记）。
- `ringTrack`、`PageLoadingStyle::Radius/TrackWidth/FromRight/FromTop/ArcEnd*` 等旧常量。
- 白板 `Status` + `Loading` 左槽路径；`I18nKey...Loading` 的使用（键是否删除见 i18n 小节）。

## 3. i18n

- 键：保留 `Conflict/ReadFailed/PresentFailed/Failed/Recovering/Retry/Wait`，页码槽不再使用 `Loading`。
- `Loading`：从 20 个 `Inkeys/src/i18n/*.jsonc` 与 `IdtI18nKeys.g.h` 生成链中移除，或保留不用——**以用户决定为准**（待决）。
- 逐语言复核 `Recovering`（进行态）、`Wait`（状态词，区别于动作 `Retry`），修正见 prd 的 i18n 小节。
- 槽宽验证：复用 `PageControl.cpp:4297-4312` 的 `MeasureText` 做法，对 20 语言 × 状态（Wait、Recovering、Retry、Conflict、ReadFailed、PresentFailed、Failed）在最大页码宽度（`maximum` 9999/999）下断言文字宽度 ≤ 槽宽，不通过则缩写。

## 4. 兼容与风险

- **多表面**：四个分页表面共享 `Scene`，`loadingSession`/`episode` 的 session 隔离逻辑保持（`Advance` 已有）。
- **输入 worker**：`PageLoadingDeadline` 与 `WakeDeadline` 的契约不变，只是触发条件扩成所有非 `None` 状态；注意 `Consume` 的一次性语义（`due`）在 `Loading→FinalFailed` 转换时不应重复唤醒。
- **帧预算**：`FinalFailed`/`RetryableFailed` 稳定后必须能 idle（`NeedsFrames=false`），否则违反“空闲不渲染”约束（见 `rendering-and-ui.md` / `ui3-render-diagnostics.md`）。
- **回滚**：改动集中在 `Bar.PageLoadingIndicator.h`、`Bar.Scene.cpp/.cppm`、`PageControl.cpp`、i18n；可按“指示器 / 变浅 / 环 / i18n”四个提交分段回滚。
- **PPT 现状保持**：PPT 翻页等待期间哪些按钮可点保持现状；本任务只改视觉和失败态上/下一页的锁。

## 5. 已决策（用户确认）

1. PPT 加载（Loading）/终态失败期间，非分页按钮（更多/拖动柄等）视觉变浅但保持现有可点性。
2. `Loading` i18n 键删除（20 语言 + `IdtI18nKeys.g.h` 生成链）。
3. 所有状态文字切换、变浅、加载环统一遵循 500ms；短于 500ms 的跳变不出现任何变化。
