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
- Laser 使用独立瞬态稳定颜色层与单笔 coverage scratch，永不写入 L2；工具切换后未到期轨迹仍保留在普通墨迹上方。
- 最后一根 Laser 抬起后按可配置秒数保持完整显示，默认 `3.0s`，随后在固定 `0.8s` 内平滑淡出。完全消失前新的 Laser Down 令整组恢复满 opacity 并重新计时。
- 对外提供线程安全的留存秒数 getter/setter；运行中修改按最后一次全部抬笔时间立即重算当前组，缩短可立即进入 fade，延长则继续保持。
- 96 DPI、基准压力下白芯与红色实体外套的总直径为 5px，白芯保持实体直径的三分之一；红色实体轮廓沿法线向外固定漫反射 5px，因此完整视觉直径为 15px。漫反射在实体边界处 alpha 为 1，向外平滑单调衰减，并在 5px 外缘达到 alpha 0；实体尺寸随 DPI 和 Pen 压力缩放，5px 漫反射只随 DPI 缩放并覆盖完整 dirty bounds。
- Pen 压力按 `0.65 + 0.75 * clamp(p, 0, 1)` 缩放实体总直径，Mouse/Touch 与 Hover 保持 5px 基准实体直径；无效压力沿用上一有效宽度，prediction 沿用最后真实宽度。
- Pen/Mouse Hover 和每个活动 Touch contact 使用与真实固定宽度轨迹相同的白芯、红框与漫反射尺寸。
- 多支 Laser 按 Down 顺序分层；后 Down 的整支轨迹位于上层，其红框可以覆盖下层轨迹的白芯。同一支轨迹内部仍使用 coverage 并集，避免自交和相邻段重复加深。
- 粒子默认关闭并可由外部线程安全开关；关闭后下一帧清理旧粒子，不影响主轨迹、笔尖或留存计时。
- Down 在笔尖生成 12-18 个小粒子，约 180ms 内向周围散开 4-10px；书写中沿真实曲线每 8-12px 生成一枚，每 contact 同时不超过 48 个，粒子流速为 `clamp(0.025 * smoothedInputSpeed, 8*dpiScale, 36*dpiScale)` px/s。
- 粒子保存所属真实路径 segment/弧长，沿红边外侧跟随曲线转弯，到达当前路径末端停止并淡出，不向前直线外推；prediction 不生成粒子，追加真实点不得改变既有粒子身份。
- Up 停止生成，现存粒子单次查找最近真实路径点后，75% 收束至红边、25% 收束至中心线，在约 220ms 内缩小并淡出。
- Hover 静止和留存等待期不持续 Present；只有输入、prediction、粒子动画和 fade 驱动帧。
- Laser 不触发倒转笔尾橡皮覆盖和触觉反馈。

## Out Of Scope

- 不新增设置 UI、持久化格式、L2 烘干、保存/回放或断触修正。
- 不实现全屏多级 Bloom；发光使用解析式 SDF/coverage 材质完成。

## Acceptance Criteria

- [ ] 按键 4 可选择 Laser，Pen/Mouse/多 Touch 能同时获得平滑白芯红边轨迹和正确 Hover/接触笔尖。
- [ ] 每支未烘干 Laser 独立生成 coverage，并按 Down 顺序有序叠加；完成批次烘入稳定瞬态颜色层且不改变 L2 像素。
- [ ] 最后 Up 后默认完整显示 3.0s，再在 0.8s 内淡出；fade 完成前新 Down 恢复全部旧轨迹并重置计时。
- [ ] 外部可读取和修改留存秒数；运行中修改立即按最后 Up 时间重算，非法输入被拒绝且不改变旧值。
- [ ] 96 DPI 下实体总直径为 5px、白芯约 1.67px、两侧漫反射各 5px；漫反射从实体边界 alpha 1 平滑衰减到外缘 alpha 0，Hover/Touch tip 与固定宽度轨迹一致，Pen 压力只缩放实体。
- [ ] 后 Down 的 Laser 整笔稳定覆盖先 Down 的 Laser；上层红框可进入下层白芯，同笔自交无接缝或重复加深。
- [ ] 粒子默认关闭、数量受限、空间身份稳定；外部开启后 Down 散开、Up 就近收束消失，关闭能清除旧粒子且不改变主轨迹。
- [ ] resize、clear、工具切换、窗口重现和 Present 失败恢复不留下激光残影或破坏普通 L0/L1/L2。
- [ ] 白色、深色和混合背景下主轨迹清楚，premultiplied alpha 在 DComp 与 QCOM ARM64 ULW 路径无黑边或异常叠色。
- [ ] Pen 轻/中/重压力能连续改变整组激光材质；Pen 离开后不残留 Laser 笔尖，只有新的真实 Mouse 或 Pen 样本恢复对应光标。
- [ ] Debug/Release ARM64 全解决方案构建和自动测试通过；活动性能保持现有 120 FPS 阈值，静态留存期间 frame/Present 零增长。
