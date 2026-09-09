# UI3 主栏拖动闪动与缩窄残影修复 - Design

## Scope

修改 `Bar.BottomDock.h`、`Bar.Main.cppm`、`Bar.Interaction.cpp`、`Bar.RenderLoop.cpp` 及对应 Headless 测试。保持既有窗口、线程、D2D target、GDI interop 和 ULW 架构。

## Evidence And Root Cause

### Release handoff race

`Seek()` 先发布 `bottomDockDragActive=false` 和释放 phase，随后才把 `directWindowDragPhase` 从 `Dragging` 改为 `Idle`。渲染线程可能在两者之间取得一帧：这帧已按释放态布局，却不能吸收仍挂起的 direct translation。它会让换向从错误根位置启动，甚至在下一帧吸收后直接落到终态。

修复策略是在渲染入口显式识别“释放 tuple 已发布但 drag phase 尚未交接”的短暂状态。该帧不进入布局或呈现，待 phase 可原子取得后先吸收 translation、调用 `PositionUpdate()`，再建立换向批次。

### Stale frame destination rollback

D2D 几何计算与最终 ULW 之间允许交互线程继续直移 HWND。当前 `ResolveBarBottomDockFrameTranslation()` 在 transition serial 改变时回退到帧内旧 translation，随后 ULW 会把真实 HWND 移回旧位置，下一帧再移到新位置，形成单帧闪回。

同一个 `directWindowDragMutex` 已保证读取实际 HWND 位移与 ULW 提交不会并发。对于屏障已成功呈现后的纯直移过期帧，应使用锁内读取到的 `directWindowPresentedTranslation` 作为屏幕目的地：纯直移时不回退，捕获/脱离屏障期间该值仍是上一成功位置，因此不会提前移动新形态。帧内 D2D 几何仍保持自洽，下一帧再消费新 serial。

### Full replacement split decision

`viewportMappingChanged` 会强制业务全脏，但 ULW 的 `prcDirty` 只由 `presentMappingTracker` 的另一项结果控制。两条判定可在 committed anchor 平移、最终收窗或失败恢复时分离，使重新解释 client/source 的帧仍走局部 layered-window 更新。

将两者合并为单一 `forceFullWindowReplacement`。该值同时控制业务 full damage、debug damage、`presentDirty` 和 `ulwi.prcDirty=nullptr`。成功后按现有顺序共同提交 viewport、mapping、window bounds 与输入快照。

### Split-axis hit snapshot

`ApplyBarBottomDockBodyHitTestFromRigid()` 依次调用 Y/X 方法，而两个方法分别读取 `BottomDockPresentedSnapshot()`。两轴 serial 在调用之间更新时，会生成从未上屏的组合坐标。

在 `BarUISetClass` 增加点级组合命中入口，一次捕获 snapshot 后同时解析 visual X/Y 和 logical X/Y。Grip 与 Body 沿用各自映射分类；旧的单轴入口保留给确实只处理单轴的调用方。

## Data Flow

```text
Seek sample
  -> transition seqlock publishes axes + target translation
  -> optional try_lock SetWindowPos updates actual presented translation
  -> render snapshots one stable tuple
  -> release gate / direct translation absorb
  -> layout + two-axis mapping + dirty + viewport
  -> directWindowDragMutex
       -> resolve destination from current actual HWND translation
       -> full or local ULW
  -> successful snapshot commit
  -> queued screen point -> one-snapshot X/Y inverse hit mapping
```

## Failure And Compatibility Matrix

| Case | Required behavior |
| --- | --- |
| Release publication races with render | Skip premature release frame; absorb first |
| Interaction moves HWND after frame snapshot | Stale frame stays at actual HWND position |
| Capture/deattach barrier is pending | Actual translation remains previous presented position |
| Viewport/source/size tuple changes | Full ULW replacement with null dirty pointer |
| GetDC/ULW/ReleaseDC/EndDraw fails | Do not advance any success snapshot; retain full retry |
| X/Y mapping serial changes during hit | One captured snapshot supplies both axes |
| Animation disabled | Same ownership/order, immediate geometry values |

## Rollback

改动分为四个窄边界：释放门禁 helper、帧目的地 helper、整窗替换布尔值、组合命中方法。任一行为回归可单独回退；不得恢复 client 坐标异步解释，也不得移除现有成功快照 seqlock。

