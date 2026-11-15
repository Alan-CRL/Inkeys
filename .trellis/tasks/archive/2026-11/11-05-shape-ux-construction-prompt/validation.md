# construction-prompt 本轮交接

- 2026-11-09：按用户授权仅读取与补丁；未运行git、构建、产品程序、Python或PowerShell。最初的默认shell读取请求被CreateProcess权限拒绝，未启动进程；之后全部使用文件API。
- 以本轮开始时文件字节为比较基准，检查修改范围、UTF-8/BOM与CRLF；不读取或更改git index。Bar.ShapeEditor.cpp、Bar.GeometryAttribute.cpp与生成头均未修改。
- 本轮无编译/运行证据；不能声明compile-clean、测试通过或截图已生成。
- 文件级检查：20个产品/测试/双语源修改文件的UTF-8/BOM/CRLF均保持；移除新enum/helper后，Bar.BottomDock.h的原有内容与本轮开始字节完全一致；两个禁改生产文件和生成头字节不变。

## 新增i18n源键
前缀`UI/Bar/ShapeConstruction/`；仅修改zh-CN.jsonc及en-US.jsonc：
`StepFormat`、`PointCountFormat`、`BackName`、`BackTooltip`、`ForwardName`、`ForwardTooltip`、`CompleteName`、`CompleteTooltip`、`CancelName`、`CancelTooltip`。
消费者使用`I18nKey.UI.Bar.ShapeConstruction.<key>`。构建前由orchestrator执行i18n sync补齐其余语言及生成头；当前生成头没有这些新成员。

## 后续验证入口与截图（均待执行）
- `--draw3-shape-core-test`：共享核心与生产构造事务/身份测试，包含阈值、闭合、一对象/一历史、恢复栈与未确认扫角丢弃。
- `--state-mode-production-test`：挂接RunConstructionAutoCollapseProductionTest，复用生产起笔策略入口。
- `--ui3-geometry-attribute-test`：提示沿用Bar.GeometryAttribute.Test.cpp现有离屏入口，保留生产ULW回执/像素读取。
- 预期目录`Build/shape-p34-ui/`：`construction-arc-{1,2}-{dark,light}-{100,200}.png`、`construction-polygon-{dark,light}-{100,200}.png`、`construction-{appear,disappear,folded}-{dark,light}-{100,200}.png`、`construction-compact-200.png`。测试保存后输出绝对路径；本轮尚无PNG产物。

## 冲突待决
R2终结重试分类器在Bar.ShapeEditor.cpp:432–445，当前仅识别CommitEdit/CancelEdit/CloseEditor/ConstructionComplete/ConstructionCancel，并仅为后两者识别construction快照。用户禁止修改该文件生产代码，因此本轮停止该点；Forward已接入核心/生产命令/身份校验，但QueueFull仍不能续接重试。协调者需解决分类器修改授权后补齐，并扩展该文件已有终结重试测试。

## 修改文件
- `Inkeys/Inkeys/UI/Bar/Bar.ConstructionPrompt.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`
- `inkStrokeModelerTest/draw3/shape_interaction.h`
- `inkStrokeModelerTest/draw3/shape_interaction.inl`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeInteraction.h`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeRuntimeActions.inl`
- `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h`
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeRuntime.inl`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.ShapeRuntimeTests.inl`
- `Inkeys/Inkeys/UI/Bar/Bar.AutoCollapse.h`
- `Inkeys/Inkeys/UI/Bar/Bar.GeometryAttribute.Test.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Rendering.cppm`
- `Inkeys/IdtMain.cpp`
- `Inkeys/src/i18n/zh-CN.jsonc`
- `Inkeys/src/i18n/en-US.jsonc`
- `inkStrokeModelerTestTests/shape_core_tests.cpp`
- `.trellis/spec/native-desktop/shapes.md`
- `.trellis/tasks/11-05-shape-ux-construction-prompt/validation.md`
