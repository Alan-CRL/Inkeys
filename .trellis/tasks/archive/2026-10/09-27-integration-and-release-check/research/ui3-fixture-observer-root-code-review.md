# UI3 F Root A/B/C 实码独立复审

日期：2026-10-01。Active task：`.trellis/tasks/09-27-integration-and-release-check`。仅审 Root 冻结的五源及三项窄修补；Source/Test/Interaction/Button 和 Host/HiddenTest writer 的进行中源码不在本轮审查范围。唯一写入本报告；未改任何源码、工程、其它报告或spec，未运行 Git、构建、产品/测试EXE、GUI或递归派发。

## 判决

**SCOPED_STATIC_GREEN：本轮 Root A/B/C、DPI/early dispatch及已修三项边界无剩余源码阻断。** 45AA设计的初始真实冻结、精确完成记录、一次原BGRA同步owner读回已落实到真实调用点。整体F、Auth suite、Source bootstrap/动作/完整owning job/全部join、Host smoke和新GUI仍 **NOT RUN-APPROVED**；本结论不授予构建之外的运行许可，也不把旧Headless/H1结果升级为新完整Main/C运行通过。

## 冻结身份与前像核对

| 审查源 | 最终SHA256 |
| --- | --- |
| Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.h | 4B82819DF7D37557CA020FB19FA7ED38912AC9DFF507190464F88CE16F512793 |
| Inkeys/Inkeys/UI/Bar/Bar.PresentationProbe.cpp | 0538C544E77749E57017B36783A23BA9437BC3AA0BBA28CC7208009C1F5B45A8 |
| Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp | 520190BD6FCB18D7F68E7B5EAE2214FB79C3CCD3C7CA867EF9F1C9E42D382EC4 |
| InkeysHeadlessTests/render_scheduler_tests.cpp | 49A6585EADCFF9898629037C2751C111C6BC2F06D7F2EA95656CE5C0D37CAD45 |
| Inkeys/IdtMain.cpp | 00C13D99A4F73915F8375AD38FB14DB6B5AFBADAF971855D9826C2F191FE70C3 |

初始 dispatch 指定的 RenderLoop `783BCF…D79A1E` / Main `9F8006…2EB78` 已由下面三项修补覆盖；Probe/Headless没再改。Root implementation报告已同步本表最终散列，并明确三项修补尚未完整构建；恢复后再次核对五源均与本表相同。

直接读取 `ui3-b3-h1-green-frozen-debug-candidate.json` 和 H1独立报告；将**内存字符串**中的本轮A/B/C新增include/成员/方法/接线逐项撤除，恢复后的完整文件结果如下，未写回任何文件：

| 重建前像 | bytes | SHA256 | 与H1 frozen JSON |
| --- | ---: | --- | --- |
| Probe.h | 25061 | E2DE55BE5A73C7CB7AC5DCFCFA3488D3E52F54B89D984D79AB8A72A4BAE570F7 | 完全相同 |
| Probe.cpp | 64632 | EB99F2A10C13B337408382C567C33FA8E898514FC53B2F3EC8BB2C4D3C2FF4B9 | 完全相同 |
| RenderLoop.cpp | 632036 | AB1E5B8B5CEDC58F90961EEBB1B3632E2F93A71912DA5A59DB718A71CA89474F | 完全相同 |

由此确认本轮没有夹带H1 paint谱系、SVG/shape/text/lighting producer或原绘制/四API次序修改。H1的 UnknownWrite撤销早先full-clear资格和tracked未知域调用同入口仍保留；未扩RetainedVerified。Main与新增F201–F204按实际符号/调用链审查；该H1 JSON未提供它们的完整前像，未冒称已对它们做同样的字节重建。

## A：Freeze / 真实Fit / 线程先行关系

