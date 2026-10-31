# 实施计划（已批准，2026-10-18开始执行）

## 0. 当前门禁

- [x] 用户已授权创建任务和调查规划。
- [x] 已创建 planning 任务并写入 PRD。
- [x] 用户审阅并批准 design.md + 本计划，并确认HWND是已测试过的包装器例外。
- [x] 已获产品、规范和同次DLL/TLB修复授权，执行task.py start进入实施；原有GUI/commit/push限制仍生效。
- [ ] GUI/Office启动验收另行获授权；commit/push不自动授权。

这是一个连贯的 COM 边界修复任务，不拆成可独立发布的无关子任务；研究按版本、所有权和会话分工，实施按下面依赖顺序进行。

## 1. 冻结基线与红测试

- [x] 记录 HEAD、用户已有dirty文件、源文件编码/BOM/换行和当前 DLL/TLB/EXE资源哈希；不覆盖用户已有修改。
- [x] 读取本任务研究和最新版规范，应用 PRD 中用户要求对旧 InvokeMember 条款的覆盖。
- [x] 把前轮 busy/缺HWND 复现加入正式测试，并补正常/事件不可用、重开旧窗口仍可读、多候选释放等红例。
- [x] 用生产适配器的 dynamic-only对象和隔离 IDispatch COM fixture 验证 QI失败、ITypeInfo失败、GetIDsOfNames/Invoke 可用时的实际 .NET4 行为；仅普通 FakeGraph 不够。
- [x] 检查随附 PIA 元数据和可得真实提供方的 HWND dynamic/typed 差异，记录受限制成员的例外依据；不激活未授权Office。
- [x] 对 Win32候选关联做最小无窗口测试，冻结强/弱证据边界，并给现有分页控件补基础控制状态发布的红测试。新增 wire schema、额外窗口或更广UI重做均需回到 planning 提审；本设计已包含现有控件的基础控制/墨迹页码局部分离。

## 2. dynamic 访问与逐笔所有权

- [x] 将常规生产 getter/item/method 改为显式 dynamic，删除这些成员的反射默认路径；保留内部可注入测试边界。
- [x] 仅按 design 的例外清单保留事件包装与有证据的 HWND 标量路径；busy 不触发重复有副作用调用。
- [x] 修复 ROT candidate/bestApp/去重列表所有权：同一 acquisition 仅转移或释放一次，重复 identity 的另一次取得仍需配平。
- [x] 修复比较 helper、轮询和 window安装/替换的临时引用转移；分清 alias 与本次新取得。
- [x] 将 Next/Previous/End/View/annotation 等调用中的临时 View/Navigation 拆到具名局部并 finally 释放。
- [x] 检查 reader 每个失败点和 loop 项恰好释放，禁止链式/foreach/临时 FinalRelease。
- [x] 运行相关 ledger 和生产adapter测试；没有新增差异/失败则不反复扩大测试。

## 3. owner、事件与轮询

- [x] 复用既有 owner/mailbox 串行化访问长期root的业务命令；纯值getter不访问Office。
- [x] 把清理、重绑、请求过期和在途调用配合收束；锁外COM/解绑/释放，避免callback/owner自等待。
- [x] 事件按订阅项记账，部分失败回滚；取消一个失败仍清理其余项，进入独立polling。
- [x] Begin/End/Page callback只发轻量提示；不再用借用WnObj无所有权地覆盖长期window。
- [x] 无事件时按成功取得的当前show identity更新window；旧对象还能读Active不代表仍是当前放映。showCount=0明确结束后废弃旧window，重开推进场次。
- [x] 检查快速A→B→A、同窗口重开、事件迟到、owner停止时排队请求和末页Next(check)保护。
- [x] 去掉用GC/终结器等待来补漏或证明正常退出的依赖，所有清理依赖账本和使用结束。旧WPS若另需特例先补证据并提审。

## 4. busy与窗口回退的跨层修复

- [x] 统一 legacy/session 快照的业务语义，区分旧稳定显示与当前页可写证据；不把旧stable直接Publish成新Valid。
- [x] 增加独立有界busy恢复调度，不受稳定3秒复核deadline阻挡；下一次尝试默认不晚于一轮500ms维护调度，COM调用本身阻塞单列。
- [x] 同场不确定时保留旧画面但暂停新写入；fresh当前页/代次证据足够后恢复；新binding/session首次失败保持隔离。
- [x] 先建立可信COM活动场次，再单独判定窗口/墨迹能力；HWND0不得由IsWindow误判退出。保留现有分页控件的基本命令与独立基础页码，不依赖Draw3 ready/完整拓扑。
- [x] 在Ppt模块显式区分基础控制页码和已呈现墨迹页码，基础模式不推进PptInfoStateBuffer、不发送pagePresented/UI-ready、不复用旧Presentation墨迹。主要范围为IdtPlug-in、PptSession、UI/Ppt的窄状态发布，避免改渲染器和另建UI。
- [x] 无HWND的退出只在owner校验捕获的COM show/session/generation后执行；未知窗口不抢焦点、不猜测OS激活目标。
- [x] HWND解析统一供descriptor/session/旧GetPptHwnd使用，接入有证据的typed例外及受验证Win32后备。
- [x] 拒绝仅标题匹配、编辑窗口、前台误用、多实例/同名文稿冲突、已销毁/复用窗口；无法建立可靠身份时明确降级，不用假PID/HWND放开墨迹门。
- [x] 保持 page target→成功Present→对应UI ack 的输入门禁与文稿/SlideID/ordinal存储隔离。
- [x] 添加必要低频数值诊断，记录访问路径/事件或轮询/失败能力/原始HRESULT；不输出路径、文稿标题或墨迹。

