# 荧光笔固定竖直矩形画刷

## Goal

将荧光笔改为固定在画布 Y 轴方向的 `6.25×50px` 矩形画刷，删除切线旋转、12px 可见闸门、方向锁定和 round join，消除起笔端帽错误及低速方向跳变；Up 继续复用已经可见的缓存几何，不整笔重建。

## Requirements

### R1. 固定画刷

- 长边固定为 50px，短边固定为 6.25px，精确比例 8:1。
- 指针位于矩形中心，矩形始终与画布 Y 轴平行，不跟随路径、笔倾角或设备方向旋转。
- 单点直接生成一个居中的矩形；不足、等于和超过 12px 的路径使用同一规则。

### R2. 连续 sweep

- 每对相邻点生成固定矩形从 `p1` 平移到 `p2` 的凸扫掠区域。
- 相邻 sweep 通过同笔 coverage union 连接，不生成独立 cap、sector、circle 或 short mark。
- 0.25px 内的连续采样视为亚像素抖动；累计超过阈值后再生成新段。

### R3. 分层和完成态

- Down 起即可在 L0 显示单点矩形，并立即允许 prediction。
- L1 只按提交游标增量缓存稳定 sweep；L0 从 `committedIndex` 继续，无额外方向上下文。
- Up 只合并已提交缓存与最后一帧 L0；仅同帧 Down→Up 且从未生成 L0 时允许补一次点击矩形。

### R4. 契约和指标

- `HighlighterPrimitive` 的 CPU/HLSL 布局为 `p1 + p2 + halfSize`，共 24 bytes。
- 高亮 landing 统一记录 Down→Present，不再使用 `HighlighterVisibleEligibility`。
- 普通笔、橡皮、颜色、透明度和 prediction 模型保持不变。

## Acceptance Criteria

- [x] 单点生成居中的 `6.25×50px` 竖直矩形。
- [x] 水平、竖直、斜线、曲线、锐角和回折只生成固定矩形 sweep。
- [x] `<12px`、`=12px` 和长笔画没有可见性或几何分支。
- [x] 低速亚像素点不会旋转画刷，累计有效移动后路径连续。
- [x] 缓存完成态不读取变化后的 `realPoints`，Up 不整笔重建。
- [x] ARM64 Debug 全解决方案和自动化测试通过。
- [ ] ARM64 Release 全解决方案和自动化测试通过。
- [ ] 用户完成人工单击、极慢移动、快速移动、急转弯、prediction、抬笔和 resize 验证。

## Out of Scope

- 不修改普通笔或橡皮几何。
- 不修改高亮颜色、透明混合、prediction 算法或 presenter。
- 本轮不提交、不归档任务。
