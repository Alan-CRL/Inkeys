# 后续阶段本轮执行记录

基线feature/move / 3ace726b。用户批准全部后续实施；新测试只能无窗口或严格不可见HWND，禁止Computer Use、可见窗口/图案、前台和置顶测试。无新commit/push授权。具体见implementation-authorization.md。

## 执行顺序

1. 正式取帧：复用原Mag与MagnifierThread串行更新/读回、异步状态/监督/Stop/Freeze互斥、质量和元数据、无窗口测试、顺序check。
2. VisualMotionTracker：纯图像/OpenCV、可信平移与特征接力、离线序列与误差、顺序check。
3. 产品接入：ScrollFollow会话、Draw3绝对目标邮箱/结束屏障、输入/辅助呈现交接、More按钮/i18n/SVG、生命周期停止、无窗口或隐藏HWND验证、顺序check。
4. 保存回归：A->follow->B->编辑历史->保存重载的定向验证，既有Draw3/UInk/形状/定格/白板/PPT相关可执行检查，ARM64完整solution与x64尝试、i18n/PE/编码，顺序check。
5. 必要可见验收：新正式路径真实变化内容、桌面穿透和滚动墨迹、浏览器/Word/PDF/惯性/工具交接。当前未授权，若成为剩余必要步骤则停止并报告，不标全任务完成。

## 本轮起始状态（历史）

- 正式取帧实施中；线程方案已核准：原初始化/创建owner与readyPromise不变，原MagnifierThread串行执行采样/PW与Freeze，UI不等待PW。
- Draw3/Bar/持久化和Tracker正在只读调查，为后续顺序实现准备；无生产代码并行冲突。
- 上轮原Mag owner读回像素通过仅作基础能力证据，本轮新协调线程读回与完整功能需另作验收。

## 正式取帧已完成实现与check

FreezeCapture Start/Stop/GetSnapshot已接原MagnifierThread，普通Mag初始化/创建/readyPromise不变。无callback正式路径不注册回调。审查补就绪atomic与监督停止通知同步，最终完整ARM64构建29.32s exit0、定向exit0、完整--no-window 109923ms exit0。实际transform identity核验，映射1:1，不按裁切宽比缩放。详捕获子任务production-capture与production-capture-check。原异步生产读回实屏/性能/异常系统销毁未验收，非软件证据的剩余项保留到必要可见门槛。现在进入独立纯离线Tracker，不宣称产品真实屏幕验收完成。

## 全隐藏实际Host基线

在Tracker实施期间复用现有严格早退 --draw3-host-metrics-smoke，正常宿主运行exit0，约1.63s；真实WindowService/Host默认输出、五次start/stop、实际contact与present及sealed metrics通过，所有自建HWND全程visible=0、topmost=0，无Computer Use和用户数据读写。私有日志Build/ScrollFollow/hidden-baseline-8aa93957a1724173bbe29945f6cc2636/{stdout,stderr}.log。此证据只证明允许的全隐藏产品测试可执行，不等于follow或实屏验收。

## Tracker实施完成，待独立check

VisualMotionTracker生产API与严格离线矩阵freeze。完整ARM64最终build exit0/17.61s/0warning0error；最终--no-window真实exit0/157453ms/stderr0B，含6918合法帧与全部负例，末尾PASS animation correctness。所有合法prefix严格误差通过，点数<=320，具体参数/逐序列数字/代码和exe哈希见Tracker research/implementation-current.md。本轮没有实屏操作。接下来独立trellis-check，完成后再进入产品接入。

## Tracker独立审查发现并修复中

审查扩充majority-fixed中央小区域运动1/2/4/10原图px。4px被误接受零位移，6918合法帧仍过；根因为次流候选>2*1.5处理px才检查，2处理px次流被跳过。主会话批准零主模式独立>1.5候选+原15%空间/6中格的局部修复，不改普通runner-up门或严格精度。1/2原图px落原1.5处理px合并半径，作为当前分辨边界记录，不扩张新的多模态算法。修复后重跑完整ARM64/定向/--no-window，再进入产品。具体证据/修订由Tracker check-current记录。

