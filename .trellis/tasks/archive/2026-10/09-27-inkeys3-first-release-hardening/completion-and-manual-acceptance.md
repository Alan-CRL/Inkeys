# 工程完成状态与人工验收清单

当前状态唯一入口：[handoff.md](handoff.md)。本表保留必需验收步骤；历史阶段与当前候选区分，不给完成百分比或时间估算。

## 当前验收状态（2026-10-02 续接）

- 分支 chore/publish，HEAD 9cce16bd5c4778926d92f24b31eff65c755ba821，父 e32a5fc06096c1e4ab88a29866c60ebe323fd722；远端只读确认包含且相同。源码修补尚在执行，最终候选未冻结。
- 最新主 `InkeysRepo.sln` Debug|ARM64 Build exit0；当前候选 `Build/ARM64/Debug/Inkeys.exe` SHA `4E31A8C9BAF87E26C5645BC1F7911F07E6603385767D973E7D82B4655AF56DF0`，Headless SHA `AE437D10D198342EEB417A5D8617CA1369F7AD94D65104C9A2D16A78CFEB179B`。独立 `inkStrokeModelerTest.sln` 五个 selector 均 exit0。主 Debug 不是最终 Release/HF。
- B363 retention 的生产离屏回归和 Headless 当前候选均 exit0；UI3 首场景仍 exit90，唯一当前缺口为绑定快照确认的 `More` tag `0x2000C` 严格 Overwrite（3px真实相交），button-layout 有界诊断未证明可安全豁免，不能写成 Coverage 全部失败或发布通过。
- 用户确认性能对照为现存 20260811a Canary（0809–0811 优化），此前“0813”只是本轮命名线索；包与二进制身份核验后再做同配置比较。
- 611 项已定义历史集合逐 diff 记录保留，不重做。四生成 cso 已备份，不提交/删除。

| 原职责 | 当前状态与证据边界 | 尚需的原定验收 |
| --- | --- | --- |
| E01 关闭/重启期限 | 已验证普通合法 ArmFailed 的原绝对截止兜底；不从 helper 推所有异常场景 | 最终受影响候选回归；Win7 真关闭/重启人工项 |
| E02 启动/保存/退出 | failed-start清理和 Desktop/PPT worker hold/最后 durable reader 已验证；PPT session/CAP selector 最新 exit0 | 真实放映退出 Selection 穿透、Win7/Office/窗口边界仍人工；不把同 override fixture当普通重启 |
| E03 输入生命周期 | 精确contact handle/generation取消已验证 | 完整Move/Up/Laser与功能场景、最终受影响回归和真笔人工体验 |
| E04 UI3/Draw3性能 | UI3 retention B363 已真实 D2D/BGRA 红→绿；最新 on4096 accepted2/completed1，第一目标2/2 verified，第二目标9/10 verified，绑定快照确认 `More` tag `0x2000C` strict Overwrite，后215未开始；Draw3 control/fallback/retry/renderer/laser probes 与 Host metrics smoke 均 exit0，但仍是各自合同/小 smoke | 追真实 Overwrite composition 证据；完整场景、同效果 Release 三轮 median/P95/噪声、Draw3 Move/Up/Laser/Present/长期数据 |
| E05 数据/局部缺陷/集成 | F069/CAP 和 session selector 最新 standalone exit0；ASYNC01 确定性交错已 RED→GREEN；仍无最终 Release/HF | 生产 macro-off 最终复建、独立最终 diff review、三架构/资源/兼容矩阵与 HF 指纹 |

目前仍有我方可执行工程工作，不是“只剩人工”。人工验收、自动验证、工程闭环与发布许可分别记录。HTTP/HTTPS及HTTP回退、旧智绘教.exe、不自动跨进程恢复旧PPT墨迹、关闭功能gate和Win7/FLIP/DComp/ULW约束保持。

## 人工测试共用准备

