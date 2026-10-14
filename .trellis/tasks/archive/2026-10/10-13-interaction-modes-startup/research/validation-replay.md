# 本轮无窗口回放

本轮恢复下拉框，并新增生产调度线程回归；上轮radio.cpp回放仅属历史验证，不再作为当前修复证据。

## 生产线程调度
运行 research/replay-setting-threading.py，从当前 Setting.cpp 提取生产 FIFO 的 Start/Enqueue/Run/Stop、render scope、defer helper、Sync/Write 执行分支与写盘快照捕获；检查 IdtState guard 与两处 Resume scope。输出 Build/ModeReplay/threading.cpp。

在现有 VS vcvarsall.bat arm64 环境中，于 Build/ModeReplay 执行：
```text
cl /nologo /utf-8 /EHsc /std:c++20 /Fe:threading.exe threading.cpp
threading.exe
```

覆盖渲染持锁提交返回、业务worker等待模拟HWND回调获取ImGui锁、FIFO同步/两次冻结写盘顺序、嵌套/异常marker恢复、停止排空/停止后不回退及非渲染同步。第一次运行退出0。
模型有界等待，不创建真实HWND；不能称为真实Window Service现场运行测试。

## 下拉及卡片几何
运行 research/replay-interaction-modes.py，从当前模式块和实际 ComboClass/PushComboColors 方法提取源码，使用三语JSONC实际文案。输出 Build/ModeReplay/combo.cpp。

在上述原生环境中编译：
```text
cl /nologo /utf-8 /EHsc /std:c++20 /DIMGUI_DISABLE_SSE /I ../../Inkeys/additional/imgui combo.cpp ../../Inkeys/additional/imgui/imgui.cpp ../../Inkeys/additional/imgui/imgui_draw.cpp ../../Inkeys/additional/imgui/imgui_widgets.cpp ../../Inkeys/additional/imgui/imgui_tables.cpp /Fe:combo.exe
combo.exe
```

三语 × 0.6/1/1.5/2缩放共12组，双向0→1→0、重复选择不保存、控件几何无裁切，退出0。
使用默认字体及业务计数桩，验证选择与布局，不单独证明生产线程安全或字体像素效果；生产调度由上面的独立线程回归验证。

本轮i18n check：en-US/zh-TW均334/334，100%，退出0。原编码/BOM/CRLF保持。最终solution/headless及审查结果见 validation-freeze-fix.md。