- `Probe.cpp:847–874` 的Enable核无frame/owner/ready、pub0、mutation/goal/revision/seen/completed均空及完整支持的seed签名；普通constructor保持原冻结行为。MarkConsumed仅在显式bootstrap初始阶段暂允许实际configZoom不同；工具/宽色/版本、theme、DPI/display/厚度模式继续FrozenFiniteInputsMatch，不吸收外来状态。
- Freeze为private，只从CompleteAttempt的初始同候选、真实committed、settled/无高位干扰、有效epoch/surface/dims及完整有限anchors路径调用。flags与seed保持一致，只更新真实Fit后的Zoom基线；pub0不生成测量目标、不写CompletedRevision。不能以bootstrap layout/anchor就绪代替SVG完成。
- `RenderLoop.cpp:13619附近` 在同一次原Submit/MarkConsumed后比实际frame.zoom与新barStyle.zoom，错位标原InitialPending。Settle原pending规则使该过渡帧不Freeze；沿Fit自己已有Request等下一自然帧。未重跑Submit、修改本帧绘制或补Request。
- Freeze写plain publication.initialStable后才由同render publisher发布现ready atomic words/even release。后续runner必须acquire取得bootstrap ready，再创建Interact；实际Run再发布Interaction bit后才开Source。这是安全写plain基线所必需的调用前提，本observer接口本身没有创建线程，也没有替Source证明该先行关系。任何bootstrap期间主线程并发读plain InitialStableSignature/提前Interact仍不在允许用法内。

## B：实际owner、原首次Completed行

- 第一BeginFrame以release发布GetCurrentThreadId；Copy先acquire读这个atomic并拒非owner，再读sealed/frameClosed/普通prefix。非owner不会先触plain ledger，现实现无lock/等待/clock/新容器。
- 最多512 retained rows，完整run/step/source/revision匹配并核scene、原Accepted、terminal CompletedLayoutAndSvg、role/proof、零pending、非零epoch/surface/attempt与全部required SVG成功；不把AcceptedNoChange的旧revision引用当新完成。失败清空out，Seal后拒绝。
- StoreOutcome仍在第一次Completed后保留原行；ready可以被后来同goal的成功但资源未认证帧更新。B复制原consumed/settled/finalCommitTicks及资源摘要，不组合CompletedRevision和后帧ready，不拿poll或控制任务执行时刻替目标时间。
- failureFlags保留历史失败/pending见证，未错误要求全0；当前完整证明由该行Completed/roles/resources决定。全run仍须保留历史失败分母，不能因copy成功删掉这些记录。B不新增测量clock，off保留纯完成身份而时间null。

## C：最后真实提交的资源身份与完整字节

- 普通同步wrapper先核exact Auth和Source的本地EquivalenceReady，再用B取得canonical完整行，复核提供的run/step/source/revision与真实attempt/epoch/surface/ticks/timing，之后才访问coordinator。Source EquivalenceReady的原子发布/线程安全、216完成和Source/Interact真join是外部依赖，尚未审其进行中实现；本轮只确认Root没用mutable Packet/round/capacity猜权限。
- 原完整四API决策成功后，且finite observer/probe安装时才锁存实际final goal、current attempt/epoch/surface、ULW真实source/完整viewport、alpha及mutation/invalidation serial。目标MeasuredEnd行与当前图的提交attempt分开，允许后者晚于前者但同目标/语义/epoch/surface；未替换目标完成时间。
- 下一真实BeginDraw前立即撤Available并Reset最后成功bitmap lease。failed/deferred已经写backing时无法借旧提交读回；target discard/recreate、epoch/surface或alpha不符继续拒绝。仅保留成功target的私有ComPtr，不持旧context/raw HDC跨帧，不复制额外像素。
- C在实际render owner建立当前context/bitmap局部ComPtr lease，核当前target与该成功bitmap对象完全相同、BGRA premultiplied、source非负、完整source矩形落在target真实尺寸。保持成功bitmap活使同地址ABA无法追认；新same-epoch/same-size对象即使其它serial尚未前移也不通过。
- 创建单次CPU_READ|CANNOT_DRAW readable bitmap，CopyFromBitmap完整ULW源rect，Map后按真实pitch逐行复制全部width×height×4原BGRA，结算Unmap并核其HRESULT；没有裁dirty子区、缩放/色彩或alpha转换、绘制、Present或显式Flush。该同步调用返回前所有Map/局部COM借用已结束，外层Source job必须在此返回/lease结算后才能最后发完成event。
- destination与readable各限功能上限一半，即≤16MiB；width×4用uint64，checked除法再乘height，真实mapped.pitch×height也≤16MiB，故两份同时payload≤32MiB。source+extent以uint64先验，之后RectU不会溢出。**共同fixed/raw/source/scratch≤64MiB仍由Source allocator证明，Root C只提供自己的32MiB局部门，不能据此宣布整体预算已过。** readonly bitmap实际pitch只在Map后可核；未对D2D/驱动内部GPU/RSS给出硬上限。
- 普通null observer不走Freeze/owner记录/新读回stamp/AddRef/bitmap创建；只有固定空ComPtr/POD成员大小与null判断。现计时门及H1原stage/resource逻辑保持前像，off不凭B1 stamp作功能成功证明。Main必要的命令行解析是启动路由成本，不是逐帧采样或新的后台owner。

