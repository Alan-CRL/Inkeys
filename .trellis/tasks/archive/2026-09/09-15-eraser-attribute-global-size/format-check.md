# 最终文件格式检查

所有已跟踪文本文件BOM与HEAD一致；修改延续本地原有UTF-8与CRLF/LF格式。Git autocrlf对原有LF文件给出下一次Git操作可能转换CRLF的提示，本次未用Git改写工作区。git diff未出现整文件格式化。

- `.trellis/spec/native-desktop/index.md`: UTF-8, CRLF
- `Inkeys/IdtMain.cpp`: UTF-8 BOM, CRLF
- `Inkeys/IdtState.cpp`: UTF-8 BOM, CRLF
- `Inkeys/IdtState.h`: UTF-8 BOM, CRLF
- `Inkeys/Inkeys.vcxproj`: UTF-8, CRLF
- `Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl`: UTF-8 BOM, CRLF
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`: UTF-8 BOM, CRLF
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`: UTF-8, CRLF
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp`: UTF-8, LF
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h`: UTF-8, LF
- `Inkeys/Inkeys/Other/Other.Config.cppm`: UTF-8, CRLF
- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`: UTF-8, CRLF
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`: UTF-8 BOM, CRLF
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`: UTF-8 BOM, CRLF
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`: UTF-8, CRLF
- `Inkeys/Inkeys/UI/Bar/Bar.State.cppm`: UTF-8, CRLF
- `Inkeys/Inkeys/UI/Bar/Bar.ToggleClickCoalescer.cppm`: UTF-8, CRLF
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`: UTF-8, CRLF
- `Inkeys/Inkeys/UI/Setting/Setting.cppm`: UTF-8, CRLF
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`: UTF-8, CRLF
- `InkeysHeadlessTests/animation_tests.cpp`: UTF-8 BOM, CRLF
- `InkeysHeadlessTests/speed_eraser_tests.cpp`: UTF-8, LF

PptCOM.dll SHA-256与任务开始时副本相同：`779908b2acc9f37241a4c59c36822685e87a07c4bc886bde22d9d167e68fe117`。
