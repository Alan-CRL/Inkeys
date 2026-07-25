# 发光激光笔

## Goal

为演示、讲解和临时标注提供按键 4 可选的多指激光笔。轨迹以白色高光芯、白红散射过渡、红色边框和法线方向红晕显示，保持实时、清晰且不过度遮挡底层内容；轨迹只属于瞬时视觉，不进入 L2 或持久墨迹。

## Background

- 当前工具为 Pen/Highlighter/Eraser，数字键 1/2/3 已占用，数字键 4 可用于 Laser。
- RTS 已支持多 contact 独立 CPU runtime 和共享帧合成；应用内 cursor 已有 backbuffer 顶层瞬态绘制与 dirty rect 清除契约。
- Goodnotes Trail 的临时轨迹可作为交互参考，本任务采用用户指定的白芯红边视觉、可调留存时间和更克制的动态粒子。

## Requirements

- 新增 `DrawingTool::Laser`，数字键及小键盘 4 选择，支持 Pen、Mouse 和多 Touch；同一活动批次继续锁定同一工具。
- Laser 使用 stroke modeler 平滑和 prediction，但不参与断触重连；Up 使用模型 `kUp` 的确认结果收尾并清除 prediction。
- Laser 稳定轨迹和 live/prediction 使用独立瞬态 coverage 资源，永不写入 L2；工具切换后未到期轨迹仍保留在普通墨迹上方。
- 最后一根 Laser 抬起后按可配置秒数保持完整显示，默认 `3.0s`，随后在固定 `0.8s` 内平滑淡出。完全消失前新的 Laser Down 令整组恢复满 opacity 并重新计时。
- 对外提供线程安全的留存秒数 getter/setter；运行中修改按最后一次全部抬笔时间立即重算当前组，缩短可立即进入 fade，延长则继续保持。
- 96 DPI 下白芯直径约 5px、红色实体外径约 10px、完整红晕外径约 24px、白红散射带约 0.75px，全部随 DPI 缩放并覆盖完整 dirty bounds。
- Pen/Mouse Hover 显示静态白芯红晕光点；每个活动 Touch contact 显示独立接触笔尖。
- 粒子默认开启并可由外部线程安全开关；关闭后下一帧清理旧粒子，不影响主轨迹、笔尖或留存计时。
- Down 在笔尖生成约 3-5 个小粒子并轻微散开；书写中按 36-48px 真实弧长稀疏生成，每 contact 同时不超过 10 个，流速只随平滑笔速变化。
- Up 停止生成粒子，现存粒子约 140ms 内就近回到所属轨迹边缘/中心线，同时缩小并淡出；粒子不得形成连续带或抢占主轨迹视觉。
- Hover 静止和留存等待期不持续 Present；只有输入、prediction、粒子动画和 fade 驱动帧。
- Laser 不触发倒转笔尾橡皮覆盖和触觉反馈。

## Out Of Scope

- 不新增设置 UI、持久化格式、L2 烘干、保存/回放或断触修正。
- 不实现全屏多级 Bloom；发光使用解析式 SDF/coverage 材质完成。

## Acceptance Criteria

- [ ] 按键 4 可选择 Laser，Pen/Mouse/多 Touch 能同时获得平滑白芯红边轨迹和正确 Hover/接触笔尖。
- [ ] 稳定 coverage 只增量写入，live coverage 只重绘活动尾部和 prediction；Laser 不改变 L2 像素。
- [ ] 最后 Up 后默认完整显示 3.0s，再在 0.8s 内淡出；fade 完成前新 Down 恢复全部旧轨迹并重置计时。
- [ ] 外部可读取和修改留存秒数；运行中修改立即按最后 Up 时间重算，非法输入被拒绝且不改变旧值。
- [ ] 粒子默认开启、数量受限、空间身份稳定；Down 散开、Up 就近收束消失，外部关闭能清除旧粒子且不改变主轨迹。
- [ ] resize、clear、工具切换、窗口重现和 Present 失败恢复不留下激光残影或破坏普通 L0/L1/L2。
- [ ] 白色、深色和混合背景下主轨迹清楚，premultiplied alpha 在 DComp 与 QCOM ARM64 ULW 路径无黑边或异常叠色。
- [ ] Debug/Release ARM64 全解决方案构建和自动测试通过；活动性能保持现有 120 FPS 阈值，静态留存期间 frame/Present 零增长。