## Tracker检查通过并进入产品

Tracker检查最终完整ARM64 build真实exit0/18.99s(外部19470ms)/0warning0error；target exit0/90771ms；full --no-window exit0/153486ms/stderr0B；7文件hash未变，原6918合法诊断逐项一致。4/10px误接受修复，1/2px合并边界明示。具体见Tracker check-current.md。现在开始产品代码与严格隐藏验证；完整实屏Capture/穿透仍未验收，不标父任务完成。

## 产品实施中与共享存储基线

Core统一Business/Draw3/IdtState/Bar交互/CLI/工程，资源实现者独立Config/enum/SVG/rc/i18n；严格不可见测试边界不变。Business接口已定稿RequestToggle/RequestStop/GetSnapshot/Available和强类型ActionTicket/hold等；实际实现由产品报告记录。

主会话同时用既有inkStrokeModelerTest.sln Debug ARM64完整构建共享storage基线exit0/55.10s(23既有warning/0error)，没有启动demo/benchmark。纯CLI在各自私有TEMP：--uink-file-only exit0/100433ms(包含page-file)，--uink-presentation-only exit0/149ms，--desktop-autosave-only exit0/2135ms。--presentation-autosave-only原长TEMP约束导致大量失败；改为短48字符Build/sfp-94de65ce后exit1/3654ms，仅剩independentlySaved Committed+SlideIdSession断言；原因尚在只读调查，不能先归为当前feature或环境。日志Build/ScrollFollow/storage-baseline-*。不为独立PPT基线问题改生产代码。


## 共享存储基线已全部通过

只读研究确认Presentation fixture在TEMP之后还追加深层session路径，48字符TEMP使最终路径达到265字符。相同二进制、相同--presentation-autosave-only，改用39字符私有TEMP后exit0/3537ms，stdout/stderr均0B。未经采集的原失败completion只作源码推断，不能报成已测GetLastError。四组既有storage CLI现在全部通过；相关legacy工程/实现/tests相对HEAD diff为空，无源码修复。证据见产品research/storage-baseline-current.md。后续legacy PPT CLI继续使用<=40字符新建私有TEMP，不修改系统或生产路径策略。

## 产品第二轮编译与定向检查

完整InkeysRepo.sln Debug ARM64第二轮exit0/91.4s，--scroll-follow-protocol-test、--bar-view-adjust-config-test、--state-mode-production-test均exit0。真实全隐藏Host首跑在Start安全拒绝；补原WindowService要求的MagHost/Freeze隐藏角色后进行第三轮构建。最终跨帧End、原动作发布receipt与精确窗口ACK仍在验证，尚未冻结或声称产品check通过。


## 产品第四轮全隐藏Host通过

第三轮C++编译已过但link.exe的IncrBuildImage出现C0000005/LNK1000；只删除已核准workspace生成Inkeys.ilk后按原工程重链，第四轮完整Solution exit0/73.5s。没有修改依赖/工具链/源码绕过链接器。新--scroll-follow-hidden-test真实ULW Host与RTS通过A起笔、被gate拒绝Down/真Up回收、latest200、End后原Undo FIFO阻挡、base-total/迟到拒绝、原输出ready及shutdown最后CPU target。End经历14帧，其中13帧仍在恢复；所有创建/销毁HWND visible=0。实际窗口显隐ACK用严格seam，不等于系统穿透已测。Core尚在补最终严格argv和投影回归，冻结后独立check。


## 产品独立check补齐Business证据与跨Host代问题

Core源码冻结26.661s exit0及报告已交独立trellis-check；full --no-window尚未跑，由最终check统一执行。主会话批准文件内harness-only最小外部依赖/clock接缝，直接运行同一个Service::Tick/Frame和真实Tracker，补F0/ACK前状态、初始化重试/失败回滚、严重与普通拒绝Pause、票据取消/退休及Shutdown软件矩阵；外部Capture/Host/Window是seam，真实Host另有全隐藏证据。