- 使用独立程序副本、独立测试配置和复制的 PPT/UInk 文档；每组记录 commit、EXE SHA-256、Release/架构、Windows build/补丁、GPU/驱动、实际 driver/feature level/presenter、DPI/分辨率/刷新率、多屏、供电和效果开关。
- 最终候选尚未冻结；完整签字验收使用交付的同一 Release 二进制身份。已有阶段包只供限定范围预验，后续改动补受影响回归。
- 每项分别填写 PASS / FAIL / 未验证 / 不适用（附理由），保留失败轮。性能至少三轮，分首交互、预热、长期；相同轨迹/效果/Release/设备比较。
- 本次约定禁止 computer-use 工具。人工点击和用户自行执行命令不受此工具禁令影响。
- 不用外部强杀证明 UEF 捕获或自动重启；不承诺未落盘墨迹零丢失，不测试/开放被 gate 关闭的白板入口。

## 必需的设备组合

| 组合 | 目标行为 | 必须记录 |
| --- | --- | --- |
| W7-A：Windows 7 SP1，仅 KB2670838，硬件支持 FL11.0，x86/x64 各可得版本 | Draw3 Hardware FL11.0；DComp 不可用时 ULW，交换链保持 FLIP_SEQUENTIAL；两个 DWM 方案不使用。 | 实际创建的 driver/feature level/presenter、启动与成功 Present、透明/穿透、帧失败日志。 |
| W7-B：同一系统/补丁，硬件不支持 FL11.0 | Hardware 创建失败后 WARP FL11.0；ULW FLIP 正常输入/显示。 | 不能只以 dxdiag 设备声明推断应用回退成功，须核实际 WARP 日志/诊断及显示结果。 |
| W7-C：只有可得的“Hardware/WARP 均无法创建”机器才测 | 明确启动失败，窗口/捕获清理，无透明输入拦截和重启循环。 | 无此环境标未验证，不人为升级最低要求或增加 KB。 |
| MOD-A：现代 Windows、真实 Hardware GPU | UI3 与 Draw3 各自设备/线程；DComp 路径及当前支持的 ULW 路径。 | 混合 GPU/远程桌面/驱动组合分别记录，不把开发机 WARP 结果外推所有 GPU。 |
| OFFICE：实际使用的 Office/PPT 或 WPS 版本 | 放映身份、EndScreen、翻页、退出和页墨迹一致。 | 产品/Office 架构与版本、多窗口/多文稿条件；WPS 与 Office 分开。 |

Win7 SP1 + 仅 KB2670838 支持 FLIP_SEQUENTIAL 按用户实测作为本项目约束；不得为通过验收改 bitblt、关闭动画/光影、降画质或另装系统补丁。

## 可执行人工验收项目

