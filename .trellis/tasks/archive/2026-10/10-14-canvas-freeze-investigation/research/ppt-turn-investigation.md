# PPT 翻页后画布失去响应：专项调查（2026-10-17）

## 当前结论

F1–F4 已按用户要求提交，提交号 68cd297da63642d58fd755da20ec4c8bac42bd1a。提交保留 SSH 签名，未 push。原默认窗口套件 11 项 owner/Z序失败按用户要求暂缓，不在本轮继续修改。

新反馈只有两个已知条件：发生在 PPT 翻页之后，UI 仍能操作。PowerPoint/WPS、PPT 能否继续翻页、输入设备及是否仍按住均未知。

发现一条与这些条件相容的新状态竞争，并取得真实生产组件＋原码摘录的可执行证据：旧 raw 页与新 descriptor 在同轮被观察到，descriptor revision 已消费而 target 未更新，之后同 revision 的空响应使 target 长期保留旧页。UI 的新页回执不匹配旧 target，生产输入门禁持续关闭。尚未用真实 Office/WPS 完整产品复现，未认定用户现场根因。

## PPT-1：页面快照交错后旧 target 不再重算

1. 已在第 A 页正常书写，cachedTarget=A。
2. PptInfo 先读取 rawPage=A（IdtPlug-in.cpp:576）；翻页发生后，GetSlideShowState 返回描述 B（608）。程序立即更新 cachedState 和 observedStateRevision（615–617）。
3. 同轮 raw=A、descriptor=B，trustedPage=false；因此 target 更新分支（742–748）跳过，但版本已消费。旧 target A 未清除。
4. 下一轮 raw=B，GetSince 相同版本返回空，descriptorChanged=false。cachedTarget 已存在，所以仍不解析 B。trustedTarget 仅检查 pageKind，未核对缓存目标的具体页身份（770–773）。
5. 上轮 suspend 已清除 UI-ready；本轮取消 suspend 不恢复该标记。UI 发布 B 页码搭配 A 的 target revision；AcknowledgePptPageUi 按 expected A 拒绝 B 回执，Host 的 requirePresentationUiReady 门持续拒绝输入。
6. managed 同内容指纹不会推进 stateRevision，因此普通周期刷新同一 B 不保证恢复；新的实际状态版本、场次/绑定变化或其它重建目标操作可能解除。不能称为任何后续操作都无法解除的永久死锁。

这一路不要求开启控制台、不要求文件读取失败，也不要求手指/笔一直按着。症状机制是输入准入被持续关闭，绘制线程未必停止运行，UI 可继续响应。

## 可执行验证及独立复核

- 脚本：research/ppt-turn-original-probe.py。
- 输出：Build/CanvasFreezeInvestigation/ppt-turn-20261017/events/，含 probe.cpp、source-manifest.json、build.log、run.log、results.json。
- 直接编译真实 Draw3.Bridge.cpp、Draw3.Presentation.cpp；自动摘录 trusted/cache/target、AcknowledgePptPageUi 与 Host blocked 判断，留存源文件与摘录 SHA。
- ARM64 编译/执行均 exit0。指定交错连续 10,000 次逻辑迭代：raw=2、descriptor=2、cachedTarget=1、revision=1、uiReady=0、blocked=1。
- 三个对照通过：首次读取已对齐；raw 先变化、descriptor 下一轮到达；后续新 envelope 强制重新解析。均得到 cachedTarget=2、revision=2、uiReady=1、blocked=0。
- 第二研究者逐字核对 5 段摘录、10 个源文件 hash，确认初值和对照合理，未发现替身直接写死结论。详见 ppt-turn-controller-audit.md。
- 限制：COM envelope 投递、成功呈现、部分 Host 端点是替身；没有执行完整 PptInfo/COM/PageControl/Host/contact/GPU。10,000 次是逻辑迭代，不是运行时间或真实翻页次数。

## 其它路径与排除范围

- 正常目标切换会主动封存已接受接触并隔离旧接触，不能仅凭“翻页时笔没抬起”认定必须永久等 Up；暂未发现该普通路径的新增确定死锁。
- 同文稿普通暖页切换不必读取磁盘，不能把所有翻页卡死都归因于此前 F2 冷槽 IoError。
- 持久化 worker 存在 root 级命名 mutex 的无限等待（PresentationAutoSave.cpp:148），Save/Load 均可到达。若另一个活着的拥有者长期不释放，冷槽 load 无 completion，IoError 重试也不会启动。这是另一条条件性等待风险，本轮没有实际构造该故障，也没有证据连接用户现场；普通暖页不能直接套用。

## 原有回归及二进制身份

复核发现 Build/ARM64/Debug/Inkeys.exe 已被另一轮构建更新为 SHA6691e9a93c0d75cfb9b518523123acb3abd9e8c457f6b63811d6fdcbfae31217，不能当作此前最终修复身份；首次运行记录独立保留，不用于声称当前源码验收。

随后明确改用保留的 fixes/run-f4/Inkeys.exe，核对 SHA4188e2eb39cca05f40ac68e1730cc85f343c1bfe122c1ee78c920a2dca075c7f。parked-ppt-retained、pending-topology-load、ppt-current-load-retry 三条现有无 GUI CLI 均 exit0 且输出 PASS，最后一条含真实 worker sharing/retry/install PASS。证据在 ppt-turn-20261017/verified-final/results.json。它们只证明已有覆盖仍通过，不覆盖本次新增的 PptInfo 交错。

## 建议修复方向与后续验收

让 descriptor 的已观察版本与 target 的已应用状态分别记账；只有目标成功从可信 descriptor 解析后才确认已应用，暂时 raw/descriptor 不一致不能永久丢掉待应用更新。对 trustedTarget 增加确切页面身份一致性，并让页码 UI 与 target 使用同一被接受快照。不得通过绕过 UI-ready 门来让不匹配页面变成可写，否则可能把墨迹写到错误幻灯片。

下一步验收应在真实 PptInfo/Host 链路安排读取屏障，固定 rawA→descriptorB 交错，证明最后实际呈现/可写目标是 B，旧页墨迹不串页；再覆盖快速连续翻页、动画不换页、末尾黑屏、返页及切出放映。真实 Office/WPS 与实体触摸/笔另做完整产品确认。

本轮仅调查，产品源码未追加行为修改。新增研究文件与任务进度保留在工作区，未与此前已授权的修复提交混在一起。
