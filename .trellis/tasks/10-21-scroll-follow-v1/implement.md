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


## 当前阶段结果（捕获门槛未放行）

独立冷启动捕获服务曾在两轮专用图案/排除/启停中取得真实像素，最新check复跑55帧全NearBlack；保留定格运行库的bare-lease及实际创建即隐藏的first-owner/global-once对照均Filter50且0有效。原生生命周期实验尚无安全替代；原型保持批准的全局最后使用者才反初始化策略。阶段不能标为完成，后三子任务保持planning。已顺序执行implement/check；ARM64完整solution、基础定向和完整无窗口回归通过，i18n/XML/PE/物理格式检查通过。捕获验收保持失败，x64最终结果见子任务research/check.md；交付原型和真实失败证据，不宣称产品功能完成。

- research/validation-matrix.md：各验收项当前状态。
- research/capture-debug-retrospective.md：调试事实、未证明的部分和预防合同。
- .trellis/spec/native-desktop/scroll-follow-capture.md：长期接口/所有权/失败门槛规范。
