# Research: 当前绑定达到最高优先级时的 ROT 减量选项

- Query: 在不改变完整 Observe→命令顺序、不放宽场次/墨迹准入的前提下，能否因当前绑定本轮已达到优先级上限 3 而跳过 ROT 发现，减少 forced 扫描与重复读取？
- Scope: internal；只读产品/测试/规范和采样；仅写本研究文件，不实施产品修改。
- Date: 2026-11-01

## Findings

### 1. 结论：不能由 root 的新鲜 3 分直接跳过全部 ROT，但存在更窄的可证终止点

**否决“在 ROT 外 fresh-probe 当前 root 得到 3 分，就完全不进入 ROT”作为严格等价的小修。** 当前算法的 targetPriority 来自本轮 ROT 实际枚举到 target 后的读取；root 自身仍可读取，不等于本轮 ROT 能找到并成功评分该 target。两者差异会改变是否向另一个应用重绑。

**可进一步审查的更窄方案：本轮原 scanner 已实际枚举到同一 COM target，并按原逻辑得出 targetPriority=3 后，终止剩余枚举。** 只用于 `MaintainBinding` 的健康绑定路径，保持原 `GetAnyActivePowerPoint` 完整调用语义。此点有本轮 membership 与原评分的共同证据，并能从“后续最高也是 3、重绑只接受严格更高”导出无需再发现剩余应用的本轮绑定决策。

这不是已经实施或验证的方案；收益仅是**到达该点之后**的扫描尾部。若主要耗时在取得 target/评分它之前，或者原 pending 检查已在更早位置 Deferred，此方案可能几乎无收益。

### 2. 当前算法可确认的决策合同

- `ScanActivePowerPoint` 由本轮 ROT 枚举取得 Application，或从文稿 moniker 取得 Application；逐个应用以 COM identity 去重（`PptCOM/PptCOM.cs:965-1040`）。
- 非空 fresh ActivePresentation 得 1 分，fresh SlideShowWindow 得 2 分；Active 标量严格为 `int -1` 或 `bool true` 得 3 分；否则现有 `IsSlideShowWindowActive` 成功也升级为 3（`:1058-1110`）。当前没有大于 3 的赋值。
- `isTarget` 由本次 candidate 和借用 targetApp 的 COM identity 判定（`:1049-1053`），该项计算结果写入 targetPriority（`:1122-1126`）。
- `highestPriority`/best 只在**严格大于**旧最高值时更新（`:1128-1137`），因此同分保持枚举中先到者。
- `MaintainBinding` 在原 roots 完整时，跨应用重绑需要 `bestPriority > targetPriority` 且 COM identity 不同，再经过同文稿/窗口保护（`:1214-1225`）。roots 不完整则有独立初始化绑定条件，不能套最高分优化。
- 原 scanner 评分 target 后不会再次评分它的重复 moniker；后续重复 candidate 只释放本次取得并跳过。

用固定当前源代码的分数域 `0 <= priority <= 3` 表示：只要**本轮原评分**已经给 target 记下 3，之后无论 best 是当前 app 还是此前先出现的另一 app，`bestPriority > targetPriority` 都不可能成立。若后续遍历抛出外层异常导致 bestPriority 仍为旧初始化值 0，也同样不会触发 `>3`。不能由此推广成“外部 probe 的分数可以代替 scanner 的 targetPriority”。

### 3. 完全跳过 ROT 的反例

可用现有 fake ROT 直接构造，不需要假定真实 Office 经常发生：

1. 当前 roots 指向 app A，A.ActivePresentation/Window 可读且 Active=-1；缓存场次/descriptor 也健康。
2. 本轮 fake ROT 的 Rows 只有不同文稿的 app B，B 得 3 分，A 没有被枚举到。
3. 原扫描得到 best=B、bestPriority=3、targetPriority=0；同文稿保护为 false，因此重绑 B。
4. ROT 外 probe A 的得分为 3 而跳过扫描，则继续留 A。行为不同。

另两类同样的分歧：

- ROT 中有 A，但其本轮 `GetObject` 或 `.Application` 失败，scanner 未能给 A 评分；借用 A root 的另一次读取成功不能补作本轮 targetPriority=3。
- A 已成功进入去重列表，但本轮 ActivePresentation/Window/Active 读取缺失或失败使得分 0/1/2，后续同 identity moniker 被去重；外部另一次成功的 probe 不等于原评分，可能抑制原本会进行的 B=3 重绑。

