# 主栏特殊任务栏贴底与显隐动画

## 状态与来源
父任务：10-19-ui3-taskbar-dock-adaptation。本子任务已获用户明确批准并start为in_progress。

## 交付
- 桌面普通/居中底栏对顶部、左侧、右侧任务栏距屏幕底部5真实屏幕DIP；底部常驻任务栏维持贴任务栏top。
- 该屏底部自动隐藏：Hidden时主体边框外沿距屏底5真实屏幕DIP，Raised时贴稳定任务栏top，支持类似现有PPT的Y动画、快速反向及同目标去重。
- 保护桌面底部autoHide对应屏幕的最底1物理像素，Bar主体/描边/光影/回弹及直移均不能覆盖；间隙按主体边框而非HWND边缘计算。
- 保持PPT进入/退出、专用5DIP和白板优先级、用户X/居中态、浮动未吸附态及上一任务缩放行为。
- 显隐观察低开销、同值不发布，无UI帧/鼠标Move查询Shell，支持多屏、缺任务栏/失效查询/Explorer重建与旧Shell降级。

画布Desktop/PPT全局1px及非全屏由另一个子任务负责；本子任务不改Drawpad/DrawpadPresentation策略、RTS、笔迹或全屏标记。

## 验收
- [ ] 四edge × 显隐 × 普通/居中/浮动 × DPI/UI倍率/负屏坐标；边框Y准确，X/状态不被重置。
- [ ] 动画同目标不重播，反向连续，拖动/恢复单写者，PPT/白板优先；taskbar完整厚度不随显隐变化。
- [ ] Bar最终viewport及SetWindowPos直移均不能占保护行；模式/屏幕变化清除旧像素。
- [ ] 事件burst/epoch/缓存窗口/快照去重和资源清理回归；无底部autoHide时停快速采样，无更高系统版本硬依赖。
- [ ] 相关Bar/Display/PPT/启动预览无窗口测试通过，父任务统一完成完整Debug|ARM64 solution验证；未授权GUI不操作实际任务栏。

与画布子任务无功能依赖，建议后实施；本任务不commit/push。

## 实施授权更新
2026-10-19：用户明确回复“批准，并开始实现”。以上规划门禁的批准条件已满足；按既有计划进入实施，GUI与commit/push仍未授权。

## 自动验证交付
2026-10-19：实现已完成，最终完整InkeysRepo.sln Debug|ARM64增量构建exit0（0errors/0warnings），最终新Headless --no-window exit0（104.12秒）。未执行真实Shell/窗口GUI，PRD中与实机现象相关的验收继续以用户确认或后续明确授权GUI为准；任务暂不归档。详细证据见父任务research/validation.md。

## 追加5DIP调整验证
2026-10-19：用户追加要求顶部/左右任务栏也留5真实DIP，已更新生产Display目标helper及固定多DPI/负坐标用例。新完整solution与Headless均exit0，见父任务research/followup-validation.md；保留PPT及其他任务栏规则。未GUI/commit/push。