## 2026-09-09 续修边界

差异在 UI3 Bar 的交互发布、动画布局继承、变换后 damage 与 ULW 提交之间；最终修改为 Bar.BottomDock.h、Bar.WindowGeometry.h、Bar.PresentDecision.h、Bar.RenderLoop.cpp、Bar.Interaction.cpp、Bar.Main.cppm、Bar.Animation.cppm，以及现有 animation_tests.cpp、bar_bottom_dock_tests.cpp、dirty_region_tests.cpp、window_geometry_tests.cpp。分别承载纯坐标/弹簧逻辑、呈现锁及决策、生产接线/发布顺序、布局批次和组合回归；没有修改其他产品子系统。

调查与修复合同：

1. 沿屏幕点 -> 抓取偏移 -> desired/presented translation -> 形变映射 -> viewport/source/destination 检查同一帧；窗口尺寸或原点变化不能反过来改变抓取基准或重复应用位移。
2. 居中根节点移动时核对 tracker 对上一成功边界的坐标解释；根节点移动属于布局重排时必须保留原屏幕位置 damage，只有真正整窗刚性移动才允许重基准旧边界。
3. 核对隐藏按钮的局部轨迹、继承父节点与屏幕目标之间的组合，保留原曲线/时长，不重启居中主栏动画；不得用整体关闭动画规避抽动。
4. 水平捕获主按钮使用刚性抓手映射，不能把居中布局锚点修正丢到主按钮屏幕位置上。远端弹簧从上一成功像素播入，左右展开镜像。
5. 保留既有成功事务和失败重试；不引入新窗口、配置、后端或通用动画重构。最终根因、取舍及验证证据在本轮结束前回写此处。

### 本轮直接确认的根因与修复选择

- HWND resize/ULW 已发生而 EndDraw 与成功快照尚未发布期间，呈现锁已释放。Seek 可用旧 committedWindowScreenBounds 做 SetWindowPos，随后旧渲染帧又可能覆盖新 direct translation。提交锁应贯穿窗口更新到成功快照发布；ULW 成功但后续失败时，禁止旧快照驱动直移，等待完整恢复帧。
- 居中缩短使主按钮和 capacityOrigin 同向移动，viewport 也随 committedAnchor 平移；source/size 数值可能保持不变。旧缓冲区像素因此落到旧布局边界加原点差的位置，普通 dirty union 漏掉右侧尾部。真实根布局移动导致的映射重解释必须整窗清除/替换；纯直移吸收继续复用既有容量。
- 新主栏布局批次会启动/更换父级 x/w 动画，但目标未变化的在途按钮 x 因 SetTar 的同目标早退而保留旧进度/曲线。只在实际布局批次变化时同步重定向子位置动画，保留独立按压/悬停以及稳定居中根节点每帧派生。
- 水平捕获带为 40 DIP，却同时把刚性抓手补偿和远端成功像素重基准截断到 24 DIP。捕获边缘主按钮可跳 16 DIP，39 -> 41 DIP 脱离时远端仍可跳 17 DIP。水平刚性偏移与保证连续性的初值保留精确值，弹簧从当前位置连续恢复；竖向 24 DIP、20/40 DIP 阈值及频率/阻尼不变。viewport/capacity 必须容纳水平捕获带和实际端点。

### 首次转换帧被后续采样赶超

独立审查发现同一手势的新采样可以发生在 D2D 几何计算之后、ULW 之前。首次居中捕获帧若使用窗口 translation=-39 与刚性抓手 +39，组合屏幕位置本应不变；下一次同模式采样令 serial 过期后，旧规则会改用 presentedTranslation=0，却保留位图中的 +39，使主按钮跳 39 DIP。竖向捕获同样受该组合影响。

目的地策略必须区分已成功呈现后的纯直移与尚未上屏的形态屏障：已被当前帧覆盖、仍未提交的屏障应将位图与帧内 translation 成对提交，即使后续普通采样已推进 serial；比当前帧更新的形态/显示屏障应使旧帧放弃上屏并保留完整重试。不得简单让所有过期采样重试，否则连续输入可能饿死首次捕获呈现。
