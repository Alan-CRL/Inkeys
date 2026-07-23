# UI3 第三光源休眠状态机实施清单

## Implementation

- [x] 在 `Bar.Main.cppm` 增加跟踪状态、可见区域缓存、Raw Input 动态启停和画布休眠通知接口。
- [x] 在 `barWindowMsgCallback` 接入 UI 进入、5 秒定时器和画布休眠消息。
- [x] 重写第三光源输入更新：区域状态转换、固定截止时间、`50 × zoom` 距离判定和无关位置更新裁剪。
- [x] 将第三光源 300ms 动画扩展为可逆淡入淡出，并保持动画关闭立即隐藏。
- [x] 从当前外层 UI 几何发布至多三个可见区域矩形。
- [x] 在画布鼠标按下入口发送一次休眠通知，不直接跨线程修改渲染状态。

## Validation

- [x] 静态检查 Raw Input 只有激活/宽限状态注册，Dormant 和动画关闭路径注销。
- [x] 静态检查区域外移动不重置 5 秒截止时间，定时器与输入事件均检查绝对截止。
- [x] 静态检查 50px 使用缩放倍率、距离平方和缓存外层矩形。
- [x] 静态检查淡出期间使用最后坐标、重新进入可从当前强度续接。
- [x] 检查 UTF-8/CRLF、`git diff --check` 和改动范围。
- [x] 使用 ARM64 Host MSBuild 构建完整 `InkeysRepo.sln` 的 `Debug | ARM64`。

> 2026-07-23 使用 ARM64 Host MSBuild 完整构建两次，均为 0 error。首次构建发现本次新增的 `GetMessageExtraInfo` 窄化告警，改用 `ULONG_PTR` 后增量构建通过且该告警消失；其余告警均来自既有代码或第三方代码。实际 CPU 降幅和分层窗口命中行为仍需在 UI3 运行态手工观察。

## Risk and Rollback

- 高风险点是全屏分层窗口的实际命中判定、Raw Input 动态注销以及窗口线程与渲染线程的状态同步。
- 每一步保持独立可回退：画布通知、动态注册、50px 裁剪和可逆淡出不与 PointLight/Gaussian 绘制重构混合。
