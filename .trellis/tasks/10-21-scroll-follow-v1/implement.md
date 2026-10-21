# 实施与验证

以下勾选区分软件交付与必要现场验收；本轮仅允无窗口或全程不可见HWND，不操作桌面、不显示或前置窗口。父任务保留in_progress，未完成真实画面门槛不归档。

- [x] 用户批准本轮全部后续代码，四子任务依次validate/start及implement/check，真实规范与研究进入JSONL。
- [x] 保留原定格Mag初始化/窗口/readyPromise设计，无第二套Mag。上轮原owner无callback基础PoC已通过；本轮正式MagnifierThread异步服务及状态/寿命无窗口check通过。
- [x] Tracker严格离线14组6918合法帧每prefix误差通过，静止300帧/长滚动/原特征全离屏/反向水平/干扰/变形拒绝/暂停，记录参数与模型边界。
- [x] 产品Business/Draw3/Bar接线及独立check：latest absolute、身份/seq、最后目标/跨帧present、准入/真contact收尾、旧Host凭据、生命周期/钳制、工具/动作receipt与独立模式投影；窗口ACK是明确seam。
- [x] 真实Shape/Pen四场景A->磁盘恢复->Follow->B/编辑->Exit文件回读；viewport-only最后Publish立即Stop。8真UInk、A/codec点/宽/style逐值相等、工作区/PageGUID、Viewport、Redo、真mask覆盖均经独立check。
- [x] 完整InkeysRepo.sln Debug ARM64与x64均exit0；vswhere ARM64原生MSBuild、同PowerShell规范Path/禁node reuse、>=5分钟期限。生成ILK/PDB故障仅按核实路径清理或留档后原配置重建。
- [x] 完整--no-window、原8项退出失败接管、4组共享storage CLI已通过；最终ARM64保存/协议/业务/状态/配置/全隐藏/形状短回归与x64五项纯CLI通过。长矩阵复用依据见保存check-current。
- [x] i18n426/426、47源/工程文本UTF-8/BOM/CRLF/EOF、4XML、diff及最终双架构PE检查通过，不修改依赖/第三方/SDK/渲染参数。
- [x] 每阶段implementation/check、最终构建/PE、用户用法/现场步骤、规范和总矩阵已记录；本轮不commit/push。
- [ ] 正式Mag worker真实变化/静止/排除、无callback启停/Freeze前后、持续耗时/FPS/长期资源：须现场可见内容，本轮停止于此。
- [ ] 真实双窗显隐/穿透、可见墨迹及擦除、底层滚动、主栏/快捷键/Paused退出；专用页面后Edge/Chrome及可用Word/PDF，含惯性/边界/至少十屏。
- [ ] 真实笔/触控、DPI/多屏/设备事件、锁屏、旧Windows/WOW64等实机补测；缺设备/应用逐项保留未测。

## 操作约束
不 commit/push，不无关重构，不修改第三方子模块，不跑会重写生产文件的无关格式化。修改保持既有编码/BOM/CRLF。归档和会话脚本必须 --no-commit。构建/测试日志写 Build/ 下，不提交生成产物。


## 先前准备阶段与历史证据

旧独立原型已完成过 implement/check 并提交 f07b562e，但真实共存门槛未通过。按后续用户指示，本轮删除独立服务/runtime/旧测试，恢复原定格寿命设计，仅增加 Inkeys.exe 内部 PrintWindow(2) 与可选回调验证。实际构建和软件内测试尚须以当前代码重新核对，旧报告不构成当前通过证据。

- 捕获子任务 prd/design/implement：当前收窄范围、实际实现与本轮验证。
- research/validation-matrix.md：持续维护当前结果与原完整需求的区别。
- 捕获子任务 research/inkeys2-mag-thread-history.md：同线程创建与同步等待的准确历史。
- .trellis/spec/native-desktop/scroll-follow-capture.md：当前共用原定格的边界与像素验证合同。

## 上轮 Mag 准备验证完成

真实 Inkeys.exe 共用原定格 Mag 的无回调路径已通过两个新进程的冷启动、静止和变化图案验证；恢复空回调后继续正确。可选 scaling callback 也实际触发；原线程设计未重排。完整 ARM64 solution、无窗口回归、i18n与格式检查通过。详细证据见捕获子任务 research/freeze-printwindow.md。按用户最新要求，本轮确认准备后结束，后续阶段下一轮开始；此里程碑不代表完整滚动批注或连续服务已实现。

## 本轮继续实施（2026-10-21）

用户已批准完成正式取帧及全部后续阶段，上一轮仅诊断的范围限制结束。本轮验证仅无窗口或完全不在屏幕显示的HWND，禁止Computer Use/前台/可见测试；必要可见验收时停止。当前授权见[implementation-authorization.md](../10-21-scroll-follow-v1/research/implementation-authorization.md)，其优先于本文旧轮次边界。原定格Mag的无callback基础验证见research/freeze-printwindow.md（捕获子任务）；后续正式路径须分别验证。本轮不自动commit/push。
