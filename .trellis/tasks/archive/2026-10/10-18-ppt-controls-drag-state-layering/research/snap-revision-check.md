# 吸附修订独立审查

任务：10-18-ppt-controls-drag-state-layering；日期：2026-10-18。

## 阶段 A：生产实现期间只读审查

本阶段按主会话要求只核对新合同、三份 canonical spec 与已冻结 page_control_tests.cpp，不审核仍在写入的 PageControl.cpp/.cppm、不修改任何生产/测试/spec、不构建或运行二进制。本记录为本角色唯一写入。

已加载最新 check.jsonl 六份上下文与 prd/design/implement、snap-revision-design.md、snap-revision-tests.md。cpp-conventions.md、drag-hang-locks.md、page-state-and-layering.md 当前内容与本角色此前完整读取的 hook 保存内容一致，程序逐字比对为 true；其余修订内容已重新完整读取。当前20DIP双门槛与140ms过渡覆盖历史40DIP方案，旧研究/首轮验证不作为本次通过证据。

## Findings (fixed)

无。本阶段未发现需要修改冻结测试或当前合同的本地机械问题。

## Findings (not fixed)

当前合同、spec 与冻结 headless 测试未发现阻断项。生产实现尚在进行，下面结论不代表实际动画、锁和调用接线已审查通过。

### 合同与规范一致性

- 同时要求 abs(middlePairHeight)<=20*归一化DPI、middlePairWidth<=20*归一化DPI，含等号；复用 BarBottomDockThresholdDip，不修改主栏自身20/40常量或行为。
- middlePairWidth 已是扣除 PageControlGapDip*实际controlScale 后的额外侧距，不从 HWND/presentation/shadow 再扣间隙；只将吸附Y设零，X保留。
- 最新spec将普通拖动直接落定与140ms吸附Y过渡明确区分；中间直移只发布过期帧屏障并续帧，不能提前提交最终pair。两面同started、X及时、重复Move/页码/Up不重启、同目标结束不重播、pending真实到位后提交均有明确条款。
- 真正Cancel/新手势/新session/epoch/模式或显示布局失效可打断；Up后无活动pointer的旧CaptureChanged不得清退released pending。捕获锁外、D→S、raw/fit身份、COM与第4项层级边界仍保留。

### 冻结测试覆盖与夹具有效性

- page_control_tests.cpp:616–675 的1152组合数量正确：2屏幕×3 DPI×4倍率×2侧×2个Y符号×3个Y阈值×4个侧距。19.9/20/20.1和远侧距120覆盖AND/包含边界，当前大屏尺寸不会把这些原始值意外Clamp进阈值；DPI与控件倍率独立。
- :655–661 通过真正ResolveSurfaceLayout得到logical RECT，分别减去已缩放并舍入的既有gap，核验额外侧距等于candidate.middlePairWidth，能识别把gap/shadow算入门槛或重复扣除。
- :678–690 的200px高屏幕会将Y裁剪到12.5px，确实进入20DIP带；默认X=0也满足侧边条件。底部pair反例改为19px后仍处阈值内而不得吸附；当前scale0.5/旧candidate scale3的496.25px裁剪回归保持。
- :718–746 的中心碰撞夹具确实满足双阈值（X=19、Y=±19、DPI1）。按既有165×42.5DIP主体、5DIP间隙及middle scale0.5，中心middle约Y498..581；bottom height450时约Y582..625，中心在5px gap内但上移19后可行；bottom height577时约Y455..498，中心在gap内但下移19后可行。因此centerBlocked覆盖确实来自中心碰撞，不是未达到吸附前提。
- :784–802 的bottom height592夹具约Y440..483：raw middle上移19后Y479..562相交，而center Y498..581与其保持gap，覆盖raw不可行但center可行及另一组新publication/身份优先。
- 这些测试直接调用生产纯策略和布局，不复制另一份实现；不将1152策略组合说成cpp私有动画、两HWND同步或USER32捕获已经执行。

