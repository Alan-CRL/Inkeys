# 修复白板进入不可见与后台持续闪烁回归

## Goal

相对 20261021a 查明并修复首次打开白板只有任务栏图标、主栏交互反复重置，以及白板进入后其他应用取得焦点时持续闪烁的回归。

## Background

- 历史调查和此前获准现场数据已能对应到 77f76038 引入的完整窗口组排序硬门及过宽事务恢复。
- 现场初次进入反复 Inactive/Entering；进入后 mode/背景/工作区稳定，但恢复屏障持续、画布显隐切换。九个受管窗口连续且 owner/band 正确，隐藏 Middle 高于 Bar；不能仅归因于外部窗口穿插或焦点失败。
- 当前工作树已有异步激活、epoch guard、Shell 全屏邮箱、Setting Present 解锁和可见性读回等改动，均须保留。
- 用户最新要求禁止后续 GUI 测试及运行时现场采样，仅允许离屏/无窗口验证；确认因果后实施，不能确认则停止。

## Requirements

- 白板后台排序无需获得前台权限，不把隐藏 PPT 与可见 Bar 全组反复抬到全局顶部；保留 owner、代际、非置顶和内部先后关系的真实核验。
- 纯层级命令失败的恢复只操作其层级政策和物理写集，不重建画布策略、激活样式、几何或显隐。
- 同一进入意图的窗口失败重试不反复关闭 More 等面板；新意图仍执行一次必要清理，可取消，不伪造 Active/背景成功。
- 保留完整画布/模式事务恢复、后到安全 Hide/穿透、stale/generation/stop 保护及单次异步激活。
- Desktop/PPT 正常置顶语义不变，不用强制白板置顶、循环激活或超时丢弃恢复责任。
- 最小 diff，保留编码/换行、全部既有改动和用户 shader；本轮不扩展背景 token、公有窗口 API 或最小化退出等独立议题。

## Acceptance Criteria

- [x] 同一生产排序 helper 的无窗口回归覆盖现场隐藏 Middle 在 Bar 上方、后台禁止 Bar 全局 TOP、PPT 保序/提升、部分成员及外部穿插；真实 owner/band/代际/API/读回失败不能假成功。
- [x] 生产 Z-only 恢复路径不调用 Hide/Show/FRAMECHANGED/CanvasPolicy 或无关几何/样式写入；失败保留责任，健康后退休，不受原命令已过期而永久阻止。
- [x] Promote 失败可恢复原 PPT 兄弟序，保持 Bar>PPT；新窗口代际、后到安全覆盖和 stop 不被旧恢复撤销。
- [x] 同 epoch 多次失败只准备 UI 一次，新的进入 epoch 会重新准备；有界退避不阻挡取消或新意图。
- [x] InkeysRepo.sln Debug|ARM64、Inkeys.exe --state-mode-production-test、InkeysHeadlessTests.exe --no-window 通过，既有 Setting/Shell/激活回归继续通过。
- [x] 静态审阅及编码/diff 检查通过，CSO 内容保持，明确没有执行修复后的 GUI 或各 Windows 版本实机验证。

## Notes

技术方案见 design.md；执行计划见 implement.md；历史归因与此前授权现场证据见 research/。禁止以本轮离屏通过冒充 GUI 实测。