即使外部 probe 增加 fresh ActivePresentation identity、fresh Window identity、可靠 HWND/foreground/PID 检查，仍无法弥补“target 是否被本轮 ROT 发现且成功取得”的差异。将策略改成“已活动的 current root 永远优于 ROT 中同样活动的其它应用”可以是另一个产品选择，但它不是当前比较式的等价性能优化。

### 4. 可证的窄边界：scanner 内证明最高分后结束

候选只缩短剩余发现，不提前命令、不拆 Observe，不引入新的 live 控制 proof。

#### 方法边界

- `MaintainBinding` 仅在原健康资格成立、app/presentation/window roots 都完整时允许这一终止选项；Unknown、恢复重试、Unavailable/TransientBusy、新绑定/未绑定保持原路径。
- scanner 的旧 private 入口或 `GetAnyActivePowerPoint` wrapper 默认禁用此选项，避免改变其它调用者对完整扫描/后续异常输出的理解。
- 保留既有 ROT acquisition、候选转换、去重、Active 判定及全部 pending 检查。**不能先在借用 root 上做一次 probe，然后把 scanner 的 targetPriority 人为初始化成 3。**
- 终止点应在当前项 `targetPriority = currentPriority` 及 `highestPriority/bestApp` 的配套 bookkeeping 完成后，仍位于当前 moniker 的 `try/finally` 内。建议在固定词诊断之后返回；诊断失败不能决定是否泄漏取得。
- 条件只接受本轮 `isTarget && currentPriority == MaximumPriority`，并确认 targetPriority/highestPriority 均已记为 MaximumPriority；预先捕获的 eligibility 可用纯值 stamp 复核进一步收窄，但 stamp 不提供分数证明。
- 将本次认定为“已证明不存在严格更高候选”，不是 Deferred。原一次 Deferred 的公平预算在这次逻辑完成后可复位；原 500ms 节奏、请求 2s 超时和每轮有界邮箱不变。

可审查的局部伪代码仅为说明位置：

~~~csharp
// 完成本项原 target / best bookkeeping，最高分边界须与评分规则共用。
if (allowTargetMaximumCompletion && isTarget && currentPriority == MaximumPriority)
{
    bestPriority = highestPriority;
    return bestApp; // 仍经过本项与最外层原 finally；调用者释放这笔返回取得。
}
~~~

不建议为了这一点引入新的 RCW 缓存、枚举器续跑、PID app 注册表或通用发现框架。当前 first-max winner 已确定，沿用原 winning-acquisition 返回/释放链即可；没有必要为保持 binding 再造一个跨层句柄。

#### 为什么同分规则不变

- target=3 是本轮首个 3：当前 best 已是 target；后续任何 3 不会按原 `>` 替换它。
- 其它 app=3 已先出现，之后 target=3：当前 best 仍是前者，后续候选也不能替换；`MaintainBinding` 比较 3>3 为 false，继续保持当前绑定。
- 仅看到其它 app=3、尚未看到 target：**不能终止**。target 后面也可能是 3，从而将本来 3>0/2 的重绑变成同分不重绑。
- target=0/1/2、缺失或取得失败：**不能终止**，仍需原后续发现和同文稿保护。

#### 所有权

`bestApp` 是列表持有取得的选择别名。上述返回应保留正常成功返回语义：本项 finally 释放 ssWindow→activePres；keepAlive=true 的 candidate 交由外层列表；外层只跳过返回的 best 那一笔取得，其它列表项、comObject/moniker/bindctx/enum/ROT 各按原 finally 释放。caller 的 `finally { SafeRelease(bestApp); }` 仍必须运行，即使返回取得与长期 application root 同 COM identity。

不能照搬 Deferred 的“清 best/priority 再返回”之后又把它当 normal winner；也不能在确认 target=3 时先释放列表中的 candidate 再返回同一笔取得。正常完成点后的第一笔导航必须在所有本轮 scanner temporary 清理完成且原完整 Observe 返回之后。

### 5. 场景审计

