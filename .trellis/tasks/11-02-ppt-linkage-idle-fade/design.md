# Design

## A. 设置导航

- `Design::PageId` 新增 `Linkage = 17`（保留旧编号，不复用空洞）；`Setting.cpp` 的 `settingTabEnum` 增加 `tabLinkage = 17`，与 `settingTab = static_cast<int>(page)` 映射一致。
- `Setting.Shell.cpp` middleEntries 在 Performance 后插入 `{ PageId::Linkage, SettingsUI.Linkage.N, glyph }`，`NavigationMiddleItemCount` 7→8。
- 联动页子状态 `settingLinkageTab`（Overview / Ppt），ScrollChild 的 PushID 区分子页；侧栏导航进入时重置为 Overview（与插件页相同）。
- 原 `tab4/tabPlug2` 的 PPT 详情代码整体迁到 `tabLinkage/Ppt`，插件页删除 PPT 卡片与 `tabPlug2` 分支（枚举值保留不复用）。

## B. 配置

- `Inkeys::config.PlugIn.PPTHelper.IdleFadeSeconds`：`IdtAtomic<int>`，NoUpload，默认 10；0 表示关闭。设置下拉映射 {0,5,10,15,30}，读盘只接受这些整数，其他值回退 10（审阅后修正）。
- 变更后 `QueueConfigWrite()` + `Ppt::NotifyConfigurationChanged(ConfigGroup::IdleFade)`（新增枚举值，只重新发布快照）。

## C. 数据流

```
config.IdleFadeSeconds ─ Ppt::PublishSnapshot → PptState.idleFadeSeconds
PublishPptState(merged)：同 session 且 currentPage 变化 且 距最近控件发起翻页 > 3s → externalFadeSerial++
InvokeDirection(控件翻页/长按连发) → lastControlNavigation = now；MarkIdleActivity(index)
输入 worker：Move(非触摸生成, 命中) → hovered=true + activity；Leave → hovered=false + activity
            Down 接受 → pressed=true + activity；Up/Cancel/CaptureChanged → pressed=false + activity
            拖动 Move → activity
渲染线程 ConfigureSurface：
   entering 可见 → activity=now, 消费当前 externalFadeSerial
   decision = ResolvePptIdleFade({timeout, ppt 模式可见, hovered||pressed, externalSerial!=consumed, activity, now})
   desired = visible ? (dimmed ? 0.4 : 1) : 0；变化时 SetOpacity(desired, 时长)
   发布 idleDeadlines[index] = decision.wake → PageControlInputTick 到期 RequestSurface
```

- 活动/悬停/按下/外部序号均为原子量（输入 worker 与渲染线程分属不同线程），渲染线程只读后决策。
- `MarkIdleActivity` 会清除该控件的“外部淡化”（把已消费序号推进到当前序号），并在控件当前处于淡化时 `RequestSurface`。
- 透明度时长：可见性切换沿用 `LayoutTransitionDurationSeconds()`；淡出 0.5s，恢复 0.15s。Scene 的 `opacity>0` 仍为 enabled，命中不受影响。

## D. 纯策略（可测）

`PageControl.cppm` 导出：

```cpp
inline constexpr double PptIdleFadeOpacity = 0.4;
struct PptIdleFadeInput { int timeoutSeconds; bool eligible, engaged, externalTurn; time_point activity, now; };
struct PptIdleFadeDecision { bool dimmed; time_point wake; };
constexpr PptIdleFadeDecision ResolvePptIdleFade(const PptIdleFadeInput&) noexcept;
[[nodiscard]] constexpr bool IsPptExternalPageTurn(prevSession, prevPage, session, page, sinceControlNavigation) noexcept;
```

## 兼容与回滚

- 旧 main.json 没有新字段时取默认 10；回滚只需删除字段读取，其余为 UI 迁移。
- 插件页 enum 值 `tabPlug2` 保留，不复用编号。
