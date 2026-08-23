# UI3 居中底栏主栏收缩动画根节点修复 - Design

## Scope

修改 `Bar.BottomDock.h`、`Bar.RenderLoop.cpp`、`Bar.Main.cppm` 及对应 Headless 测试；保留 `Bar.State.cpp` 的居中/无效窗口宽度方向门禁，以及 `Bar.Interaction.cpp` 的同快照双轴 hover 逆映射。不修改触摸指示器坐标。

## Disproven Approach

上一轮把可见中心拆成以下状态：当前布局方向、最后成功方向、居中锁存方向、换向批次、逐帧 correction、pending/in-flight rebase 和失败回滚备份。真实设备复测显示故障完全不变，因此“只修复重复 restart，再等待 correction 被 rebase 吸收”不是可接受的最终架构。本实现删除这些状态，不再依赖它们的时序收敛。

## Root Ownership Matrix

| 状态 | 根节点 X 所有者 |
| --- | --- |
| BottomDocked + Centered + Stable + Expanded，且无 drag/spring/display transition | 当前帧主栏几何反推 |
| Center 捕获、拖拽、脱离、恢复 | 原中心状态机与弹簧 |
| Free 底栏 | 原 `displayCenterX` 与方向分类 |
| 浮动、折叠、白板首次放置、显示切换 | 原布局/显示逻辑 |

## Stable Center Derivation

主按钮是根节点，主栏仍继承主按钮。设主按钮可见宽度为 `buttonWidth`，主栏相对主按钮的当前中心偏移为 `barX`，主栏可见宽度为 `barWidth`：

```text
relativeLeft  = min(-buttonWidth / 2, barX - barWidth / 2)
relativeRight = max( buttonWidth / 2, barX + barWidth / 2)

mainButton.x = monitorCenterLocal
             - (relativeLeft + relativeRight) / 2
```

`buttonWidth` 和 `barWidth` 都包含当前可见描边。该求解必须位于主栏、主按钮和按钮动画值推进之后，同时早于 Popup、颜色面板、粗细面板等下游绝对几何派生；这样整个既有继承树在同一帧消费同一个新根节点。求得根节点后执行：

1. `mainButton.x.SetDirect(derivedX)`；
2. 同步 `displayCenterX`，保证下一帧原显示位置阶段不会覆盖结果；
3. 更新主按钮继承原点；
4. 重新执行 `MainBar.Inherit(Center, MainButton)`；
5. 重新计算本帧主按钮/主栏绝对边界和水平映射；
6. 根节点变化时标记 Main、Draw、Geometry、More 视觉组。

根节点直接派生自已经推进的 `mainBar.x/w`，不创建自身动画，也不修改主栏时间线。

## Direction Handling

稳定居中展开时保持渲染线程已有 `mainBarLayoutSide`，忽略由动态 HWND 包络产生的瞬时方向请求。离开居中后恢复消费 `widgetPosition.mainBar`，普通换向继续使用既有关键帧。无需成功方向锁存或居中清理批次。

## Predicted Viewport

当前帧反推只能保证已呈现像素；viewport 还必须覆盖后续动画。预测阶段读取 `mainBar.x/w` 及描边的完整曲线 range，保守计算联合左右边界范围，再反推 `mainButton.x` 范围。该范围作为主按钮、主栏和继承子视觉的共同父 X range。不得再对整个预测包络增加 correction outset。

## Input Mapping

真实弹簧期间水平主体映射仍可非恒等。普通按钮 hover 一次捕获 `BottomDockPresentedSnapshot()`，用同一 tuple 逆映射 X/Y；原消息保留给后续刚性浮层。

## Failure Matrix

| 场景 | 行为 |
| --- | --- |
| 稳定居中 Draw → Selection | `x/w` 正常推进；根节点随当前值逐帧反推 |
| 左向展开 | 使用同一联合边界公式，结果镜像 |
| 描边变化 | 可见宽度包含当前描边 |
| drag/spring/display transition | 不反推根节点，原所有者继续工作 |
| 无效宽度 | helper 返回 invalid，不改根节点 |
| 非居中换向 | 保留原换向关键帧 |
| 呈现失败 | 现有 dirty/present 重试生效；没有额外 rebase 事务 |

## Rollback

核心改动集中在纯 helper、稳定居中派生块和预测 range。若真实设备复测失败，可整体回退这三处；旧 correction/rebase 代码不应恢复，因为它已被实际结果否定。
