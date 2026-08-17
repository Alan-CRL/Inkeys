# 当前实现调查

## Evidence

- `Inkeys/IdtState.h`：`StateModeClass` 分离 `StateModeSelect`、`Pen.ModeSelect` 和 `laserActive`；普通笔/荧光笔各自保存 width/color。
- `Inkeys/IdtState.cpp:28`：`ColorRefToRgba` 只发布 RGB 和不透明 alpha；`CurrentDraw3Tool` 负责旧产品状态到 `Bridge::Tool` 的映射；`PublishDraw3State` 发布当前工具通用的 width/color。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:301`：Host 只在 bridge revision 变化时消费快照，并在设置工具/橡皮模式后调用 `SetProductVisualStyle`。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.WindowControl.cpp:385`：产品样式写入 atomics，并请求 cursor render/control wake。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:1129`：普通笔/荧光笔 appearance 只在构造函数配置一次。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:4248`：绘制循环每帧读取最新 `ProductVisualStyle`，但当前没有同步刷新 appearance。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2430`：活动笔画在 Down 时锁存自己的产品样式；笔迹颜色/宽度没有同类陈旧问题。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PenCursor.cpp:238`：default opaque 分支覆盖了所有 Pen 样本，包括 Highlighter 和 inverted Eraser Hover。
- `Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl:250`：cursor 最终 fill alpha 为 `appearance.opacity * appearance.fillAlpha`。
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:1199`：透明度数字仍使用旧 Draw2 `130/255`；Draw3 的 `ColorForTool(Highlighter)` 实际限制为 `0.35`。
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.Renderer.cppm:25` 与 `Draw3.DrawingController.cpp:1172,2456`：Laser cursor 和 stroke 都使用 `kLaserSolidDiameterAt96Dpi * dpiScale`。

## Conclusions

- 普通笔/荧光笔的首要根因是 appearance 生命周期错误，而不是 bridge 没有发布 state。
- 荧光笔和橡皮还有第二个独立根因：resolver 把部分透明外观改成全不透明。
- Bar 显示问题是迁移遗留常量，不需要新增 opacity 编辑或配置迁移。
- Laser 当前实现已满足粗细一致性，只需记录和防回归验证。

## Existing Test Surface

- 主 Solution 中的 `InkeysHeadlessTests/draw3_contact_tests.cpp` 已 import 产品集成的 Draw3 module，可扩展 resolver 测试。
- `inkStrokeModelerTestTests/pen_cursor_tests.cpp` 覆盖源 Draw3 副本但不属于 `InkeysRepo.sln`；本任务的主回归应落在产品集成测试，避免只验证源副本。
- 隐藏 Host 测试可覆盖 bridge/绘制线程 smoke path，但不能替代透明度与 resolver 的纯逻辑断言。
