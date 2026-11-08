# 候选1保存快照释放调查：独立审查

2026-11-05。范围以 `research/candidate1-investigation-boundary.md` 为准；审查起点为 `e225c210a64be114ed9d83709ac0978d80feedc5`。本轮仅调查并增加共享CPU测试，不实施保存去重、COW或候选6。

质量门通过：未发现需要修复的确定问题。审查者未修改产品或测试源码，未重复构建、运行测试或启动窗口；运行结果来自主会话保存的原始日志，以下另列独立静态与数据核对。

## Findings (fixed)

- 本次审查无新增自修。
- 首轮构建的 `presentation_autosave_tests.cpp` helper 误放在函数体内，首个有效错误为C2601；主会话已在审查前移回匿名命名空间。已核对最终作用域、成功编译/链接及后续运行，首次失败日志保留于 `build-shared-attempt1.*`。这是已修复的测试源码错误，不是环境错误，也未修改工程配置绕过。

## Findings (not fixed)

无待修复的确定缺陷或未决产品设计问题。以下为本次结论的覆盖范围，不能作为全进程泄漏排除证明：

- 地址探针观察已登记的原始canvases、strokes、operations和points分配。没有观察全部中间副本、extra字符串、编码器直接malloc、全部对齐分配或OS/GPU载荷；这些局部中间对象依赖生产源码RAII审计。没有全堆快照、ETW分配栈或Private/WS回落实测。
- 新增Whiteboard地址探针覆盖成功与文件系统失败；Whiteboard加载完成文档、排队替换/拒绝和停止的完整地址矩阵未新增实测。报告已明确白板Load仅静态审计，不把Desktop/PPT的最后owner结果外推到该路径。
- Desktop/PPT停止场景在 `CloseAndDrain()` 返回后判断释放；其余正常保存探针在服务/worker仍存活、Save回执尚未领取时判断。成功Load回执、失败Clear恢复点、Redo/retained业务内容和元数据仍可合法持有。
- 观察器是单个串行Scope、登记后转移所有权的测试工具，不是可跨任意Scope或并发登记生产数据的通用堆分析器。现有用例在Scope退出前排空/join对应worker，未依赖不受支持的嵌套或跨代交接。

## 静态审查

### 观察器与并发

- `contact_input_tests.cpp` 的观察表是256槽静态POD数组、SRWLOCK、原子开关与标量；默认关闭，没有动态分配或需要先销毁的容器。静态析构期间未留下依赖已析构map/互斥对象的delete路径。
- 原 `operator new/new[]`、累计分配次数、线程局部失败注入和malloc策略保持不变；四个原有普通/sized delete入口只委托同一个Free。关闭时直接调用原std::free。
- Watch/Read/Free的表访问受同一锁保护；Free在锁内调用std::free，返回后才标记地址释放。记录uintptr值，不读取已释放内存；已释放槽保留完成证据，同地址新登记追加新槽而不复活旧记录。新地址登记无法越过该free与发布区间造成错误归代。
- 空地址、重复活地址、容量耗尽、非法range都使overflow可见，Released拒绝通过。固定容量和无自分配避免全局delete观察器递归分配；实际日志全部overflow=0。
- Scope声明早于Gate和service，按逆序先销毁/join服务，再关闭事件，最后关闭观察器；显式正常尾路径也先CloseAndDrain。PPT测试hook只在worker join后清理，借用Gate没有悬空。

### 真实所有权与事件门

- Desktop只保留小型seed，放大后的request在登记后move给真实 `SubmitPrepared`；生产服务再move进Record。wake门位于完成队列发布之后、同一轮 `completedSnapshot` 析构之前，确实证明worker曾持有全部登记原缓冲。放行后另等实际free，不把pending/queuedSnapshotBytes归零当析构门。
- PPT登记active/legacy两份原始点数组，move给真实 `SubmitSave`；既有WorkerOperation hook发生在真实queue→working之后、保存处理之前。替换用例在首个请求被事件阻塞时提交第二/最新请求，验证ReplacedPending立即释放第二份，并保留首份/最新份直到放行；终态释放在未取回执时观察。
- Desktop/PPT Load取真实生产返回值；消费端保留强引用时要求weak未过期、登记数组未全部释放，清空回执后reset最后consumer，再要求weak过期且全部登记free。fixture没有额外大快照副本充当假owner。
- Whiteboard调用真实Store/序列化/文件系统路径，request move后不留副本。失败由独有临时目录下的同名文件阻断真实目录创建；Save回执不得带document。在Store仍存活时，Drain之后再有界等待真实free，吸收busy=false先于request析构的间隔。
- clear负控先clear真实1MiB vector，要求尚未释放；再与空vector交换归还容量并要求释放。它验证观察的是分配寿命，而不是size或账面估算。

