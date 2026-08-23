# 居中底栏 Draw -> Selection 根因调查

## Inputs

- 录屏：`D:\Personal\Downloads\2026-08-23 12-28-28.mp4`
- 指定工具：`D:\Project\Resource\ffmpeg\ffmpeg.exe`
- 基线提交：`595121fb fix: stabilize centered dock layout transactions`
- 父任务：`.trellis/tasks/08-20-ui3-bar-bottom-dock-feedback/`

## Video Evidence

ffmpeg 读取结果为 46.67 秒、2880x1920、60 fps。关键帧显示：Draw 宽度时联合外框稳定居中；点击 Selection 后活动脏区和 HWND 包络扩大，主栏先向左/左上移动且宽度几乎不变；随后短栏闪现，最后才重新居中并进入空闲帧。

这说明宽度、根位置和窗口包络不是同一逐帧几何合同，而不只是按钮内容更新较晚。

## Disproven Hypothesis

上一轮把根因归为“居中方向误锁存 -> 遗留换向状态 -> 每帧 forceRestart -> 宽度冻结 -> correction 无法 rebase”。该链条在静态代码上成立，并促成了方向门禁、清理上升沿与两阶段 rebase；但用户在真实设备上确认问题与原来完全一样。由此只能保留两项局部结论：

- 居中态不应由动态 HWND 宽度重新分类方向；
- hover 必须对主体 X/Y 使用同一成功快照。

不能再宣称 stale-side cleanup 或 rebase 时序是已验证根因，相应测试也不能作为修复证据。

## Architectural Cause

稳定居中的可见结果由多套状态共同决定：`displayCenterX/mainButton.x` 控制根，`mainBar.x/w` 控制子几何，水平 mapping correction 控制最终像素，pending/in-flight rebase 再尝试把 correction 吸收到根。方向锁存和 viewport 门禁又分别依赖成功提交时序。

因此“联合外框始终居中”不是某个单一所有者直接保证的，而是期待多个动画和事务最终收敛。中间任一状态延迟、拒绝或跨帧不一致，都允许出现先平移、后闪现、再跳到最终位置的路径。

## Chosen Fix

稳定居中态把 `mainButton.x` 设为唯一根位置所有者。渲染线程在主栏 `x/w` 已推进后，直接从主按钮与主栏当前可见联合边界反推根节点；主栏及全部子控件仍沿原继承树定位。拖拽、弹簧、捕获、恢复和显示切换时不接管根节点。

旧 correction/rebase/方向清理状态全部删除，预测 viewport 使用同一反推关系传播完整动画范围。

## Deferred Issue

触摸指示器的屏幕坐标/布局坐标问题是独立缺陷，本任务不处理。
