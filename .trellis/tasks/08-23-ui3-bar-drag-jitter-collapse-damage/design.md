# UI3 主栏拖动闪动与缩窄残影修复 - Design

## Scope

修改 `Bar.BottomDock.h`、`Bar.Main.cppm`、`Bar.Interaction.cpp`、`Bar.RenderLoop.cpp` 及对应 Headless 测试。保持既有窗口、线程、D2D target、GDI interop 和 ULW 架构。

## Evidence And Root Cause

### Release handoff race

`Seek()` 先发布 `bottomDockDragActive=false` 和释放 phase，随后才把 `directWindowDragPhase` 从 `Dragging` 改为 `Idle`。渲染线程可能在两者之间取得一帧：这帧已按释放态布局，却不能吸收仍挂起的 direct translation。它会让换向从错误根位置启动，甚至在下一帧吸收后直接落到终态。

修复策略是在渲染入口显式识别“释放 tuple 已发布但 drag phase 尚未交接”的短暂状态。该帧不进入布局或呈现，待 phase 可原子取得后先吸收 translation、调用 `PositionUpdate()`，再建立换向批次。

### Stale frame destination rollback

D2D 几何计算与最终 ULW 之间允许交互线程继续直移 HWND。当前 `ResolveBarBottomDockFrameTranslation()` 在 transition serial 改变时回退到帧内旧 translation，随后 ULW 会把真实 HWND 移回旧位置，下一帧再移到新位置，形成单帧闪回。

同一个 `directWindowDragMutex` 已保证读取实际 HWND 位移与 ULW 提交不会并发。过期帧应使用锁内读取到的 `directWindowPresentedTranslation` 作为屏幕目的地：纯直移时不回退，捕获/脱离屏障期间该值仍是上一成功位置，因此不会提前移动新形态。帧内 D2D 几何仍保持自洽，下一帧再消费新 serial。

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
