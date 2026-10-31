# Touch 场景曲线研究记录（2026-09-23）

## 当前代码与历史边界

- 本地 `bugfix/eraser` HEAD 为 `94e07b2599adab9de4286aa5fb7526e2c1c681f6`，初查工作区干净。原任务 `in_progress`，历史 `feature/eraser` 与 `base_branch=main` 是旧元数据；本轮只恢复会话上下文，不切 Git 版本。
- `Draw3.SpeedEraser.cpp::ResolveConfig` 对 DirectTouch 的 `physical` 分支固定输出 30/90/60/250 mm/s，未使用 Laptop/LargeScreen。LargeScreen 只参与无可靠物理标尺时的经验回退。`FollowTarget` 的清扫资格/证据读取 `Config` 的 enter/exit/large，`ReferenceTargetDiameterDip` 读取同一组 enter/large，因此应在解析层集中修改。
- 当前 `ResolveSizes` 使用 24/32/40 的 0.5B/B/5B；旧文档 16/32/64 和固定 50 DIP 属于历史。五入口与总开关合同以当前解析器为准。
- `DrawingController` 帧级诊断的 `cursorDiameterPx` 取 `currentCursorVisuals.front()`，该项可能是鼠标或另一 Touch contact，与当前 `r` 不一致。

## 实机证据范围

用户提供的教室日志是间隔诊断快照：Screen Touch / DirectTouch / TrustedPhysical，映射可靠，1920×1080、144DPI、约 0.723958×0.722222 mm/px（元数据约 139×78cm），面积 requested/latched/active 均为 0。第一段稀疏速度含 270.498、321.777、282.742、432.376 mm/s，旧目标全为 160 DIP；实际直径约 33.806→124.896→156.640→160 DIP。用户认为这一类动作属于普通擦除，预期接近所选基础大小。

“约 10cm/s”是用户对动作的估计；上述 mm/s 是控制器报告值；“普通擦除”是体验分类。三者不可互作同一实测量。后半段高速读数无逐段标签，不可全部归为普通；快照不能逐点重放。上一轮噪声合成结果仅显示噪声可能增大累计路程，未证明本机根因。此次优先修正已确定的曲线场景缺口，噪声仅做健壮性回归。

## 经验节点与待实测项

Surface 约 28×19cm 与教室约 139×78cm 是两端参考。320/1200mm 长边节点、Laptop 上限 0.25、LargeScreen 下限 0.25 只是首版平滑经验先验，不是人体工学定律或字迹尺度测量。合成场景可检查连续性、单位和覆盖，不能证明所有设备最佳手感。真人 Surface 和教室设备的普通/清扫轨迹仍需验收。
