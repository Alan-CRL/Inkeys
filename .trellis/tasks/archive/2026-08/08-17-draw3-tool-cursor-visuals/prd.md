# 修复 Draw3 工具光标与透明度

## Goal

修复 Draw3 普通画笔、荧光笔和橡皮的应用内光标外观，使光标在可见的 Hover/Contact 阶段准确反映工具的实际粗细、颜色与透明度；同时把 UI3 主栏颜色选择器显示的透明度改为 Draw3 实际合成透明度，并确认激光笔光标与激光笔迹使用同一粗细来源。

## Background

- `stateMode` 保存当前产品工具、普通笔/荧光笔的颜色与宽度以及独立的 `laserActive` 状态；`IdtState.cpp` 把当前工具快照转换为 `Bridge::ProductState`。
- Draw3 Host 在绘制线程消费 bridge revision，把工具、橡皮模式和 `colorRgba/widthDip` 发布给 `WindowController`；活动笔画在 Down 时锁存自己的 `ProductVisualStyle`。
- 当前普通笔/荧光笔的 `DrawingCursorAppearance` 只在 `DrawingController` 构造时创建，后续产品样式变化没有重建光标外观，导致光标与实际笔画脱节。
- 当前 resolver 会在默认非半透明模式下把 Pen 样本的外观整体改为不透明，这会覆盖荧光笔的实际 alpha，也会把倒转笔橡皮的 Hover alpha 从 `0.5` 改成 `1.0`。
- UI3 主栏仍按旧 Draw2 的 `130/255` 显示荧光笔透明度；Draw3 实际使用 `0.35`。
- 激光笔迹基准直径与激光光标直径当前都来自 `kLaserSolidDiameterAt96Dpi * dpiScale`，没有独立的产品宽度状态。

## Requirements

- R1：普通画笔光标必须随最新产品颜色和宽度更新，不能停留在 Draw3 启动时样式；正在绘制的笔画仍保持 Down 时锁存的颜色和宽度。
- R2：普通画笔光标直径必须为“实际笔画基准直径”和原有 `5 DIP` 最小光标直径中的较大者；仅最小光标直径乘当前 DPI scale，不能把实际笔画宽度重复缩放。
- R3：普通画笔光标中心 RGB 必须与当前实际笔画 RGB 一致；不得改变既有轮廓颜色、系统鼠标光标策略或 Pen Contact 显隐策略。
- R4：荧光笔必须在既有允许显示应用光标的 Hover 场景中显示矩形笔尖光标，其长边等于实际荧光笔基准直径，短边保持 `8:1` 笔尖比例，RGB 与实际笔画一致。
- R5：荧光笔光标最终 alpha 必须与 Draw3 笔画的实际 `0.35` alpha 一致，且不能被普通 Pen 的默认不透明逻辑覆盖。
- R6：UI3 主栏颜色选择器的透明度数字必须显示当前工具的实际最终透明度：普通笔/Shape 为 `100%`，荧光笔为 `35%`；此任务不新增透明度编辑交互。
- R7：固定橡皮、速度橡皮和倒转笔橡皮在 Hover 时必须保持 `0.5` 整体 alpha，在 Contact 时保持 `1.0`；速度橡皮的动态直径逻辑不得退化。
- R8：激光笔光标和实际激光笔迹必须继续使用相同的 `5 DIP * dpiScale` 实体直径；本任务只验证并补充防回归覆盖，不新增激光粗细设置或 state 字段。
- R9：保留 pointer authority、Touch pan 抑制、系统光标切换、dirty rect、L0/L1/L2 和透明 presenter 既有合同。
- R10：保持目标文件原编码和 CRLF；只做本任务所需修改，不创建 commit、不 push、不启动可见窗口。

## Acceptance Criteria

- [x] AC1：切换普通笔宽度或颜色后，下一次光标帧使用新样式；直径为 `max(actualDiameter, 5 DIP * dpiScale)`，中心 RGB 与新笔画一致。
- [x] AC2：当实际普通笔宽度小于 `5 DIP` 最小值时光标仍可辨识；大于最小值时光标准确增长到实际笔画粗细。
- [x] AC3：荧光笔 Hover 光标可见，矩形尺寸匹配实际 `8:1` 笔尖，RGB 与笔画一致，最终 alpha 为 `0.35`。
- [x] AC4：主栏普通笔/Shape 显示 `100%`，荧光笔显示 `35%`，不再显示旧 Draw2 的约 `51%`。
- [x] AC5：固定/速度/倒转笔橡皮 Hover 光标最终 alpha 为 `0.5`，Contact 为 `1.0`；速度橡皮动态直径仍随 OC controller 更新。
- [x] AC6：静态检查或无窗口测试证明激光笔迹和激光光标仍共享 `kLaserSolidDiameterAt96Dpi * dpiScale`，当前 state 不增加激光宽度字段。
- [x] AC7：产品集成的无窗口测试覆盖荧光笔 alpha、橡皮 Hover/Contact alpha 和既有光标显隐语义；相关测试通过。
- [x] AC8：ARM64 `Debug|ARM64` 使用 ARM64 MSBuild 构建完整 `InkeysRepo.sln` 通过，`InkeysHeadlessTests.exe --no-window` 与 `Inkeys.exe --draw3-hidden-test` 通过且不显示窗口。

## Out Of Scope

- 新增或开放透明度滑块、保存自定义透明度、迁移历史配置的 alpha 语义。
- 改变普通笔/荧光笔压力宽度算法、荧光笔几何、激光材质或粒子效果。
- 改变鼠标默认使用系统箭头、Pen Contact 默认隐藏光标、Touch pan/authority 或 inverted pen 工具选择规则。
- 新增激光笔宽度 UI、配置项或独立持久化 state。
