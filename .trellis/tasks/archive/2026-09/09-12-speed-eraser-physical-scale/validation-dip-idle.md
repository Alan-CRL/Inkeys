# DIP尺寸与静止回缩验收（2026-09-13）

## 基线与范围
- 分支feature/eraser；HEAD精确等于7af954bb17bd4e4b8e3b76d009ab11e939160c17，ahead/behind=0/0。
- 初始只有Inkeys/PptCOM.dll未提交改动；保留。全程主Agent，无子Agent、切分支、reset、commit、push或归档。
- 尺寸16/32/160DIP，Touch16DIP，固定50DIP；EDID不再生成尺寸。固定模式保留96DPI基准观感，按新规格随DPI换算；旧eraserSize保存值未被重解释。
- 修改源码/测试：Draw3.SpeedEraser.h/.cpp、Draw3.DrawingController.cpp/.cppm、Draw3.StrokeGeometry.cpp、Draw3.InkPrediction.cppm、Draw3.Host.h/.cpp、Draw3.WindowControl.cpp/.cppm、Draw3.HiddenWindowTest.h/.cpp、IdtMain.cpp（专项测试路由）、InkeysHeadlessTests/speed_eraser_tests.cpp。
- Display有效性实现、RTS采集、ContactInput/MPMC、模型、shader、UInk格式实现及工程配置未修改。

## 已执行并通过
- 使用vswhere找到当前ARM64原生MSBuild，完整执行：
  MSBuild.exe InkeysRepo.sln /m:4 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal /fl
  最终退出0，产物Inkeys.exe与HeadlessTests生成；已有第三方转换警告未为清零而修改。
- Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window：退出0，failures=0。采样/帧率最大偏差1.71061%，小于保留的5%上限。
- 基线先加入新回归，出现5项预期失败：标准仍为最小、EDID/模式改变上限、普通500DIP/s长擦放大。完成修正后全部通过。
- 覆盖16/32/160/Touch16尺寸与DPI96/144/192、不同EDID/物理/模式、固定42DIP旁路、普通20秒/中速扫描、无事件ContactSizeState推进、同位置/小噪声与无包对照、真实Touch普通移动到标准、重连/时序/生命周期及尺寸断点。

## 实际产品接入
- Inkeys.exe --draw3-eraser-hidden-test：退出0，DComp与ULW均PASS。
- 使用Start-Process -WindowStyle Hidden -PassThru及输出重定向显式等待真实退出，测试窗口保持不可见；不使用SendInput或Computer Use。
- 快擦实测诊断：device=MouseLeft，effective=160DIP、最终cursor=160px（测试DPI96），随后不再投递任何Move/鼠标消息。
- 无Move时最终接触光标与下一段半径回标准附近；历史点数不变，历史末点仍保留大半径；达到标准后250ms内没有多余渲染帧。
- 恢复仅一个短Move：检查真实模型输出新段的半径与足迹包围框，存在小半径同位锚点，没有跨静止期旧大半径拖尾。
- 真实应用Undo/Redo成功。截取实际历史点/锚点/新末点三个有界观测值，经生产ExportDraw3SnapshotToUInk、SaveUInkFile、ReadUInkFile、ImportDraw3UInkDocument保持坐标及各点宽度，旧大宽度未被改写。
- 文件往返产物在Build/eraser-size-boundary-<随机GUID>.uink，不覆盖用户文件。

## 未通过或不能冒充通过
- 原全量Inkeys.exe --draw3-hidden-test运行退出1。翻页/Clear/PPT场景出现clear-preserves-other-pages、undo-round-trip、Desktop-ownership等断言失败及save_submit/load_submit失败（测试未配置相关服务）。未完成这些场景的基线对照或在本任务修复，不能称全量隐藏套件通过。
- 最初用PowerShell直接启动GUI子系统时启动命令提前返回且日志为空；该退出值未作为验收结果。确认实例结束后，改为显式进程等待/输出捕获。
- 旧全量日志保存Build/eraser-dip-hidden-full-err.log；专项日志Build/eraser-dip-focused-{out,err}.log；构建/单测日志Build/eraser-dip-build.log和Build/eraser-dip-tests.log。
- 专项验证的是实际构造/提交的光标和几何足迹，不等同于真实人手体验或整幅GPU像素级差分。真实鼠标/数位板/触屏、热插拔/旋转和Win7运行仍需用户设备验收。

## 参数方向
- 进入/退出/大目标速度：笔电800/600/1900DIP/s，大屏650/450/1700DIP/s，可信直接Touch25/18/70cm/s。降低进入速度更易获得资格；降低大目标速度更易达到大尺寸；二者独立于尺寸范围。
- 证据100/240ms，始终350ms时间常数泄漏：提高门槛要求更持续动作；降低泄漏常数更快遗忘。
- 有效移动噪声阈值0.75DIP或0.02cm；改变它影响静止确认，不改变几何位置。
- 静止280ms开始、200ms跟随、对数限速4/s；提高延迟更抗折返，提高跟随常数回落更慢。鼠标标准目标32DIP；未解锁Touch16DIP。
- Up140ms视觉收尾及既有并发/取消/批次语义保留。