## Findings (fixed)

1. **Main dispatcher次序，Root已修。** 初版Main:973–977先旧SS再UI3，与45AA冻结的先识别新UI内部词不符。最终`:973–977`先UI3 early-return再旧SS；正常配置/单实例/HWND之前已处理，旧SS实现未动。
2. **C同尺寸资源替换空档，Root已修。** 初版仅比epoch/surface/serial/尺寸，owner若在下一BeginBackingWrite前同epoch同尺寸替换bitmap，旧receipt可能复制新未提交对象；这是一项合同反例，未冒称自然故障复现。最终RenderLoop:632持成功target lease，`:989`比较当前对象身份，BeginDraw前Reset/撤Available，完整成功后再持lease。没有新增renderer API或修改H1。
3. **quoted Host smoke回落普通Main，Root已修。** 初版以lpCmdLine原始前缀识别，合法给mode加引号时可跳过selector并继续普通GUI。最终Main:979–992用解析后的exact argv1，只有argc4/唯一output-root形状调用原函数；已识别坏形状return2，argv释放完整。这里只审Root路由，未审Host/HiddenTest writer或执行新smoke。

本 reviewer仅发现并向Root报告，源码修改均由Root完成；最后新hash已核。**本scope无剩余must-fix。**

## Findings (not fixed) / 必需后续门

- 新C、Main、新Auth/Source普通链接依赖未完成新完整主Solution编译；source header/普通函数的global module fragment已有Rootinclude位置，但它们的最终声明/定义、named-module链接、三个架构布局与整体工程登记仍须停写后核最终集成。未看Source writer半写内容，不给它的Auth positive/文件写/action/停止路径安全结论，不使用success stub。
- Source唯一owning checkpoint job、EquivalenceReady事实和所有返回的Close先行/Source+Interact join、task最后回执→Unregister/Seal/reset、Window两线程/Display drain/Scheduler真join、全BGRA固定CREATE_NEW/离线配对/64MiB总峰值仍待整合实码审查。Unregister只等activeCallbacks这条边界没有被Root C修补改变；超时须保活cap/probe/span/job/coordinator到任务完成或自身监督死亡，不能先reset再等迟到task。
- C的真实GetDC/ULW/ReleaseDC/EndDraw提交身份、same-size recreate/失败与延后写、非零source rect/stride/预算/alpha/on-off全字节，以及quoted/bad-shape CLI、真实Fit producer未动态验证。shape/text/lighting Unknown仍保守，真实两scene可能ResourceUnverified；严格216目标不得补帧/省光影/追认Retained造绿。
- A/B新数值例仍缺真实小屏高DPI/跨线程bootstrap启动的整条证据；它们是有效helper测试，未覆盖真Fit/UI3 HWND。Win7、Release三轮、CPU/GPU/光学、其它scene/family、HC/H2、HF/发布继续保留原门禁。整体集成后Root再同步native UI/diagnostics/resources中的新增接口与寿命规则。

## Verification

- 静态：**PASS于本轮五源及三项Root修补**；符号/调用链、最终hash、完整H1前像内存重建均已核。五源严格UTF-8、CRLF/bareLF0；Main保留BOM，其它四源无BOM。未执行Git/diff-check；对新增块检查格式，没有全文件格式化。
- Lint：未运行独立lint，按readonly dispatch不run；未发现本增量静态类型/import机械问题。
- TypeCheck / Build：本reviewer未构建。已直接读Root `ui3-f-ab-helper-debug-arm64-build.status.txt` / build.log：仅Headless项目Debug|ARM64实际exit0/0errors。它在局部first→firstCompleted命名修补前；该修补不改数值行为，最终五源仍须完整主Solution复验，**C/Main不能借此记编译PASS**。
- Tests：本reviewer未运行。Root `ui3-f-ab-helper-debug-arm64-headless.status.txt`明确pid34412、`--no-window`、自然exit0，输出layouts216/failures0与animation correctness PASS；实码F201–204覆盖bootstrap pending/失败commit/缺anchor→一次Freeze、新Zoom接受、原constructor/外来颜色拒绝、首次完成行与后帧ready区别、错tuple/非owner/NoChange/Seal拒绝。它们直接调用生产observer，手工资源/identity是数值前提，**不是实际Fit、GDI四API或完整F**。