## 5. 规范与产物

- [x] 适量中文注释说明 dynamic例外、每笔所有权转移、事件回滚和busy/未知边界。
- [x] 更新 ppt-interop/com-contract.md、native-session-ui3.md、index.md；必要时更新 cross-layer COM 检查。只更新当前合同，不改历史任务的研究结论。
- [x] 保持 .NET4、PIA/NuGet、GUID、vtable和既有JSON schema；不改无关sln/vcxproj/CI。
- [x] 生成同次PptCOM.dll/TLB，显式同步Inkeys所用资源和#import位置；核对类型库接口及最终EXE嵌入DLL哈希。
- [x] 全量检查本任务diff，保持已有编码/换行；不顺手格式化或重构其他层。

## 6. 最小充分自动化验证

构建前确认当前仓库的 MSBuild solution 及工具链，使用现有配置。每次 MSBuild 在同一个 PowerShell invocation 中先执行用户规定的环境处理：

~~~powershell
Remove-Item Env:PATH -ErrorAction SilentlyContinue
$env:MSBUILDDISABLENODEREUSE = '1'
# 通过当前 VS 安装/vswhere 定位 ARM64 原生 MSBuild.exe，不能硬编码 VS 版本。
# 同 invocation 执行后续 MSBuild，/nr:false，日志和真实退出码均保留。
~~~

- [x] 托管范围：PptCOM.csproj Release|AnyCPU 与 PptCOM.Tests，检查进程退出码、生产dynamic适配器和逐异常点ledger。
- [x] 托管产物显式交接后：InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /p:PreferredToolArchitecture=arm64 /nr:false。不能单独构建Inkeys.vcxproj冒充成功。
- [x] 每个完整solution构建至少允许5分钟，拟用900秒监督超时；长时间无输出不当卡死，超时报告阶段和已有日志，不重复重启同一构建。
- [x] 跑本次相关managed/native纯逻辑/--no-window；不运行会创建窗口的harness或主程序。
- [x] ABI/旧接口顺序、JSON schema、DLL/TLB与嵌入资源检查。
- [x] git diff --check、文件编码/BOM/CRLF检查及仅任务范围dirty清单。
- [x] 独立 trellis-check 审核整个修复范围及测试覆盖，实施者/检查者不在本轮规划阶段提前运行。
- [x] 首个有意义错误先分类；环境/PATH/SDK/缺本地依赖问题明确报告，不改源码/项目绕过。

## 7. 提供方/设备验收（单列，不以mock替代）

每次记录提供方、完整build、位数/安装方式、Windows/架构、事件/轮询模式、访问路径与产物哈希。

- [ ] PowerPoint2010/MSO14：可得x86/x64，核心dynamic属性/命令、事件/轮询、页画布、关闭应用及下一文稿。
- [ ] WPP/WPS2013：可得位数，尤其无PIA/事件能力下的轮询、工具可选属性和进程释放。
- [ ] PowerPoint2007：保留旧声明的核心能力回归；新可选功能不可用不阻断核心。
- [ ] 现代Office对照，确认未回归当前设备上已可用的功能。
- [ ] 类型接口/事件异常但dynamic仍可用：隔离夹具和有条件的真实反馈设备；不修改全局注册表模拟损坏。
- [ ] 对自有测试进程/文稿执行关闭与再开；记录可控引用/事件连接清空、进程退出及下一文稿成功，不调用强制GC、killOffice或等待自然回收使测试通过。
- [ ] 有其他文稿/用户Office进程时，后台仍存在不自动判定泄漏；不自动接管、Quit或结束用户已有会话。
- [x] 若旧版/损坏设备不可得，逐项NOT VERIFIED，兼容发布验收保持待办，由用户决定补测或显式接受剩余风险；不能标全部兼容通过。

## 8. 收尾

- [x] 汇总已修机制、实际测试和未验证设备，明确代码错误与环境限制。
- [x] 核对PRD全部AC；不因为编译通过或fake通过就关闭真实兼容/退出验收。
- [x] 未有本任务明确授权则不commit/push；不自动归档未验收任务。


## 本轮执行结果

- 代码与无窗口自动化验证完成；托管x64/ARM64各43个真实COM cases通过，完整Debug|ARM64 solution及headless通过。
- 最终证据、环境对照、产物哈希和未验证设备见research/verification.md；独立审查见research/implementation-check.md。
- 第7节实际提供方/关闭下一文稿验收仍待完成，PRD AC10保持未勾选；2026-10-18用户追加授权commit并结束任务，按该指示提交和归档，保留NOT VERIFIED；不push。
