# 荧光笔固定竖直矩形画刷实施计划

## 1. CPU/GPU 几何

- [x] 将 `HighlighterPrimitive` 收敛为 24-byte `p1/p2/halfSize` 布局。
- [x] 用固定矩形 sweep 替换 tangent body、round join 和 short mark shader 分支。
- [x] 同步 CPU bounds、structured buffer stride 和中文契约注释。

## 2. 运行流程

- [x] 删除 12px 门槛、方向锁定、boundary flags 和首尾路径折叠。
- [x] Down 起显示 L0 点击矩形并立即启用 prediction。
- [x] L1 从 `committedIndex` 增量提交，Up 只重放缓存与最后一帧 L0。
- [x] 高亮 landing 改为 Down→Present。

## 3. 自动化测试

- [x] 覆盖单点 `6.25×50px`、`<12px`、`=12px` 和长路径。
- [x] 覆盖水平、竖直、斜线、曲线、锐角、回折和切片连接。
- [x] 覆盖 0.25px 去抖、缓存合并和 `realPoints` 改写不影响完成态。

## 4. 质量门

- [x] ARM64 Debug 全解决方案构建，vertex/pixel shader 编译成功。
- [x] ARM64 Debug 自动化测试通过。
- [x] ARM64 Release 全解决方案构建和自动化测试通过。
- [x] `git diff --check`、旧符号扫描和 UTF-8 BOM + CRLF 检查通过。
- [x] 用户完成人工视觉验证；验证通过后提交并归档任务。
