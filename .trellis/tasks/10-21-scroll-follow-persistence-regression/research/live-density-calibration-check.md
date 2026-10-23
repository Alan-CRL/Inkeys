# 稀疏格票权校准：独立检查

Active task: scroll-follow-persistence-regression。当前生产源码已按root授权应用7936941A、测试E72D3158，独立静态复审通过；完整ARM/x64构建、新Headless、默认native短回归及一次Guest空画布200px跟随已实际通过；外部墨迹、Word/PDF与新算法实屏长程不由本次证明。下文BF3409为应用前原算法及历史阶段事实。checker不构建、运行Tracker/CLI或GUI，只读审源码与root实际输出。

## 实测依据与静态候选

完整真实输入、原逐帧0差回放及专用正文200px精确像素对应见 `live-external-frame-trace-check.md`。a6ce失败帧13个错误匹配格只含34点、24个正确格143点，原单位格票24/37=.64864865触发.65门。底部错误流通过FB，不能靠FB证明正确。

`Build/ScrollFollow/recorded-motion-replay/density-floor3-candidate/candidate.patch` 仅四hunks，候选 **79A1617FD257FB34ECCFA83C02B671DDA61CB87C4EAA9F6AC1BDC2D92DA0B7A0**。fit/MedianFlow/anchor票1/max(cellCount,3)，spatial分母为实际全fit票和；每格仍至多一票，稀疏格不足整票。原歧义单独OriginalCellWeight保留1/cellCount/occupiedCells，包括零主流/runnerUp门；其余.65/FB/生存/覆盖/NonTranslation/跳变/3Pause/lastTrusted事务不改。实际counts正且matches≥40保证分母正。header原813509、原testsEF45、原replayCF56不改。若生产应用，旧每格‘总票为一’注释需同步；当前实验未改生产。

## 同一实输入的候选结果（限定改善）

root实际 `actual-a6ce-result.json`：候选replay **EA9CDA8A9A9F7E77D99126D86C633B3EB72AC64C3E4E3F2276C3FC6B706C269B**，strictcompare exit1/累计89字段差异，符合与旧失败结果不同的预期，不能叫精确回放PASS。frame11..13实际accepted/None，total=(0,-199.99993896484375)，frame11spatial=.70103092783505172、随后1，对独立正文/DOM200px误差**.00006103515625px**。summary max_total_difference150.993225是与旧失败总量49.0067之差，不是候选对GT误差。原89CC原算法仍0差准确复现失败。

## 原完整Tracker矩阵：实际通过