| 场景 | 结论/必须保持 |
| --- | --- |
| 多 PowerPoint/WPS、多个 app 同为3 | 原严格更高才重绑，target 本轮已评分3后剩余同分不可改变结果；不得仅因其它 app先3就早停 |
| target 未在 ROT / ROT取得它失败 | 不得用 root probe 快退；没有本轮 target=3，保留原发现与重绑 |
| foreground 在 target 评分前切走 | 按原 Active/PID逻辑得到的实际分数决定；低于3则完整继续，不用旧 foreground 记录补分 |
| foreground 在 target 已评分3后切到另一app | 原本轮也不会再次评分target，另一app最高同分不会重绑；下次到期必须从新取得/新评分开始，不缓存3跨轮 |
| fresh active presentation/window 与缓存root不同 | scanner评分的是本次 fresh 图，不得拿旧窗口 Active替代；早停仅保留原“本轮不跨app重绑”的结论，随后的完整Observe仍发现换文稿/换窗并推进或清理场次 |
| HWND0 | Active=-1/true本来即可给3；scanner内早停无需以 HWND非零为基础控制前提。若只能靠原 PID后备且其失败，则得分仍2，继续原发现 |
| HWND复用、多show | 不能把 cached HWND/PID/标题当3分。只沿原 fresh对象评分；窗口/页/墨迹可信度仍由完整Observe及native合同决定，不由ROT最高分开放 |
| WPS跨进程fallback | 原 `IsSlideShowWindowActive` 接受fg PID=show PID，或进程名wps/wpp组合；这是当前发现排名规则，不是强窗口身份证明。不得加宽为任意wps/wpp缓存PID命中 |
| 无事件轮询 | 最高分不确认场次；完整Observe的Count0、新COM窗口身份及current/total/State/HWND检查必须原样执行，旧请求仍须拒绝 |
| COM重入 Begin/End/PageChanged | 原事件只提示、不安装/释放root。候选可因stamp变更放弃优化继续扫描；即便本轮保留绑定，随后完整Observe/命令的generation屏障不能跳过 |
| target=3之后剩余扫描会失败/返回partial | 对MaintainBinding跨app决策仍不会形成>3，早停不制造本轮可触发的重绑；不能据此声称其它private入口输出/异常诊断完全逐字等价，因此默认保留wrapper完整扫描 |
| 未绑定/Unknown/恢复 | 不允许最高分捷径；沿原完整发现。健康但只得0/1/2的fallback沿原“一次延期、下次强制完整尝试”公平策略 |
| rank以后增加4 | 上限3的证明立即失效；最高分必须是明确共用的内部常量/合同，回归须防止rank域变化而遗留旧早停条件 |

`IsSlideShowWindowActive` 本身（`:717-773`）没有证明“前台 HWND 就是特定 show”，仅比较 PID或WPS进程名；`GetPptHwndFromSlideShowWindow`（`:1635-1642`）只读取受限标量，亦不做完整窗口身份证明。不能给现有排名 helper 附会新的可靠生命周期语义。

### 6. 公平性与性能边界

此窄选择不是永远延期其它 app：当 current 的本轮评分已经3，原策略也不可能因其它app得3而切换，剩余发现对本轮**绑定选择**没有用途；当它降分、失效、不在ROT，下一次扫描不再满足终止条件，原发现和严格更高规则继续工作。不得永久记住3或以一个TTL避免再评分。

原 `rot-forced` 应仍表示“本次不允许 pending Deferred”，不能在报告中继续误写成“必然访问了所有moniker”。如采纳早停，诊断应增加纯值 `rot-target-maximum` 或等效完成原因，明确它是逻辑最优解完成，而非不公平饿死发现。禁用与正常日志输出均不新增COM读取。

最新144216样本最大Previous epoch6为1075.947ms，邮箱931.2541ms=ROT剩余180.5474ms+Observe750.6926ms；该ROT实际是Deferred而非forced。**最多**全部删除这条请求的ROT重叠，也只将该样本降到约895.3996ms；本方案不一定能删除其中任何部分，因为它必须先完成target的实际取得/评分。上一轮Next epoch39即便删掉全部ROT重叠585.2994ms，完整Observe与命令仍使总量约1051.2371ms。

当前PptPerf只记录聚合rot，未记录target在枚举中的序号、何时得3、剩余还有多少COM调用；stdout/stderr中也没有可关联到各轮的priority行（设置关闭了这类Console输出）。所以不能预测该窄方案会省684ms、会稳定低于1s，或认为它会解决当前最大的Deferred样本。若target在最后一项或只有单项，扫描尾部收益可能接近零。

真正想跳过整个 EnumRunning 的下一种选择，必须先解决“本轮ROT会员身份/同一Application”的新鲜证明，例如准确重建并查找原注册 moniker后再核对GetObject identity。但当前没有可直接复用且已经验证的这类单项查找合同；缓存moniker字符串/CLSID并不能保证多实例或同文稿另存后的绑定相同，新增查找框架不属于本次最小选择。

### 7. 最小实施范围与回归

如主会话决定推进，产品预期仅 `PptCOM/PptCOM.cs` 内 scanner 的受控完成选项、MaintainBinding调用和简短中文上限证明注释；测试扩展既有 `PptCOM.Tests/ManagedCompatibilityTests.cs` 的 RotFixture。不修改public ABI/TLB/schema、owner邮箱、Observe、native target/Present/ack、退出策略和GUI。