## Verification（本阶段）

- Lint：选定冻结测试与三份spec的 git diff --check，exit 0。
- Format：page_control_tests.cpp UTF-8无BOM，1455个LF全部为CRLF；未改文件。
- TypeCheck / Tests：未运行，主会话明确要求生产冻结后统一执行；首轮solution/headless/offscreen exit0不代表本次修订通过。
- GUI/Office、真实鼠标/触摸与USER32：NOT VERIFIED，未启动。

## 生产冻结后待审

- 140ms进度与pair身份是否独立于不断变化的candidate revision，连续X采样/页码/Up不反复重启；两面共享started，结束后不重播。
- X直移、Y中间直移和最终候选geometry/commit是否严格分开，旧frame不能回拉；try_lock/窗口失败仍有显式续帧。
- 真正Cancel、released Up的CaptureChanged、离开X/Y任一门槛、新手势与session/epoch/显示或设备布局变化的清退和打断是否实际接线。
- 真实内部离屏用例是否检查AnimatedBounds/Scene的中间Y、两面同时间、多帧稳定和raw/fit连续手势，而非只检查布尔策略或tracker字段。

## 阶段 B：完整源码、测试与接线审查

已读取全部当前 tracked git diff（PageControl.cpp/.cppm、page_control_tests.cpp、三份canonical），及snap-revision-implementation.md和新增真实离屏用例。原R1 raw/fit身份修复、普通拖动立即落定、Up最终采样、捕获锁外与页码门禁保留。

### Findings (fixed)：R2 批次时钟回退

- File: PageControl.cpp 的 ResolveMiddleSnapBounds（现约912行）；调度依据为 RenderPipeline.cpp:738、793、807、837。
- Issue: owner用真实sampleTime推进吸附Y，而renderer使用整批固定frameTime。若同批先执行其他client，owner可在t60成功直移并发布最新revision，PPT callback随后才获取该新快照，但仍用批次t40算进度；所有revision门禁均可通过，实际Y仍会回退到更早位置。这是可达的调用时序，不是故障设备已复现结论。
- Fix: 主会话认可后由原生产责任代理最小补正。MiddleSnapState增加由T保护的pair共享sampledAt，新吸附和started同时初始化；Resolve按max(now,sampledAt)推进，再相对不变的started计算原cubic曲线。两面共享单调时刻，不新增锁/线程，140ms截止点不延长。
- Regression: 真实内部Scene用例先owner t60更新最新X与Y，再以同一最新revision无条件执行旧batch t40 Configure两面，再跑t80。断言Y均不低于owner t60、X最新、revision未替换、sampledAt=t80且started未变；随后Up85和原140ms终点继续覆盖。这个用例没有靠跳过旧版本掩盖时钟问题。

### 其余完整审查结论

- 双20屏幕DIP策略与1152二维组合一致；Clamp使用当前拟合倍率，既有间隙只计一次，X不归零，中心碰撞保留可行自由候选。
- MiddleSnapState实际仅由T保护；未增加D→T、S→D等反向锁序。Snapshot仍D→S采raw身份，runtime fit用于目标和实际几何；原R1的released pending二次Down/零Move Up用例完整保留。
- 两侧实际Y来自同一started/sampleAt，目标X即时更新；带内新candidate revision只关联同一动画，不重启；页码publication与合法Up同样不重启。结束后latched防重播，离带恢复直接自由位置。
- 中间直移顺序两HWND成功后才写两个bounds，发布directMoveRevision屏障并显式RequestPair，保持pending mask；两面最终几何才Mark。renderer实际logical/presentation/scale对本帧fit目标的证明仍在成功Present/Window提交和第二次版本门后消费。
- P只try_lock，锁忙有真实另一线程注入；第二面失败的0→1→0回滚使用非零X变化，不把空移动当回滚。单面最终成功仍pending，第二面成功才完成；未增加inputLocked来源。
- 活动Cancel回滚到最后committed位置；新Down先清旧吸附；new session/epoch/layout通过已有身份和目标复核失效。Up清pointer后同步旧CaptureChanged保留released pending，与活动Cancel分开。第4项owner/TOPMOST和COM/DLL/TLB不改。
- 新真实offscreen实际执行生产Begin/Update/Finish/Configure、AnimatedBounds/Scene与提交仲裁，覆盖中间Y/同进度/单调/终点/取消/新手势/场次/资源预算身份；操作HWND和capture仍只在原接缝注入。既有headless只编cppm，两者必须分列运行。

