# 双画布底部1px与白板全屏策略

## 状态与来源
父任务：10-19-ui3-taskbar-dock-adaptation。用户2026-10-19明确要求：绘制主画布及选择预览两个画布，任何桌面/PPT均留底1px且非全屏；白板取消间隙并保持全屏。用户随后已明确批准实现，本子任务已start为in_progress。

## 交付
- Drawpad及DrawpadPresentation在Desktop/PPT始终从实际可见/输入区域排除目标屏幕最底1物理像素，不受DPI/UI倍率、任务栏方向或autoHide影响。
- 两窗首次显示前明确NonRudeHWND，禁止Shell自动将Inkeys画布识别为全屏；既有overlay显式标记为false。PPT应用本身的全屏不变。
- 白板进入统一恢复双窗完整覆盖，移除两窗非全屏属性，保留Freeze既有全屏/激活锚点；退出/失败回滚恢复双窗1px及非全屏。
- 统一初始创建、surface显隐、bounds变化、窗口重建、白板Entering取消/Exiting重入；失败可回滚重试，不留下两个画布混合策略。

## 实现边界
使用region+NonRude组合，保留完整HWND/client尺寸、原点、Host/RTS/Presenter/doc映射与墨迹数据。仅裁实际覆盖形状，不裁写笔迹、不重启Host、不为任务栏取消capture。已有capture的区域外消息遵守原输入所有权。
Window Service owner线程应用；不改Display采样、Bar动画/PPT贴底、设置窗口或缩放配置。保留既有COM全屏标记best-effort边界，不以期望标记值冒充Shell实测成功。

## 验收
- [ ] Desktop/PPT × 绘制/选择预览/穿透均有底1物理行空隙，双窗NonRude在显示前生效；Whiteboard双窗完整覆盖、属性移除，既有全屏意图保留。
- [ ] 负屏坐标、bounds改变、重建/场景切换与失败回滚一致；相同策略不反复SetWindowRgn/属性/隐藏显示。
- [ ] client/Host/RTS/doc尺寸与像素标尺不变；仅策略变化不增加Host resizeCount，持久化墨迹与撤销不受影响。
- [ ] 双窗事务/HRGN资源归属/代际测试及相关无窗口回归通过；完整solution验证由父任务集成一次完成。
- [ ] 未授权GUI时不操作Shell、不启动应用，真实DComp/ULW和Shell行为注明未实测。

与主栏子任务无功能依赖。建议先实施本项；不commit/push，实施已获用户批准。

## 实施授权更新
2026-10-19：用户明确回复“批准，并开始实现”。以上规划门禁的批准条件已满足；按既有计划进入实施，GUI与commit/push仍未授权。

## 自动验证交付
2026-10-19：实现已完成，最终完整InkeysRepo.sln Debug|ARM64增量构建exit0（0errors/0warnings），最终新Headless --no-window exit0（104.12秒）。未执行真实Shell/窗口GUI，PRD中与实机现象相关的验收继续以用户确认或后续明确授权GUI为准；任务暂不归档。详细证据见父任务research/validation.md。