| ID | 操作与重复量 | 通过条件 |
| --- | --- | --- |
| M01 | 在 W7-A、W7-B、MOD-A 冷启动各 3 次；第一次切绘图，再切选择；保持 idle 10 分钟后再展开主栏。 | driver/presenter 符合矩阵；无黑屏、无崩溃、空选择双画布隐藏且桌面可点击；idle 首次唤醒无跳变。 |
| M02 | 主栏展开/收起连续 20 次；动画中快速反向 20 次；拖动、吸附、解除吸附、屏幕边缘各 10 次。 | 动画时长/轨迹正常，无先停顿再跳位、阴影残留、丢 capture、点击区域错位或最终位置漂移。 |
| M03 | 属性栏、颜色、粗细、Fine Dial；软/硬笔、荧光、橡皮、激光及已开放形状快速交替；重复选择同一工具。 | UI 高亮、光标、真实工具/宽色一致；旧 PPT/手势回调不覆盖后来的选择，取消/重复操作无旁路副作用。 |
| M04 | 两种主题和正常动画/动态光影配置；首次显示 SVG、同尺寸重复交互、开设置同时操作主栏。 | SVG/路径/着色/阴影完整且清晰，无缓存错色、丢图、动画被设置窗口阻塞；不靠关闭效果得到顺畅。 |
| M05 | 真笔/鼠标每种工具画单点、极慢、快速直线、折返、转角、停住再动、抬笔各 20 条；含压感/倒转笔尾（设备支持时）。 | 每次 Down 出现墨迹；压力/转角/尾端正确，无漏笔、异常粗细、预测残影；鼠标不冒充真笔验收。 |
| M06 | 真 Touch：单指、多指、Pen+Touch、Mouse+Touch；Laser 多指开/关，每种第二触点加入/抬起/取消 50 次。 | 首指生命周期完整，禁多指时第二指不绘制且终态不耗槽；连续循环仍接受新 Down，无跨接触串笔/光标回弹。 |
| M07 | 固定/速度橡皮：慢/快、极细、长按、停住再动、Touch 面积辅助开/关；高 DPI 下重复。 | 光标与实际擦除半径一致，idle 不重写历史轨迹或制造点，面积异常不制造大洞，恢复无旧大半径尾巴。 |
| M08 | 画 100 条线后撤销/重做/清屏/撤销清屏；自动保存开/关；保存未完成时重复 Clear/Undo。 | 画布/历史一致，无数据串页；最后已提交 UInk/索引有效；开关不丢已接受请求，失败不伪报成功。 |
| M09 | PPT 两个文稿/多窗口：进入、连续翻页、返回旧页、最后一页、EndScreen、退出，至少 3 场。 | 当前页才能输入；两文稿/页身份不串，最后一页和结束页墨迹独立；退出后选择模式能穿透桌面。 |
| M10 | 在复制文稿中重排/删除幻灯片，重新进入同文稿；已有旧 page-index 文件时开启新会话。 | StableSlideId 对应内容不按 ordinal 错绑；旧文件保持不变，新会话独立保存，保留页不污染其它文稿。 |
| M11 | 画完 Desktop 未清屏，切 PPT，再从设置页关闭；另做主动重启；在当前已开放的加载入口检查保存点。 | parked Desktop/当前 PPT 各保存到正确身份；只验证已 durable 内容和已开放恢复能力，未开放自动上屏记不适用。 |
| M12 | 100%/150%/200% DPI、跨屏拖动、resize、旋转/分辨率变化、主屏切换/插拔显示器；条件允许时重复 10 次。 | UI/光标/真实输入坐标和粗细一致，旧区域/阴影清除，无残留透明拦截、黑屏或错误 DWM 回退。 |
| M13 | 正在绘图/idle/开设置三种状态分别睡眠恢复、锁屏解锁；混合 GPU/远程连接另列组合。 | 恢复后继续输入，旧 HWND/设备资源不误用；失败安全退场或按支持路径恢复。自然 device-lost 证据单独记录。 |
| M14 | 所有真实关闭/重启入口：设置页直接按钮、Bar 确认关闭、重复点击、确认取消；分别在 idle、绘图、PPT、设置打开时做 3 次。 | 确认取消仍运行；有效请求正常尽快退出，卡住时 15 秒退场；Restart 旧实例结束后只启动一个新实例，无透明拦截残留。需 E01/E02 后的最终构建。 |
| M15 | 在 Win7 独立程序副本上运行 Inkeys.exe --shutdown-supervisor-tests，保存 stdout/stderr 与它创建的私有报告。 | exit0；真实 UEF 产有效 MDMP/报告，manual/auto 报告 watchdog、唯一拉起、约15秒强退通过。该 CLI 自建 child，外部强杀不替代本项。 |
| M16 | 用户确认的“上一个 Canary”和 H2：同 Release/架构/设备/供电/DPI/效果，按 M02/M04/M05 轨迹至少 3 轮。 | 无超出噪声的关键退化；落笔和尾延迟分开判断，不以总 FPS 掩盖；输入到具体成功帧统计入口由我补 E04，不能把回调数写帧率。 |
| M17 | 长时间使用 60–120 分钟，累计约 1000 笔、工具/页面反复切换；每 10 分钟记录内存、GPU内存（可得时）、GDI/USER/句柄及失败日志。 | 无持续失控增长/资源耗尽、越来越慢/输入失效；为保存保护而保留的 UInk 版本磁盘增长与 RAM/GPU 泄漏分开。 |
| M18 | 定格/放大镜/设置 owner、窗口排除列表、设置显隐；反复切选择/绘图；尝试白板等关闭入口。 | 当前已开放功能正常，owner/capture 不阻桌面；白板/Unsupported/NotReady gate 维持关闭，不为验收开启新入口。 |
| M19 | 仅在独立测试目录/卷或虚拟机中验证无写入权限、磁盘满、坏配置/UInk/index、半写文件和恢复失败；使用文档副本。 | 保存失败不伪报成功，不覆盖最后 committed 恢复点、不跨 workspace 串页；恢复失败有安全退路。不能改真实系统目录权限或破坏真实文档。 |

