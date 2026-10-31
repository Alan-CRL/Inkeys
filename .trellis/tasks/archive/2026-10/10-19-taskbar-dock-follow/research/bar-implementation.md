# Bar / 启动预览实施记录（2026-10-19）

## 已实施

- `BarPendingDisplaySnapshot` 与最后成功 `BarBottomDockPresentedSnapshot` 新增 `desktopDockLine/protectBottomEdge`，两字段和 bounds/workArea/DPI/UI基准受同一偶数 serial 保护。Display callback只发布目标/唤醒，不查询 Shell。发布增加同值去重，并补齐这条 seqlock 原有 release/acquire fence。
- `ResolveBarBottomDockLine` 增加可选桌面线；PPT/白板 scene inset 仍优先。渲染稳定线、输入 tracker/重基准/blocked release以及 CenteredBottomBar startup/Preview使用同一值；角落浮动启动位置照旧。
- 同屏同DPI/UI基准的 Display 改变只更新环境，不重新钳制 X，不把浮动栏吸到底部。稳定桌面底栏按0.4秒和原动画开关/速度，仅重定向 `displayCenterY`。同目标不重启，反向从最后成功呈现 Y 开始；拖动/恢复/场景动画期间不启动新的任务栏Y写者，稳定后收敛最新目标。
- 最终 ULW candidate viewport 在 source/size tuple 形成前排除最底1物理行，包含尚未吸收的 translation。source/size变化沿既有全窗替换清旧区；稳定保留 partial dirty。提交锁内重查实际 translation，若候选越界，配对 EndDraw并保留visual retry，不发布成功快照。
- SetWindowPos直移先检查目标窗口是否越过保护行；越界保留最新输入 translation并请求ULW完整裁剪呈现，继续允许安全直移。
- Mini 启动通过一次 `QueryTaskbarMonitorInfo` 同时派生桌面线和UI基准，不再独立重复采样；失败只回退真实DPI和旧几何。
- 自查补充：`recoveryActive` 原来只表达浮动恢复，因此新动画门禁还检查竖向/水平phase是否Stable；PPT/白板消费入口显式清任务栏动画/待定向状态。Absorbing沿既有帧入口同步吸收并回到Idle，release尚有待吸收translation时在Apply之前Retry。
- 空viewport在BeginDraw之前保留完整damage/visual demand并返回Idle等待新输入/显示唤醒；不会把零尺寸送给ULW或热重试。最终translation竞态的正常defer仍走Retry且不进入target/device失败分类。

## 所有权 / 范围

此片只改Bar、StartupPlacement、StartupPreview、IdtMain Mini与对应既有headless用例。Display的任务栏状态/observer由另一代理负责，画布Window policy由另一子任务负责。未改Draw3、PPT业务进退、配置项、Setting缩放或项目注册。

## 验证

- `git diff --check --ignore-submodules=all` 通过。
- 新增无窗口回归：真实 `BarUiValueClass` + 生产retarget helper的同目标续播/快速反向/关闭动画/X不变，所有权门禁矩阵；新线下输入捕获/真实DPI场景优先/可见stroke；负原点+translation的物理1px裁剪及安全直移边界；Preview/formal同线且UI倍率与真实DPI独立。
- 本代理未运行全量构建；由主代理整合Display/Canvas后按完整solution Debug|ARM64及headless --no-window执行。没有GUI、commit或push。

## 仍需真实环境验证

Explorer显隐通知/动画矩形时序、多屏和Win7真实运行、实际ULW阴影/高倍率动画视觉以及任务栏触发由人工GUI验收证明；本次无窗口用例只证明生产策略与动画/几何合同。