必须覆盖：

1. 完整模式与受控模式相同binding决策：target=3首项/中项/末项；另一app先3、target后3；target2后other3；本轮缺target；same-document保护分支。只有确定不能严格超过时减少后续GetObject/属性读取。
2. 取得失败/缺属性/busy使target评分低于3、同identity的后续重复项不重新评分；不能从缓存或额外成功probe补分。保留GetROT失败但out非空、取得后抛错账本。
3. Active的int -1、bool true/false、非既有类型long -1/异常COM标量；生产现有PID/WPS后备规则不改。若无法无GUI覆盖真实Win32后备，将其静态审查与真实WPS验收明确分列，不增加新的通用mock框架。
4. current App仍可读3但fake ROT只含另一3分app的反例，必须仍扫描并按原规则重绑，证明没有外部root-only快退。
5. 先其它best=3再target=3，完整与early的同分best选择/返回取得一致；caller仍release best。target root与本轮candidate同identity但多笔acquisition分别配平。
6. 停点内所有temporary/root账本：ssWindow、activePres、当前moniker/bindctx/comObject、列表best/非best、enum/ROT；正常返回/throwing writer/Release故障均不额外Release、不跨轮缓存RCW，无GC/FinalRelease。
7. stop资格禁用时完整遍历；unbound/Unknown/retry/Unavailable/busy保留旧完整路径；一次Deferred→forced的预算在“最高分逻辑完成”和正常完整完成后正确复位，cleanup重置不变。
8. COM读取泵送Begin/End/PageChanged，或freshWindow更换/HWND0、Count0/再开：随后原完整Observe先于单次导航，旧request零Invoke；early结果不能直接作control/ink proof。
9. target得3后在未访问尾部模拟其他app激活，下一轮target降2时能发现并重绑；不缓存排名跨轮，不延长500ms节奏。
10. 开关诊断等价/IErrorInfo、现有core与动态所有权测试、完整Debug|ARM64及DLL/TLB/资源交接。用户真实Office性能与退出/下一文稿仍独立验收。

## Files Found

- `PptCOM/PptCOM.cs:934-1190`：实际ROT取得、去重、评分、strict-best及逐项/外层finally。
- `PptCOM/PptCOM.cs:1192-1244`：健康资格、一次Deferred预算、严格更高且异app才重绑。
- `PptCOM/PptCOM.cs:701-773`：同文稿保护、foreground/PID/WPS名称排名后备；不提供可靠场次proof。
- `PptCOM/PptCOM.cs:1299-1453`、`:1484-1493`：完整Observe和之后邮箱/descriptor顺序，候选不改变这些路径。
- `PptCOM/PresentationDescriptor.cs:107-141`：普通成员实际C#dynamic；HWND为既有受限标量例外。
- `PptCOM.Tests/ManagedCompatibilityTests.cs:1377-1483`、`:1529-1740`：现成fake ROT/receipt、实际CreateBindCtx、部分winner/重复取得和公平性/新鲜观察回归。
- `Build/Validation/ppt-navigation-20261101-144216-578-e2c353b3/summary.json`：35请求和最大Previous的聚合rot/Observe重叠。

## Related Specs

- `.trellis/spec/ppt-interop/com-contract.md`：普通dynamic、每笔acquisition、owner与deterministic finally、HWND例外、禁止GC/FinalRelease。
- `.trellis/spec/ppt-interop/native-session-ui3.md:300-346`：原ROT Deferred不得转移partial winner、一次延期公平预算、返回后完整Observe。
- `research/rot-yield-fix-plan-20261101.md`、`research/rot-yield-design-audit-20261101.md`：既有scanner安全检查点与所有权/公平性审计。
- `research/observe-navigation-next-design-20261101.md`：不推进partial-proof重构；本方案只减量发现，不改变命令准入。

## External References

无外部查询。结论是当前本地评分/重绑代码的逻辑推导；不把构造反例描述成已复现的真实Office状态，不新增Office/WPS支持声明。

## Caveats / Not Found

- 未修改产品/测试/规范，未运行构建、GUI、Office、git操作或子代理。
- root-only最高分跳过全ROT不能证明严格等价；scanner内部本轮target=3后早停可证明本轮绑定选择不变，但尚未实施或测试。
- 尚无target达到3的具体时刻/枚举尾部耗时，因此没有已证实的实机性能收益。若大头在target取得/评分，进一步插桩/优化需要新的具体证据。
- 三点软件提交不等于用户肉眼看见；可见性由主会话与专门UI调查确认，本报告不作验收结论。
- 行号基于2026-11-01当前工作区，后续以方法与判断语句定位。
