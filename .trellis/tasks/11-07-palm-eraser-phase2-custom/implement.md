# 实施计划：手掌橡皮擦除样式与大小自定义

前置：`11-07-palm-eraser-phase2-shapes` 已完成并合入当前分支；`11-07-palm-eraser-phase2-blocking` 已提供 `PalmExclusionContains`。

1. 复核 shapes 落地后的形状枚举、尺寸换算与 `EraserStylePicker` 接口。
2. 配置 6 项 + 校验 + 默认（含倍率有限性/范围）。
3. `PalmEraser::Settings` 扩展与偏好传递、Down 锁存（含普通橡皮形状与默认大小快照）。
4. `State::Observe` 定尺：形状 × 方法 × 倍率 → 等效 r；纯函数 + Headless 数值测试（prd 表全部 12 种组合、倍率边界、默认组合等于现行为）。
5. 三种粗细方式接入运行时（动态 / 固定跟随默认含笔速 / 固定不含笔速）；生产 probe 覆盖续接与工具转换。
6. 屏蔽范围矩形扩展（design §4）+ 用例。
7. 设置 UI 两张卡片 + 条件显示 + i18n sync/补译/check。
8. 验证：ARM64 原生 MSBuild 完整构建 `InkeysRepo.sln` Debug|ARM64（≥5 分钟，处理 PATH 冲突）；`--palm-eraser-only`、`--setting-eraser-only`、`--no-window` 全量、`--config-boundary-test`、`--draw3-palm-eraser-test`。实机未测列出。
9. 更新 spec：`palm-eraser.md`（样式、粗细方式、计算方式、倍率、屏蔽形状）。

回到规划的条件：笔速控制器无法在不改动 SpeedEraser 合同的前提下用于手掌；设置控件无法复用导致需要新的布局系统。
