# Implement: 按设备光标模型与专页

前置:先读 `.trellis/spec/native-desktop/draw3-cursors.md`、`input-and-ink.md`(Touch 归属/过滤)、`settings-performance-and-drawing.md`、`rendering-and-ui.md`;`task.py start` 前需用户审阅 prd/design 并确认“开放问题”。

## 步骤

1. **模型**:改 `Draw3.CursorSettings.h`(枚举、`CursorSettings`、`Restore*`);`Draw3.Bridge.h` 同步工具投影;Headless 先写红灯测试(默认值、合法范围、旧值兼容)。
2. **配置**:`Other.Config` 新键校验/默认/迁移、`IdtState.cpp` 读入;配置往返、NoUpload、非法值、旧 `PenStyle` 回落测试。
3. **Controller 打包**:重排 `cursorSettings_` 位域与 `SetCursorSettings`;保证等值首次仍同步 Window 策略。
4. **解析**:`Draw3.PenCursor.cpp` 增样本→设备行映射、Dot/HighlightDot 外观、高亮模式判定;resolver 测试矩阵(4 设备×5 样式×3 模式×工具)。
5. **触摸光标**:接入逐接触(多指)Touch 落笔可视,补多指 Down/Up/Cancel、Touch suppress/owner、荧光笔/激光触摸不变的回归测试。
6. **设置页**:`DrawingSubpage::Cursor`、入口、专页、预览绘制、选项映射;`setting_design_tests` 覆盖导航往返、选项集(无光标仅触摸)、默认值。
7. **i18n**:新增键 → sync/check → 更新生成头。
8. **验证**:完整 `InkeysRepo.sln Debug|ARM64`、`InkeysHeadlessTests.exe --no-window`、`Inkeys.exe --draw3-pen-width-test`、i18n check、`git diff --check`;GUI/硬件验收(屏幕笔、数位板、触摸、鼠标)单列并请用户授权。
9. **Spec**:更新 `draw3-cursors.md`(配置表、签名、工具/来源/模式表、验证矩阵、Wrong/Correct)。

## 评审门

- 步骤 4、5 完成后跑一次 `trellis-check`;步骤 6 完成后核对预览与实际一致性。

## 回滚点

- 步骤 1–2 后:模型+配置可独立保留。
- 步骤 6 前:UI 仍为旧两下拉,可单独发布模型。

## 进展记录
- 2026-11-03:步骤 1–9 已实现并提交(模型/配置/Controller 打包/解析/触摸多指落笔光标/专页与预览/注入鼠标开关/R6 来源沿用/三语言 i18n/spec)。`trellis-check` 修了配置宏排版、小圆点预览高亮误显、触摸光标 DPI 取值三处。Debug|ARM64 完整构建、`InkeysHeadlessTests --no-window`、`--draw3-pen-width-test`、i18n check 均通过。待实机验收:专页预览与实际一致、触摸多指、屏幕笔进入闪高亮、M6 开关。
- 2026-11-04:实机评审后改为统一五种样式(鼠标/画笔/画笔(高亮)/小圆点/小圆点(高亮),触摸多无光标),取消高亮模式,新增自绘箭头(GPU type 22)并重新生成 Win7 guarded shader,说明改为蓝色两行信息条。用户实机验收通过,任务完成。
