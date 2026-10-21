# 实施与验证

- [x] 用户明确批准创建任务和实施；确认 feature/move、受控 GUI、保守互斥。
- [x] 只读调查并保存原始需求与研究。
- [ ] 顺序实施四个子任务；每个子任务启动前 task.py validate/start，implement/check 使用真实规范与研究上下文。
- [ ] 捕获子任务先真实 PoC（不可见/连续变化/排除/停止失败），通过才开始 tracker。
- [ ] tracker 合成图至少十屏、反向、静止300帧、原点全离屏、固定UI/动态/缩放旋转、失败暂停。静止误差<=0.5原图px，累计<=max(3px,路程*0.2%)，报告每项误差/接力。
- [ ] Draw3/主栏：latest-only/重复跳号/旧session和scene/最终屏障/准入收尾、双窗ready、Tile恢复/resize/设备/钳制。
- [ ] UInk：A->scroll->B->eraser/Undo/Redo->Clear或Exit保存->读回->再次scroll；核对点/viewport/PageGUID，不写视觉状态；回归形状识别/Freeze/白板/PPT。
- [ ] vswhere定位原生ARM64 MSBuild；同PowerShell Remove-Item Env:PATH -ErrorAction SilentlyContinue；$env:MSBUILDDISABLENODEREUSE='1'；完整 InkeysRepo.sln /m /p:Configuration=Debug /p:Platform=ARM64，允许至少5分钟（依赖安装延长），记录exit和首个有意义错误。
- [ ] 尝试完整x64构建；静态PE导入/Win7新增硬依赖审查；不通过改配置绕过环境问题。
- [ ] InkeysHeadlessTests --no-window、定向测试、相关Draw3/UInk CLI、i18n check、git diff --check及编码/BOM/EOL审查。
- [ ] 受控 GUI 使用 computer-use 技能，专用内容/临时数据/自动清理：浏览器上下/水平/边界/惯性/多屏/主栏/Paused退出；缺失硬件/应用明确未测。
- [ ] 每阶段将文件、实现、测试和遗留项写入子任务 implement.md/research；最终全范围 check、规范更新并交付。

## 操作约束
不 commit/push，不无关重构，不修改第三方子模块，不跑会重写生产文件的无关格式化。修改保持既有编码/BOM/CRLF。归档和会话脚本必须 --no-commit。构建/测试日志写 Build/ 下，不提交生成产物。


## 当前阶段与历史证据

旧独立原型已完成过 implement/check 并提交 f07b562e，但真实共存门槛未通过。按后续用户指示，本轮删除独立服务/runtime/旧测试，恢复原定格寿命设计，仅增加 Inkeys.exe 内部 PrintWindow(2) 与可选回调验证。实际构建和软件内测试尚须以当前代码重新核对，旧报告不构成当前通过证据。

- 捕获子任务 prd/design/implement：当前收窄范围、实际实现与本轮验证。
- research/validation-matrix.md：持续维护当前结果与原完整需求的区别。
- 捕获子任务 research/inkeys2-mag-thread-history.md：同线程创建与同步等待的准确历史。
- .trellis/spec/native-desktop/scroll-follow-capture.md：当前共用原定格的边界与像素验证合同。

## 本轮 Mag 准备验证完成

真实 Inkeys.exe 共用原定格 Mag 的无回调路径已通过两个新进程的冷启动、静止和变化图案验证；恢复空回调后继续正确。可选 scaling callback 也实际触发；原线程设计未重排。完整 ARM64 solution、无窗口回归、i18n与格式检查通过。详细证据见捕获子任务 research/freeze-printwindow.md。按用户最新要求，本轮确认准备后结束，后续阶段下一轮开始；此里程碑不代表完整滚动批注或连续服务已实现。