主会话读Host/Business发现跨Host旧hold风险，checker独立确认：Start重建follow并清hold没有保护门，旧Context/action release可递减新代hold。批准最小修复：Start command→presentation保护重置和Channel复制；Begin/End/Hold携带可选expected Channel，Business持旧所有者凭据，Host同门不匹配拒绝。补实际全隐藏Host重启旧End/hold/Begin不能干扰新代用例。修复/验证仍在进行，不提前标check通过。


## 产品独立check通过，保存阶段启动

产品check-current.md完整记录两项修复（跨Host通道/hold；SetOffSignal先NoWait/先Arm）和实际Tick/Frame/Tracker控制矩阵。最后完整ARM64 solution exit0/15.332s，四定向通过；唯一full --no-window exit0/157.972s，stderr35B仅protocol failures=0；原8项无GUI失败接管exit0/95.773s。46文本/4XML/diff/i18n426检查通过，未新增已确认源码缺口。新expected Channel合同已同步spec。当前实际Mag/系统ACK/Bar可见尚未验收。

保存子任务已validate/start in_progress，新的implement agent统一拥有真实私有文件/编辑测试和必要hidden有限receipt，源码预期不改原保存/历史/格式。以产品check-current及viewport-only-save-current为真实前置，不新增冷载功能、不复制已有seq/跨代专测。完整x64/最终PE由主会话等最终源码统筹，期间不并发MSBuild。


## 保存实施冻结并进入独立check

最终完整ARM64 solution exit0/7.926s（原配置重链），新--scroll-follow-persistence-test四场景Shape/Pen viewport-only/edit exit0/5.237s，共8个实际UInk。每个场景确认Clear Committed被Controller消费、CPUfallback释放后，Undo真实worker读取并安装A（diskInstalled=1、memoryRestored=0）；A所有点/宽/style/GUID不变，最终viewport(144,-64,1)，B真实Down(80,100)锁存新视口得到(224,36)。Pen实际23/22点，不以2点Shape代普通笔。viewport-only最后Publish立即Stop、不预等Applied。mask实际文件几何正覆盖而非屏幕像素证明；7badargv全exit2/0B，9文本/2XML/diff通过。详细见保存research/implementation-current.md。源码冻结后已交原产品checker独立审查本阶段及全范围交界；主会话随后串行x64与PE，不并发MSBuild。正式Mag/系统ACK/实屏仍为必要未测门槛。


## 最终软件验收完成，停在必要现场门槛

保存独立check仅加强equalStroke零偏移A/codec逐值精确比较，无生产逻辑变化；最终完整ARM64 exit0/8.143s，四场景exit0/4.194s、8真UInk、Pen23/23点；protocol/Business/state/config/隐藏Host/shape/capture短回归及badargv通过。源码冻结后主会话串行完整x64：首次109.839s在Headless生成PDB损坏LNK1285失败，主程序已成功；核实普通/ignored/非reparse工作区PDB后同目录留档，同配置solution重建exit0/6.791s，五项x64纯CLI通过。两架构PE确认Mag延迟加载、无OpenCV/WGC动态依赖，版本查询动态，无扩旧系统已测承诺。详细见final-build-and-pe.md。四阶段软件check均已结束；规范、manifest、总矩阵与用法已更新。当前只剩正式Mag连续像素/排除/FPS与系统窗口/实际内容/硬件现场门槛，按本轮限制停止，不Computer Use/显示/前置窗口、不归档全任务、不commit/push。


## 2026-10-22 本地提交授权，保留任务

用户要求“为改动提交 commit，不结束任务，后面再开始进行测试”。本次仅提交既有软件实现、规范及验证记录，不启动后续测试、不push、不调用finish/archive；父任务和当前保存子任务保持in_progress，正式Mag/系统窗口/现场验收仍待后续。此授权仅适用于本次本地提交。