### Findings (not fixed)

R2补正复核后没有遗留阻断项或本轮范围内应修未修的源码缺陷。实际鼠标/触摸、USER32捕获重入、Office/WPP及用户局部停住现场仍NOT VERIFIED；不能把纯策略/无HWND注入说成设备验收通过。

### 当前 Verification 与冻结结论

- Lint：最终源码补正后的git diff --check，exit0。
- TypeCheck / Tests：本次修订尚未执行，等待主会话统一完整solution、--no-window、--bar-eraser-offscreen-test的新结果；首轮旧日志不计本次通过。
- 最终源码审查结论：可冻结并进入统一构建。检查者不再修改编译输入，只维护本报告并读取主会话新验证日志。

## 最终 Verification（本次20DIP+140ms修订）

以下结果均由主会话在最终源码冻结后统一执行，检查者直接只读核对新的JSON/stdout/stderr，未复用首轮结果，也未另起构建/二进制。

| 检查 | 最新证据 | 结论 |
| --- | --- | --- |
| Lint / whitespace | 最终git diff --check exit0 | pass |
| TypeCheck / compile | snap-solution-debug-arm64.result.json：完整InkeysRepo.sln Debug / ARM64、原生ARM64 MSBuild、900秒监督，exit0，29.95秒；stdout明确编译PageControl.cpp/.cppm与page_control_tests.cpp，stderr为空 | pass |
| Headless | snap-headless-no-window.result.json：精确--no-window，exit0，77.67秒；stdout末PASS animation correctness，stderr 0字节 | pass |
| 生产无HWND离屏 | snap-product-offscreen.result.json：精确--bar-eraser-offscreen-test，exit0，23.66秒；stderr [PageControlScene] failures=0，Build/eraser-b/offscreen-results.log末[EraserVisual] failures=0 | pass |
| 编码/BOM/CRLF/冻结 | snap-tracked-format.json：6 tracked files全部encoding_bom_crlf_ok=true，errors=[]，frozen_unchanged=true | pass |
| COM保护/范围 | snap-source-freeze.json与snap-artifact-hashes.json记录；protected_com_unchanged=true，DLL/TLB哈希维持前任务产物 | pass |

新headless执行1152二维门槛组合及原生产纯策略/页码门禁；新生产离屏执行cpp私有实际Scene、140ms中间/终点、双面同步、owner/render单调时间、错误回滚/真P锁争用与raw/fit连续手势断言。两者边界已分列；离屏用例没有创建真实HWND或调用真实capture/Office。

既有hashlib++/IdtMain等warning和非致命pwsh PATH提示未通过改源码、项目、SDK或依赖绕过。本次编译输入冻结后仅task/research文档更新，未发生源码二次变化；未commit/push。

## 最终结论

- R2已由原生产责任代理修复，独立复核源码和真实Scene反例完成，新一轮完整构建及两类测试均pass。
- 原R1、普通拖动/合法Up、capture锁外、pending真实几何提交、20DIP双门槛和碰撞约束均保留，本修订没有遗留阻断项或范围内应修未修的源码缺陷。
- 真实USER32鼠标/触摸捕获交错、故障设备局部停住、实际HWND层级/命中、Office/WPP和损坏COM设备仍NOT VERIFIED；自动验证不代表这些现场已复现或通过。
- 最终代码可交付用户评审；不修改第4项层级、COM或已冻结编译输入。本检查者的唯一写入仍为本报告。