只读核 `matrix-result.json`/原matrix.log：单次root执行exit0/**202135ms**，EXE **081F91451442B0F94A9438C4F84DBBBDAD043D40685C0C961DFC75D611EB12FC**；当前实际测试文件SHA **EF45B732AE3D2D9432F9FFA76E6C06C1697BE17E711F01B152F6B2BEA92FE667** 与收据一致。

独立解析原log：旧14序列6918合法帧、新native22序列704合法帧均全部accepted/rejected0；最终PASS failures0，原4个native negative分别正确AmbiguousMotion/ForwardBackward/NonTranslation/NonTranslation。native stationary300漂移0；128往返284帧、路径36352、初点frame14全部退休后再270帧接受，最大/最终误差.00114px。原native固定多数middle4仍AmbiguousMotion、runnerUp.16964，旧960固定多数4/10px亦拒绝。旧合成子像素/固定UI等全部原断言通过，不从总耗时推算产品FPS。

## 仍需门槛

旧矩阵证明原回归保持，尚不足证明票权变化对密度不均内容安全。已获root批准的独立真实pixel反例正在staged：稀疏少数流/稠密固定多数、两反向流互换密度、前置FB/生存/fit充分但覆盖不足。需核原reason/runnerUp/覆盖先决条件、3拒绝/lastTrusted，且不能修改原测试bodies或新建公开API来注入匹配。后续仅root编译/运行新增cases，不重复完整矩阵。当前无生产放行或真实新GUI结论。

## 新增pixel保护用例：静态放行，实际执行待定

`density-guard-tests.cpp` SHA **5C4A8DFA2E3407895E7567ED3125A7B12981E1AFD46CE06228BE6D6C30C09208** 仅ignored TU include原EF45测试/生成器，原bodies不变。main只运行4新增用例（稀疏12格移动/36稠密固定；24+24反向流两种密度互换；8稠密格保留/40稀疏格消失），16次Process，不重复原矩阵。像素patch整体按独立GT移动，格间空白避免固定裁切角点；真实半分辨率GFTT sanity要求稀疏1..2/稠密≥6角点，明确不是实际FB匹配计数。

每次拒绝先断言实际fbValid/inliers≥40、survival≥.35、spatial≥.65、affineVariation≤1；再断言具体保护门。minority为AmbiguousMotion且.15<runnerUp≤.35，结合原源码只能由零主流且≥6interior moving格分支触发；两个opposite分别runnerUp>.35；局部Coverage必须InsufficientCoverage且occupied≤8/inlierCells<16。拒绝不得提交total/delta/ref/generation，第三次Paused，初始图再输入仍Paused。静态未发现剩余阻断，已放行root仅编译/运行newcases-only；尚未把预计门结果写成通过。

## V1反例实际失败、原算法对照与V2夹具修正

原V1实际candidate `density-guards-result.json`：F16E90F1…、exit1/1303ms、15断言失败，保留原raw。minority实际Ambiguous/.25与Coverage56FB/8格按预期通过；opposite原密度实际已Ambiguous/.351351，但affineVariation1.35使测试单项≤1前提误报。checker最初静态审未指出此过强前提；生产NonTranslation是variation/残差/支持率联合条件，不能以variation一项代替。

opposite swap只有12稀疏格FB存活、单位格12/36=.3333未达原>.35门，实际接受约-8。为判断是否新回归，root用相同5C4A V1测试对照原BF3409，独立baseline99A9F242…仍exit1/15fail且同样接受swap约-8/.3333，证明此现象不是floor3引入；两份同15fail不代表每条断言成因完全相同。原V1与对照、日志、exe均保留，未改判通过。

V2独立目录测试 **1421CCD308D8A37B7C75168C660A8004DA5E1CB5DA424FCE283A3E7C5A66244F** 精确diff只将opposite两流±8改±4full像素并同步GTlog，以使稀疏小patch实际可追踪；处理图流间距仍4>双半径3，其余scene仍8。删除单项affineVariation≤1测试前提，下方严格Ambiguous/Coverage目标reason证明已通过真实NonTranslation联合门；FB/inliers/生存/spatial/runnerUp及拒绝事务/3Pause断言完全保留。原测试EF45/候选79A/生产BF不变。checker静态放行root仅编译/运行新V2，尚未预判实际结果。

## V2真实保护门：实际通过，获准最小生产应用待复审

root独立执行 `density-guards-v2/run-result.json`：EXE **068678A3DCD65A4F4610D15A282573D410040A3AB3BAAA8940CC6681F75EB971**，exit0/failures0。checker只读原stdout/stderr：4case的GFTT sanity通过，12次拒绝+4次Paused输入共16Process断言通过。minority runnerUp.25（原零主流/interior保护），opposite两种密度均runnerUp.5、spatial.75（原单位格歧义保护），Coverage实际56FB、8占用/内点格；每组ref0/gen1/zero total-delta及第三次Paused、不自动恢复均保持。

因此候选已满足同一真实输入改善、未改原matrix完整通过、四新增density保护门的离线条件。root明确授权implement只推广Tracker原四hunks+准确中文注释、测试V2helpers+一次Run调用两个文件；旧所有casebody/header/依赖/阈值保持。已告implement可应用并给精确before/diff/旧bodies恢复证据；checker待生产增量复审，root才完整ARM构建及必要新产品GUI。尚未将该离线改善写成真实新binary功能通过。

## 生产两文件应用：静态复审通过

root明确授权且V2实际raw核准后，implement仅修改Tracker/test两文件。实际TrackerSHA **7936941A08CA16A42906CB4FFB7C40E228EAF31A638B53B4A358D3623E7CAFF5**；tests **E72D3158E44A058D85750089A1069C60340012E68EFC61980E4A4EC5F1252FEE**，与 `production-promotion/applied-manifest.json`/精确promotion.patch一致。

checker独立证明：剔除中文行注释后Tracker与已实测79A候选代码逐字节相同；新增helpers与已通过V2 1421逐字节相同；仅逆删helpers及单次Run调用即精确恢复原EF45测试全部旧bodies。原BOM/纯CRLF/EOF保留，无header/API/依赖/阈值/产品捕获/目标/GPU/DPI/保存修改。中文注释已准确说明fit每格至多一票、稀疏格降权、anchor同权与歧义原票分离。

静态无剩余阻断，已放行root完整ARM solution构建及必要新binary验证；checker未执行构建/CLI/GUI。离线实输入改善不等于新版真实Guest跟随通过，等待实际收据。

## 新生产binary构建与默认短回归

ARM完整solution第一次默认sandbox命中C1902（Inkeys Tracker及Headless测试/Tracker共3条），原日志保留；root在正常宿主不改源码/配置/cache按原命令构建通过，外层15130ms/内部14.81s、0warning/0error，属于已知环境差异。新EXE **8E3005FAE889388EE49D1A49108D1708DD46799922903C3F65E73388BE251A97**。`offline-21299640/result.json` 独立核每35坏argv均exit2/双0B、protocol/state0。

新默认native short `live-product-ink-floor3-production-1023-test.json` PID38824、exit0/6071ms，case `product-live-38824-21318906`。checker只读内存复用原图像/标准MessagePack读取器，精确追加新hash允许值且禁用写artifact，不改任何脚本或oracle。8PNG全部核准：A四次各37606双向核心比较最大0；B unknown0、13已知小alpha边缘(residual.6982)独立区间法通过；两source专用crop精确RGB/alpha255。实际Exit文件12467B/SHA **F1BD93A09AF2F3911B452E07B53A0B609E264D8FF0380A1449B1ABA1302289B7**，两Pen224/231、viewport(0,16,1)，GUID/index/实际RTS首点受限量化界/codec核准，11帧全部接受、原cleanup全归还。此项只证明默认native短链，不外推新外部浏览器跟随。

随后新binary external `7be3f4baa8b344d9a1d9595070600c0c` receipt exit1/8771ms、Edge16216/Inkeys0。2.294s原观察URL/run/fixtureId均exact但ready=false、visibility=hidden、document.complete，严格早门正确拒绝；未启动产品Mag/Tracker。早期CloseOwned无法证明root而拒绝操作，最终owned CDPclose正常退出、cleanupErrors[]，没有blanket kill。该次是准备顺序问题证据，不能记成新算法跟随失败。root仅授权先证精确ownedGuest主root、执行既有一次activation、再严格visible/ready等待的proposal，尚待diff审查，无hidden capture许可。

### Owned Guest启动顺序的最小修正静态审核

`external-entry-stage/startup-order-v1/candidate.patch` 当前CS4D1EBA07/PSD1602449仅ignored两文件。原首个Stable8s改成最多8s只读窗口发现：每轮Safe/held进程PID+exact creation/image、原CDP target URL/id；仅可等待尚无nonce窗，多个nonce root或Guest后缀错误即硬fail。唯一可见nonce Guest GA_ROOT核准后调用既有Activate一次，再原strict visible/ready Stable8s（初始ready豁免仍须visible、首有效后永久关闭）。原总双8s/runner预算不加长，后续所有门/保存不变，没有hidden capture、重复激活、JS写入或额外输入。checker静态放行root应用ignored两文件及一次default external，无产品构建；visibility是否及时更新仍待实际结果。

### 激活后准备过渡的只读等待与最终辅助脚本

`external-a810b57d5175430094cbdfc707a89909` actual receipt exit1/**3113ms**（最终cleanup3135），Edge38468/Inkeys0。1.975s已证明唯一own nonce Guest并调用一次Activate，2.152s仍readyfalse/hidden/doccomplete即严格准备失败；clean关闭，无产品运行。

root据此明确授权更窄的准备过渡等待。最终已应用ignored CS **2429CFF896A6742252001EAD62E761DBCF5B1B266AFEBC1FA6051C819DD3865E** / PS **8BA5491E0221BBC296E4D20FCE9CAE07BBD8D06D451E627BFC998B359E7B033E**，checker核精确candidate/comment-only/applied记录。仅单次activation完成后、首Stable/无Inkeys/首有效观察前、exact targetURL/run/fixture与原数值门过且ready=false，hidden才可内部pending；每次重核原held进程+同nonce Guest root，returnnull阻断FindContent/Marker/启动Inkeys。readytruehidden/首有效后hidden仍硬fail；8s预算不加长，不再激活、不输入、不截屏。一次metadata只含root/foreground/centerhit/class/PID/rect，无title。注释已同步，AST0/格式保持。此为准备阶段只读等待许可，不是hidden capture许可；尚待root新run。

### 最终x64、PE与i18n只读范围

完整x64 `solution-build-x64-density-floor3-production-1023.log` exit0、外层23919ms（内部23.59s）、0warnings/0errors；实际EXE **DA0D0726B63A9A00F0C309DB1FEF14A61CFD51805245D06F8C268D01CB906DC3**。最新ARM offline21299640/x64 offline21512375均独立核35坏argvexit2/双0B、protocol/state0。`pe-{arm64,x64}-density-floor3-1023-*` 两者Magnification仍delayload，x64无WaitOnAddress/新增DPI/WDA/WGC硬导入，ARM原WaitOnAddress仍在；非旧OS运行验证。

i18n原空stdout是Write-Host信息流未进入stdout，checker不把空日志当通过。root另保存allstream `i18n-density-floor3-1023-all.log`，实际en-US/zh-TW均426/426+PASS；初版result.exit=null已指出；root随后按实际exec process exit_code0与两语言PASS修正receipt为exit0并显式exitEvidence（脚本未设置LASTEXITCODE），checker已读修正记录。新完整Headless实际binary B09ABE8BDECB650FFD16D80BCA8779E9A0CD6BA3E4BBBF9499BFF9238A991E69已核hash，root唯一运行中，未提前判定PASS或重复启动。无生产追加修改。

## 最新生产完整Headless：实际通过

`headless-density-floor3-production-1023-result.json`：实际 **B09ABE8BDECB650FFD16D80BCA8779E9A0CD6BA3E4BBBF9499BFF9238A991E69**、PID29512，单次exit0/**501287ms**（约8分21秒，原15min预算）。checker独立读取原stdout/stderr并解析36组：旧14/6918与native22/704共7622合法帧全部接受、rejected0；4新增density输入sanity、12拒绝日志及4noresume断言集成通过，整体visual failures0。另核WindowCommand/CanvasPolicy/Config、Freeze fake scheduling/cancellation、ScrollFollowProtocol、Draw3 mouse/RTS orchestration、ShapeRecognition、笔宽codec、擦除/Recall/快捷键/动画等原无窗口路径及总退出码；stdout/stderr均无FAIL，不以最后单行PASS替代整体验证。此前隔离候选202135ms与当前完整套件501287ms范围不同，不作性能回归比较。

## 最新默认Guest空画布200px：实际限定通过

`external-dd5ff406117a43adb551108e1b4ee9d7` 使用新生产EXE8E3005FA，**NO RecordMotionFrames**：receipt exit0/**16129ms**，Edge27584/Inkeys3384，owned processes exited、cleanupErrors[]、ownEscapeHeld=false。checker独立核5阶段文件seq1..5、run/URL/target、Edge/runner/产品PID+creation/image、root2822522/content1051972、物理content(0,94,2880,1762)、session1均一致；browser-ready产品PID0，之后3384；全观察ready/visible。

首次own Guest单次Activate后2.231s仍hidden/readyfalse，辅助脚本仅只读pending；6.225s首次visible/ready并永久关闭豁免，7.834s发布browser-ready、7.869s才启动Inkeys。初始foreground19032是其它Edge，只查询class/PID/root/centerhit元数据，没有操作该实例或捕获hidden页面。原安全门/后续hidden硬失败不变。

实际OSwheel1，独立DOM CSSY0→100、DPR2=**200物理像素**，resizeCount不变；4轮共128个真实marker采样逐点exact。原真实Tracking empty pair(0,0)/ACK到位、16连续帧全接受（1Initialized+15None，无Await/拒绝/Pause）：frame8 totalY约-3.0023、9约-151.0022、10到-200、16保持-200。实际Off业务0/phase5、latest=applied16、actualACK1、visible(1,0)，最终bounded PASS viewport日志(0.000000,200.000000)，原state/mag线程、service、occupied与自有按键均归还。

此项证明**这次正常未录制运行，专用Guest app底图真实200px已带动空画布并正常退出**。最终数值精度仅原日志6位小数，不是墨迹像素误差；visibleInk/persistence均explicit not_run，不外推浏览器墨迹/普通tab/Word/PDF、任意内容或所有大步稳健性。旧失败/录制失败不追溯改判。

本次16个逐帧日志连续齐全；未见DiagnosticOutput累计drop汇总，不能宣称整个队列零丢失。该16帧PrintWindow backendRead均65.438ms、nearest-rank P95/max73.348ms，None帧最低confidence为打印值.5071；没有严格采样起止时窗，不推算捕获FPS，不把16.129s整体用例耗时当产品延迟。没有MotionTrace请求/输出/PGM，observer默认关闭。