## 结果记录模板

每次结果记录：M编号 / 设备组合 / commit与EXE散列 / 环境与开关 / 操作轨迹 / 三轮结果与样本量 / PASS或FAIL / 原始日志位置 / 视频或截图（需要时） / 复现步骤。失败记录继续保留；修补后另加复验记录。

若 Win7 CLI 通过但 GUI/真实输入失败，两项分别记；若设备组合不可得，保持未验证。正式 IDT_RELEASE/三架构 DLL-TLB-shader-license 包和 CI 由发布准备流程核对，本表不执行发版、不自动结束任务或提交。


## 历史阶段证据（不代表当前状态）

# 提交后完成度核对与人工验收清单

核对日期：2026-10-01。人工体验、已实现代码、自动验证和允许发布分别判定。本文件提供验收步骤，不修改产品、发布宏或任务完成状态。

### 历史快照：本次结论

**目前不是“我方工作全部完成，只剩人工测试”。** e32a5fc06096c1e4ab88a29866c60ebe323fd722 是工程收口的阶段提交；存在下表中的可继续实施/自动验证事项。没有按百分比或任务数量证明完成。

- 发布分支：chore/publish；HEAD 为上述 commit，父提交为 H0 8b156fca59f0337a6afc6d722941666fcf143080。
- 此 commit 共 306 个文件、21286 行新增、2002 行删除，包含源码/测试/spec/八个 Trellis 任务；四个 inkStrokeModelerTest/*.cso 生成物未提交。
- 逐提交审计已枚举 611 个唯一 SHA：587 个进入 H0、24 个未进入 H0；均有逐 diff 记录。该比例仅为已定义集合的静态覆盖，不含不可见/删除 refs，也不是行为通过率。
- 阶段证据包括 Debug ARM64 和 Release ARM64/x64/Win32 主 Solution、Headless、PptCOM、产品 CLI、隐藏 Host 和真实 UEF/15 秒监督器。提交后又补四真实 wWinMain 故障 Hold/Natural、Failed-only 原截止、C-P1/P2、C3-A 真实清理与渲染卡住三轮以及 Main 同一 ULW 回退三轮；各范围分开，完整保存恢复与真实性能尚在实施，不能复用旧 Release 给最新源码。
- 当前 Trellis 父任务和七子任务为 in_progress，阶段 commit 已补记；父任务旧 planning 和 handoff 顶部的“未提交”已更新。实施清单及若干早期报告仍记录历史阶段，不能凭勾选空白认定代码完全未做，也不能凭 commit 存在认定所有职责已完成。

### 历史快照：2026-10-01 最新安排建议

工程工作量粗估85–90%，实现/自动验证/人工体验/发布许可分别判断。仍由我负责生产UI3完成链、Draw3 phase/CPU/MoveUp/Laser实测与资源、F069身份清理、最后Release三架构/总审；用户不必先人工验收才能推进这些。供完整人工验收的最终包尚未冻结，预计需再预留约1–2个工作日（估计，真场景新失败会影响时间）。当前可准备Win7SP1仅KB2670838、FL11.0/无FL11.0→WARP设备、Office和旧Canary版本，不把现中间包作为最终签字。

最新已验证：DebugARM64主Solution/Core各0、真实U3Host指标小smoke0；PPT合法230文件首存/更新/predecessor已从真实红转绿；Desktop三轮和PPT三轮保存worker停滞按原15秒自己退出、新进程最后durable严格reader通过。PPT正常退场与same/foreign sessions均通过，Win7/Office/可见恢复独立未验。首真正UI3 MainFold off场景第一Up被accepted，但未达严格完成、0/216，因此仍属工程诊断，未宣称流畅度达标。详见父validation/最新handoff和原始ignored evidence。

### 历史快照：仍由我负责的工程事项

| ID | 未完成内容 | 完成证据 |
| --- | --- | --- |
| E01 | 双失败同步原截止修补已红绿/独立审查、三个Release架构阶段复验通过；后续源码的最终矩阵仍要重跑。双失败Restart无launcher，不保证新实例。 | 6真实失败+2身份拒绝、耗时/过期不重加15秒和旧22回归；sentinel不代表UInk/GUI恢复。 |
| E02 | C3-A真实Host/style失败、Window两个rollback和放行均有证据；Main同span新ULW超过旧grace仍能书写三轮通过，ordinary render卡住三轮按原15秒退场。真实Desktop/PPT保存停滞、fresh严格最后committed reader仍由C3-B实施。 | 已有19positive原始观察/逐case安全审查和十primitive回归；新增保存段、RTS成功静止/独立旧Window hold及最后候选三架构仍待，不宣称所有driver/COM调用有上限。 |
| E03 | 精确initialize失败handle/generation helper已红绿和阶段三架构；普通页Closing/真实RTS callback停止/实际初始化失败分支全链、保存边界仍需补生产证据。 | E03已有11身份断言红→绿，但不替代三modeler实际失败/真provider交错；后续Root/C3/U2真实调用点与独立最终复审。 |
| E04 | UI3 B1真正软件事务/详细默认门和B2-P1publication已自动/独立GREEN；Draw3 U1有限Session/U2-P1内容helper已红绿/复审。UI3真实Submit/Advance/资源/source和Draw3真实Run/Present/Host接线及完整三轮数据仍实施；HC/H2同机尚无。 | 新量化helper只证明合同；完整场景成功因果/median/P95/样本量/资源、Move/Up与Cold/Warm/长期边界仍要给实际数据，不把helper通过等同流畅度。 |
| E05 | 提交后状态、阶段 commit、交接入口和四个生成物清理本轮已完成；E01–E04 结束后仍需最终报告、HF 和独立复审更新。 | 当前 commit/tree 与提交后工作区指纹、任务 validate；之后最终报告必须对应最后修补与复验。 |

Host 的正常退出允许按原合同 drain 已接受保存请求；当 Armed/FallbackArmed 已建立时，进程级监督提供 15 秒边界。因此不能把所有 CloseAndDrain 无内部 timeout 都认定为必现死锁。E01 是已确认的双重监督失败缺口；E02 是启动清理的边界与动态验证缺口，未有永久阻塞复现。现场截图没有线程栈，尚不能认定某一已修问题就是该次画布卡死的唯一根因。

IDT_RELEASE 目前注释是 H0 已有的开发状态。正式发布前打开宏并跑官方 CI 是维护者发布准备，不属于新发现的产品 bug。本任务不自行更改版本/渠道。HTTP/HTTPS 更新、HTTP 回退、旧智绘教.exe 启动兼容、旧 page-index 文件保留及未开放功能 gate 是用户已确认决策，不作为待修缺陷。