### 入口、范围与规格

- 新共享CLI在wmain最前识别 `--save-snapshot-lifetime-only`，要求argc恰为2；多余参数返回90。成功路径只调用CPU模型、保存服务和临时文件测试，在默认窗口/GPU测试前返回，不新增产品CLI。
- `git diff --ignore-submodules=all --exit-code e225c210 -- Inkeys inkStrokeModelerTest inkStrokeModelerTestTests/inkStrokeModelerTestTests.vcxproj inkStrokeModelerTest.sln InkeysRepo.sln` 无产品/工程差异。候选6、L0/L1/L2、UI共享WARP与Draw3独立hardware-first/fallback未变，生产绘制/输入热路径未新增分配、同步或解码。
- 三个既有cpp与新header均为UTF-8 BOM、CRLF、无裸LF；三个旧文件的BOM与Git基线一致。diff局限于观察器/入口/用例与任务规格，无整文件格式化。
- 已核对更新的资源规格及 `result-candidate1.md`：区分Save终态、合法Load回执、Clear恢复点、元数据和allocator/OS高水位。Desktop记录公式 `202+2L` 是可见文本字段账，PPT拓扑40000字节是10000个int32的元素载荷；均未冒充实测Private。保存峰值去重建议后置与当前常驻目标一致。

## Verification

- Lint：通过本轮适用的 `git diff --ignore-submodules=all --check e225c210`、源码格式与最小范围检查。未配置独立C++ lint任务，不声称执行额外静态分析器；Git提示文档LF将按仓库规则转CRLF不构成check失败。受限子模块目录的默认Git扫描曾拒绝访问，改用ignore-submodules完成本轮非子模块范围检查，未改权限或子模块内容。
- TypeCheck / C++编译：共享 `inkStrokeModelerTest.sln Debug|ARM64` 最终退出0，见 `build-shared.result.json`。原生ARM64 MSBuild动态定位，调用内规范化PATH并禁用node reuse；预算1200秒。四shader在attempt1成功，最终增量链成功。既有C4819/C4244/C4305警告未作无关修改。本轮产品未变，不重复构建InkeysRepo。
- 新探针：`lifetime/test-results.json` 退出0、32.047秒、未超时。独立从原始stdout解析出17条观测，与 `lifetime-summary.json` 逐项一致；16个服务阶段加1个clear负控，全部登记buffers与bytes均释放，overflow=0，failures=0。
- 最终同一测试二进制的5项受影响CPU回归全部退出0、未超时：Desktop AutoSave、Presentation AutoSave、UInk file、UInk presentation、release-tail，见 `regression/test-results.json`。坏argc独立检查退出90，见 `bad-argc.result.json`。
- 修改前基线：产品4项与共享保存2项均通过，分别见 `baseline/test-results-parked-desktop-exit-parked-ppt-retained-loaded-retained-install-whiteboard-storage.json` 与 `shared-baseline/test-results.json`。它们是此前二进制的基线，未计作新探针结果，也未把含离屏WARP的whiteboard storage称为纯CPU。
- 身份：独立计算当前4份测试源码及EXE SHA-256，均匹配 `shared-probe-identity.json`；最终EXE为 `2fdcd9e2ac37ea8201598672657e8196c9d568206fad6ae23d1429ca586af797`。记录的EXE CodeView/PDB GUID一致、Age2，ARM64，配置为Debug、/MT NDEBUG；新探针、5项回归与坏argc的记录均为此EXE。

本次审查不要求额外GUI、Office、ETW或流畅度实测。没有以局部释放字节充当节省量，没有强制修剪工作集，没有commit/push。
