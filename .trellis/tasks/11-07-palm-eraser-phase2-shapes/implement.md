# 实施计划：擦除形状、微光光标与样式设置

按以下顺序，每步完成后自检 diff 与编码/EOL：

1. **存储字段确认（门禁）**：在 UInk 编解码与 Draw3 导入代码中找出“旧版忽略、仍为橡皮、按圆重放”的私有字段位置，写用例证明；不满足则回到规划。
2. 形状枚举与尺寸换算（纯函数 + Headless 数值测试：等面积、2:3、圆角上限）。
3. 几何：`EraseRectSweep` 段生成、bounds、dirty、历史瓦片；单点退化。
4. 着色器：VS 包围盒、PS 凸包距离与 AA、Erase 输出；新增光标圆角矩形与微光纹样；`EraserGripVisual.h` 参数替换。重新编译 CSO，按生成器同步 `Draw3.Win7GuardedShader.h`。
5. 存储接线：UInk 私有字段读写、白板历史 JSON 可选字段、自动保存；读写往返与旧文件缺省用例。
6. 光标接线：主光标与逐 Touch 光标携带形状；手掌/笔速仅改尺寸。
7. 配置 `Drawing.Eraser.Shape` + 偏好传递 + Down 锁存。
8. 设置 UI：`EraserStylePicker` 复合控件；放入绘制-橡皮分组顶部；i18n sync/补译/check。
9. 主栏预览/命中/选中环/焦点适配。
10. 验证：ARM64 原生 MSBuild 完整构建 `InkeysRepo.sln` Debug|ARM64（≥5 分钟，处理 PATH 冲突）；`InkeysHeadlessTests.exe --no-window --setting-eraser-only`、`--palm-eraser-only`、`--no-window` 全量；`Inkeys.exe --draw3-palm-eraser-test`；i18n check；着色器字节码一致性检查。GUI/实机/Win7 未测列出。
11. 更新 spec：`input-and-ink.md`（擦除形状与几何）、`palm-eraser.md`（光标）、UInk 章节（私有字段与 1.1 计划）、设置控件规范。

回到规划的条件：第 1 步找不到安全字段位置；Win7 字节码生成器不支持新增类型；凸包 PS 在 SM5.0/WARP 下出现无法规避的精度问题。
