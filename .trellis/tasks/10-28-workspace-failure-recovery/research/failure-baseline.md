# 失败场景基线

- `IdtState.cpp:3407-3448`：退出先 inactive 背景，之后 native Prepare/Complete；nativeFailed 只计失败和清 prepared，没有恢复 Whiteboard 源背景。
- `Window.cpp:2902`：Workspace Restore 负责原生窗口/region/owner/Z-order，不负责 UI 背景或分页。
- `IdtState.cpp:3409,3431,3439,3448`：非原生等待只 continue，不进入失败预算。
- `Draw3.TransparentPresentation.cpp:627-640`：DComp Reset 同步 WaitForCommitCompletion，无期限。
- `Draw3.TransparentPresentation.cpp:648,814,1217-1238`：DComp 设置不可变样式，ULW 需清除，运行期自动回退可能失败。
- `Draw3.DrawingController.cpp:14774`：恢复失败进入 ConsumerFatal，不等同可靠 ULW 降级。
- `IdtMain.cpp:1051-1124`：已有启动期 StopProduct/Window owner join/清样式/重建/ULW 路径，优先复用而非另建恢复框架。

本文件只记录当前代码事实，不把风险当作已经发生的所有现场。
